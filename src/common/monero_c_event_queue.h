/**
 * Copyright (c) Libmonero
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MONERO_C_EVENT_QUEUE_H
#define MONERO_C_EVENT_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

// the queue that delivers the events of a listener to the callbacks of a program. Not part of the public ABI, and it needs no other header of
// monero-c, so a test can run it.
//
// monero-cpp calls a listener while it holds its own locks, and a sync waits for the call. A callback that asks the wallet for something that
// the sync holds then waits for the sync, and the sync waits for the callback. The adapter of a listener only puts the event in this queue,
// which returns at once, and a thread of its own calls the callbacks, in order, with no lock of monero-cpp held.
namespace monero_c {

class event_queue : public std::enable_shared_from_this<event_queue> {
public:
  typedef std::function<void()> event;

  event_queue() = default;
  event_queue(const event_queue&) = delete;
  event_queue& operator=(const event_queue&) = delete;

  // puts an event in the queue and starts the thread on the first one. kind tells which events are alike, and limit how many of them may wait:
  // 0 for all, and otherwise the newest `limit` of the kind, so an event that finds the limit reached drops the oldest of its kind. The limit
  // 1 replaces the event that waits with the new one, at the end of the queue. It does nothing once the queue is stopped
  void post(int kind, size_t limit, event fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) return;
    entry item;
    item.kind = kind;
    item.limit = limit;
    item.fn = std::move(fn);
    if (limit > 0) {
      std::list<std::list<entry>::iterator>& waiting = waiting_[kind];
      while (waiting.size() >= limit) {
        pending_.erase(waiting.front());
        waiting.pop_front();
      }
      pending_.push_back(std::move(item));
      waiting.push_back(std::prev(pending_.end()));
    } else {
      pending_.push_back(std::move(item));
    }
    if (!thread_.joinable()) {
      std::shared_ptr<event_queue> self = shared_from_this();
      thread_ = std::thread([self] { self->run(); });
      thread_id_ = thread_.get_id();
    }
    work_.notify_one();
  }

  // returns when the events that wait and the one that runs are done, or the queue is stopped. A callback that calls it for its own queue gets
  // it back at once, since it would wait for itself
  void flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (std::this_thread::get_id() == thread_id_) return;
    idle_.wait(lock, [this] { return stopped_ || (pending_.empty() && !running_); });
  }

  // drops the events that wait, and returns when the one that runs is done. A callback that calls it for its own queue only drops them
  void cancel() {
    std::unique_lock<std::mutex> lock(mutex_);
    clear();
    if (std::this_thread::get_id() == thread_id_) return;
    idle_.wait(lock, [this] { return stopped_ || !running_; });
  }

  // drops the events that wait, refuses new ones, and ends the thread after the event that runs. It waits for the thread, unless a callback
  // calls it for its own queue, which has to let the callback return: the thread is let go and ends when the callback returns
  void stop() {
    std::thread joined;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
      clear();
      if (thread_.joinable()) {
        if (std::this_thread::get_id() == thread_id_) thread_.detach();
        else joined = std::move(thread_);
      }
    }
    work_.notify_all();
    idle_.notify_all();
    if (joined.joinable()) joined.join();
  }

private:
  struct entry {
    int kind = 0;
    size_t limit = 0;
    event fn;
  };

  void clear() {
    pending_.clear();
    waiting_.clear();
  }

  void run() {
    for (;;) {
      event fn;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        work_.wait(lock, [this] { return stopped_ || !pending_.empty(); });
        if (stopped_) {
          running_ = false;
          idle_.notify_all();
          return;
        }
        entry& first = pending_.front();
        if (first.limit > 0) waiting_[first.kind].pop_front();
        fn = std::move(first.fn);
        pending_.pop_front();
        running_ = true;
      }
      // a callback is C code, and an exception that leaves it would end the process
      try {
        fn();
      } catch (...) {
      }
      fn = nullptr;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
      }
      idle_.notify_all();
    }
  }

  std::mutex mutex_;
  std::condition_variable work_;
  std::condition_variable idle_;
  std::list<entry> pending_;
  std::map<int, std::list<std::list<entry>::iterator>> waiting_;  // the events of a kind with a limit, oldest first
  std::thread thread_;
  std::thread::id thread_id_;
  bool running_ = false;
  bool stopped_ = false;
};

} // namespace monero_c

#endif

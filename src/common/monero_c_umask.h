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

#ifndef MONERO_C_UMASK_H
#define MONERO_C_UMASK_H

#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#define MONERO_C_HAS_UMASK 1
#include <mutex>
#include <sys/stat.h>
#include <sys/types.h>
#endif

// the files that a wallet makes are for its owner. Not part of the public ABI, and it needs no other header of monero-c, so a test can run it
namespace monero_c {

// makes the files that the process creates while it lives readable and writable by the owner only, with no permission for the group and the
// others, on the systems that have a umask. wallet2 makes the files of a wallet, and the one that it writes (the cache and the keys, and the
// new file that it renames to the old one) have the permissions that the umask of the process leaves, which are 0644 with the usual 022. A chmod
// after the call leaves the files readable for the time between the creation and the chmod, so the call is made with the umask of the owner,
// and the chmod is for the files that were already there.
//
// The umask belongs to the process, so it holds for every thread, not only for the one that has the guard: a file that another thread makes
// meanwhile has no permission for the group and the others too. The guards of the threads count, and the umask that was there before the first
// one is put back when the last one ends, so two calls at the same time don't put back the umask of the other. A program that needs a file with
// other permissions while it makes or saves a wallet has to chmod it. The guard of a call that doesn't write a file is not made (active false)
class private_file_creation {
public:
  explicit private_file_creation(bool active = true) : active_(active) {
#if defined(MONERO_C_HAS_UMASK)
    if (!active_) return;
    umask_state& state = shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.holders++ == 0) state.previous = ::umask(S_IRWXG | S_IRWXO);
#else
    (void) active_;
#endif
  }

  ~private_file_creation() {
#if defined(MONERO_C_HAS_UMASK)
    if (!active_) return;
    umask_state& state = shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (--state.holders == 0) ::umask(state.previous);
#endif
  }

  private_file_creation(const private_file_creation&) = delete;
  private_file_creation& operator=(const private_file_creation&) = delete;

private:
  bool active_;

#if defined(MONERO_C_HAS_UMASK)
  struct umask_state {
    std::mutex mutex;
    int holders = 0;
    mode_t previous = 0;
  };

  // one for the process, which is never destroyed: a thread that ends after the exit handlers have run still finds it
  static umask_state& shared() {
    static umask_state* state = new umask_state();
    return *state;
  }
#endif
};

} // namespace monero_c

#endif

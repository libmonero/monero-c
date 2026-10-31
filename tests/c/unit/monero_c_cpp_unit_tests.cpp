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

#include "monero_c_test.h"
#include "common/monero_c_callbacks.h"
#include "common/monero_c_event_queue.h"
#include "common/monero_c_secret.h"
#include "common/monero_c_tls.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <set>
#include <string>
#include <thread>
#include <vector>

// links only if the headers declare the functions extern "C"

static void test_calls_from_cpp() {
  char* json = nullptr;
  monero_rpc_connection* connection = nullptr;
  monero_wallet* wallet = nullptr;

  CHECK(!monero_utils_is_valid_address("", MONERO_UTILS_NETWORK_MAINNET));
  EXPECT_OK(monero_rpc_connection_create("{\"uri\":\"http://127.0.0.1:1\"}", &connection));
  EXPECT_OK(monero_rpc_connection_serialize(connection, &json));
  CHECK(json != nullptr);
  monero_utils_free(json);
  json = nullptr;
  monero_rpc_connection_free(connection);

  EXPECT_OK(monero_wallet_keys_create_random(MONERO_UTILS_NETWORK_MAINNET, nullptr, &wallet));
  EXPECT_OK(monero_wallet_get_primary_address(wallet, &json));
  CHECK(json != nullptr);
  monero_utils_free(json);
  monero_wallet_free(wallet);

  EXPECT_ERR_MSG(monero_daemon_get_height(nullptr, nullptr), "daemon must not be null");
}

// the struct of a first version, and the one of a later version that has one more callback at the end
struct callbacks_v1 {
  uint32_t struct_size;
  void* user_data;
  void (*first)(void*);
};

struct callbacks_v2 {
  uint32_t struct_size;
  void* user_data;
  void (*first)(void*);
  void (*second)(void*);
};

static void callback(void*) {}

static const size_t V1_MIN_SIZE = offsetof(callbacks_v1, user_data) + sizeof(void*);

// a program built with the first version, read by a library of the second: the callback that the program doesn't have is NULL
static void test_older_callbacks_are_completed() {
  callbacks_v1 older = {sizeof(callbacks_v1), &older, callback};
  callbacks_v2 copy = monero_c::copy_callbacks(*reinterpret_cast<const callbacks_v2*>(&older), V1_MIN_SIZE);
  CHECK(copy.struct_size == sizeof(callbacks_v2));
  CHECK(copy.user_data == &older);
  CHECK(copy.first == callback);
  CHECK(copy.second == nullptr);
}

// a struct that ends right after user_data has no callbacks, whatever the bytes after it are
static void test_bytes_past_the_size_are_not_read() {
  callbacks_v2 full = {offsetof(callbacks_v2, user_data) + sizeof(void*), &full, callback, callback};
  callbacks_v2 copy = monero_c::copy_callbacks(full, V1_MIN_SIZE);
  CHECK(copy.user_data == &full);
  CHECK(copy.first == nullptr && copy.second == nullptr);
}

// a program built with a later version, read by a library of the first: the members that the library doesn't know are left out
static void test_newer_callbacks_are_cut() {
  callbacks_v2 newer = {sizeof(callbacks_v2), &newer, callback, callback};
  callbacks_v1 copy = monero_c::copy_callbacks(*reinterpret_cast<const callbacks_v1*>(&newer), V1_MIN_SIZE);
  CHECK(copy.struct_size == sizeof(callbacks_v1));
  CHECK(copy.user_data == &newer);
  CHECK(copy.first == callback);
}

static void test_struct_size_too_small() {
  const uint32_t sizes[] = {0, 1, 4, static_cast<uint32_t>(V1_MIN_SIZE - 1)};
  for (uint32_t size : sizes) {
    callbacks_v1 callbacks = {size, nullptr, callback};
    bool threw = false;
    try {
      monero_c::copy_callbacks(callbacks, V1_MIN_SIZE);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    CHECK(threw);
  }
}

// wipe() zeroes the bytes in place, whether the characters are in the string itself or on the heap
static void test_wipe() {
  unsigned char bytes[16];
  std::memset(bytes, 0xAB, sizeof(bytes));
  monero_c::wipe(bytes, 12);
  for (size_t i = 0; i < 12; i++) CHECK(bytes[i] == 0);
  for (size_t i = 12; i < sizeof(bytes); i++) CHECK(bytes[i] == 0xAB);

  const char* texts[] = {"short", "a seed of twenty-five words, long enough to leave the buffer of the string and live on the heap"};
  for (const char* text : texts) {
    std::string value(text);
    const char* data = value.data();
    size_t size = value.size();
    monero_c::wipe(value);
    CHECK(value.empty());
    bool zeroed = true;
    for (size_t i = 0; i < size; i++) zeroed = zeroed && data[i] == 0;
    CHECK(zeroed);
  }

  std::string empty;
  monero_c::wipe(empty);
  CHECK(empty.empty());
}

static void test_secret_string() {
  monero_c::secret_string none(nullptr);
  CHECK(none.get().empty());
  monero_c::secret_string password("a password");
  CHECK(password.get() == "a password");
}

// ------------------------------ EVENT QUEUE ---------------------------------

using monero_c::event_queue;

// an event that blocks the thread of the queue until the test opens the gate, so the events behind it wait
struct gate {
  std::atomic<bool> entered{false};
  std::atomic<bool> open{false};

  void block() {
    entered = true;
    while (!open) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  void wait_until_entered() {
    for (int i = 0; i < 5000 && !entered; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
};

static std::shared_ptr<event_queue> new_queue() { return std::make_shared<event_queue>(); }

static void test_queue_delivers_in_order() {
  auto queue = new_queue();
  std::vector<int> seen;
  for (int i = 0; i < 200; i++) queue->post(0, 0, [&seen, i] { seen.push_back(i); });
  queue->flush();
  CHECK(seen.size() == 200);
  bool in_order = true;
  for (size_t i = 0; i < seen.size(); i++) in_order = in_order && seen[i] == static_cast<int>(i);
  CHECK(in_order);
  queue->stop();
}

// a kind with the limit 1 keeps only the newest event that waits, at the end of the queue; a limit of 3 keeps the newest three; and
// a kind with no limit keeps everything
static void test_queue_limits() {
  auto queue = new_queue();
  gate blocker;
  std::vector<int> seen;
  queue->post(0, 0, [&blocker] { blocker.block(); });
  blocker.wait_until_entered();

  for (int i = 0; i < 5; i++) queue->post(1, 1, [&seen, i] { seen.push_back(10 + i); });
  for (int i = 0; i < 6; i++) queue->post(2, 3, [&seen, i] { seen.push_back(20 + i); });
  for (int i = 0; i < 4; i++) queue->post(3, 0, [&seen, i] { seen.push_back(30 + i); });
  blocker.open = true;
  queue->flush();

  // the event of kind 1 was replaced by the newest, and the kind 2 events are the last three
  const int expected[] = {14, 23, 24, 25, 30, 31, 32, 33};
  CHECK(seen.size() == 8);
  bool same = seen.size() == 8;
  for (size_t i = 0; same && i < 8; i++) same = seen[i] == expected[i];
  CHECK(same);
  queue->stop();
}

static void test_queue_flush_waits_for_the_event_that_runs() {
  auto queue = new_queue();
  std::atomic<bool> done{false};
  queue->post(0, 0, [&done] {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    done = true;
  });
  queue->flush();
  CHECK(done);
  queue->stop();
}

static void test_queue_cancel_drops_what_waits() {
  auto queue = new_queue();
  gate blocker;
  std::atomic<bool> first_done{false};
  std::atomic<int> ran_after{0};
  queue->post(0, 0, [&] {
    blocker.block();
    first_done = true;
  });
  blocker.wait_until_entered();
  queue->post(0, 0, [&ran_after] { ran_after++; });
  queue->post(1, 1, [&ran_after] { ran_after++; });

  std::thread opener([&blocker] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    blocker.open = true;
  });
  queue->cancel();
  opener.join();
  CHECK(first_done);
  CHECK(ran_after == 0);

  // the queue works after a cancel
  queue->post(0, 0, [&ran_after] { ran_after++; });
  queue->flush();
  CHECK(ran_after == 1);
  queue->stop();
}

// a callback can post, flush and cancel its own queue without waiting for itself
static void test_queue_callbacks_can_use_their_queue() {
  auto queue = new_queue();
  std::atomic<int> count{0};
  std::atomic<bool> finished{false};
  queue->post(0, 0, [&, queue] {
    queue->post(0, 0, [&count] { count++; });
    queue->flush();   // would wait for itself
    queue->cancel();  // drops the event it just posted, and would wait for itself
    finished = true;
  });
  queue->flush();
  CHECK(finished);
  CHECK(count == 0);
  queue->stop();
}

static void test_queue_survives_an_exception() {
  auto queue = new_queue();
  std::atomic<int> count{0};
  queue->post(0, 0, [] { throw std::runtime_error("from a callback"); });
  queue->post(0, 0, [&count] { count++; });
  queue->flush();
  CHECK(count == 1);
  queue->stop();
}

static void test_queue_stop_waits_for_the_event_that_runs_and_refuses_more() {
  auto queue = new_queue();
  gate blocker;
  std::atomic<bool> done{false};
  std::atomic<int> ran_after{0};
  queue->post(0, 0, [&] {
    blocker.block();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    done = true;
  });
  blocker.wait_until_entered();
  std::thread opener([&blocker] { blocker.open = true; });
  queue->stop();
  opener.join();
  CHECK(done);
  queue->post(0, 0, [&ran_after] { ran_after++; });
  queue->flush();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(ran_after == 0);
}

// a callback that stops its own queue, which is what freeing a listener from its own callback does, lets the callback return
static void test_queue_can_be_stopped_by_its_own_callback() {
  auto queue = new_queue();
  std::atomic<bool> returned{false};
  std::atomic<int> ran_after{0};
  queue->post(0, 0, [&, queue] {
    queue->post(0, 0, [&ran_after] { ran_after++; });
    queue->stop();
    returned = true;
  });
  for (int i = 0; i < 5000 && !returned; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(returned);
  queue.reset();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(ran_after == 0);
}

// several threads post at once
static void test_queue_with_several_producers() {
  auto queue = new_queue();
  std::atomic<int> count{0};
  std::vector<std::thread> producers;
  for (int t = 0; t < 4; t++) {
    producers.emplace_back([queue, &count] {
      for (int i = 0; i < 500; i++) queue->post(0, 0, [&count] { count++; });
    });
  }
  for (std::thread& producer : producers) producer.join();
  queue->flush();
  CHECK(count == 2000);
  queue->stop();
}

// a system with the files that have content and the directories that have certificates by hashed name
struct fake_system {
  std::set<std::string> files;
  std::set<std::string> directories;

  monero_c::ca_paths find(const char* env_file, const char* env_dir, const std::string& default_file, const std::string& default_dir) const {
    return monero_c::find_ca_paths(env_file, env_dir, default_file, default_dir,
                                   [this](const std::string& path) { return files.count(path) > 0; },
                                   [this](const std::string& path) { return directories.count(path) > 0; });
  }
};

// the OpenSSL of the library has the directory of the machine that built it, and the helper chooses what to export when the
// certificates aren't there. It is not part of the ABI, so it runs here on fake systems
static void test_ca_paths() {
  const std::string linux_file = "/etc/ssl/cert.pem";
  const std::string linux_dir = "/etc/ssl/certs";
  const std::string mac_file = "/opt/homebrew/etc/openssl@3/cert.pem";
  const std::string mac_dir = "/opt/homebrew/etc/openssl@3/certs";

  // Debian and Ubuntu: the default directory of the Linux build has the certificates, so nothing is set
  fake_system debian;
  debian.files = {"/etc/ssl/certs/ca-certificates.crt"};
  debian.directories = {"/etc/ssl/certs"};
  monero_c::ca_paths paths = debian.find(nullptr, nullptr, linux_file, linux_dir);
  CHECK(paths.file.empty() && paths.dir.empty());

  // a Mac without Homebrew: the default directory isn't there, and the system has /etc/ssl/cert.pem
  fake_system mac;
  mac.files = {"/etc/ssl/cert.pem"};
  paths = mac.find(nullptr, nullptr, mac_file, mac_dir);
  CHECK(paths.file == "/etc/ssl/cert.pem" && paths.dir.empty());

  // a Mac with Homebrew: its default file is there, even if the directory is empty
  fake_system brew = mac;
  brew.files.insert(mac_file);
  paths = brew.find(nullptr, nullptr, mac_file, mac_dir);
  CHECK(paths.file.empty() && paths.dir.empty());

  // Fedora and RHEL: the directory has no hashed names, so the bundle is chosen
  fake_system fedora;
  fedora.files = {"/etc/pki/tls/certs/ca-bundle.crt"};
  paths = fedora.find(nullptr, nullptr, linux_file, linux_dir);
  CHECK(paths.file == "/etc/pki/tls/certs/ca-bundle.crt" && paths.dir.empty());

  // openSUSE
  fake_system suse;
  suse.files = {"/etc/ssl/ca-bundle.pem"};
  paths = suse.find(nullptr, nullptr, linux_file, linux_dir);
  CHECK(paths.file == "/etc/ssl/ca-bundle.pem");

  // the bundle and the directory are both chosen when the default has neither
  fake_system both;
  both.files = {"/etc/ssl/certs/ca-certificates.crt", "/etc/ssl/cert.pem"};
  both.directories = {"/etc/ssl/certs"};
  paths = both.find(nullptr, nullptr, "/opt/openssl/cert.pem", "/opt/openssl/certs");
  CHECK(paths.file == "/etc/ssl/certs/ca-certificates.crt" && paths.dir == "/etc/ssl/certs");

  // Android has a directory and no bundle
  fake_system android;
  android.directories = {"/system/etc/security/cacerts"};
  paths = android.find(nullptr, nullptr, linux_file, linux_dir);
  CHECK(paths.file.empty() && paths.dir == "/system/etc/security/cacerts");

  // nothing to choose from
  paths = fake_system().find(nullptr, nullptr, mac_file, mac_dir);
  CHECK(paths.file.empty() && paths.dir.empty());

  // the caller's variables win, and an empty one is not set
  paths = mac.find("/my/bundle.pem", nullptr, mac_file, mac_dir);
  CHECK(paths.file.empty() && paths.dir.empty());
  paths = mac.find(nullptr, "/my/certs", mac_file, mac_dir);
  CHECK(paths.file.empty() && paths.dir.empty());
  paths = mac.find("", "", mac_file, mac_dir);
  CHECK(paths.file == "/etc/ssl/cert.pem");
}

// the certificates by hashed name that OpenSSL looks for in a directory, such as 5ad8a5d6.0. A name that is too short, or that
// has no number after the dot, is not one, and the check doesn't read past the end of the name
static void test_hashed_certificate_names() {
  CHECK(monero_c::is_hashed_certificate_name("5ad8a5d6.0"));
  CHECK(monero_c::is_hashed_certificate_name("5ad8a5d6.12"));
  CHECK(monero_c::is_hashed_certificate_name("0A1b2C3d.3"));

  CHECK(!monero_c::is_hashed_certificate_name(""));
  CHECK(!monero_c::is_hashed_certificate_name("."));
  CHECK(!monero_c::is_hashed_certificate_name("5ad8a5d"));
  CHECK(!monero_c::is_hashed_certificate_name("5ad8a5d6"));
  CHECK(!monero_c::is_hashed_certificate_name("5ad8a5d6."));
  CHECK(!monero_c::is_hashed_certificate_name("5ad8a5d6.r0"));   // a CRL
  CHECK(!monero_c::is_hashed_certificate_name("5ad8a5d6.0.crt"));
  CHECK(!monero_c::is_hashed_certificate_name("5ad8a5dz.0"));
  CHECK(!monero_c::is_hashed_certificate_name("5ad8a5d60.0"));   // nine digits
  CHECK(!monero_c::is_hashed_certificate_name("ca-certificates.crt"));
}

int main() {
  test_calls_from_cpp();
  test_older_callbacks_are_completed();
  test_bytes_past_the_size_are_not_read();
  test_newer_callbacks_are_cut();
  test_struct_size_too_small();
  test_wipe();
  test_secret_string();
  test_queue_delivers_in_order();
  test_queue_limits();
  test_queue_flush_waits_for_the_event_that_runs();
  test_queue_cancel_drops_what_waits();
  test_queue_callbacks_can_use_their_queue();
  test_queue_survives_an_exception();
  test_queue_stop_waits_for_the_event_that_runs_and_refuses_more();
  test_queue_can_be_stopped_by_its_own_callback();
  test_queue_with_several_producers();
  test_ca_paths();
  test_hashed_certificate_names();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

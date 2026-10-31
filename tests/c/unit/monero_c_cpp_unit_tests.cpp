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
#include "common/monero_c_tls.h"

#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <set>
#include <string>

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
  test_ca_paths();
  test_hashed_certificate_names();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

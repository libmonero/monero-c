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
#include "common/monero_c_tls.h"

#include <cstdlib>
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

int main() {
  test_calls_from_cpp();
  test_ca_paths();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

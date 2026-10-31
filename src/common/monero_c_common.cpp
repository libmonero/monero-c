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

#include "monero_c_common.h"
#include "monero_c_tls.h"

#include "common/monero_error.h"

#include <boost/system/system_error.hpp>
#include <ios>
#include <new>
#include <stdexcept>
#include <system_error>

#if !defined(_WIN32)
#include <dirent.h>
#include <openssl/x509.h>
#include <sys/stat.h>
#endif

// monero-project's own, from crypto/slow-hash.c: it frees the scratchpad of the calling thread
extern "C" void slow_hash_free_state(void);

namespace monero_c {

namespace {

thread_local std::string g_last_error;
thread_local monero_error_code g_last_error_code = MONERO_ERROR_NONE;

struct slow_hash_state_owner {
  ~slow_hash_state_owner() { slow_hash_free_state(); }
};

} // namespace

void free_slow_hash_state_at_thread_exit() noexcept {
  thread_local slow_hash_state_owner owner;
  (void) owner;
}

void set_last_error(const char* msg, monero_error_code code) noexcept {
  g_last_error_code = code;
  try { g_last_error = msg; } catch (...) { g_last_error.clear(); }
}

void set_null_error(const char* name) noexcept {
  g_last_error_code = MONERO_ERROR_INVALID_ARGUMENT;
  try { g_last_error = std::string(name) + " must not be null"; } catch (...) { g_last_error.clear(); }
}

void set_last_error_from_current_exception() noexcept {
  try {
    throw;
  } catch (const std::bad_alloc& e) {
    set_last_error(e.what(), MONERO_ERROR_OUT_OF_MEMORY);
  } catch (const std::invalid_argument& e) {
    set_last_error(e.what(), MONERO_ERROR_INVALID_ARGUMENT);
  } catch (const monero::monero_rpc_error& e) {
    set_last_error(e.what(), MONERO_ERROR_RPC);
  } catch (const std::ios_base::failure& e) {
    set_last_error(e.what(), MONERO_ERROR_IO);
  } catch (const std::system_error& e) {
    set_last_error(e.what(), MONERO_ERROR_IO);
  } catch (const boost::system::system_error& e) {
    set_last_error(e.what(), MONERO_ERROR_IO);
  } catch (const std::exception& e) {
    set_last_error(e.what(), MONERO_ERROR_GENERIC);
  } catch (...) {
    set_last_error("unknown error", MONERO_ERROR_GENERIC);
  }
}

const std::string& last_error() {
  return g_last_error;
}

monero_error_code last_error_code() {
  return g_last_error_code;
}

void check_json_depth(const std::string& json) {
  size_t depth = 0;
  bool in_string = false;
  for (size_t i = 0; i < json.size(); i++) {
    char c = json[i];
    if (in_string) {
      if (c == '\\') i++;
      else if (c == '"') in_string = false;
    } else if (c == '"') {
      in_string = true;
    } else if (c == '[' || c == '{') {
      if (++depth > MAX_JSON_DEPTH) throw std::invalid_argument("JSON is nested deeper than " + std::to_string(MAX_JSON_DEPTH) + " levels");
    } else if ((c == ']' || c == '}') && depth > 0) {
      depth--;
    }
  }
}

} // namespace monero_c

#if !defined(_WIN32)

namespace {

bool is_file_with_content(const std::string& path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

// true if the directory has a certificate by its hashed name, such as 5ad8a5d6.0
bool has_hashed_certificates(const std::string& path) {
  DIR* dir = opendir(path.c_str());
  if (dir == nullptr) return false;
  bool found = false;
  while (struct dirent* entry = readdir(dir)) {
    if (monero_c::is_hashed_certificate_name(entry->d_name)) {
      found = true;
      break;
    }
  }
  closedir(dir);
  return found;
}

// runs when the library is loaded, before a connection makes a TLS context. See find_ca_paths() for why. Windows needs none, since
// epee reads the root store of the system there
__attribute__((unused)) const bool g_ca_paths_set = [] {
  monero_c::ca_paths paths = monero_c::find_ca_paths(getenv("SSL_CERT_FILE"), getenv("SSL_CERT_DIR"), X509_get_default_cert_file(), X509_get_default_cert_dir(),
                                                     is_file_with_content, has_hashed_certificates);
  if (!paths.file.empty()) setenv("SSL_CERT_FILE", paths.file.c_str(), 1);
  if (!paths.dir.empty()) setenv("SSL_CERT_DIR", paths.dir.c_str(), 1);
  return true;
}();

} // namespace

#endif

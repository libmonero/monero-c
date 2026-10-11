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

#ifndef MONERO_C_TLS_H
#define MONERO_C_TLS_H

#include <cctype>
#include <functional>
#include <string>

// where OpenSSL finds the CA certificates of the system. Not part of the public ABI, and it needs no OpenSSL header, so a test
// can run it on a fake file system
namespace monero_c {

// what to export as SSL_CERT_FILE and SSL_CERT_DIR. An empty string leaves the variable as it is
struct ca_paths {
  std::string file;
  std::string dir;
};

// the bundles of the CA certificates of the common systems, in the order that they are tried: Debian and Ubuntu, Fedora and RHEL,
// openSUSE, then Alpine and macOS
static const char* const CA_FILES[] = {
  "/etc/ssl/certs/ca-certificates.crt",
  "/etc/pki/tls/certs/ca-bundle.crt",
  "/etc/ssl/ca-bundle.pem",
  "/etc/ssl/cert.pem",
};

// the directories of certificates by hashed name (5ad8a5d6.0), the one of Android included
static const char* const CA_DIRS[] = {
  "/etc/ssl/certs",
  "/etc/pki/tls/certs",
  "/system/etc/security/cacerts",
};

// true if the name is a certificate by its hashed name, which is 8 hex digits, a dot and a number, such as 5ad8a5d6.0
inline bool is_hashed_certificate_name(const std::string& name) {
  if (name.size() < 10 || name[8] != '.') return false;
  for (size_t i = 0; i < 8; i++) {
    if (!std::isxdigit(static_cast<unsigned char>(name[i]))) return false;
  }
  for (size_t i = 9; i < name.size(); i++) {
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
  }
  return true;
}

// chooses what to export so that OpenSSL finds the CA certificates. The OpenSSL in the library has the directory of the machine
// that built it in its code, and it looks for the certificates there, so a connection with a certificate of a CA fails where that
// directory isn't, such as a Mac without Homebrew. Nothing is chosen if the caller set SSL_CERT_FILE or SSL_CERT_DIR, or if
// OpenSSL finds the certificates by itself: its default file is there, or its default directory has certificates by hashed name.
// A directory that exists proves nothing, since the one of Homebrew exists and is empty. is_file tells that a path is a file with
// content, and has_certificates that a path is a directory with certificates by hashed name
inline ca_paths find_ca_paths(const char* env_file, const char* env_dir, const std::string& default_file, const std::string& default_dir,
                              const std::function<bool(const std::string&)>& is_file, const std::function<bool(const std::string&)>& has_certificates) {
  ca_paths paths;
  if ((env_file != nullptr && env_file[0] != '\0') || (env_dir != nullptr && env_dir[0] != '\0')) return paths;
  if (!default_file.empty() && is_file(default_file)) return paths;
  if (!default_dir.empty() && has_certificates(default_dir)) return paths;
  for (const char* file : CA_FILES) {
    if (is_file(file)) {
      paths.file = file;
      break;
    }
  }
  for (const char* dir : CA_DIRS) {
    if (has_certificates(dir)) {
      paths.dir = dir;
      break;
    }
  }
  return paths;
}

} // namespace monero_c

#endif // MONERO_C_TLS_H

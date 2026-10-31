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

#ifndef MONERO_C_SECRET_H
#define MONERO_C_SECRET_H

#include <cstddef>
#include <string>

// helpers for the copies of secrets that monero-c makes: a seed, a private key or a password. Not part of the public ABI, and it needs no other
// header of monero-c, so a test can run it
namespace monero_c {

// overwrites n bytes with zeros in a way the compiler can't take away because the memory isn't read again, as memwipe() of monero-project does
inline void wipe(void* data, size_t size) noexcept {
  volatile unsigned char* bytes = static_cast<volatile unsigned char*>(data);
  for (size_t i = 0; i < size; i++) bytes[i] = 0;
#if defined(__GNUC__) || defined(__clang__)
  __asm__ __volatile__("" : : "r"(data) : "memory");
#endif
}

// overwrites the characters of a string and empties it
inline void wipe(std::string& text) noexcept {
  if (!text.empty()) wipe(&text[0], text.size());
  text.clear();
}

// a copy of a text that is wiped when it goes out of scope, for a password or a seed that a function takes as a C string
class secret_string {
public:
  explicit secret_string(const char* text) : value_(text != nullptr ? text : "") {}
  ~secret_string() { wipe(value_); }
  secret_string(const secret_string&) = delete;
  secret_string& operator=(const secret_string&) = delete;

  const std::string& get() const { return value_; }

private:
  std::string value_;
};

} // namespace monero_c

#endif

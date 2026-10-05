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

#ifndef MONERO_C_COMMON_H
#define MONERO_C_COMMON_H

#include "utils/monero_c_utils.h"

#include <boost/optional.hpp>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// helpers shared by the monero_* translation units. Not part of the public ABI
namespace monero_c {

// records msg as the calling thread's last error
void set_last_error(const std::string& msg);

// returns the calling thread's last error, or "" if none
const std::string& last_error();

inline std::string safe_str(const char* s) {
  return s ? std::string(s) : std::string();
}

// mallocs at least 1 byte so a successful zero-length allocation is never
// mistaken for malloc() failure (which is the only case this throws)
inline char* dup_string(const std::string& s) {
  char* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (out == nullptr) throw std::bad_alloc();
  std::memcpy(out, s.c_str(), s.size() + 1);
  return out;
}

// runs fn, converting any thrown exception into MONERO_ERROR + last_error
template <class F>
monero_result guard(F&& fn) {
  try {
    fn();
    return MONERO_OK;
  } catch (const std::exception& e) {
    set_last_error(e.what());
    return MONERO_ERROR;
  } catch (...) {
    set_last_error("unknown error");
    return MONERO_ERROR;
  }
}

// checks a required pointer. On failure, sets last_error to "<name> must not be null"
inline bool require(const void* ptr, const char* name) {
  if (ptr != nullptr) return true;
  set_last_error(std::string(name) + " must not be null");
  return false;
}

// copies a C array. Throws if it is NULL and the count is not zero
template <class T>
std::vector<T> array_of(const T* items, size_t count) {
  if (count == 0) return std::vector<T>();
  if (items == nullptr) throw std::invalid_argument("array must not be null");
  return std::vector<T>(items, items + count);
}

// copies a C array of strings. Throws if an element is NULL
inline std::vector<std::string> string_array(const char* const* items, size_t count) {
  std::vector<std::string> out;
  if (count == 0) return out;
  if (items == nullptr) throw std::invalid_argument("array must not be null");
  for (size_t i = 0; i < count; i++) {
    if (items[i] == nullptr) throw std::invalid_argument("array element must not be null");
    out.push_back(items[i]);
  }
  return out;
}

// maps an optional argument (NULL means none) to boost::optional
template <class T>
boost::optional<T> optional_of(const T* value) {
  boost::optional<T> out;
  if (value != nullptr) out = *value;
  return out;
}

// copies items into a new array that free() releases with monero_utils_free(). The
// allocation is never NULL, so an empty result is still a valid pointer
template <class T>
T* dup_array(const std::vector<T>& items, size_t* out_count) {
  static_assert(std::is_trivially_copyable<T>::value, "dup_array assigns into malloc'd memory, so T must be trivially copyable");
  T* out = static_cast<T*>(std::malloc((items.empty() ? 1 : items.size()) * sizeof(T)));
  if (out == nullptr) throw std::bad_alloc();
  for (size_t i = 0; i < items.size(); i++) out[i] = items[i];
  *out_count = items.size();
  return out;
}

} // namespace monero_c

#endif // MONERO_C_COMMON_H

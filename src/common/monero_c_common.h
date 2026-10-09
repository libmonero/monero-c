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
#include "common/monero_c_rpc_connection.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <boost/optional.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace monero {
class monero_rpc_connection;
}

// helpers shared by the monero_* translation units. Not part of the public ABI
namespace monero_c {

// the monero-cpp connection behind a handle, shared with the daemons and wallets created from it
std::shared_ptr<monero::monero_rpc_connection> connection_of(::monero_rpc_connection* connection);

// records msg as the calling thread's last error. It never throws: if the message can't be
// stored because memory ran out, the last error is empty
void set_last_error(const char* msg) noexcept;

// records "<name> must not be null" as the last error, with the same guarantee
void set_null_error(const char* name) noexcept;

// returns the calling thread's last error, or "" if none
const std::string& last_error();

inline std::string safe_str(const char* s) {
  return s ? std::string(s) : std::string();
}

// the deepest nesting that a JSON argument may have. The parsers behind the arguments
// recurse once per level, so a deeper document overflows the stack
const size_t MAX_JSON_DEPTH = 64;

// throws std::invalid_argument if json nests objects and arrays deeper than MAX_JSON_DEPTH
void check_json_depth(const std::string& json);

// the longest mnemonic that is checked. A valid one is a few hundred bytes, and monero-project
// splits it in time quadratic in its length, so a megabyte of text would hold a call for minutes
const size_t MAX_MNEMONIC_LENGTH = 4096;

// throws std::invalid_argument if the mnemonic is longer than MAX_MNEMONIC_LENGTH
inline void check_mnemonic_length(const std::string& mnemonic) {
  if (mnemonic.size() > MAX_MNEMONIC_LENGTH) throw std::invalid_argument("mnemonic is longer than " + std::to_string(MAX_MNEMONIC_LENGTH) + " bytes");
}

// mallocs at least 1 byte so a successful zero-length allocation is never
// mistaken for malloc() failure (which is the only case this throws)
inline char* dup_string(const std::string& s) {
  char* out = static_cast<char*>(std::malloc(s.size() + 1));
  if (out == nullptr) throw std::bad_alloc();
  std::copy(s.c_str(), s.c_str() + s.size() + 1, out);
  return out;
}

// maps the C network enum to monero-cpp's, which has the same values except regtest. Regtest maps
// to mainnet, and the caller keeps the flag. Returns false if the value is out of range
template <class NetworkType>
bool to_network_type(int32_t network_type, NetworkType& out) {
  if (network_type < MONERO_UTILS_NETWORK_MAINNET || network_type > MONERO_UTILS_NETWORK_REGTEST) return false;
  out = network_type == MONERO_UTILS_NETWORK_REGTEST ? NetworkType::MAINNET : static_cast<NetworkType>(network_type);
  return true;
}

// like to_network_type(), but throws if the value is out of range
template <class NetworkType>
NetworkType network_of(int32_t network_type) {
  NetworkType out = NetworkType::MAINNET;
  if (!to_network_type(network_type, out)) throw std::invalid_argument("unknown network type");
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
  set_null_error(name);
  return false;
}

// sets an output to NULL (a count to 0), unless the pointer to it is NULL. Done before the
// arguments are checked, so a failed call never leaves a stale pointer for the caller to free
template <class T>
void reset_out(T* out) {
  if (out != nullptr) *out = T();
}

// copies a C array. Throws if it is NULL and the count is not zero. The name is the argument,
// for the error message
template <class T>
std::vector<T> array_of(const T* items, size_t count, const char* name) {
  if (count == 0) return std::vector<T>();
  if (items == nullptr) throw std::invalid_argument(std::string(name) + " must not be null");
  return std::vector<T>(items, items + count);
}

// copies a C array of strings. Throws if the array is NULL and the count is not zero, or if
// an element is NULL
inline std::vector<std::string> string_array(const char* const* items, size_t count, const char* name) {
  std::vector<std::string> out;
  if (count == 0) return out;
  if (items == nullptr) throw std::invalid_argument(std::string(name) + " must not be null");
  for (size_t i = 0; i < count; i++) {
    if (items[i] == nullptr) throw std::invalid_argument(std::string(name) + " must not contain NULL");
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

// copies binary data into a new buffer that monero_utils_free() releases. The buffer is
// never NULL, so an empty result is still a valid pointer
inline uint8_t* dup_buffer(const std::string& bin, size_t* out_len) {
  uint8_t* out = static_cast<uint8_t*>(std::malloc(bin.empty() ? 1 : bin.size()));
  if (out == nullptr) throw std::bad_alloc();
  std::copy(bin.begin(), bin.end(), out);
  *out_len = bin.size();
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

// serializes a list of monero-cpp objects as a JSON array. Null items stay null
template <class T>
std::string json_of_list(const std::vector<std::shared_ptr<T>>& items) {
  std::string json = "[";
  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0) json += ",";
    json += items[i] ? items[i]->serialize() : std::string("null");
  }
  return json + "]";
}

// serializes a list of monero-cpp objects that are held by value
template <class T>
std::string json_of_list(const std::vector<T>& items) {
  std::string json = "[";
  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0) json += ",";
    json += items[i].serialize();
  }
  return json + "]";
}

// serializes a JSON array of strings, e.g. hashes or hexes
inline std::string json_of_strings(const std::vector<std::string>& items) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartArray();
  for (const auto& item : items) writer.String(item.c_str(), static_cast<rapidjson::SizeType>(item.size()));
  writer.EndArray();
  return buffer.GetString();
}

} // namespace monero_c

#endif // MONERO_C_COMMON_H

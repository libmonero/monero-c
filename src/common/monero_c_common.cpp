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

namespace monero_c {

namespace {

thread_local std::string g_last_error;

} // namespace

void set_last_error(const char* msg) noexcept {
  try { g_last_error = msg; } catch (...) { g_last_error.clear(); }
}

void set_null_error(const char* name) noexcept {
  try { g_last_error = std::string(name) + " must not be null"; } catch (...) { g_last_error.clear(); }
}

const std::string& last_error() {
  return g_last_error;
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

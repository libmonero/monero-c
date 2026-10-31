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

#ifndef MONERO_C_CALLBACKS_H
#define MONERO_C_CALLBACKS_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

// how the structs of callbacks of the public ABI are read. Not part of the public ABI, and it needs no other header of monero-c, so a test
// can run it on a struct of its own
namespace monero_c {

// copies the callbacks of a struct that the caller built with a header of any version: the members that fit in struct_size, and NULL for
// the ones that a newer header has and the caller's struct hasn't. T has struct_size as its first member, and min_size is the size up to
// the end of the last member that the first version of the struct had, user_data, which struct_size has to reach. Throws
// std::invalid_argument if it doesn't. The copy has the struct_size of T, so what is read from it is complete
template <class T>
T copy_callbacks(const T& callbacks, size_t min_size) {
  if (callbacks.struct_size < min_size) {
    throw std::invalid_argument("callbacks.struct_size is " + std::to_string(callbacks.struct_size) + ", set it to sizeof of the struct");
  }
  T copy = T();
  std::memcpy(&copy, &callbacks, std::min<size_t>(callbacks.struct_size, sizeof(T)));
  copy.struct_size = static_cast<uint32_t>(sizeof(T));
  return copy;
}

} // namespace monero_c

#endif

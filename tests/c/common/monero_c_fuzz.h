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

#ifndef MONERO_C_FUZZ_H
#define MONERO_C_FUZZ_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// a deterministic mutation of the arguments, for the tests that give the library what it should never trust: JSON, hex and binary
// that a caller or a peer wrote. The same seed gives the same inputs on every platform, so a failure can be reproduced. Set
// MONERO_C_FUZZ_SEED and MONERO_C_FUZZ_ITERATIONS to run others

typedef struct fuzz_rng {
  uint64_t state;
} fuzz_rng;

static inline void fuzz_seed(fuzz_rng* rng, uint64_t seed) {
  rng->state = seed != 0 ? seed : 0x9e3779b97f4a7c15ULL;
}

// xorshift64*
static inline uint64_t fuzz_next(fuzz_rng* rng) {
  rng->state ^= rng->state >> 12;
  rng->state ^= rng->state << 25;
  rng->state ^= rng->state >> 27;
  return rng->state * 0x2545F4914F6CDD1DULL;
}

// a number in 0..n-1, or 0 if n is 0
static inline size_t fuzz_below(fuzz_rng* rng, size_t n) {
  return n == 0 ? 0 : (size_t) (fuzz_next(rng) % n);
}

// the seed of the run
static inline uint64_t fuzz_seed_value(void) {
  const char* text = getenv("MONERO_C_FUZZ_SEED");
  return text != NULL && text[0] != '\0' ? (uint64_t) strtoull(text, NULL, 10) : 20241012ULL;
}

// the number of inputs for each target of the run
static inline size_t fuzz_iterations(size_t fallback) {
  const char* text = getenv("MONERO_C_FUZZ_ITERATIONS");
  return text != NULL && text[0] != '\0' ? (size_t) strtoull(text, NULL, 10) : fallback;
}

// the length of a text, up to max. The mutators read the text that the tests give them, and this reads no further than the buffer that holds it
#define FUZZ_MAX_TEXT 1048576u

static inline size_t fuzz_text_length(const char* text, size_t max) {
  size_t length = 0;
  while (length < max && text[length] != '\0') length++;
  return length;
}

// copies count bytes to a buffer of cap bytes, and no more than it holds. The ranges can overlap
static inline void fuzz_copy(void* destination, size_t cap, const void* source, size_t count) {
  memmove(destination, source, count < cap ? count : cap);
}

// what a JSON value is replaced by, or what is inserted: the numbers at the edges of the integer types, the text that parsers get wrong,
// and the shapes that no model has
static const char* const FUZZ_TOKENS[] = {
  "null", "true", "false", "0", "-0", "1", "-1", "2", "255", "256", "65535", "65536", "2147483647", "2147483648", "4294967295", "4294967296",
  "9007199254740993", "9223372036854775807", "9223372036854775808", "18446744073709551615", "18446744073709551616", "-9223372036854775808",
  "-9223372036854775809", "0.5", "1e308", "1e999", "-1e999", "1E-999", "1.7976931348623157e309", "\"\"", "\"x\"", "\" \"", "\"0\"", "\"-1\"", "\"1e5\"",
  "\"0x10\"", "\"18446744073709551616\"", "\"true\"", "\"null\"", "\"%s%n\"", "\"\\u0000\"", "\"\\ud800\"", "\"\\udfff\"", "\"\\u00e9\\u20ac\"", "\"\\\\\"",
  "\"\\\"\"", "[]", "{}", "[[]]", "[{}]", "[null]", "[1,2,3]", "[\"x\",\"y\"]", "{\"a\":{}}", "{\"a\":[]}", "[[[[[[[[]]]]]]]]", "{\"a\":{\"a\":{\"a\":{}}}}",
  "\"\xff\xfe\"", "\"\xc3\x28\"", "\"\xf0\x9f\x98\x80\"", "{\"txs\":[]}", "{\"hex\":\"\"}", "{\"amount\":\"1\"}", "[\"\"]", "[0]", "[-1]",
};

#define FUZZ_TOKEN_COUNT (sizeof(FUZZ_TOKENS) / sizeof(FUZZ_TOKENS[0]))

// the characters that a byte is replaced with in a text: the ones that give JSON its structure, and a few that break it
static const char FUZZ_STRUCTURE[] = "{}[]\",:\\-+.eE0123456789tfnul \t\n";

// the end of the JSON value that starts at start, in a text of length. It reads a string, a number or a literal up to the next , } or ], and
// a nested value up to its closing bracket
static inline size_t fuzz_value_end(const char* text, size_t length, size_t start) {
  size_t i = start;
  int depth = 0;
  int in_string = 0;
  while (i < length) {
    char c = text[i];
    if (in_string) {
      if (c == '\\') i++;
      else if (c == '"') {
        in_string = 0;
        if (depth == 0) return i + 1;
      }
    } else if (c == '"') {
      in_string = 1;
    } else if (c == '{' || c == '[') {
      depth++;
    } else if (c == '}' || c == ']') {
      if (depth == 0) return i;
      if (--depth == 0) return i + 1;
    } else if (c == ',' && depth == 0) {
      return i;
    }
    i++;
  }
  return length;
}

// copies text into out with one to four changes, and returns the length. The result has no NUL, since it is passed as a C string, and
// it is at most cap - 1 long
static inline size_t fuzz_mutate_text(fuzz_rng* rng, const char* text, char* out, size_t cap) {
  size_t length = fuzz_text_length(text, FUZZ_MAX_TEXT);
  size_t changes = 1 + fuzz_below(rng, 4);
  if (length >= cap) length = cap - 1;
  fuzz_copy(out, cap, text, length);

  for (size_t change = 0; change < changes; change++) {
    size_t at = length == 0 ? 0 : fuzz_below(rng, length);
    switch (fuzz_below(rng, 10)) {
      case 0:  // a bit of a byte
        if (length > 0) {
          out[at] = (char) (out[at] ^ (1 << fuzz_below(rng, 7)));
          if (out[at] == '\0') out[at] = ' ';
        }
        break;
      case 1:  // a character that gives JSON its structure
        if (length > 0) out[at] = FUZZ_STRUCTURE[fuzz_below(rng, sizeof(FUZZ_STRUCTURE) - 1)];
        break;
      case 2: {  // a range that goes
        size_t count = 1 + fuzz_below(rng, 32);
        if (at + count > length) count = length - at;
        memmove(out + at, out + at + count, length - at - count);
        length -= count;
        break;
      }
      case 3:  // a token that is added where it is
      case 4: {
        const char* token = FUZZ_TOKENS[fuzz_below(rng, FUZZ_TOKEN_COUNT)];
        size_t count = fuzz_text_length(token, 256);
        if (length + count < cap) {
          memmove(out + at + count, out + at, length - at);
          fuzz_copy(out + at, cap - at, token, count);
          length += count;
        }
        break;
      }
      case 5:
      case 6: {  // the value that follows a colon is replaced by a token
        size_t colon = at;
        while (colon < length && out[colon] != ':') colon++;
        if (colon < length) {
          size_t start = colon + 1;
          size_t end = fuzz_value_end(out, length, start);
          const char* token = FUZZ_TOKENS[fuzz_below(rng, FUZZ_TOKEN_COUNT)];
          size_t count = fuzz_text_length(token, 256);
          if (length - (end - start) + count < cap) {
            memmove(out + start + count, out + end, length - end);
            fuzz_copy(out + start, cap - start, token, count);
            length = length - (end - start) + count;
          }
        }
        break;
      }
      case 7: {  // a range that is copied where it is
        size_t count = 1 + fuzz_below(rng, 64);
        if (at + count > length) count = length - at;
        if (length + count < cap) {
          memmove(out + at + count, out + at, length - at);
          fuzz_copy(out + at, cap - at, out + at + count, count);
          length += count;
        }
        break;
      }
      case 8: {  // a run of one character
        static const size_t runs[] = {8, 100, 4000, 20000};
        size_t count = runs[fuzz_below(rng, 4)];
        char ch = "A0[{\"9-"[fuzz_below(rng, 7)];
        if (length + count < cap) {
          memmove(out + at + count, out + at, length - at);
          memset(out + at, ch, count);
          length += count;
        }
        break;
      }
      default:  // the end of the text
        length = at;
        break;
    }
  }
  out[length] = '\0';
  return length;
}

// the same for hex: the characters stay hex digits, except for a rare one that isn't, and the length can be odd
static inline size_t fuzz_mutate_hex(fuzz_rng* rng, const char* hex, char* out, size_t cap) {
  static const char digits[] = "0123456789abcdef";
  size_t length = fuzz_text_length(hex, FUZZ_MAX_TEXT);
  size_t changes = 1 + fuzz_below(rng, 4);
  if (length >= cap) length = cap - 1;
  fuzz_copy(out, cap, hex, length);

  for (size_t change = 0; change < changes; change++) {
    size_t at = length == 0 ? 0 : fuzz_below(rng, length);
    switch (fuzz_below(rng, 9)) {
      case 0:
        if (length > 0) out[at] = digits[fuzz_below(rng, 16)];
        break;
      case 1: {  // a range that goes
        static const size_t sizes[] = {1, 2, 8, 64, 512};
        size_t count = sizes[fuzz_below(rng, 5)];
        if (at + count > length) count = length - at;
        memmove(out + at, out + at + count, length - at - count);
        length -= count;
        break;
      }
      case 2: {  // digits that are added
        static const size_t sizes[] = {1, 2, 16, 100, 1000};
        size_t count = sizes[fuzz_below(rng, 5)];
        if (length + count < cap) {
          memmove(out + at + count, out + at, length - at);
          for (size_t i = 0; i < count; i++) out[at + i] = digits[fuzz_below(rng, 16)];
          length += count;
        }
        break;
      }
      case 3:
        length = at;
        break;
      case 4: {  // a range that is copied where it is
        static const size_t sizes[] = {2, 16, 64, 500};
        size_t count = sizes[fuzz_below(rng, 4)];
        if (at + count > length) count = length - at;
        if (length + count < cap) {
          memmove(out + at + count, out + at, length - at);
          fuzz_copy(out + at, cap - at, out + at + count, count);
          length += count;
        }
        break;
      }
      case 5:  // bytes that are all ones, which is a count or a size that is too large
      case 6: {
        size_t count = 2 * (1 + fuzz_below(rng, 8));
        char digit = fuzz_below(rng, 2) == 0 ? 'f' : '0';
        if (at + count > length) count = length - at;
        memset(out + at, digit, count);
        break;
      }
      case 7:  // something that isn't a digit
        if (length > 0) out[at] = "gG x-"[fuzz_below(rng, 5)];
        break;
      default:  // a digit that is flipped
        if (length > 0) {
          const char* p = strchr(digits, out[at]);
          out[at] = digits[((p != NULL ? (size_t) (p - digits) : 0) ^ (1u << fuzz_below(rng, 4))) & 15];
        }
        break;
    }
  }
  out[length] = '\0';
  return length;
}

// the same for bytes, which can be anything
static inline size_t fuzz_mutate_bytes(fuzz_rng* rng, const uint8_t* data, size_t length, uint8_t* out, size_t cap) {
  size_t changes = 1 + fuzz_below(rng, 4);
  if (length > cap) length = cap;
  if (length > 0) fuzz_copy(out, cap, data, length);

  for (size_t change = 0; change < changes; change++) {
    size_t at = length == 0 ? 0 : fuzz_below(rng, length);
    switch (fuzz_below(rng, 8)) {
      case 0:
        if (length > 0) out[at] = (uint8_t) (out[at] ^ (1u << fuzz_below(rng, 8)));
        break;
      case 1:
        if (length > 0) out[at] = (uint8_t) fuzz_below(rng, 256);
        break;
      case 2: {
        size_t count = 1 + fuzz_below(rng, 64);
        if (at + count > length) count = length - at;
        memmove(out + at, out + at + count, length - at - count);
        length -= count;
        break;
      }
      case 3: {
        size_t count = 1 + fuzz_below(rng, 32);
        if (length + count <= cap) {
          memmove(out + at + count, out + at, length - at);
          for (size_t i = 0; i < count; i++) out[at + i] = (uint8_t) fuzz_below(rng, 256);
          length += count;
        }
        break;
      }
      case 4:
        length = at;
        break;
      case 5: {  // a field that is all ones or all zeros
        size_t count = 1 + fuzz_below(rng, 8);
        if (at + count > length) count = length - at;
        memset(out + at, fuzz_below(rng, 2) == 0 ? 0xff : 0x00, count);
        break;
      }
      case 6: {  // a range that is copied where it is
        size_t count = 1 + fuzz_below(rng, 256);
        if (at + count > length) count = length - at;
        if (length + count <= cap) {
          memmove(out + at + count, out + at, length - at);
          fuzz_copy(out + at, cap - at, out + at + count, count);
          length += count;
        }
        break;
      }
      default: {  // a size that is large
        static const uint32_t sizes[] = {0, 1, 0x7f, 0x80, 0xff, 0x100, 0xffff, 0x7fffffffU, 0x80000000U, 0xffffffffU};
        uint32_t value = sizes[fuzz_below(rng, sizeof(sizes) / sizeof(sizes[0]))];
        if (at + 4 <= length) fuzz_copy(out + at, length - at, &value, 4);
        break;
      }
    }
  }
  return length;
}

// writes the bytes as hex into out, which holds 2 * length + 1 characters
static inline void fuzz_to_hex(const uint8_t* data, size_t length, char* out) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < length; i++) {
    out[2 * i] = digits[data[i] >> 4];
    out[2 * i + 1] = digits[data[i] & 15];
  }
  out[2 * length] = '\0';
}

#endif // MONERO_C_FUZZ_H

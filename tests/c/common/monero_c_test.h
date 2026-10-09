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

#ifndef MONERO_C_TEST_H
#define MONERO_C_TEST_H

#include "monero_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// hand-rolled checks for the black-box test programs. Each program keeps its own main()

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
  g_checks++; \
  if (!(cond)) { \
    g_failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
  } \
} while (0)

#define EXPECT_OK(expr) do { \
  monero_result _r = (expr); \
  g_checks++; \
  if (_r != MONERO_OK) { \
    g_failures++; \
    fprintf(stderr, "FAIL %s:%d: expected OK, got error: %s\n", __FILE__, __LINE__, monero_last_error()); \
  } \
} while (0)

#define EXPECT_ERR(expr) do { \
  monero_result _r = (expr); \
  g_checks++; \
  if (_r != MONERO_ERROR) { \
    g_failures++; \
    fprintf(stderr, "FAIL %s:%d: expected ERROR, call unexpectedly succeeded\n", __FILE__, __LINE__); \
  } \
} while (0)

#define EXPECT_ERR_MSG(expr, expected_msg) do { \
  monero_result _r = (expr); \
  g_checks++; \
  if (_r != MONERO_ERROR) { \
    g_failures++; \
    fprintf(stderr, "FAIL %s:%d: expected ERROR, call unexpectedly succeeded\n", __FILE__, __LINE__); \
  } else if (strcmp(monero_last_error(), (expected_msg)) != 0) { \
    g_failures++; \
    fprintf(stderr, "FAIL %s:%d: expected error \"%s\", got \"%s\"\n", __FILE__, __LINE__, (expected_msg), monero_last_error()); \
  } \
} while (0)

// the wallet file, plus path + ".keys" and path + ".address.txt"
static inline void remove_files(const char* path) {
  char keys[256];
  char address[256];
  snprintf(keys, sizeof(keys), "%s.keys", path);
  snprintf(address, sizeof(address), "%s.address.txt", path);
  remove(path);
  remove(keys);
  remove(address);
}

// skips spaces, tabs and line breaks
static inline const char* skip_space(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// copies the value of a JSON string key, with or without spaces around the colon. Returns 0 if
// the key is missing, is not a string, or is too long
static inline int json_string(const char* json, const char* key, char* out, size_t size) {
  char pattern[128];
  const char* start = NULL;
  const char* end = NULL;
  int pattern_len = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  if (pattern_len < 0 || (size_t) pattern_len >= sizeof(pattern)) return 0;
  start = strstr(json, pattern);
  if (start == NULL) return 0;
  start = skip_space(start + pattern_len);
  if (*start != ':') return 0;
  start = skip_space(start + 1);
  if (*start != '"') return 0;
  start++;
  end = strchr(start, '"');
  if (end == NULL) return 0;
  size_t len = (size_t) (end - start);
  if (len >= size) return 0;
  snprintf(out, size, "%.*s", (int) len, start);
  return 1;
}

// a value no call returns: an output set to it before a call that fails has to be reset
#define POISON_PTR ((void*) 1)

// counts the words of a seed, which are separated by spaces. Returns 0 for an empty seed
static inline size_t count_words(const char* seed) {
  size_t words = 0;
  if (seed == NULL || seed[0] == '\0') return 0;
  words = 1;
  for (const char* p = seed; *p != '\0'; p++) {
    if (*p == ' ') words++;
  }
  return words;
}

#endif // MONERO_C_TEST_H

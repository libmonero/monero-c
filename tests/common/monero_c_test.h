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

#endif // MONERO_C_TEST_H

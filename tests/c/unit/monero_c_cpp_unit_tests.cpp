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

#include "monero_c_test.h"

#include <cstdlib>

// links only if the headers declare the functions extern "C"

static void test_calls_from_cpp() {
  char* json = nullptr;
  monero_rpc_connection* connection = nullptr;
  monero_wallet* wallet = nullptr;

  CHECK(!monero_utils_is_valid_address("", MONERO_UTILS_NETWORK_MAINNET));
  EXPECT_OK(monero_rpc_connection_create("{\"uri\":\"http://127.0.0.1:1\"}", &connection));
  EXPECT_OK(monero_rpc_connection_serialize(connection, &json));
  CHECK(json != nullptr);
  monero_utils_free(json);
  json = nullptr;
  monero_rpc_connection_free(connection);

  EXPECT_OK(monero_wallet_keys_create_random(MONERO_UTILS_NETWORK_MAINNET, nullptr, &wallet));
  EXPECT_OK(monero_wallet_get_primary_address(wallet, &json));
  CHECK(json != nullptr);
  monero_utils_free(json);
  monero_wallet_free(wallet);

  EXPECT_ERR_MSG(monero_daemon_get_height(nullptr, nullptr), "daemon must not be null");
}

int main() {
  test_calls_from_cpp();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

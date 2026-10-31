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

// the library linked as an archive. On Windows the functions of the headers are imported from a DLL unless MONERO_C_STATIC is defined, and then
// this program does not link. It calls the functions that reach the static archives of the closure: the version of the ABI, the validation of
// an address, a wallet with only the keys, and a listener

static void on_new_block(void* user_data, uint64_t height) {
  (void) user_data;
  (void) height;
}

int main(void) {
  uint32_t major = 0, minor = 0, patch = 0;
  EXPECT_OK(monero_utils_get_abi_version(&major, &minor, &patch));
  CHECK(major == MONERO_C_ABI_VERSION_MAJOR && minor == MONERO_C_ABI_VERSION_MINOR && patch == MONERO_C_ABI_VERSION_PATCH);

  EXPECT_ERR(monero_utils_validate_address("not an address", MONERO_UTILS_NETWORK_MAINNET));
  CHECK(monero_last_error_code() == MONERO_ERROR_INVALID_ARGUMENT);

  monero_wallet* wallet = NULL;
  char* address = NULL;
  EXPECT_OK(monero_wallet_keys_create_random(MONERO_UTILS_NETWORK_MAINNET, NULL, &wallet));
  if (wallet != NULL) {
    EXPECT_OK(monero_wallet_get_primary_address(wallet, &address));
    CHECK(address != NULL && monero_utils_is_valid_address(address, MONERO_UTILS_NETWORK_MAINNET));
    monero_utils_free(address);
    monero_wallet_free(wallet);
  }

  monero_wallet_listener_callbacks callbacks = {sizeof(monero_wallet_listener_callbacks), NULL, NULL, on_new_block, NULL, NULL, NULL};
  monero_wallet_listener* listener = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));
  monero_wallet_listener_free(listener);

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

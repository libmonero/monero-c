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

#include <stddef.h>
#include <stdlib.h>

// the bindings copy these values and layouts by hand, so a change breaks them at runtime

static void test_enum_values(void) {
  CHECK(sizeof(monero_result) == 4);
  CHECK(MONERO_OK == 0);
  CHECK(MONERO_ERROR == -1);

  CHECK(sizeof(monero_utils_network_type) == 4);
  CHECK(MONERO_UTILS_NETWORK_MAINNET == 0);
  CHECK(MONERO_UTILS_NETWORK_TESTNET == 1);
  CHECK(MONERO_UTILS_NETWORK_STAGENET == 2);
  CHECK(MONERO_UTILS_NETWORK_REGTEST == 3);

  CHECK(sizeof(monero_key_image_spent_status) == 4);
  CHECK(MONERO_KEY_IMAGE_NOT_SPENT == 0);
  CHECK(MONERO_KEY_IMAGE_CONFIRMED == 1);
  CHECK(MONERO_KEY_IMAGE_TX_POOL == 2);

  CHECK(sizeof(monero_message_signature_type) == 4);
  CHECK(MONERO_MESSAGE_SIGN_WITH_SPEND_KEY == 0);
  CHECK(MONERO_MESSAGE_SIGN_WITH_VIEW_KEY == 1);

  CHECK(sizeof(monero_tx_priority) == 4);
  CHECK(MONERO_TX_PRIORITY_DEFAULT == 0);
  CHECK(MONERO_TX_PRIORITY_UNIMPORTANT == 1);
  CHECK(MONERO_TX_PRIORITY_NORMAL == 2);
  CHECK(MONERO_TX_PRIORITY_ELEVATED == 3);

  CHECK(sizeof(monero_optional_bool) == 4);
  CHECK(MONERO_OPTIONAL_BOOL_UNSET == -1);
  CHECK(MONERO_OPTIONAL_BOOL_FALSE == 0);
  CHECK(MONERO_OPTIONAL_BOOL_TRUE == 1);
}

// user_data, then the callbacks in order, no padding
static void test_callback_layout(void) {
  const size_t p = sizeof(void*);

  CHECK(sizeof(monero_daemon_listener_callbacks) == 2 * p);
  CHECK(offsetof(monero_daemon_listener_callbacks, user_data) == 0);
  CHECK(offsetof(monero_daemon_listener_callbacks, on_block_header) == p);

  CHECK(sizeof(monero_wallet_listener_callbacks) == 6 * p);
  CHECK(offsetof(monero_wallet_listener_callbacks, user_data) == 0);
  CHECK(offsetof(monero_wallet_listener_callbacks, on_sync_progress) == p);
  CHECK(offsetof(monero_wallet_listener_callbacks, on_new_block) == 2 * p);
  CHECK(offsetof(monero_wallet_listener_callbacks, on_balances_changed) == 3 * p);
  CHECK(offsetof(monero_wallet_listener_callbacks, on_output_received) == 4 * p);
  CHECK(offsetof(monero_wallet_listener_callbacks, on_output_spent) == 5 * p);
}

// bool is one byte across the ABI
static void test_bool_size(void) {
  CHECK(sizeof(bool) == 1);
}

int main(void) {
  test_enum_values();
  test_callback_layout();
  test_bool_size();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

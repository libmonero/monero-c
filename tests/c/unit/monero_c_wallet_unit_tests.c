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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// unit tests for the full wallet binding: creation, opening, argument checks and listeners
// none needs a daemon, and each test removes the files it creates

#define NETWORK MONERO_UTILS_NETWORK_STAGENET
#define PASSWORD "password"

static void test_create_random(void) {
  const char* path = "monero_c_wallet_unit_random";
  remove_files(path);

  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_create_random(path, PASSWORD, NETWORK, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  char* address = NULL;
  EXPECT_OK(monero_wallet_get_primary_address(wallet, &address));
  CHECK(address != NULL && monero_utils_is_valid_address(address, NETWORK));
  monero_utils_free(address);

  char* seed = NULL;
  EXPECT_OK(monero_wallet_get_seed(wallet, &seed));
  CHECK(count_words(seed) == 25);
  monero_utils_free(seed);

  char* language = NULL;
  EXPECT_OK(monero_wallet_get_seed_language(wallet, &language));
  CHECK(language != NULL && strcmp(language, "English") == 0);
  monero_utils_free(language);

  monero_utils_network_type network = MONERO_UTILS_NETWORK_MAINNET;
  EXPECT_OK(monero_wallet_get_network_type(wallet, &network));
  CHECK(network == NETWORK);

  bool view_only = true;
  bool multisig = true;
  EXPECT_OK(monero_wallet_is_view_only(wallet, &view_only));
  EXPECT_OK(monero_wallet_is_multisig(wallet, &multisig));
  CHECK(!view_only);
  CHECK(!multisig);

  uint64_t height = 1;
  uint64_t balance = 1;
  uint64_t unlocked = 1;
  EXPECT_OK(monero_wallet_get_height(wallet, &height));
  EXPECT_OK(monero_wallet_get_balance(wallet, &balance));
  EXPECT_OK(monero_wallet_get_unlocked_balance(wallet, &unlocked));
  // a new wallet knows only the genesis block
  CHECK(height == 1);
  CHECK(balance == 0);
  CHECK(unlocked == 0);

  EXPECT_OK(monero_wallet_save(wallet));
  EXPECT_OK(monero_wallet_close(wallet, false));
  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_restore_and_open(void) {
  const char* source = "monero_c_wallet_unit_source";
  const char* from_seed = "monero_c_wallet_unit_seed";
  const char* from_keys = "monero_c_wallet_unit_keys";
  const char* view_only = "monero_c_wallet_unit_view";
  remove_files(source);
  remove_files(from_seed);
  remove_files(from_keys);
  remove_files(view_only);

  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_create_random(source, PASSWORD, NETWORK, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  char* address = NULL;
  char* seed = NULL;
  char* view_key = NULL;
  char* spend_key = NULL;
  EXPECT_OK(monero_wallet_get_primary_address(wallet, &address));
  EXPECT_OK(monero_wallet_get_seed(wallet, &seed));
  EXPECT_OK(monero_wallet_get_private_view_key(wallet, &view_key));
  EXPECT_OK(monero_wallet_get_private_spend_key(wallet, &spend_key));
  EXPECT_OK(monero_wallet_save(wallet));
  monero_wallet_free(wallet);
  wallet = NULL;

  // a saved wallet opens with its password only
  EXPECT_OK(monero_wallet_open(source, PASSWORD, NETWORK, &wallet));
  if (wallet != NULL) {
    char* reopened = NULL;
    EXPECT_OK(monero_wallet_get_primary_address(wallet, &reopened));
    CHECK(reopened != NULL && address != NULL && strcmp(reopened, address) == 0);
    monero_utils_free(reopened);
    monero_wallet_free(wallet);
    wallet = NULL;
  }
  EXPECT_ERR(monero_wallet_open(source, "wrong password", NETWORK, &wallet));
  CHECK(wallet == NULL);

  // the same seed gives the same primary address
  EXPECT_OK(monero_wallet_create_from_seed(from_seed, PASSWORD, NETWORK, seed, NULL, 0, NULL, &wallet));
  if (wallet != NULL) {
    char* restored = NULL;
    EXPECT_OK(monero_wallet_get_primary_address(wallet, &restored));
    CHECK(restored != NULL && address != NULL && strcmp(restored, address) == 0);
    monero_utils_free(restored);
    monero_wallet_free(wallet);
    wallet = NULL;
  }

  // with the spend key the restored wallet is a full wallet
  EXPECT_OK(monero_wallet_create_from_keys(from_keys, PASSWORD, NETWORK, address, view_key, spend_key, 0, NULL, &wallet));
  if (wallet != NULL) {
    bool is_view_only = true;
    EXPECT_OK(monero_wallet_is_view_only(wallet, &is_view_only));
    CHECK(!is_view_only);
    monero_wallet_free(wallet);
    wallet = NULL;
  }

  // without a spend key the restored wallet is view-only
  EXPECT_OK(monero_wallet_create_from_keys(view_only, PASSWORD, NETWORK, address, view_key, NULL, 0, NULL, &wallet));
  if (wallet != NULL) {
    bool is_view_only = false;
    EXPECT_OK(monero_wallet_is_view_only(wallet, &is_view_only));
    CHECK(is_view_only);

    // a view-only wallet has neither the seed nor the spend key
    char* no_seed = NULL;
    char* no_spend_key = NULL;
    EXPECT_ERR(monero_wallet_get_seed(wallet, &no_seed));
    EXPECT_ERR(monero_wallet_get_private_spend_key(wallet, &no_spend_key));
    CHECK(no_seed == NULL);
    CHECK(no_spend_key == NULL);

    monero_wallet_free(wallet);
    wallet = NULL;
  }

  monero_utils_free(address);
  monero_utils_free(seed);
  monero_utils_free(view_key);
  monero_utils_free(spend_key);
  remove_files(source);
  remove_files(from_seed);
  remove_files(from_keys);
  remove_files(view_only);
}

static void test_invalid_arguments(void) {
  monero_wallet* wallet = NULL;
  EXPECT_ERR_MSG(monero_wallet_create_random(NULL, PASSWORD, NETWORK, NULL, &wallet), "path must not be null");
  EXPECT_ERR_MSG(monero_wallet_create_random("monero_c_wallet_unit_bad", PASSWORD, 9, NULL, &wallet), "unknown network type");
  EXPECT_ERR_MSG(monero_wallet_create_random("monero_c_wallet_unit_bad", PASSWORD, -1, NULL, &wallet), "unknown network type");
  EXPECT_ERR_MSG(monero_wallet_open("monero_c_wallet_unit_bad", PASSWORD, 4, &wallet), "unknown network type");
  CHECK(wallet == NULL);
  EXPECT_ERR(monero_wallet_open("monero_c_wallet_unit_missing", PASSWORD, NETWORK, &wallet));
  CHECK(wallet == NULL);

  uint64_t height = 0;
  EXPECT_ERR_MSG(monero_wallet_get_height(NULL, &height), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_save(NULL), "wallet must not be null");

  monero_wallet_listener* listener = NULL;
  EXPECT_ERR_MSG(monero_wallet_listener_create(NULL, &listener), "callbacks must not be null");
  CHECK(listener == NULL);

  monero_wallet_free(NULL);
  monero_wallet_listener_free(NULL);
}

// a seed is checked up to 4096 bytes, since a longer one can't be a mnemonic
static void test_seed_length_limit(void) {
  const char* path = "monero_c_wallet_unit_long_seed";
  char* longest = repeated_text('a', 4096);
  char* too_long = repeated_text('a', 4097);
  monero_wallet* wallet = POISON_PTR;
  remove_files(path);

  EXPECT_ERR(monero_wallet_create_from_seed(path, PASSWORD, NETWORK, longest, NULL, 0, NULL, &wallet));
  CHECK(strstr(monero_last_error(), "longer than") == NULL);
  CHECK(wallet == NULL);
  wallet = POISON_PTR;
  EXPECT_ERR_MSG(monero_wallet_create_from_seed(path, PASSWORD, NETWORK, too_long, NULL, 0, NULL, &wallet), "mnemonic is longer than 4096 bytes");
  CHECK(wallet == NULL);
  wallet = POISON_PTR;
  EXPECT_ERR_MSG(monero_wallet_rpc_create_from_seed("http://127.0.0.1:1", NULL, NULL, "name", NULL, too_long, NULL, 0, NULL, &wallet), "mnemonic is longer than 4096 bytes");
  CHECK(wallet == NULL);

  free(longest);
  free(too_long);
  remove_files(path);
}

// a call that fails on a NULL argument resets its outputs too, whichever output it can reach
static void test_null_arguments_reset_outputs(void) {
  monero_wallet* wallet = POISON_PTR;
  monero_wallet_listener* listener = POISON_PTR;
  char* json = POISON_PTR;

  EXPECT_ERR(monero_wallet_create_random(NULL, PASSWORD, NETWORK, NULL, &wallet));
  CHECK(wallet == NULL);
  wallet = POISON_PTR;
  EXPECT_ERR(monero_wallet_create_from_seed("monero_c_wallet_unit_bad", PASSWORD, NETWORK, NULL, NULL, 0, NULL, &wallet));
  CHECK(wallet == NULL);
  wallet = POISON_PTR;
  EXPECT_ERR(monero_wallet_open(NULL, PASSWORD, NETWORK, &wallet));
  CHECK(wallet == NULL);
  wallet = POISON_PTR;
  EXPECT_ERR(monero_wallet_rpc_open(NULL, NULL, NULL, "name", NULL, &wallet));
  CHECK(wallet == NULL);
  wallet = POISON_PTR;
  EXPECT_ERR(monero_wallet_rpc_create_random(NULL, NULL, NULL, "name", NULL, NULL, &wallet));
  CHECK(wallet == NULL);
  wallet = POISON_PTR;
  EXPECT_ERR(monero_wallet_rpc_create_from_keys(NULL, NULL, NULL, "name", NULL, NULL, NULL, NULL, 0, NULL, &wallet));
  CHECK(wallet == NULL);
  EXPECT_ERR(monero_wallet_listener_create(NULL, &listener));
  CHECK(listener == NULL);

  EXPECT_ERR(monero_wallet_get_seed(NULL, &json));
  CHECK(json == NULL);
  json = POISON_PTR;
  EXPECT_ERR(monero_wallet_get_subaddresses(NULL, 0, NULL, 0, &json));
  CHECK(json == NULL);
  json = POISON_PTR;
  EXPECT_ERR(monero_wallet_get_address_book_entries(NULL, NULL, 0, &json));
  CHECK(json == NULL);
}

static void test_listeners(void) {
  const char* first_path = "monero_c_wallet_unit_listener_first";
  const char* second_path = "monero_c_wallet_unit_listener_second";
  remove_files(first_path);
  remove_files(second_path);

  monero_wallet* first = NULL;
  monero_wallet* second = NULL;
  EXPECT_OK(monero_wallet_create_random(first_path, PASSWORD, NETWORK, NULL, &first));
  EXPECT_OK(monero_wallet_create_random(second_path, PASSWORD, NETWORK, NULL, &second));
  CHECK(first != NULL && second != NULL);
  if (first == NULL || second == NULL) {
    monero_wallet_free(first);
    monero_wallet_free(second);
    remove_files(first_path);
    remove_files(second_path);
    return;
  }

  monero_wallet_listener_callbacks callbacks = {NULL, NULL, NULL, NULL};
  monero_wallet_listener* listener = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));
  CHECK(listener != NULL);

  EXPECT_OK(monero_wallet_add_listener(first, listener));
  EXPECT_OK(monero_wallet_add_listener(first, listener));
  EXPECT_ERR_MSG(monero_wallet_add_listener(second, listener), "listener is already registered with another wallet");

  // after removal from the first wallet, the listener can join the second one
  EXPECT_OK(monero_wallet_remove_listener(first, listener));
  EXPECT_OK(monero_wallet_add_listener(second, listener));
  EXPECT_OK(monero_wallet_remove_listeners(second));
  EXPECT_OK(monero_wallet_add_listener(first, listener));

  monero_wallet_listener_free(listener);
  monero_wallet_free(first);
  monero_wallet_free(second);
  remove_files(first_path);
  remove_files(second_path);
}

static void test_listener_outlives_wallet(void) {
  const char* path = "monero_c_wallet_unit_outlives";
  remove_files(path);

  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_create_random(path, PASSWORD, NETWORK, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  monero_wallet_listener_callbacks callbacks = {NULL, NULL, NULL, NULL};
  monero_wallet_listener* listener = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));
  EXPECT_OK(monero_wallet_add_listener(wallet, listener));

  // the wallet is freed first, so the listener has nothing to unregister from
  monero_wallet_free(wallet);
  monero_wallet_listener_free(listener);
  remove_files(path);
}

static void test_exists_and_seed_languages(void) {
  const char* path = "monero_c_wallet_unit_exists";
  bool exists = true;
  EXPECT_OK(monero_wallet_exists(path, &exists));
  CHECK(!exists);

  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_create_random(path, PASSWORD, NETWORK, NULL, &wallet));
  if (wallet != NULL) monero_wallet_free(wallet);
  EXPECT_OK(monero_wallet_exists(path, &exists));
  CHECK(exists);
  remove_files(path);

  EXPECT_OK(monero_wallet_exists(path, &exists));
  CHECK(!exists);

  char* json = NULL;
  EXPECT_OK(monero_wallet_get_seed_languages(&json));
  CHECK(json != NULL && json[0] == '[' && strstr(json, "\"English\"") != NULL);
  monero_utils_free(json);
}

// every function rejects a NULL wallet before it reads the other arguments
static void test_every_function_rejects_null_wallet(void) {
  char* json = NULL;
  bool flag = false;
  int integer = 0;
  uint64_t number = 0;
  size_t count = 0;
  monero_optional_bool status = MONERO_OPTIONAL_BOOL_UNSET;
  monero_tx_priority priority = MONERO_TX_PRIORITY_DEFAULT;
  monero_utils_network_type network = MONERO_UTILS_NETWORK_MAINNET;
  monero_wallet_listener_callbacks callbacks = {NULL, NULL, NULL, NULL, NULL, NULL};
  monero_wallet_listener* listener = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));

  EXPECT_ERR_MSG(monero_wallet_save(NULL), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_close(NULL, false), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_network_type(NULL, &network), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_is_view_only(NULL, &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_is_multisig(NULL, &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_seed(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_seed_language(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_primary_address(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_private_view_key(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_private_spend_key(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_height(NULL, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_balance(NULL, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_unlocked_balance(NULL, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_account_balance(NULL, 0, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_subaddress_balance(NULL, 0, 0, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_account_unlocked_balance(NULL, 0, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_subaddress_unlocked_balance(NULL, 0, 0, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_address(NULL, 0, 0, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_integrated_address(NULL, "x", "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_decode_integrated_address(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_account(NULL, 0, false, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_subaddresses(NULL, 0, NULL, 0, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_sign_message(NULL, "x", MONERO_MESSAGE_SIGN_WITH_SPEND_KEY, 0, 0, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_verify_message(NULL, "x", "x", "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_add_address_book_entry(NULL, "x", "x", &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_edit_address_book_entry(NULL, 0, "x", "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_delete_address_book_entry(NULL, 0), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_address_book_entries(NULL, NULL, 0, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_change_password(NULL, "x", "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_create_account(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_create_subaddress(NULL, 0, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_accounts(NULL, false, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_address_index(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_subaddress(NULL, 0, 0, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_subaddress_label(NULL, 0, 0, "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_account_tag_label(NULL, "x", "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_account_tags(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_tag_accounts(NULL, "x", NULL, 0), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_untag_accounts(NULL, NULL, 0), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_attribute(NULL, "x", &flag, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_attribute(NULL, "x", "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_default_fee_priority(NULL, &priority), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_path(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_public_view_key(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_public_spend_key(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_version(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_restore_height(NULL, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_restore_height(NULL, 0), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_is_closed(NULL, &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_export_key_images(NULL, false, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_import_key_images(NULL, "x", 0, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_export_outputs(NULL, false, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_import_outputs(NULL, "x", &integer), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_freeze_output(NULL, "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_thaw_output(NULL, "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_is_output_frozen(NULL, "x", &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_tx_note(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_tx_note(NULL, "x", "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_tx_notes(NULL, NULL, 0, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_tx_notes(NULL, NULL, NULL, 0), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_daemon_connection(NULL, "x", "x", "x", "x", NULL, false), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_daemon_connection(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_is_daemon_trusted(NULL, &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_add_listener(NULL, listener), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_remove_listener(NULL, listener), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_remove_listeners(NULL), "wallet must not be null");

  // daemon
  EXPECT_ERR_MSG(monero_wallet_sync(NULL, NULL, listener, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_start_syncing(NULL, 0), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_stop_syncing(NULL), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_is_connected_to_daemon(NULL, &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_is_daemon_synced(NULL, &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_is_synced(NULL, &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_daemon_height(NULL, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_daemon_max_peer_height(NULL, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_wait_for_next_block(NULL, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_scan_txs(NULL, NULL, 0), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rescan_blockchain(NULL), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rescan_spent(NULL), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_height_by_date(NULL, 0, 0, 0, &number), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_start_mining(NULL, NULL, NULL, NULL), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_stop_mining(NULL), "wallet must not be null");

  // transactions
  EXPECT_ERR_MSG(monero_wallet_get_txs(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_transfers(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_outputs(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_create_tx(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_create_txs(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_relay_tx(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_relay_tx_json(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_relay_txs(NULL, NULL, 0, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_relay_txs_json(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_submit_txs(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_sign_txs(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_describe_tx_set(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_sweep_output(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_sweep_dust(NULL, false, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_sweep_unlocked(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_move_to(NULL, "x", "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_payment_uri(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_parse_payment_uri(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_tx_key(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_check_tx_key(NULL, "x", "x", "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_tx_proof(NULL, "x", "x", "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_check_tx_proof(NULL, "x", "x", "x", "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_spend_proof(NULL, "x", "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_check_spend_proof(NULL, "x", "x", "x", &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_reserve_proof_wallet(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_reserve_proof_account(NULL, 0, 0, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_check_reserve_proof(NULL, "x", "x", "x", &json), "wallet must not be null");

  // multisig
  EXPECT_ERR_MSG(monero_wallet_is_multisig_import_needed(NULL, &flag), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_get_multisig_info(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_prepare_multisig(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_make_multisig(NULL, NULL, 0, 0, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_exchange_multisig_keys(NULL, NULL, 0, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_export_multisig_hex(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_import_multisig_hex(NULL, NULL, 0, false, &integer), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_sign_multisig_tx_hex(NULL, "x", &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_submit_multisig_tx_hex(NULL, "x", &json), "wallet must not be null");

  // rpc wallets
  EXPECT_ERR_MSG(monero_wallet_rpc_open_wallet(NULL, "x", "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_create_wallet(NULL, "x"), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_stop(NULL), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_get_seed_languages(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_get_connection(NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_set_poll_period(NULL, 0), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_get_balances(NULL, NULL, NULL, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_get_account(NULL, 0, false, false, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_get_accounts(NULL, false, "x", false, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_get_subaddresses(NULL, 0, NULL, 0, false, &json), "wallet must not be null");
  EXPECT_ERR_MSG(monero_wallet_rpc_set_daemon_connection(NULL, "x", false, "x"), "wallet must not be null");

  CHECK(json == NULL);
  monero_wallet_listener_free(listener);
}

int main(void) {
  test_create_random();
  test_restore_and_open();
  test_invalid_arguments();
  test_seed_length_limit();
  test_null_arguments_reset_outputs();
  test_listeners();
  test_listener_outlives_wallet();
  test_exists_and_seed_languages();
  test_every_function_rejects_null_wallet();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

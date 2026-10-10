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

// full wallet methods that need no daemon. Each test creates a wallet from the public test seed
// of monero-python's config.ini in the working directory and removes its files. Methods that full
// wallets don't support are checked as errors

#define MAINNET MONERO_UTILS_NETWORK_MAINNET
#define PASSWORD "password"
#define SEED "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex"
#define ADDRESS "48W9YHwPzRz9aPTeXCA6kmSpW6HsvmWx578jj3of2gT3JwZzwTf33amESBoNDkL6SVK34Q2HTKqgYbGyE1hBws3wCrcBDR2"
#define SUBADDRESS "8ACn8f66cT5G7SWn3NuDP8Tm8JjG4PUiSjQJji9puDXcTXPhBgj3FKDiL4kCKvLWg416ifbdFzzrxC6t3E9MxixSDoYNL1Y"
#define PUBLIC_VIEW_KEY "42e465bdcd00de50516f1c7049bbe26bd3c11195e8dae5cceb38bad92d484269"
#define PUBLIC_SPEND_KEY "b58d33a1dac23d334539cbed3657b69a5c967d6860357e24ab4d11899a312a6b"
#define KEY_IMAGE "0000000000000000000000000000000000000000000000000000000000000000"
#define TX_HASH "1111111111111111111111111111111111111111111111111111111111111111"
#define OTHER_TX_HASH "2222222222222222222222222222222222222222222222222222222222222222"

static monero_wallet* create_test_wallet(const char* path) {
  monero_wallet* wallet = NULL;
  remove_files(path);
  EXPECT_OK(monero_wallet_create_from_seed(path, PASSWORD, MAINNET, SEED, NULL, 0, NULL, &wallet));
  CHECK(wallet != NULL);
  return wallet;
}

static int contains(const char* json, const char* fragment) {
  return json != NULL && strstr(json, fragment) != NULL;
}

static void test_address_book(void) {
  const char* path = "monero_c_wallet_offline_address_book";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  uint64_t index = 1;
  EXPECT_OK(monero_wallet_add_address_book_entry(wallet, ADDRESS, "primary", &index));
  CHECK(index == 0);

  char* json = NULL;
  EXPECT_OK(monero_wallet_get_address_book_entries(wallet, NULL, 0, &json));
  CHECK(contains(json, "\"address\":\"" ADDRESS "\"") && contains(json, "\"description\":\"primary\""));
  monero_utils_free(json);

  // a NULL description keeps the current one
  EXPECT_OK(monero_wallet_edit_address_book_entry(wallet, 0, SUBADDRESS, NULL));
  json = NULL;
  EXPECT_OK(monero_wallet_get_address_book_entries(wallet, NULL, 0, &json));
  CHECK(contains(json, "\"address\":\"" SUBADDRESS "\"") && contains(json, "\"description\":\"primary\""));
  monero_utils_free(json);

  EXPECT_OK(monero_wallet_delete_address_book_entry(wallet, 0));
  json = NULL;
  EXPECT_OK(monero_wallet_get_address_book_entries(wallet, NULL, 0, &json));
  CHECK(json != NULL && strcmp(json, "[]") == 0);
  monero_utils_free(json);

  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_accounts_and_subaddresses(void) {
  const char* path = "monero_c_wallet_offline_accounts";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  char* json = NULL;
  EXPECT_OK(monero_wallet_create_account(wallet, "savings", &json));
  CHECK(contains(json, "\"index\":1"));
  monero_utils_free(json);

  json = NULL;
  EXPECT_OK(monero_wallet_create_subaddress(wallet, 0, "shop", &json));
  CHECK(contains(json, "\"index\":1") && contains(json, "\"label\":\"shop\""));
  monero_utils_free(json);

  json = NULL;
  EXPECT_OK(monero_wallet_get_accounts(wallet, false, &json));
  CHECK(contains(json, "\"index\":0") && contains(json, "\"index\":1") && !contains(json, "\"subaddresses\""));
  monero_utils_free(json);

  EXPECT_OK(monero_wallet_set_subaddress_label(wallet, 0, 1, "label2"));
  json = NULL;
  EXPECT_OK(monero_wallet_get_accounts(wallet, true, &json));
  CHECK(contains(json, "\"label\":\"label2\""));
  monero_utils_free(json);

  json = NULL;
  EXPECT_OK(monero_wallet_get_address_index(wallet, SUBADDRESS, &json));
  CHECK(contains(json, "\"accountIndex\":0") && contains(json, "\"index\":1"));
  monero_utils_free(json);

  // empty indices return every subaddress of the account
  json = NULL;
  EXPECT_OK(monero_wallet_get_subaddresses(wallet, 0, NULL, 0, &json));
  CHECK(contains(json, "\"index\":0") && contains(json, "\"index\":1"));
  monero_utils_free(json);

  // balances are zero until the wallet has outputs
  uint64_t balance = 1;
  EXPECT_OK(monero_wallet_get_account_balance(wallet, 1, &balance));
  CHECK(balance == 0);
  EXPECT_OK(monero_wallet_get_subaddress_balance(wallet, 0, 1, &balance));
  CHECK(balance == 0);
  EXPECT_OK(monero_wallet_get_account_unlocked_balance(wallet, 0, &balance));
  CHECK(balance == 0);
  EXPECT_OK(monero_wallet_get_subaddress_unlocked_balance(wallet, 0, 1, &balance));
  CHECK(balance == 0);

  // full wallets don't support these
  uint32_t accounts[1] = {1};
  json = NULL;
  EXPECT_ERR_MSG(monero_wallet_get_subaddress(wallet, 0, 1, &json), "get_subaddress() not supported");
  EXPECT_ERR_MSG(monero_wallet_tag_accounts(wallet, "family", accounts, 1), "tag_accounts() not supported");
  EXPECT_ERR_MSG(monero_wallet_set_account_tag_label(wallet, "family", "Family"), "set_account_tag_label() not supported");
  EXPECT_ERR_MSG(monero_wallet_get_account_tags(wallet, &json), "get_account_tags() not supported");
  EXPECT_ERR_MSG(monero_wallet_untag_accounts(wallet, accounts, 1), "untag_accounts() not supported");
  CHECK(json == NULL);

  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_change_password(void) {
  const char* path = "monero_c_wallet_offline_password";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  EXPECT_ERR_MSG(monero_wallet_change_password(wallet, "wrong", "new password"), "Invalid original password.");
  EXPECT_OK(monero_wallet_change_password(wallet, PASSWORD, "new password"));
  EXPECT_OK(monero_wallet_save(wallet));
  monero_wallet_free(wallet);

  wallet = NULL;
  EXPECT_OK(monero_wallet_open(path, "new password", MAINNET, &wallet));
  monero_wallet_free(wallet);

  wallet = NULL;
  EXPECT_ERR(monero_wallet_open(path, PASSWORD, MAINNET, &wallet));
  CHECK(wallet == NULL);

  remove_files(path);
}

static void test_attributes_and_settings(void) {
  const char* path = "monero_c_wallet_offline_settings";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  bool found = true;
  char* value = NULL;
  EXPECT_OK(monero_wallet_get_attribute(wallet, "missing", &found, &value));
  CHECK(!found && value == NULL);

  EXPECT_OK(monero_wallet_set_attribute(wallet, "key", "value \"x\""));
  EXPECT_OK(monero_wallet_get_attribute(wallet, "key", &found, &value));
  CHECK(found && value != NULL && strcmp(value, "value \"x\"") == 0);
  monero_utils_free(value);

  EXPECT_OK(monero_wallet_set_restore_height(wallet, 1234));
  uint64_t height = 0;
  EXPECT_OK(monero_wallet_get_restore_height(wallet, &height));
  CHECK(height == 1234);

  monero_tx_priority priority = MONERO_TX_PRIORITY_ELEVATED;
  EXPECT_OK(monero_wallet_get_default_fee_priority(wallet, &priority));
  CHECK(priority <= MONERO_TX_PRIORITY_ELEVATED);

  char* wallet_path = NULL;
  EXPECT_OK(monero_wallet_get_path(wallet, &wallet_path));
  CHECK(wallet_path != NULL && strcmp(wallet_path, path) == 0);
  monero_utils_free(wallet_path);

  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_keys_and_version(void) {
  const char* path = "monero_c_wallet_offline_keys";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  char* key = NULL;
  EXPECT_OK(monero_wallet_get_public_view_key(wallet, &key));
  CHECK(key != NULL && strcmp(key, PUBLIC_VIEW_KEY) == 0);
  monero_utils_free(key);

  key = NULL;
  EXPECT_OK(monero_wallet_get_public_spend_key(wallet, &key));
  CHECK(key != NULL && strcmp(key, PUBLIC_SPEND_KEY) == 0);
  monero_utils_free(key);

  char* json = NULL;
  EXPECT_OK(monero_wallet_get_version(wallet, &json));
  CHECK(contains(json, "\"number\":") && contains(json, "\"isRelease\":"));
  monero_utils_free(json);

  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_key_images_and_outputs(void) {
  const char* path = "monero_c_wallet_offline_outputs";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  char* json = NULL;
  EXPECT_OK(monero_wallet_export_key_images(wallet, false, &json));
  CHECK(contains(json, "\"offset\":0"));
  monero_utils_free(json);

  json = NULL;
  EXPECT_OK(monero_wallet_import_key_images(wallet, "[]", 0, &json));
  CHECK(contains(json, "\"height\":0"));
  monero_utils_free(json);

  json = NULL;
  EXPECT_ERR_MSG(monero_wallet_import_key_images(wallet, "{}", 0, &json), "key images must be a JSON array");
  CHECK(json == NULL);

  char* outputs = NULL;
  EXPECT_OK(monero_wallet_export_outputs(wallet, true, &outputs));
  CHECK(outputs != NULL && outputs[0] != '\0');
  int imported = -1;
  EXPECT_OK(monero_wallet_import_outputs(wallet, outputs, &imported));
  CHECK(imported == 0);
  monero_utils_free(outputs);
  EXPECT_ERR(monero_wallet_import_outputs(wallet, "", &imported));

  // the wallet has no output with this key image
  bool frozen = false;
  EXPECT_ERR(monero_wallet_freeze_output(wallet, KEY_IMAGE));
  EXPECT_ERR(monero_wallet_is_output_frozen(wallet, KEY_IMAGE, &frozen));
  EXPECT_ERR(monero_wallet_thaw_output(wallet, KEY_IMAGE));

  monero_wallet_free(wallet);
  remove_files(path);
}

// a JSON argument that nests too deeply fails before the parser, which would overflow the stack
static void test_json_depth_limit(void) {
  const char* path = "monero_c_wallet_offline_depth";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  const char* message = "JSON is nested deeper than 64 levels";
  char* array = nested_json(100000);
  char* object = nested_json_object(100000);
  char* at_limit = nested_json_object(64);
  char* json = POISON_PTR;

  EXPECT_ERR_MSG(monero_wallet_import_key_images(wallet, array, 0, &json), message);
  CHECK(json == NULL);
  json = POISON_PTR;
  EXPECT_ERR_MSG(monero_wallet_relay_txs_json(wallet, array, &json), message);
  CHECK(json == NULL);
  json = POISON_PTR;
  EXPECT_ERR_MSG(monero_wallet_relay_tx_json(wallet, object, &json), message);
  CHECK(json == NULL);
  EXPECT_ERR_MSG(monero_wallet_create_tx(wallet, object, &json), message);
  EXPECT_ERR_MSG(monero_wallet_create_txs(wallet, object, &json), message);
  EXPECT_ERR_MSG(monero_wallet_sweep_output(wallet, object, &json), message);
  EXPECT_ERR_MSG(monero_wallet_sweep_unlocked(wallet, object, &json), message);
  EXPECT_ERR_MSG(monero_wallet_describe_tx_set(wallet, object, &json), message);
  EXPECT_ERR_MSG(monero_wallet_get_payment_uri(wallet, object, &json), message);
  EXPECT_ERR_MSG(monero_wallet_get_txs(wallet, object, &json), message);
  EXPECT_ERR_MSG(monero_wallet_get_transfers(wallet, object, &json), message);
  EXPECT_ERR_MSG(monero_wallet_get_outputs(wallet, object, &json), message);

  // 64 levels pass the check, and are then rejected as a config or a transaction
  EXPECT_ERR(monero_wallet_create_tx(wallet, at_limit, &json));
  CHECK(strcmp(monero_last_error(), message) != 0);
  EXPECT_ERR(monero_wallet_relay_tx_json(wallet, at_limit, &json));
  CHECK(strcmp(monero_last_error(), message) != 0);

  free(array);
  free(object);
  free(at_limit);
  monero_wallet_free(wallet);
  remove_files(path);
}

// an array that is NULL with a nonzero count, or has a NULL element, fails with the name of the argument
static void test_null_arrays(void) {
  const char* path = "monero_c_wallet_offline_arrays";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  const char* with_null[1] = {NULL};
  const char* hashes[1] = {TX_HASH};
  char* json = POISON_PTR;

  EXPECT_ERR_MSG(monero_wallet_get_tx_notes(wallet, NULL, 1, &json), "tx_hashes must not be null");
  CHECK(json == NULL);
  json = POISON_PTR;
  EXPECT_ERR_MSG(monero_wallet_get_tx_notes(wallet, with_null, 1, &json), "tx_hashes must not contain NULL");
  CHECK(json == NULL);
  json = POISON_PTR;
  EXPECT_ERR_MSG(monero_wallet_get_address_book_entries(wallet, NULL, 1, &json), "indices must not be null");
  CHECK(json == NULL);
  json = POISON_PTR;
  EXPECT_ERR_MSG(monero_wallet_get_subaddresses(wallet, 0, NULL, 1, &json), "subaddress_indices must not be null");
  CHECK(json == NULL);
  EXPECT_ERR_MSG(monero_wallet_untag_accounts(wallet, NULL, 1), "account_indices must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_tx_notes(wallet, NULL, hashes, 1), "tx_hashes must not be null");
  EXPECT_ERR_MSG(monero_wallet_set_tx_notes(wallet, hashes, NULL, 1), "notes must not be null");

  // arrays that pass the check are used, so the calls fail on the missing daemon
  json = POISON_PTR;
  EXPECT_ERR(monero_wallet_scan_txs(wallet, hashes, 1));
  CHECK(strstr(monero_last_error(), "must not be null") == NULL);
  EXPECT_ERR(monero_wallet_relay_txs(wallet, hashes, 1, &json));
  CHECK(strstr(monero_last_error(), "must not be null") == NULL);
  CHECK(json == NULL);

  // a zero count is valid with a NULL array
  json = NULL;
  EXPECT_OK(monero_wallet_get_tx_notes(wallet, NULL, 0, &json));
  CHECK(contains(json, "[]"));
  monero_utils_free(json);

  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_tx_notes(void) {
  const char* path = "monero_c_wallet_offline_notes";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  EXPECT_OK(monero_wallet_set_tx_note(wallet, TX_HASH, "note \"a\""));
  char* note = NULL;
  EXPECT_OK(monero_wallet_get_tx_note(wallet, TX_HASH, &note));
  CHECK(note != NULL && strcmp(note, "note \"a\"") == 0);
  monero_utils_free(note);

  const char* hashes[2] = {TX_HASH, OTHER_TX_HASH};
  const char* notes[2] = {"first", "second"};
  EXPECT_OK(monero_wallet_set_tx_notes(wallet, hashes, notes, 2));
  char* json = NULL;
  EXPECT_OK(monero_wallet_get_tx_notes(wallet, hashes, 2, &json));
  CHECK(json != NULL && strcmp(json, "[\"first\",\"second\"]") == 0);
  monero_utils_free(json);

  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_daemon_connection(void) {
  const char* path = "monero_c_wallet_offline_daemon";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  char* json = NULL;
  EXPECT_OK(monero_wallet_get_daemon_connection(wallet, &json));
  CHECK(json == NULL);

  bool yes = true;
  bool no = false;
  EXPECT_OK(monero_wallet_set_daemon_connection(wallet, "http://127.0.0.1:1", "", "", "", &yes, false));
  EXPECT_OK(monero_wallet_get_daemon_connection(wallet, &json));
  CHECK(contains(json, "\"uri\":\"http://127.0.0.1:1\"") && contains(json, "\"sslVerify\":false"));
  monero_utils_free(json);

  bool trusted = false;
  EXPECT_OK(monero_wallet_is_daemon_trusted(wallet, &trusted));
  CHECK(trusted);

  // false wins over a local address, and NULL leaves the choice to the address, which is loopback here
  EXPECT_OK(monero_wallet_set_daemon_connection(wallet, "http://127.0.0.1:1", "", "", "", &no, false));
  EXPECT_OK(monero_wallet_is_daemon_trusted(wallet, &trusted));
  CHECK(!trusted);
  EXPECT_OK(monero_wallet_set_daemon_connection(wallet, "http://127.0.0.1:1", "", "", "", NULL, false));
  EXPECT_OK(monero_wallet_is_daemon_trusted(wallet, &trusted));
  CHECK(trusted);

  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_close(void) {
  const char* path = "monero_c_wallet_offline_close";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  bool closed = true;
  EXPECT_OK(monero_wallet_is_closed(wallet, &closed));
  CHECK(!closed);

  // a shutdown request with nothing in flight leaves the wallet open, and closing it still works, and so does asking again
  EXPECT_OK(monero_wallet_request_shutdown(wallet));
  EXPECT_OK(monero_wallet_is_closed(wallet, &closed));
  CHECK(!closed);
  EXPECT_OK(monero_wallet_close(wallet, false));
  EXPECT_OK(monero_wallet_is_closed(wallet, &closed));
  CHECK(closed);
  EXPECT_OK(monero_wallet_request_shutdown(wallet));

  monero_wallet_free(wallet);
  remove_files(path);
}

// the methods that need a daemon fail or report no connection without one. The transactions of
// a fresh wallet are empty, and the RPC methods need an RPC wallet
static void test_daemon_surface(void) {
  const char* path = "monero_c_wallet_offline_surface";
  monero_wallet* wallet = create_test_wallet(path);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  bool flag = true;
  EXPECT_OK(monero_wallet_is_connected_to_daemon(wallet, &flag));
  CHECK(!flag);
  char* json = NULL;
  EXPECT_ERR(monero_wallet_sync(wallet, NULL, NULL, &json));
  CHECK(json == NULL);

  // the transactions and the transfers are read through the daemon, so monero-cpp asks for a connection
  EXPECT_ERR_MSG(monero_wallet_get_txs(wallet, NULL, &json), "no connection to daemon");
  CHECK(json == NULL);
  EXPECT_ERR_MSG(monero_wallet_get_transfers(wallet, NULL, &json), "no connection to daemon");
  CHECK(json == NULL);

  // a query with a transfer, an input and an output query points to them and they point back, so the call frees it when it
  // fails. LeakSanitizer fails the run if it doesn't
  EXPECT_ERR_MSG(monero_wallet_get_txs(wallet, "{\"transferQuery\":{\"isIncoming\":true},\"inputQuery\":{\"isSpent\":true},\"outputQuery\":{\"isSpent\":false}}", &json), "no connection to daemon");
  CHECK(json == NULL);

  // describing a tx set reads its hex and ignores its txs, which monero-cpp can't parse when they have an outgoing transfer
  EXPECT_ERR_MSG(monero_wallet_describe_tx_set(wallet, "{\"txs\":[{\"hash\":\"00\",\"outgoingTransfer\":{}}]}", &json), "no txset provided");
  CHECK(json == NULL);
  EXPECT_ERR_MSG(monero_wallet_describe_tx_set(wallet, "{\"unsignedTxHex\":5}", &json), "unsignedTxHex must be a string");
  EXPECT_ERR_MSG(monero_wallet_describe_tx_set(wallet, "[]", &json), "tx_set must be a JSON object");
  EXPECT_ERR_MSG(monero_wallet_describe_tx_set(wallet, "{\"unsignedTxHex\":\"00\"}", &json), "failed to parse unsigned transfers: cannot load unsigned_txset");
  CHECK(json == NULL);

  // a fresh wallet has no outputs, so the list is empty
  EXPECT_OK(monero_wallet_get_outputs(wallet, NULL, &json));
  CHECK(json != NULL && strcmp(json, "[]") == 0);
  monero_utils_free(json);
  json = NULL;
  EXPECT_ERR(monero_wallet_get_txs(wallet, "[]", &json));
  CHECK(json == NULL);

  // a payment URI parses back to its destination
  char* uri = NULL;
  EXPECT_OK(monero_wallet_get_payment_uri(wallet, "{\"destinations\":[{\"address\":\"" ADDRESS "\",\"amount\":1000}]}", &uri));
  CHECK(uri != NULL && strncmp(uri, "monero:", 7) == 0);
  EXPECT_OK(monero_wallet_parse_payment_uri(wallet, uri, &json));
  CHECK(contains(json, "\"address\":\"" ADDRESS "\""));
  monero_utils_free(json);
  json = NULL;
  monero_utils_free(uri);

  // the RPC methods need an RPC wallet
  EXPECT_ERR_MSG(monero_wallet_rpc_get_connection(wallet, &json), "not an RPC wallet");
  CHECK(json == NULL);
  EXPECT_ERR_MSG(monero_wallet_rpc_stop(wallet), "not an RPC wallet");

  // only a wallet that is multisig can need an import
  bool needed = true;
  EXPECT_OK(monero_wallet_is_multisig_import_needed(wallet, &needed));
  CHECK(!needed);

  // the wallet moves to another path, and the new path is reported
  const char* moved = "monero_c_wallet_offline_moved";
  EXPECT_OK(monero_wallet_move_to(wallet, moved, PASSWORD));
  EXPECT_OK(monero_wallet_get_path(wallet, &json));
  CHECK(contains(json, moved));
  monero_utils_free(json);
  json = NULL;

  monero_wallet_free(wallet);
  remove_files(moved);
  remove_files(path);
}

// monero-cpp runs regtest as mainnet with a flag, so the network must come from the handle
static void test_regtest_network(void) {
  const char* path = "monero_c_wallet_offline_regtest";
  monero_utils_network_type network = MONERO_UTILS_NETWORK_MAINNET;
  monero_wallet* wallet = NULL;
  monero_wallet* keys = NULL;
  remove_files(path);

  EXPECT_OK(monero_wallet_create_from_seed(path, PASSWORD, MONERO_UTILS_NETWORK_REGTEST, SEED, NULL, 0, NULL, &wallet));
  if (wallet != NULL) {
    EXPECT_OK(monero_wallet_get_network_type(wallet, &network));
    CHECK(network == MONERO_UTILS_NETWORK_REGTEST);
    EXPECT_OK(monero_wallet_save(wallet));
    monero_wallet_free(wallet);
  }

  wallet = NULL;
  EXPECT_OK(monero_wallet_open(path, PASSWORD, MONERO_UTILS_NETWORK_REGTEST, &wallet));
  if (wallet != NULL) {
    EXPECT_OK(monero_wallet_get_network_type(wallet, &network));
    CHECK(network == MONERO_UTILS_NETWORK_REGTEST);
    monero_wallet_free(wallet);
  }

  EXPECT_OK(monero_wallet_keys_create_from_seed(MONERO_UTILS_NETWORK_REGTEST, SEED, NULL, NULL, &keys));
  if (keys != NULL) {
    EXPECT_OK(monero_wallet_get_network_type(keys, &network));
    CHECK(network == MONERO_UTILS_NETWORK_REGTEST);
    monero_wallet_free(keys);
  }

  remove_files(path);
}

// one listener can be on one wallet at a time, so it can't join a second wallet
static void test_listener_one_wallet(void) {
  const char* path_a = "monero_c_wallet_offline_listener_a";
  const char* path_b = "monero_c_wallet_offline_listener_b";
  monero_wallet* a = create_test_wallet(path_a);
  monero_wallet* b = create_test_wallet(path_b);
  monero_wallet_listener_callbacks callbacks = {NULL, NULL, NULL, NULL};
  monero_wallet_listener* listener = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));

  if (a != NULL && b != NULL && listener != NULL) {
    EXPECT_OK(monero_wallet_add_listener(a, listener));
    EXPECT_ERR_MSG(monero_wallet_add_listener(b, listener), "listener is already registered with another wallet");
    EXPECT_OK(monero_wallet_remove_listener(a, listener));
    EXPECT_OK(monero_wallet_add_listener(b, listener));
    EXPECT_OK(monero_wallet_remove_listener(b, listener));
  }

  if (listener != NULL) monero_wallet_listener_free(listener);
  monero_wallet_free(a);
  monero_wallet_free(b);
  remove_files(path_a);
  remove_files(path_b);
}

int main(void) {
  test_address_book();
  test_accounts_and_subaddresses();
  test_change_password();
  test_regtest_network();
  test_listener_one_wallet();
  test_attributes_and_settings();
  test_keys_and_version();
  test_key_images_and_outputs();
  test_json_depth_limit();
  test_null_arrays();
  test_tx_notes();
  test_daemon_connection();
  test_close();
  test_daemon_surface();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

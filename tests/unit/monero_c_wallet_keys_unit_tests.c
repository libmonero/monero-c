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

// keys-only wallet tests, modeled on monero-python's test_monero_wallet_keys.py. Operations that
// monero-python marks as not supported are checked as errors. Vectors come from the public,
// funds-free wallet in monero-python's tests/config/config.ini

#define MAINNET MONERO_UTILS_NETWORK_MAINNET
#define PASSWORD "password"
#define SEED "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex"
#define ADDRESS "48W9YHwPzRz9aPTeXCA6kmSpW6HsvmWx578jj3of2gT3JwZzwTf33amESBoNDkL6SVK34Q2HTKqgYbGyE1hBws3wCrcBDR2"
#define VIEW_KEY "e8c2288181bad9ec410d7322efd65f663c6da57bd1d1198636278a039743a600"
#define SPEND_KEY "be7a2f71097f146bdf0fb5bb8edfe2240a9767e15adee74d95af1b5a64f29a0c"
#define PAYMENT_ID "0123456789abcdef"

// length of s up to max characters, without reading past the first NUL
static size_t bounded_length(const char* s, size_t max) {
  size_t n = 0;
  while (n < max && s[n] != '\0') n++;
  return n;
}

// checks the primary address and the keys of a wallet against the given values
static void check_keys(monero_wallet* wallet, const char* address, const char* view_key, const char* spend_key) {
  char* value = NULL;
  EXPECT_OK(monero_wallet_get_primary_address(wallet, &value));
  CHECK(value != NULL && strcmp(value, address) == 0);
  monero_utils_free(value);

  value = NULL;
  EXPECT_OK(monero_wallet_get_private_view_key(wallet, &value));
  CHECK(value != NULL && strcmp(value, view_key) == 0);
  monero_utils_free(value);

  value = NULL;
  EXPECT_OK(monero_wallet_get_private_spend_key(wallet, &value));
  CHECK(value != NULL && strcmp(value, spend_key) == 0);
  monero_utils_free(value);
}

static void test_create_random(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_random(MAINNET, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  char* address = NULL;
  EXPECT_OK(monero_wallet_get_primary_address(wallet, &address));
  CHECK(address != NULL && monero_utils_is_valid_address(address, MAINNET));
  monero_utils_free(address);

  char* seed = NULL;
  EXPECT_OK(monero_wallet_get_seed(wallet, &seed));
  CHECK(count_words(seed) == 25);
  monero_utils_free(seed);

  char* language = NULL;
  EXPECT_OK(monero_wallet_get_seed_language(wallet, &language));
  CHECK(language != NULL && strcmp(language, "English") == 0);
  monero_utils_free(language);

  bool view_only = true;
  EXPECT_OK(monero_wallet_is_view_only(wallet, &view_only));
  CHECK(!view_only);

  // keys-only wallets don't support multisig
  bool multisig = false;
  EXPECT_ERR_MSG(monero_wallet_is_multisig(wallet, &multisig), "get_multisig_info() not supported");

  char* view_key = NULL;
  char* spend_key = NULL;
  EXPECT_OK(monero_wallet_get_private_view_key(wallet, &view_key));
  EXPECT_OK(monero_wallet_get_private_spend_key(wallet, &spend_key));
  CHECK(view_key != NULL && bounded_length(view_key, 65) == 64);
  CHECK(spend_key != NULL && bounded_length(spend_key, 65) == 64);
  monero_utils_free(view_key);
  monero_utils_free(spend_key);

  monero_wallet_free(wallet);
}

static void test_create_random_in_language(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_random(MAINNET, "Spanish", &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  char* language = NULL;
  EXPECT_OK(monero_wallet_get_seed_language(wallet, &language));
  CHECK(language != NULL && strcmp(language, "Spanish") == 0);
  monero_utils_free(language);

  char* seed = NULL;
  EXPECT_OK(monero_wallet_get_seed(wallet, &seed));
  CHECK(count_words(seed) == 25);
  monero_utils_free(seed);

  monero_wallet_free(wallet);
}

static void test_create_from_seed(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  check_keys(wallet, ADDRESS, VIEW_KEY, SPEND_KEY);

  char* language = NULL;
  EXPECT_OK(monero_wallet_get_seed_language(wallet, &language));
  CHECK(language != NULL && strcmp(language, "English") == 0);
  monero_utils_free(language);

  monero_wallet_free(wallet);
}

static void test_seed_offset_changes_wallet(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, "offset", NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  char* address = NULL;
  EXPECT_OK(monero_wallet_get_primary_address(wallet, &address));
  CHECK(address != NULL && strcmp(address, ADDRESS) != 0);
  monero_utils_free(address);

  monero_wallet_free(wallet);
}

static void test_create_from_keys(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_keys(MAINNET, ADDRESS, VIEW_KEY, SPEND_KEY, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  check_keys(wallet, ADDRESS, VIEW_KEY, SPEND_KEY);

  bool view_only = true;
  EXPECT_OK(monero_wallet_is_view_only(wallet, &view_only));
  CHECK(!view_only);

  monero_wallet_free(wallet);
}

static void test_view_only_from_keys(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_keys(MAINNET, ADDRESS, VIEW_KEY, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  bool view_only = false;
  EXPECT_OK(monero_wallet_is_view_only(wallet, &view_only));
  CHECK(view_only);

  char* address = NULL;
  EXPECT_OK(monero_wallet_get_primary_address(wallet, &address));
  CHECK(address != NULL && strcmp(address, ADDRESS) == 0);
  monero_utils_free(address);

  // a view-only wallet has neither the spend key nor the seed
  char* spend_key = NULL;
  char* seed = NULL;
  EXPECT_ERR(monero_wallet_get_private_spend_key(wallet, &spend_key));
  EXPECT_ERR(monero_wallet_get_seed(wallet, &seed));
  CHECK(spend_key == NULL);
  CHECK(seed == NULL);

  monero_wallet_free(wallet);
}

static void test_invalid_arguments(void) {
  monero_wallet* wallet = NULL;
  EXPECT_ERR_MSG(monero_wallet_keys_create_from_seed(MAINNET, NULL, NULL, NULL, &wallet), "seed must not be null");
  EXPECT_ERR_MSG(monero_wallet_keys_create_from_seed(MAINNET, "not a valid seed", NULL, NULL, &wallet), "Invalid mnemonic");
  CHECK(wallet == NULL);
  EXPECT_ERR_MSG(monero_wallet_keys_create_from_keys(MAINNET, NULL, VIEW_KEY, NULL, NULL, &wallet), "address must not be null");
  EXPECT_ERR(monero_wallet_keys_create_from_keys(MAINNET, ADDRESS, "not hex", NULL, NULL, &wallet));
  EXPECT_ERR_MSG(monero_wallet_keys_create_random(9, NULL, &wallet), "unknown network type");
  EXPECT_ERR_MSG(monero_wallet_keys_create_random(-1, NULL, &wallet), "unknown network type");
  EXPECT_ERR(monero_wallet_keys_create_random(MAINNET, "Klingon", &wallet));
  CHECK(wallet == NULL);
}

static void test_addresses(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  // the primary address, subaddresses 1 to 10 and accounts 1 to 10 are all different
  char* seen[21];
  size_t count = 0;
  EXPECT_OK(monero_wallet_get_address(wallet, 0, 0, &seen[count++]));
  for (uint32_t i = 1; i <= 10; i++) {
    EXPECT_OK(monero_wallet_get_address(wallet, 0, i, &seen[count++]));
  }
  for (uint32_t i = 1; i <= 10; i++) {
    EXPECT_OK(monero_wallet_get_address(wallet, i, 0, &seen[count++]));
  }
  CHECK(seen[0] != NULL && strcmp(seen[0], ADDRESS) == 0);
  for (size_t i = 0; i < count; i++) {
    CHECK(seen[i] != NULL);
    for (size_t j = i + 1; j < count; j++) {
      if (seen[i] != NULL && seen[j] != NULL) CHECK(strcmp(seen[i], seen[j]) != 0);
    }
    monero_utils_free(seen[i]);
  }

  monero_wallet_free(wallet);
}

static void test_subaddresses_json(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  char* sub1 = NULL;
  char* sub2 = NULL;
  EXPECT_OK(monero_wallet_get_address(wallet, 0, 1, &sub1));
  EXPECT_OK(monero_wallet_get_address(wallet, 0, 2, &sub2));
  CHECK(sub1 != NULL && sub2 != NULL);

  uint32_t indices[2] = {1, 2};
  char* json = NULL;
  EXPECT_OK(monero_wallet_get_subaddresses(wallet, 0, indices, 2, &json));
  CHECK(json != NULL && sub1 != NULL && strstr(json, sub1) != NULL);
  CHECK(json != NULL && sub2 != NULL && strstr(json, sub2) != NULL);
  CHECK(json != NULL && strstr(json, "\"accountIndex\":0") != NULL);
  monero_utils_free(json);

  // a keys-only wallet can't enumerate its subaddresses, so the indices are required
  json = NULL;
  EXPECT_ERR(monero_wallet_get_subaddresses(wallet, 0, NULL, 0, &json));
  CHECK(json == NULL);

  monero_utils_free(sub1);
  monero_utils_free(sub2);
  monero_wallet_free(wallet);
}

static void test_account_json(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  char* json = NULL;
  EXPECT_OK(monero_wallet_get_account(wallet, 0, false, &json));
  CHECK(json != NULL && strstr(json, "\"primaryAddress\":\"" ADDRESS "\"") != NULL);
  monero_utils_free(json);

  json = NULL;
  EXPECT_OK(monero_wallet_get_account(wallet, 1, false, &json));
  CHECK(json != NULL && strstr(json, "\"index\":1") != NULL);
  monero_utils_free(json);

  monero_wallet_free(wallet);
}

static void test_integrated_address(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  char* json = NULL;
  EXPECT_OK(monero_wallet_get_integrated_address(wallet, NULL, PAYMENT_ID, &json));
  CHECK(json != NULL && strstr(json, "\"standardAddress\":\"" ADDRESS "\"") != NULL);
  CHECK(json != NULL && strstr(json, "\"paymentId\":\"" PAYMENT_ID "\"") != NULL);

  char integrated[256];
  CHECK(json != NULL && json_string(json, "integratedAddress", integrated, sizeof(integrated)));
  monero_utils_free(json);

  // decoding the integrated address gives back the standard address and the payment ID
  json = NULL;
  EXPECT_OK(monero_wallet_decode_integrated_address(wallet, integrated, &json));
  CHECK(json != NULL && strstr(json, "\"standardAddress\":\"" ADDRESS "\"") != NULL);
  CHECK(json != NULL && strstr(json, "\"paymentId\":\"" PAYMENT_ID "\"") != NULL);
  monero_utils_free(json);

  // without a payment ID, each call generates a new one
  char first[256];
  char second[256];
  json = NULL;
  EXPECT_OK(monero_wallet_get_integrated_address(wallet, NULL, NULL, &json));
  CHECK(json != NULL && json_string(json, "integratedAddress", first, sizeof(first)));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_integrated_address(wallet, NULL, NULL, &json));
  CHECK(json != NULL && json_string(json, "integratedAddress", second, sizeof(second)));
  monero_utils_free(json);
  CHECK(strcmp(first, second) != 0);

  // a standard address is not an integrated address
  json = NULL;
  EXPECT_ERR(monero_wallet_decode_integrated_address(wallet, ADDRESS, &json));
  CHECK(json == NULL);

  monero_wallet_free(wallet);
}

static void test_sign_and_verify(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  char* signature = NULL;
  EXPECT_ERR_MSG(monero_wallet_sign_message(wallet, "hello", 2, 0, 0, &signature), "unknown signature type");
  EXPECT_ERR_MSG(monero_wallet_sign_message(wallet, "hello", -1, 0, 0, &signature), "unknown signature type");
  CHECK(signature == NULL);
  EXPECT_OK(monero_wallet_sign_message(wallet, "hello", MONERO_MESSAGE_SIGN_WITH_SPEND_KEY, 0, 0, &signature));
  CHECK(signature != NULL && strncmp(signature, "Sig", 3) == 0);

  char* json = NULL;
  EXPECT_OK(monero_wallet_verify_message(wallet, "hello", ADDRESS, signature, &json));
  CHECK(json != NULL && strstr(json, "\"isGood\":true") != NULL);
  CHECK(json != NULL && strstr(json, "\"signatureType\":\"spend\"") != NULL);
  monero_utils_free(json);

  // another message or address doesn't verify, and that is not an error
  json = NULL;
  EXPECT_OK(monero_wallet_verify_message(wallet, "goodbye", ADDRESS, signature, &json));
  CHECK(json != NULL && strstr(json, "\"isGood\":false") != NULL);
  monero_utils_free(json);

  char* sub = NULL;
  EXPECT_OK(monero_wallet_get_address(wallet, 0, 1, &sub));
  json = NULL;
  EXPECT_OK(monero_wallet_verify_message(wallet, "hello", sub, signature, &json));
  CHECK(json != NULL && strstr(json, "\"isGood\":false") != NULL);
  monero_utils_free(json);
  monero_utils_free(sub);
  monero_utils_free(signature);

  // a message signed with the view key verifies too
  signature = NULL;
  EXPECT_OK(monero_wallet_sign_message(wallet, "hello", MONERO_MESSAGE_SIGN_WITH_VIEW_KEY, 0, 0, &signature));
  json = NULL;
  EXPECT_OK(monero_wallet_verify_message(wallet, "hello", ADDRESS, signature, &json));
  CHECK(json != NULL && strstr(json, "\"isGood\":true") != NULL);
  CHECK(json != NULL && strstr(json, "\"signatureType\":\"view\"") != NULL);
  monero_utils_free(json);
  monero_utils_free(signature);

  monero_wallet_free(wallet);
}

static void test_unsupported_operations(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  // keys-only wallets have no files and no height, and don't take listeners
  EXPECT_ERR_MSG(monero_wallet_save(wallet), "save() not supported");
  EXPECT_ERR_MSG(monero_wallet_close(wallet, true), "MoneroWalletKeys does not support saving");
  uint64_t height = 0;
  EXPECT_ERR_MSG(monero_wallet_get_height(wallet, &height), "get_height() not supported");
  // keys-only wallets have no balances either
  uint64_t balance = 0;
  EXPECT_ERR_MSG(monero_wallet_get_account_balance(wallet, 0, &balance), "get_balance() not supported");
  EXPECT_ERR_MSG(monero_wallet_get_subaddress_balance(wallet, 0, 1, &balance), "get_balance() not supported");
  EXPECT_ERR_MSG(monero_wallet_get_account_unlocked_balance(wallet, 0, &balance), "get_unlocked_balance() not supported");
  EXPECT_ERR_MSG(monero_wallet_get_subaddress_unlocked_balance(wallet, 0, 1, &balance), "get_unlocked_balance() not supported");

  monero_wallet_listener_callbacks callbacks = {NULL, NULL, NULL, NULL};
  monero_wallet_listener* listener = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));
  EXPECT_ERR_MSG(monero_wallet_add_listener(wallet, listener), "add_listener() not supported");
  monero_wallet_listener_free(listener);

  // the failed calls leave the wallet usable
  check_keys(wallet, ADDRESS, VIEW_KEY, SPEND_KEY);
  monero_wallet_free(wallet);
}

static void test_close(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  EXPECT_OK(monero_wallet_close(wallet, false));
  EXPECT_OK(monero_wallet_close(wallet, false));  // closing a closed wallet has no effect

  char* address = NULL;
  EXPECT_ERR(monero_wallet_get_address(wallet, 0, 0, &address));
  CHECK(address == NULL);

  monero_wallet_free(wallet);
}

static void test_listener_rollback(void) {
  const char* path = "monero_c_wallet_keys_unit_rollback";
  remove_files(path);

  monero_wallet* keys = NULL;
  monero_wallet* full = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &keys));
  EXPECT_OK(monero_wallet_create_from_seed(path, PASSWORD, MAINNET, SEED, NULL, 0, NULL, &full));
  CHECK(keys != NULL && full != NULL);

  monero_wallet_listener_callbacks callbacks = {NULL, NULL, NULL, NULL};
  monero_wallet_listener* listener = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));

  // the keys-only wallet refuses the listener, so the full wallet can still take it
  EXPECT_ERR(monero_wallet_add_listener(keys, listener));
  EXPECT_OK(monero_wallet_add_listener(full, listener));

  monero_wallet_listener_free(listener);
  monero_wallet_free(full);
  monero_wallet_free(keys);
  remove_files(path);
}

static void test_ground_truth_against_full_wallet(void) {
  const char* path = "monero_c_wallet_keys_unit_full";
  remove_files(path);

  monero_wallet* keys = NULL;
  monero_wallet* full = NULL;
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &keys));
  EXPECT_OK(monero_wallet_create_from_seed(path, PASSWORD, MAINNET, SEED, NULL, 0, NULL, &full));
  CHECK(keys != NULL && full != NULL);
  if (keys == NULL || full == NULL) {
    monero_wallet_free(keys);
    monero_wallet_free(full);
    remove_files(path);
    return;
  }

  // the same seed gives the same keys and addresses in both wallet types
  check_keys(full, ADDRESS, VIEW_KEY, SPEND_KEY);
  for (uint32_t account = 0; account < 2; account++) {
    for (uint32_t sub = 0; sub < 3; sub++) {
      char* from_keys = NULL;
      char* from_full = NULL;
      EXPECT_OK(monero_wallet_get_address(keys, account, sub, &from_keys));
      EXPECT_OK(monero_wallet_get_address(full, account, sub, &from_full));
      CHECK(from_keys != NULL && from_full != NULL && strcmp(from_keys, from_full) == 0);
      monero_utils_free(from_keys);
      monero_utils_free(from_full);
    }
  }

  monero_wallet_free(full);
  monero_wallet_free(keys);
  remove_files(path);
}

int main(void) {
  test_create_random();
  test_create_random_in_language();
  test_create_from_seed();
  test_seed_offset_changes_wallet();
  test_create_from_keys();
  test_view_only_from_keys();
  test_invalid_arguments();
  test_addresses();
  test_subaddresses_json();
  test_account_json();
  test_integrated_address();
  test_sign_and_verify();
  test_unsupported_operations();
  test_close();
  test_listener_rollback();
  test_ground_truth_against_full_wallet();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

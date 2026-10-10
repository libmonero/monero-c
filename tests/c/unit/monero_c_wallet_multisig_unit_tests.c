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

#include <stdlib.h>
#include <string.h>

// multisig tests that need no daemon: three full wallets make a 2 of 3 multisig wallet in one process

#define NETWORK MONERO_UTILS_NETWORK_STAGENET
#define PASSWORD "password"

// the multisig hex of each participant in one round, too long for the stack
static char round_hexes[3][16384];

// the values of the other participants, in order
static void others_of(const char* const* values, int index, const char* out[2]) {
  int j;
  int n = 0;
  for (j = 0; j < 3; j++) {
    if (j != index) out[n++] = values[j];
  }
}

static monero_wallet* create_random_wallet(const char* path) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_create_random(path, PASSWORD, NETWORK, NULL, &wallet));
  return wallet;
}

static void free_strings(char** values) {
  int i;
  for (i = 0; i < 3; i++) {
    if (values[i] != NULL) monero_utils_free(values[i]);
    values[i] = NULL;
  }
}

static void test_two_of_three(void) {
  const char* paths[3] = {"monero_c_wallet_multisig_a", "monero_c_wallet_multisig_b", "monero_c_wallet_multisig_c"};
  monero_wallet* wallets[3] = {NULL, NULL, NULL};
  char* prepared[3] = {NULL, NULL, NULL};
  char* made[3] = {NULL, NULL, NULL};
  char* init[3] = {NULL, NULL, NULL};
  char* exported[3] = {NULL, NULL, NULL};
  char address[3][128];
  const char* others[2];
  const char* current[3];
  bool multisig = false;
  int ready = 0;
  int round;
  int i;

  for (i = 0; i < 3; i++) wallets[i] = create_random_wallet(paths[i]);
  CHECK(wallets[0] != NULL && wallets[1] != NULL && wallets[2] != NULL);
  if (wallets[0] == NULL || wallets[1] == NULL || wallets[2] == NULL) {
    for (i = 0; i < 3; i++) {
      if (wallets[i] != NULL) monero_wallet_free(wallets[i]);
      remove_files(paths[i]);
    }
    return;
  }

  // round 1: each participant shares its multisig info
  for (i = 0; i < 3; i++) {
    EXPECT_OK(monero_wallet_prepare_multisig(wallets[i], &prepared[i]));
    CHECK(prepared[i] != NULL && prepared[i][0] != '\0');
  }
  for (i = 0; i < 3; i++) current[i] = prepared[i];

  // round 2: each makes the wallet multisig with the other two, for a threshold of 2
  for (i = 0; i < 3; i++) {
    others_of(current, i, others);
    EXPECT_OK(monero_wallet_make_multisig(wallets[i], others, 2, 2, PASSWORD, &made[i]));
  }
  for (i = 0; i < 3; i++) current[i] = made[i];

  // the exchange repeats until every participant has an address. Each round takes the
  // multisig hex that the previous round returned
  for (round = 0; round < 4 && !ready; round++) {
    for (i = 0; i < 3; i++) {
      others_of(current, i, others);
      if (init[i] != NULL) monero_utils_free(init[i]);
      init[i] = NULL;
      EXPECT_OK(monero_wallet_exchange_multisig_keys(wallets[i], others, 2, PASSWORD, &init[i]));
    }
    ready = 1;
    for (i = 0; i < 3; i++) {
      if (init[i] != NULL && json_string(init[i], "address", address[i], sizeof(address[i]))) continue;
      ready = 0;
      if (init[i] != NULL && json_string(init[i], "multisigHex", round_hexes[i], sizeof(round_hexes[i]))) current[i] = round_hexes[i];
    }
  }
  CHECK(ready);
  CHECK(strcmp(address[0], address[1]) == 0 && strcmp(address[1], address[2]) == 0);

  for (i = 0; i < 3; i++) {
    EXPECT_OK(monero_wallet_is_multisig(wallets[i], &multisig));
    CHECK(multisig);
  }

  // the seed of a multisig wallet is its multisig data in hex, which a config restores
  char* seed = NULL;
  EXPECT_OK(monero_wallet_get_seed(wallets[0], &seed));
  CHECK(seed != NULL && seed[0] != '\0');
  if (seed != NULL) {
    char config[4096];
    char* restored_address = NULL;
    monero_wallet* restored = NULL;
    snprintf(config, sizeof(config), "{\"path\":\"monero_c_wallet_multisig_restored\",\"password\":\"" PASSWORD "\",\"networkType\":%d,\"seed\":\"%s\",\"isMultisig\":true}", NETWORK, seed);
    EXPECT_OK(monero_wallet_create(config, &restored));
    CHECK(restored != NULL);
    if (restored != NULL) {
      multisig = false;
      EXPECT_OK(monero_wallet_is_multisig(restored, &multisig));
      CHECK(multisig);
      EXPECT_OK(monero_wallet_get_primary_address(restored, &restored_address));
      CHECK(restored_address != NULL && strcmp(restored_address, address[0]) == 0);
      monero_utils_free(restored_address);
      monero_wallet_free(restored);
    }
    remove_files("monero_c_wallet_multisig_restored");
    monero_utils_free(seed);
  }

  // the multisig info and the export are available once the wallet is multisig
  char* json = NULL;
  EXPECT_OK(monero_wallet_get_multisig_info(wallets[0], &json));
  CHECK(json != NULL && json[0] == '{');
  monero_utils_free(json);

  for (i = 0; i < 3; i++) {
    EXPECT_OK(monero_wallet_export_multisig_hex(wallets[i], &exported[i]));
    CHECK(exported[i] != NULL && exported[i][0] != '\0');
  }

  // the first wallet imports the exports of the other two
  const char* imports[2] = {exported[1], exported[2]};
  int imported = -1;
  EXPECT_OK(monero_wallet_import_multisig_hex(wallets[0], imports, 2, false, &imported));
  CHECK(imported >= 0);

  // a signature needs a transaction, which these tests don't create
  EXPECT_ERR(monero_wallet_sign_multisig_tx_hex(wallets[0], "00", &json));
  CHECK(json == NULL);

  free_strings(prepared);
  free_strings(made);
  free_strings(init);
  free_strings(exported);
  for (i = 0; i < 3; i++) {
    monero_wallet_free(wallets[i]);
    remove_files(paths[i]);
  }
}

static void test_not_multisig(void) {
  const char* path = "monero_c_wallet_multisig_single";
  monero_wallet* wallet = create_random_wallet(path);
  CHECK(wallet != NULL);
  if (wallet == NULL) {
    remove_files(path);
    return;
  }

  bool multisig = true;
  EXPECT_OK(monero_wallet_is_multisig(wallet, &multisig));
  CHECK(!multisig);

  // only a multisig wallet has multisig info to export
  char* hex = NULL;
  EXPECT_ERR_MSG(monero_wallet_export_multisig_hex(wallet, &hex), "This wallet is not multisig");
  CHECK(hex == NULL);

  monero_wallet_free(wallet);
  remove_files(path);
}

static void test_keys_only(void) {
  monero_wallet* wallet = NULL;
  EXPECT_OK(monero_wallet_keys_create_random(NETWORK, NULL, &wallet));
  CHECK(wallet != NULL);
  if (wallet == NULL) return;

  // keys-only wallets don't support multisig
  char* hex = NULL;
  EXPECT_ERR(monero_wallet_prepare_multisig(wallet, &hex));
  CHECK(hex == NULL);
  bool needed = false;
  EXPECT_ERR(monero_wallet_is_multisig_import_needed(wallet, &needed));

  monero_wallet_free(wallet);
}

int main(void) {
  test_two_of_three();
  test_not_multisig();
  test_keys_only();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

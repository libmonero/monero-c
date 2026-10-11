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

// integration tests for the wallet. The daemon part needs a regtest monerod, and the RPC part a
// monero-wallet-rpc server connected to it. Set the URIs of both, for example:
//
//   docker compose -f tests/c/integration/docker-compose.yml up -d
//   MONERO_C_TEST_DAEMON_URI=http://127.0.0.1:18081 MONERO_C_TEST_WALLET_RPC_URI=http://127.0.0.1:18082 \
//     ./build/tests/c/integration/monero_c_wallet_integration_tests
//
// the daemon part mines blocks, so never point it at a real node. Run the node with --fixed-difficulty=1,
// as the compose file does. The RPC part stops the server at the end. Each part is skipped, and the program
// exits with 77 when its URI is missing

#define EXIT_SKIPPED 77
#define PASSWORD "password"
#define NETWORK MONERO_UTILS_NETWORK_REGTEST

// the public test wallet of monero-python's config.ini, without funds
#define SEED "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex"
#define ADDRESS "48W9YHwPzRz9aPTeXCA6kmSpW6HsvmWx578jj3of2gT3JwZzwTf33amESBoNDkL6SVK34Q2HTKqgYbGyE1hBws3wCrcBDR2"
#define VIEW_KEY "e8c2288181bad9ec410d7322efd65f663c6da57bd1d1198636278a039743a600"

#if defined(_WIN32)

int main(void) {
  printf("skipped: the wallet integration tests run on POSIX systems only\n");
  return EXIT_SKIPPED;
}

#else

#include <pthread.h>
#include <unistd.h>

// the amount of the test transaction, in atomic units
#define AMOUNT 1000000000ULL

static volatile int g_received = 0;
static volatile int g_spent = 0;
static monero_daemon* g_daemon = NULL;
static char g_mining_address[128];

// the keys of the wallet that has the history on the chain, which the RPC part gives to a full wallet and to an RPC wallet to compare them
static char g_keys_address[128];
static char g_keys_view[128];
static char g_keys_spend[128];

// the JSON of the first output that a listener gets, which the test reads after the sync that calls it has returned
static char g_first_received[4096];
static char g_first_spent[4096];

static void on_output_received(void* user_data, const char* output_json) {
  (void)user_data;
  if (g_received == 0) snprintf(g_first_received, sizeof(g_first_received), "%s", output_json);
  g_received++;
}

static void on_output_spent(void* user_data, const char* output_json) {
  (void)user_data;
  if (g_spent == 0) snprintf(g_first_spent, sizeof(g_first_spent), "%s", output_json);
  g_spent++;
}

static int has(const char* json, const char* text) {
  return json != NULL && strstr(json, text) != NULL;
}

static int is_list(const char* json) {
  return json != NULL && json[0] == '[';
}

static int is_blocks(const char* json) {
  return json != NULL && strncmp(json, "{\"blocks\":[", 11) == 0;
}

// the length of s, up to max characters
static size_t bounded_length(const char* s, size_t max) {
  size_t n = 0;
  while (n < max && s[n] != '\0') n++;
  return n;
}

// how many times the text is in the JSON
static int count_text(const char* json, const char* text) {
  int count = 0;
  size_t length = bounded_length(text, 64);
  for (const char* p = json; json != NULL && (p = strstr(p, text)) != NULL; p += length) count++;
  return count;
}

// mines count blocks to the address, on top of the chain of the daemon
static void mine(monero_daemon* daemon, const char* address, uint64_t count) {
  char* json = NULL;
  EXPECT_OK(monero_daemon_generate_blocks(daemon, address, count, NULL, NULL, &json));
  monero_utils_free(json);
}

// mines one block after one second, for wait_for_next_block()
static void* mine_later(void* arg) {
  (void)arg;
  sleep(1);
  mine(g_daemon, g_mining_address, 1);
  return NULL;
}

static void test_daemon(const char* daemon_uri) {
  char* json = NULL;
  char address_a[128];
  char view_key[128];
  char spend_key[128];
  bool flag = false;
  bool trusted = true;
  uint64_t value = 0;
  monero_wallet_listener* listener = NULL;
  monero_wallet_listener_callbacks callbacks = {NULL, NULL, NULL, NULL, on_output_received, on_output_spent};

  EXPECT_OK(monero_daemon_connect(daemon_uri, "", "", "", 10000, &g_daemon));
  CHECK(g_daemon != NULL);
  if (g_daemon == NULL) return;

  // wallet a mines and owns the outputs. Wallet c has its keys, so it scans the same outputs
  const char* path_a = "monero_c_wallet_integration_a";
  const char* path_c = "monero_c_wallet_integration_c";
  const char* path_v = "monero_c_wallet_integration_v";
  monero_wallet* a = NULL;
  EXPECT_OK(monero_wallet_create_random(path_a, PASSWORD, NETWORK, NULL, &a));
  CHECK(a != NULL);
  if (a == NULL) {
    remove_files(path_a);
    monero_daemon_free(g_daemon);
    g_daemon = NULL;
    return;
  }

  EXPECT_OK(monero_wallet_set_daemon_connection(a, daemon_uri, "", "", "", &trusted, false));
  EXPECT_OK(monero_wallet_get_primary_address(a, &json));
  CHECK(json != NULL && snprintf(address_a, sizeof(address_a), "%s", json) > 0);
  monero_utils_free(json);
  json = NULL;
  snprintf(g_mining_address, sizeof(g_mining_address), "%s", address_a);

  // coinbase outputs unlock after 60 blocks, and a ring of 16 needs 16 unlocked outputs
  mine(g_daemon, address_a, 100);

  // sync and the daemon queries
  EXPECT_OK(monero_wallet_sync(a, NULL, NULL, &json));
  CHECK(has(json, "\"numBlocksFetched\""));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_is_connected_to_daemon(a, &flag));
  CHECK(flag);
  EXPECT_OK(monero_wallet_is_daemon_synced(a, &flag));
  CHECK(flag);
  EXPECT_OK(monero_wallet_is_synced(a, &flag));
  CHECK(flag);
  EXPECT_OK(monero_wallet_get_height(a, &value));
  CHECK(value >= 70);
  EXPECT_OK(monero_wallet_get_daemon_height(a, &value));
  CHECK(value >= 70);
  EXPECT_OK(monero_wallet_get_daemon_max_peer_height(a, &value));
  EXPECT_OK(monero_wallet_get_height_by_date(a, 2014, 5, 1, &value));

  // the first blocks are unlocked, so the balances are not zero
  EXPECT_OK(monero_wallet_get_balance(a, &value));
  CHECK(value > 0);
  EXPECT_OK(monero_wallet_get_unlocked_balance(a, &value));
  CHECK(value > 0);
  EXPECT_OK(monero_wallet_get_account_balance(a, 0, &value));
  CHECK(value > 0);
  EXPECT_OK(monero_wallet_get_subaddress_balance(a, 0, 0, &value));
  CHECK(value > 0);
  EXPECT_OK(monero_wallet_get_account_unlocked_balance(a, 0, &value));
  CHECK(value > 0);
  EXPECT_OK(monero_wallet_get_subaddress_unlocked_balance(a, 0, 0, &value));
  CHECK(value > 0);

  // the history of the wallet comes in blocks, with each tx in its block and each transfer and output in its tx
  EXPECT_OK(monero_wallet_get_txs(a, NULL, &json));
  CHECK(is_blocks(json) && has(json, "{\"blocks\":[{\"height\":") && has(json, "\"timestamp\":") && has(json, "\"txs\":[{") && has(json, "\"hash\":\""));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_transfers(a, NULL, &json));
  CHECK(is_blocks(json) && has(json, "{\"blocks\":[{\"height\":") && has(json, "\"hash\":\"") && has(json, "\"incomingTransfers\":[{"));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_outputs(a, NULL, &json));
  CHECK(is_blocks(json) && has(json, "{\"blocks\":[{\"height\":") && has(json, "\"hash\":\"") && has(json, "\"outputs\":[{") && has(json, "\"amount\""));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_txs(a, "{\"minHeight\":1000000}", &json));
  CHECK(json != NULL && strcmp(json, "{\"blocks\":[]}") == 0);
  monero_utils_free(json);
  json = NULL;

  // the query of the tx of a transfer or of an output is in "txQuery". The outputs of the miner txs unlock after 60 blocks, so the
  // two halves of the outputs are the unlocked ones and the locked ones, and a range of heights holds one transfer per block
  EXPECT_OK(monero_wallet_get_outputs(a, NULL, &json));
  int outputs_total = count_text(json, "\"stealthPublicKey\"");
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_outputs(a, "{\"txQuery\":{\"isLocked\":false}}", &json));
  int outputs_unlocked = count_text(json, "\"stealthPublicKey\"");
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_outputs(a, "{\"txQuery\":{\"isLocked\":true}}", &json));
  int outputs_locked = count_text(json, "\"stealthPublicKey\"");
  monero_utils_free(json);
  json = NULL;
  CHECK(outputs_unlocked > 0 && outputs_locked > 0 && outputs_unlocked + outputs_locked == outputs_total);
  EXPECT_OK(monero_wallet_get_transfers(a, "{\"isIncoming\":true,\"txQuery\":{\"isConfirmed\":true,\"minHeight\":50,\"maxHeight\":60}}", &json));
  CHECK(is_blocks(json) && count_text(json, "\"incomingTransfers\":[{") == 11);
  monero_utils_free(json);
  json = NULL;

  // wallets that a config connects to the daemon when it creates them: the seed of a with the first block, and a new one on regtest
  {
    const char* path_d = "monero_c_wallet_integration_d";
    char seed[512];
    char config[1536];
    uint64_t balance_a = 0;
    uint64_t balance_d = 1;
    monero_wallet* d = NULL;
    remove_files(path_d);
    EXPECT_OK(monero_wallet_get_seed(a, &json));
    CHECK(json != NULL && snprintf(seed, sizeof(seed), "%s", json) > 0);
    monero_utils_free(json);
    json = NULL;
    snprintf(config, sizeof(config), "{\"path\":\"%s\",\"password\":\"" PASSWORD "\",\"networkType\":3,\"seed\":\"%s\",\"restoreHeight\":0,\"server\":{\"uri\":\"%s\"}}", path_d, seed, daemon_uri);
    EXPECT_OK(monero_wallet_create(config, &d));
    CHECK(d != NULL);
    if (d != NULL) {
      EXPECT_OK(monero_wallet_is_connected_to_daemon(d, &flag));
      CHECK(flag);
      EXPECT_OK(monero_wallet_is_daemon_trusted(d, &flag));
      CHECK(flag);
      EXPECT_OK(monero_wallet_sync(d, NULL, NULL, &json));
      monero_utils_free(json);
      json = NULL;
      EXPECT_OK(monero_wallet_get_balance(a, &balance_a));
      EXPECT_OK(monero_wallet_get_balance(d, &balance_d));
      CHECK(balance_a > 0 && balance_d == balance_a);
      monero_wallet_free(d);
      d = NULL;
    }
    remove_files(path_d);

    snprintf(config, sizeof(config), "{\"networkType\":3,\"server\":{\"uri\":\"%s\"},\"isTrustedDaemon\":true}", daemon_uri);
    EXPECT_OK(monero_wallet_create(config, &d));
    CHECK(d != NULL);
    if (d != NULL) {
      EXPECT_OK(monero_wallet_is_connected_to_daemon(d, &flag));
      CHECK(flag);
      EXPECT_OK(monero_wallet_get_restore_height(d, &value));
      CHECK(value == 0);
      EXPECT_OK(monero_wallet_sync(d, NULL, NULL, &json));
      monero_utils_free(json);
      json = NULL;
      EXPECT_OK(monero_wallet_get_height(d, &value));
      CHECK(value >= 70);
      EXPECT_OK(monero_wallet_get_balance(d, &balance_d));
      CHECK(balance_d == 0);
      monero_wallet_free(d);
    }
  }

  // wallet c, with the keys of a, receives the outputs, and the listener is told about them
  EXPECT_OK(monero_wallet_get_private_view_key(a, &json));
  CHECK(json != NULL && snprintf(view_key, sizeof(view_key), "%s", json) > 0);
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_private_spend_key(a, &json));
  CHECK(json != NULL && snprintf(spend_key, sizeof(spend_key), "%s", json) > 0);
  monero_utils_free(json);
  json = NULL;

  snprintf(g_keys_address, sizeof(g_keys_address), "%s", address_a);
  snprintf(g_keys_view, sizeof(g_keys_view), "%s", view_key);
  snprintf(g_keys_spend, sizeof(g_keys_spend), "%s", spend_key);

  monero_wallet* c = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));
  EXPECT_OK(monero_wallet_create_from_keys(path_c, PASSWORD, NETWORK, address_a, view_key, spend_key, 0, NULL, &c));
  CHECK(c != NULL);
  if (c != NULL) {
    EXPECT_OK(monero_wallet_set_daemon_connection(c, daemon_uri, "", "", "", &trusted, false));
    g_received = 0;
    EXPECT_OK(monero_wallet_sync(c, NULL, listener, &json));
    monero_utils_free(json);
    json = NULL;
    CHECK(g_received > 0);

    // the listener gets the output in its tx and its block, with the height, the hash and the unlock time of a miner tx
    CHECK(has(g_first_received, "{\"blocks\":[{\"height\":") && has(g_first_received, "\"txs\":[{") && has(g_first_received, "\"hash\":\"") &&
          has(g_first_received, "\"unlockTime\":") && has(g_first_received, "\"isLocked\":") && has(g_first_received, "\"outputs\":[{\"amount\":") &&
          has(g_first_received, "\"accountIndex\":0") && has(g_first_received, "\"subaddressIndex\":0"));
  }

  // create a tx without relaying, relay it from its JSON and mine it
  const char* config = "{\"accountIndex\":0,\"destinations\":[{\"address\":\"" ADDRESS "\",\"amount\":1000000000}]}";
  char* tx_hash = NULL;
  EXPECT_OK(monero_wallet_create_tx(a, config, &json));
  CHECK(json != NULL && has(json, "{\"txSets\":[{\"txs\":[{") && has(json, "\"metadata\"") && !has(json, "\"unsignedTxHex\""));
  EXPECT_OK(monero_wallet_relay_tx_json(a, json, &tx_hash));
  CHECK(tx_hash != NULL);
  monero_utils_free(json);
  json = NULL;

  // a tx that is relayed and not mined is in the pool, in the block that has no height
  EXPECT_OK(monero_wallet_get_txs(a, "{\"inTxPool\":true}", &json));
  CHECK(has(json, "{\"blocks\":[{\"txs\":[{") && has(json, tx_hash) && !has(json, "\"height\""));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_transfers(a, "{\"isIncoming\":false,\"txQuery\":{\"inTxPool\":true}}", &json));
  CHECK(has(json, "{\"blocks\":[{\"txs\":[{") && has(json, tx_hash) && has(json, "\"outgoingTransfer\":{") && !has(json, "\"height\""));
  monero_utils_free(json);
  json = NULL;

  // the daemon has it in the pool in the same way: in the block that has no height
  EXPECT_OK(monero_daemon_get_tx_pool(g_daemon, &json));
  CHECK(has(json, "{\"blocks\":[{\"txs\":[{") && has(json, tx_hash) && !has(json, "\"height\""));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_daemon_get_tx(g_daemon, tx_hash, false, &json));
  CHECK(has(json, "{\"blocks\":[{\"txs\":[{") && has(json, tx_hash) && !has(json, "\"height\""));
  monero_utils_free(json);
  json = NULL;
  mine(g_daemon, address_a, 1);
  EXPECT_OK(monero_wallet_sync(a, NULL, NULL, &json));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_tx_key(a, tx_hash, &json));
  CHECK(json != NULL);
  monero_utils_free(json);
  json = NULL;

  // the daemon has the mined tx in its block, with the height of the chain at that time
  EXPECT_OK(monero_daemon_get_tx(g_daemon, tx_hash, false, &json));
  CHECK(has(json, "{\"blocks\":[{\"height\":") && has(json, "\"timestamp\":") && has(json, tx_hash));
  monero_utils_free(json);
  json = NULL;
  {
    const char* hashes[] = {tx_hash};
    EXPECT_OK(monero_daemon_get_txs(g_daemon, hashes, 1, false, &json));
    CHECK(has(json, "{\"blocks\":[{\"height\":") && count_text(json, "\"txs\":[{") == 1 && has(json, tx_hash));
    monero_utils_free(json);
    json = NULL;
  }

  // wallet c sees the tx that a spent and mined, so its listener is told about the spent output and the change, in their tx
  if (c != NULL) {
    char* synced_c = NULL;
    g_received = 0;
    g_spent = 0;
    g_first_received[0] = '\0';
    g_first_spent[0] = '\0';
    EXPECT_OK(monero_wallet_sync(c, NULL, listener, &synced_c));
    monero_utils_free(synced_c);
    CHECK(g_received > 0 && g_spent > 0);
    CHECK(has(g_first_spent, "{\"blocks\":[{\"height\":") && has(g_first_spent, tx_hash) && has(g_first_spent, "\"inputs\":[{\"amount\":") && has(g_first_spent, "\"accountIndex\":0"));
    CHECK(has(g_first_received, "{\"blocks\":[{\"height\":") && has(g_first_received, "\"outputs\":[{\"amount\":"));
  }

  // once it is mined it is in a block with a height, and its transfer is in it. It is asked for by its hash, since a query on the
  // type of the txs leaks the txs that it filters out in monero-cpp, see TODO.md. One run in eight of the first version had the wallet
  // still see it in the pool after the sync, so the wallet gets a few more syncs to see the block, and the JSON is kept if it doesn't
  char hash_query[160];
  snprintf(hash_query, sizeof(hash_query), "{\"hashes\":[\"%s\"]}", tx_hash);
  for (int attempt = 0; attempt < 5; attempt++) {
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_wallet_get_txs(a, hash_query, &json));
    if (has(json, "{\"blocks\":[{\"height\":")) break;
    char* synced = NULL;
    sleep(1);
    EXPECT_OK(monero_wallet_sync(a, NULL, NULL, &synced));
    monero_utils_free(synced);
  }
  if (!has(json, "{\"blocks\":[{\"height\":")) {
    char* seen_by_daemon = NULL;
    uint64_t chain_height = 0;
    monero_daemon_get_height(g_daemon, &chain_height);
    if (monero_daemon_get_tx(g_daemon, tx_hash, false, &seen_by_daemon) != MONERO_OK) fprintf(stderr, "the daemon has no such tx: %s\n", monero_last_error());
    fprintf(stderr, "the mined tx is not in a block. The chain is at %llu, and the daemon has it as %.600s\nthe wallet has it as %.600s\n", (unsigned long long) chain_height, seen_by_daemon != NULL ? seen_by_daemon : "(null)", json != NULL ? json : "(null)");
    monero_utils_free(seen_by_daemon);
  }
  CHECK(has(json, "{\"blocks\":[{\"height\":") && has(json, tx_hash) && has(json, "\"outgoingTransfer\":{"));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_transfers(a, "{\"isIncoming\":false}", &json));
  CHECK(has(json, "{\"blocks\":[{\"height\":") && has(json, tx_hash) && has(json, "\"outgoingTransfer\":{"));
  monero_utils_free(json);
  json = NULL;

  // tx and spend proofs of the sent tx
  char* proof = NULL;
  bool good = false;
  EXPECT_OK(monero_wallet_get_tx_proof(a, tx_hash, ADDRESS, "message", &proof));
  CHECK(proof != NULL);
  if (proof != NULL) {
    EXPECT_OK(monero_wallet_check_tx_proof(a, tx_hash, ADDRESS, "message", proof, &json));
    CHECK(has(json, "\"isGood\":true"));
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_wallet_check_tx_proof(a, tx_hash, ADDRESS, "other", proof, &json));
    CHECK(has(json, "\"isGood\":false"));
    monero_utils_free(json);
    json = NULL;
    monero_utils_free(proof);
    proof = NULL;
  }
  EXPECT_OK(monero_wallet_get_spend_proof(a, tx_hash, "message", &proof));
  CHECK(proof != NULL);
  if (proof != NULL) {
    EXPECT_OK(monero_wallet_check_spend_proof(a, tx_hash, "message", proof, &good));
    CHECK(good);
    EXPECT_OK(monero_wallet_check_spend_proof(a, tx_hash, "other", proof, &good));
    CHECK(!good);
    monero_utils_free(proof);
  }
  monero_utils_free(tx_hash);
  tx_hash = NULL;

  // sweeps without relay, so the funds stay
  char key_image[128];
  char sweep_config[512];
  EXPECT_OK(monero_wallet_get_outputs(a, "{\"isSpent\":false,\"txQuery\":{\"isLocked\":false}}", &json));
  CHECK(json_string(json, "hex", key_image, sizeof(key_image)));
  monero_utils_free(json);
  json = NULL;
  snprintf(sweep_config, sizeof(sweep_config), "{\"keyImage\":\"%s\",\"destinations\":[{\"address\":\"" ADDRESS "\"}],\"relay\":false}", key_image);
  EXPECT_OK(monero_wallet_sweep_output(a, sweep_config, &json));
  CHECK(has(json, "{\"txSets\":[{\"txs\":[{") && has(json, "\"fee\":"));
  monero_utils_free(json);
  json = NULL;
  EXPECT_ERR(monero_wallet_sweep_output(a, "{\"keyImage\":\"00\",\"destinations\":[{\"address\":\"" ADDRESS "\"}],\"relay\":false}", &json));
  CHECK(json == NULL);
  EXPECT_OK(monero_wallet_sweep_unlocked(a, "{\"accountIndex\":0,\"destinations\":[{\"address\":\"" ADDRESS "\"}],\"relay\":false}", &json));
  CHECK(has(json, "{\"txSets\":[{\"txs\":[{") && has(json, "\"fee\":"));
  monero_utils_free(json);
  json = NULL;

  // bad metadata and bad tx JSON fail
  EXPECT_ERR(monero_wallet_relay_tx(a, "00", &json));
  CHECK(json == NULL);
  EXPECT_ERR(monero_wallet_relay_tx_json(a, "{}", &tx_hash));
  CHECK(tx_hash == NULL);
  const char* const bad_metadatas[] = {"00"};
  EXPECT_ERR(monero_wallet_relay_txs(a, bad_metadatas, 1, &json));
  CHECK(json == NULL);
  EXPECT_OK(monero_wallet_relay_txs(a, NULL, 0, &json));
  CHECK(is_list(json));
  monero_utils_free(json);
  json = NULL;
  EXPECT_ERR(monero_wallet_submit_multisig_tx_hex(a, "00", &json));
  CHECK(json == NULL);
  EXPECT_OK(monero_wallet_relay_txs_json(a, "[]", &json));
  CHECK(is_list(json));
  monero_utils_free(json);
  json = NULL;

  // the proofs of a transaction need the hash of one, so the proofs of a bad hash fail
  char* signature = NULL;
  EXPECT_ERR(monero_wallet_get_tx_key(a, "00", &json));
  EXPECT_ERR(monero_wallet_check_tx_key(a, "00", "00", ADDRESS, &json));
  EXPECT_ERR(monero_wallet_get_tx_proof(a, "00", ADDRESS, "message", &signature));
  CHECK(signature == NULL);
  EXPECT_ERR(monero_wallet_get_spend_proof(a, "00", "message", &signature));
  CHECK(signature == NULL);

  // the reserve proofs need no transaction, only the balance
  EXPECT_OK(monero_wallet_get_reserve_proof_wallet(a, "message", &signature));
  CHECK(signature != NULL);
  EXPECT_OK(monero_wallet_check_reserve_proof(a, address_a, "message", signature, &json));
  CHECK(has(json, "\"isGood\":true"));
  monero_utils_free(json);
  json = NULL;
  monero_utils_free(signature);
  signature = NULL;

  EXPECT_OK(monero_wallet_get_reserve_proof_account(a, 0, 1000, "message", &signature));
  CHECK(signature != NULL);
  EXPECT_OK(monero_wallet_check_reserve_proof(a, address_a, "message", signature, &json));
  CHECK(has(json, "\"isGood\":true"));
  monero_utils_free(json);
  json = NULL;
  monero_utils_free(signature);
  signature = NULL;

  // a view-only wallet creates the unsigned tx set, and signing or submitting bad hex fails
  monero_wallet* v = NULL;
  EXPECT_OK(monero_wallet_create_from_keys(path_v, PASSWORD, NETWORK, address_a, view_key, NULL, 0, NULL, &v));
  CHECK(v != NULL);
  if (v != NULL) {
    EXPECT_OK(monero_wallet_set_daemon_connection(v, daemon_uri, "", "", "", &trusted, false));
    EXPECT_OK(monero_wallet_sync(v, NULL, NULL, &json));
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_wallet_create_txs(v, config, &json));
    CHECK(json != NULL && has(json, "{\"txSets\":[{\"unsignedTxHex\":\"") && has(json, "\"txs\":[{"));

    // the tx of a view-only wallet has its unsigned hex too, in the set that it is in
    char* single = NULL;
    EXPECT_OK(monero_wallet_create_tx(v, config, &single));
    CHECK(single != NULL && has(single, "{\"txSets\":[{\"unsignedTxHex\":\"") && has(single, "\"metadata\""));
    monero_utils_free(single);

    // the full wallet describes the set as it came, with its txs, and the view-only wallet can't
    char* described = NULL;
    EXPECT_OK(monero_wallet_describe_tx_set(a, json, &described));
    CHECK(has(described, "{\"txSets\":[{") && has(described, "\"txs\":[") && has(described, "\"fee\":"));
    monero_utils_free(described);
    described = NULL;
    EXPECT_ERR_MSG(monero_wallet_describe_tx_set(v, json, &described), "command not supported by view-only wallet");
    CHECK(described == NULL);

    // an offline wallet with the keys signs the unsigned hex of the set, which gives the set with its signed hex and its txs. A wallet
    // that has synced with a node can't, since it can't import the outputs of the view-only wallet
    const char* path_o = "monero_c_wallet_integration_o";
    monero_wallet* cold = NULL;
    size_t unsigned_size = bounded_length(json, 1 << 20) + 1;  // the set is about 40 KB
    char* unsigned_hex = (char*) malloc(unsigned_size);
    CHECK(unsigned_hex != NULL);
    EXPECT_OK(monero_wallet_create_from_keys(path_o, PASSWORD, NETWORK, address_a, view_key, spend_key, 0, NULL, &cold));
    CHECK(cold != NULL);
    if (unsigned_hex != NULL && cold != NULL) {
      CHECK(json_string(json, "unsignedTxHex", unsigned_hex, unsigned_size));
      char* signed_set = NULL;
      EXPECT_ERR_MSG(monero_wallet_sign_txs(a, unsigned_hex, &signed_set), "Failed to sign unsigned tx: Hot wallets cannot import outputs");
      EXPECT_OK(monero_wallet_sign_txs(cold, unsigned_hex, &signed_set));
      CHECK(has(signed_set, "{\"txSets\":[{\"signedTxHex\":\"") && has(signed_set, "\"txs\":[") && has(signed_set, "\"hash\""));
      monero_utils_free(signed_set);
    }
    free(unsigned_hex);
    if (cold != NULL) monero_wallet_free(cold);
    remove_files(path_o);
    monero_utils_free(json);
    json = NULL;
    EXPECT_ERR(monero_wallet_sign_txs(a, "00", &json));
    CHECK(json == NULL);
    EXPECT_ERR(monero_wallet_submit_txs(v, "00", &json));
    CHECK(json == NULL);
    EXPECT_ERR(monero_wallet_describe_tx_set(v, "{}", &json));
    CHECK(json == NULL);
    monero_wallet_free(v);
    remove_files(path_v);
  }

  // the sweep takes unmixable outputs only, and miner outputs are RingCT, so the list is empty
  EXPECT_OK(monero_wallet_sweep_dust(a, false, &json));
  CHECK(json != NULL && strcmp(json, "{\"txSets\":[]}") == 0);
  monero_utils_free(json);
  json = NULL;

  // moving the wallet keeps it usable under the new path
  if (c != NULL) {
    const char* moved = "monero_c_wallet_integration_moved";
    EXPECT_OK(monero_wallet_move_to(c, moved, PASSWORD));
    EXPECT_OK(monero_wallet_get_path(c, &json));
    CHECK(has(json, moved));
    monero_utils_free(json);
    json = NULL;
    remove_files(moved);
  }

  // background sync: wait_for_next_block() returns the height of the new block, which is the height the
  // wallet had before, since heights count from zero
  EXPECT_OK(monero_wallet_start_syncing(a, 500));
  uint64_t before = 0;
  EXPECT_OK(monero_wallet_get_height(a, &before));
  pthread_t miner;
  pthread_create(&miner, NULL, mine_later, NULL);
  EXPECT_OK(monero_wallet_wait_for_next_block(a, &value));
  pthread_join(miner, NULL);
  CHECK(value >= before);
  EXPECT_OK(monero_wallet_stop_syncing(a));

  // scans and the rescans. The rescan of the blockchain comes last, since it discards local data
  EXPECT_ERR_MSG(monero_wallet_scan_txs(a, NULL, 0), "No tx hashes given to scan");
  EXPECT_OK(monero_wallet_rescan_spent(a));

  // mining on the trusted daemon, to the address of the wallet
  uint64_t threads = 1;
  bool background = false;
  bool ignore_battery = true;
  EXPECT_OK(monero_wallet_start_mining(a, &threads, &background, &ignore_battery));
  // the daemon takes a few seconds to start its miner thread, and stop_mining() waits for it
  sleep(3);
  EXPECT_OK(monero_wallet_stop_mining(a));

  // a listener is registered with one wallet at a time. A sync with it uses the plain sync
  // when the wallet already tracks it, and fails when another wallet does
  if (c != NULL) {
    EXPECT_OK(monero_wallet_add_listener(c, listener));
    EXPECT_ERR_MSG(monero_wallet_sync(a, NULL, listener, &json), "listener is already registered with another wallet");
    EXPECT_OK(monero_wallet_remove_listener(c, listener));
    EXPECT_OK(monero_wallet_add_listener(a, listener));
    EXPECT_OK(monero_wallet_sync(a, NULL, listener, &json));
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_wallet_remove_listener(a, listener));
  }

  EXPECT_OK(monero_wallet_rescan_blockchain(a));

  if (listener != NULL) monero_wallet_listener_free(listener);
  if (c != NULL) {
    monero_wallet_free(c);
    remove_files(path_c);
  }
  monero_wallet_free(a);
  remove_files(path_a);
  monero_daemon_free(g_daemon);
  g_daemon = NULL;
}

static void test_rpc_matches_full(const char* daemon_uri, const char* rpc_uri);

static void test_rpc(const char* daemon_uri, const char* rpc_uri) {
  char* json = NULL;
  char address[128];
  char connection[512];
  uint64_t value = 0;
  monero_wallet* full = NULL;
  monero_wallet* rpc = NULL;
  monero_wallet* opened = NULL;
  monero_wallet* seed = NULL;
  monero_wallet* keys = NULL;

  // a full wallet is not an RPC wallet
  EXPECT_OK(monero_wallet_create_random("monero_c_wallet_integration_full", PASSWORD, NETWORK, NULL, &full));
  if (full != NULL) {
    EXPECT_ERR_MSG(monero_wallet_rpc_get_connection(full, &json), "not an RPC wallet");
    monero_wallet_free(full);
    remove_files("monero_c_wallet_integration_full");
  }

  // a random wallet on the server, with the seed, languages and connection
  EXPECT_OK(monero_wallet_rpc_create_random(rpc_uri, "", "", "rpc_random", PASSWORD, NULL, &rpc));
  CHECK(rpc != NULL);
  if (rpc == NULL) return;

  EXPECT_OK(monero_wallet_get_seed(rpc, &json));
  CHECK(count_words(json) == 25);
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_primary_address(rpc, &json));
  CHECK(json != NULL && monero_utils_is_valid_address(json, NETWORK));
  CHECK(json != NULL && snprintf(address, sizeof(address), "%s", json) > 0);
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_wallet_rpc_get_seed_languages(rpc, &json));
  CHECK(has(json, "\"English\""));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_rpc_get_connection(rpc, &json));
  CHECK(has(json, "\"uri\""));
  monero_utils_free(json);
  json = NULL;

  // the handle works like the other wallets, and the RPC methods read the server
  EXPECT_OK(monero_wallet_get_height(rpc, &value));
  EXPECT_OK(monero_wallet_rpc_set_poll_period(rpc, 1000));
  EXPECT_OK(monero_wallet_rpc_get_balances(rpc, NULL, NULL, &json));
  CHECK(json != NULL);
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_rpc_get_account(rpc, 0, true, false, &json));
  CHECK(has(json, "\"index\":0"));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_rpc_get_accounts(rpc, true, NULL, true, &json));
  CHECK(is_list(json) && has(json, "\"index\":0"));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_rpc_get_subaddresses(rpc, 0, NULL, 0, true, &json));
  CHECK(is_list(json) && has(json, "\"index\":0"));
  monero_utils_free(json);
  json = NULL;

  // the history of a new wallet is in blocks too, and has none
  EXPECT_OK(monero_wallet_get_txs(rpc, NULL, &json));
  CHECK(json != NULL && strcmp(json, "{\"blocks\":[]}") == 0);
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_transfers(rpc, NULL, &json));
  CHECK(json != NULL && strcmp(json, "{\"blocks\":[]}") == 0);
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_outputs(rpc, NULL, &json));
  CHECK(json != NULL && strcmp(json, "{\"blocks\":[]}") == 0);
  monero_utils_free(json);
  json = NULL;

  snprintf(connection, sizeof(connection), "{\"uri\":\"%s\"}", daemon_uri);
  EXPECT_OK(monero_wallet_rpc_set_daemon_connection(rpc, connection, true, NULL));
  EXPECT_OK(monero_wallet_rpc_set_daemon_connection(rpc, connection, true, ""));

  // a wallet with the same name on the server, and the seed and keys wallets, switch the open wallet
  EXPECT_OK(monero_wallet_rpc_open(rpc_uri, "", "", "rpc_random", PASSWORD, &opened));
  CHECK(opened != NULL);
  if (opened != NULL) {
    EXPECT_OK(monero_wallet_get_primary_address(opened, &json));
    CHECK(json != NULL && strcmp(json, address) == 0);
    monero_utils_free(json);
    json = NULL;
    monero_wallet_free(opened);
  }

  EXPECT_OK(monero_wallet_rpc_create_from_seed(rpc_uri, "", "", "rpc_seed", PASSWORD, SEED, "", 0, "English", &seed));
  CHECK(seed != NULL);
  if (seed != NULL) {
    EXPECT_OK(monero_wallet_get_primary_address(seed, &json));
    CHECK(json != NULL && strcmp(json, ADDRESS) == 0);
    monero_utils_free(json);
    json = NULL;
    monero_wallet_free(seed);
  }

  EXPECT_OK(monero_wallet_rpc_create_from_keys(rpc_uri, "", "", "rpc_keys", PASSWORD, ADDRESS, VIEW_KEY, NULL, 0, NULL, &keys));
  CHECK(keys != NULL);
  if (keys != NULL) {
    bool view_only = false;
    EXPECT_OK(monero_wallet_is_view_only(keys, &view_only));
    CHECK(view_only);
    EXPECT_ERR(monero_wallet_get_private_spend_key(keys, &json));
    monero_wallet_free(keys);
  }

  // a client from a connection creates a wallet from a config, then opens another one
  char rpc_config[256];
  monero_rpc_connection* rpc_connection = NULL;
  monero_wallet* client = NULL;
  snprintf(rpc_config, sizeof(rpc_config), "{\"uri\":\"%s\",\"timeoutMs\":60000}", rpc_uri);
  EXPECT_OK(monero_rpc_connection_create(rpc_config, &rpc_connection));
  EXPECT_OK(monero_wallet_rpc_connect(rpc_connection, &client));
  monero_rpc_connection_free(rpc_connection);
  CHECK(client != NULL);
  if (client != NULL) {
    EXPECT_OK(monero_wallet_rpc_create_wallet(client, "{\"path\":\"rpc_config\",\"password\":\"" PASSWORD "\",\"seed\":\"" SEED "\",\"restoreHeight\":0}"));
    EXPECT_OK(monero_wallet_get_primary_address(client, &json));
    CHECK(json != NULL && strcmp(json, ADDRESS) == 0);
    monero_utils_free(json);
    json = NULL;
    EXPECT_ERR(monero_wallet_rpc_create_wallet(client, "{\"path\":\"rpc_config\",\"networkType\":0}"));
    EXPECT_OK(monero_wallet_rpc_open_wallet(client, "rpc_random", PASSWORD));
    EXPECT_OK(monero_wallet_get_primary_address(client, &json));
    CHECK(json != NULL && strcmp(json, address) == 0);
    monero_utils_free(json);
    json = NULL;
    monero_wallet_free(client);
  }

  // the history of a wallet with funds is the same as the one of a full wallet with its keys, when the first part made one
  if (g_keys_address[0] != '\0') test_rpc_matches_full(daemon_uri, rpc_uri);

  // the server stops with the last wallet, so this comes last
  EXPECT_OK(monero_wallet_rpc_stop(rpc));
  monero_wallet_free(rpc);
  monero_utils_free(json);
}

// appends the values that follow "key": in the JSON, each ended with a |, up to the next comma, brace or bracket. The values of the
// results are numbers, flags and hashes, none of them has a comma, so the lists of two results can be compared without a parser
static void append_values(char* out, size_t size, const char* json, const char* key) {
  char pattern[80];
  size_t used = bounded_length(out, size);
  size_t length;
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  length = bounded_length(pattern, sizeof(pattern));
  for (const char* p = json; json != NULL && (p = strstr(p, pattern)) != NULL;) {
    p += length;
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']' && used + 2 < size) out[used++] = *p++;
    if (used + 2 < size) out[used++] = '|';
  }
  out[used] = '\0';
}

// the full wallet and the RPC wallet say the same of a key in a result, in the same order. monero-wallet-rpc doesn't give the list of the
// addresses of an outgoing transfer, nor the relay and double spend flags of a tx of an output, so those are not compared
static void expect_same_values(const char* label, const char* full_json, const char* rpc_json, const char* const* keys, size_t count) {
  const size_t size = 1 << 17;
  char* full = (char*) calloc(1, size);
  char* rpc = (char*) calloc(1, size);
  CHECK(full != NULL && rpc != NULL);
  for (size_t i = 0; full != NULL && rpc != NULL && i < count; i++) {
    full[0] = '\0';
    rpc[0] = '\0';
    append_values(full, size, full_json, keys[i]);
    append_values(rpc, size, rpc_json, keys[i]);
    CHECK(full[0] != '\0');
    g_checks++;
    if (strcmp(full, rpc) != 0) {
      g_failures++;
      fprintf(stderr, "FAIL %s, \"%s\" differs:\n  full: %.300s\n  rpc:  %.300s\n", label, keys[i], full, rpc);
    }
  }
  free(full);
  free(rpc);
}

// a full wallet and an RPC wallet with the same keys have the same history, as in testWalletEqualityRpc of monero-java: the same txs
// in the same blocks, the same transfers and outputs in them, and the same balances
static void test_rpc_matches_full(const char* daemon_uri, const char* rpc_uri) {
  const char* path = "monero_c_wallet_integration_same";
  const char* tx_keys[] = {"height", "timestamp", "hash", "fee", "numConfirmations", "unlockTime", "isMinerTx", "isConfirmed", "isLocked", "amount", "accountIndex", "subaddressIndex"};
  const char* output_keys[] = {"height", "hash", "amount", "index", "accountIndex", "subaddressIndex", "stealthPublicKey", "isSpent", "isFrozen"};
  const char* queries[] = {NULL, "{\"minHeight\":95,\"maxHeight\":103}"};
  const char* transfer_queries[] = {NULL, "{\"isIncoming\":true,\"txQuery\":{\"minHeight\":10,\"maxHeight\":20}}"};
  const char* output_queries[] = {NULL, "{\"txQuery\":{\"minHeight\":10,\"maxHeight\":20}}"};
  char connection[256];
  char* json = NULL;
  char* full_json = NULL;
  char* rpc_json = NULL;
  uint64_t full_value = 0;
  uint64_t rpc_value = 1;
  bool trusted = true;
  monero_wallet* full = NULL;
  monero_wallet* rpc = NULL;
  remove_files(path);

  EXPECT_OK(monero_wallet_create_from_keys(path, PASSWORD, NETWORK, g_keys_address, g_keys_view, g_keys_spend, 0, NULL, &full));
  EXPECT_OK(monero_wallet_rpc_create_from_keys(rpc_uri, "", "", "rpc_same", PASSWORD, g_keys_address, g_keys_view, g_keys_spend, 0, NULL, &rpc));
  CHECK(full != NULL && rpc != NULL);
  if (full != NULL && rpc != NULL) {
    snprintf(connection, sizeof(connection), "{\"uri\":\"%s\"}", daemon_uri);
    EXPECT_OK(monero_wallet_set_daemon_connection(full, daemon_uri, "", "", "", &trusted, false));
    EXPECT_OK(monero_wallet_rpc_set_daemon_connection(rpc, connection, true, NULL));
    EXPECT_OK(monero_wallet_sync(full, NULL, NULL, &json));
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_wallet_sync(rpc, NULL, NULL, &json));
    monero_utils_free(json);
    json = NULL;

    EXPECT_OK(monero_wallet_get_height(full, &full_value));
    EXPECT_OK(monero_wallet_get_height(rpc, &rpc_value));
    CHECK(full_value > 0 && full_value == rpc_value);
    EXPECT_OK(monero_wallet_get_balance(full, &full_value));
    EXPECT_OK(monero_wallet_get_balance(rpc, &rpc_value));
    CHECK(full_value > 0 && full_value == rpc_value);
    EXPECT_OK(monero_wallet_get_unlocked_balance(full, &full_value));
    EXPECT_OK(monero_wallet_get_unlocked_balance(rpc, &rpc_value));
    CHECK(full_value > 0 && full_value == rpc_value);

    for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
      EXPECT_OK(monero_wallet_get_txs(full, queries[i], &full_json));
      EXPECT_OK(monero_wallet_get_txs(rpc, queries[i], &rpc_json));
      CHECK(is_blocks(full_json) && is_blocks(rpc_json));
      expect_same_values("get_txs", full_json, rpc_json, tx_keys, sizeof(tx_keys) / sizeof(tx_keys[0]));
      monero_utils_free(full_json);
      monero_utils_free(rpc_json);
      full_json = NULL;
      rpc_json = NULL;
    }
    for (size_t i = 0; i < sizeof(transfer_queries) / sizeof(transfer_queries[0]); i++) {
      EXPECT_OK(monero_wallet_get_transfers(full, transfer_queries[i], &full_json));
      EXPECT_OK(monero_wallet_get_transfers(rpc, transfer_queries[i], &rpc_json));
      CHECK(is_blocks(full_json) && is_blocks(rpc_json));
      expect_same_values("get_transfers", full_json, rpc_json, tx_keys, sizeof(tx_keys) / sizeof(tx_keys[0]));
      monero_utils_free(full_json);
      monero_utils_free(rpc_json);
      full_json = NULL;
      rpc_json = NULL;
    }
    for (size_t i = 0; i < sizeof(output_queries) / sizeof(output_queries[0]); i++) {
      EXPECT_OK(monero_wallet_get_outputs(full, output_queries[i], &full_json));
      EXPECT_OK(monero_wallet_get_outputs(rpc, output_queries[i], &rpc_json));
      CHECK(is_blocks(full_json) && is_blocks(rpc_json));
      expect_same_values("get_outputs", full_json, rpc_json, output_keys, sizeof(output_keys) / sizeof(output_keys[0]));
      monero_utils_free(full_json);
      monero_utils_free(rpc_json);
      full_json = NULL;
      rpc_json = NULL;
    }
  }
  if (full != NULL) monero_wallet_free(full);
  if (rpc != NULL) monero_wallet_free(rpc);
  remove_files(path);
}

int main(void) {
  const char* daemon_uri = getenv("MONERO_C_TEST_DAEMON_URI");
  const char* rpc_uri = getenv("MONERO_C_TEST_WALLET_RPC_URI");
  if ((daemon_uri == NULL || daemon_uri[0] == '\0') && (rpc_uri == NULL || rpc_uri[0] == '\0')) {
    printf("skipped: set MONERO_C_TEST_DAEMON_URI and MONERO_C_TEST_WALLET_RPC_URI\n");
    return EXIT_SKIPPED;
  }

  if (daemon_uri != NULL && daemon_uri[0] != '\0') test_daemon(daemon_uri);
  else printf("skipped: the daemon part needs MONERO_C_TEST_DAEMON_URI\n");
  if (rpc_uri != NULL && rpc_uri[0] != '\0') test_rpc(daemon_uri != NULL ? daemon_uri : "", rpc_uri);
  else printf("skipped: the RPC part needs MONERO_C_TEST_WALLET_RPC_URI\n");

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif

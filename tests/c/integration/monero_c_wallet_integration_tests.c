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

static void on_output_received(void* user_data, const char* output_json) {
  (void)user_data;
  (void)output_json;
  g_received++;
}

static void on_output_spent(void* user_data, const char* output_json) {
  (void)user_data;
  (void)output_json;
  g_spent++;
}

static int has(const char* json, const char* text) {
  return json != NULL && strstr(json, text) != NULL;
}

static int is_list(const char* json) {
  return json != NULL && json[0] == '[';
}

// the length of s, up to max characters
static size_t bounded_length(const char* s, size_t max) {
  size_t n = 0;
  while (n < max && s[n] != '\0') n++;
  return n;
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

  EXPECT_OK(monero_wallet_set_daemon_connection(a, daemon_uri, "", "", "", true, false));
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

  // the history of the wallet
  EXPECT_OK(monero_wallet_get_txs(a, NULL, &json));
  CHECK(is_list(json));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_transfers(a, NULL, &json));
  CHECK(is_list(json));
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_outputs(a, NULL, &json));
  CHECK(is_list(json) && has(json, "\"amount\""));
  monero_utils_free(json);
  json = NULL;

  // wallet c, with the keys of a, receives the outputs, and the listener is told about them
  EXPECT_OK(monero_wallet_get_private_view_key(a, &json));
  CHECK(json != NULL && snprintf(view_key, sizeof(view_key), "%s", json) > 0);
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_wallet_get_private_spend_key(a, &json));
  CHECK(json != NULL && snprintf(spend_key, sizeof(spend_key), "%s", json) > 0);
  monero_utils_free(json);
  json = NULL;

  monero_wallet* c = NULL;
  EXPECT_OK(monero_wallet_listener_create(&callbacks, &listener));
  EXPECT_OK(monero_wallet_create_from_keys(path_c, PASSWORD, NETWORK, address_a, view_key, spend_key, 0, NULL, &c));
  CHECK(c != NULL);
  if (c != NULL) {
    EXPECT_OK(monero_wallet_set_daemon_connection(c, daemon_uri, "", "", "", true, false));
    g_received = 0;
    EXPECT_OK(monero_wallet_sync(c, NULL, listener, &json));
    monero_utils_free(json);
    json = NULL;
    CHECK(g_received > 0);
  }

  // create a tx without relaying, relay it from its JSON and mine it
  const char* config = "{\"accountIndex\":0,\"destinations\":[{\"address\":\"" ADDRESS "\",\"amount\":1000000000}]}";
  char* tx_hash = NULL;
  EXPECT_OK(monero_wallet_create_tx(a, config, &json));
  CHECK(json != NULL && has(json, "\"metadata\""));
  EXPECT_OK(monero_wallet_relay_tx_json(a, json, &tx_hash));
  CHECK(tx_hash != NULL);
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
  CHECK(has(json, "\"fee\":"));
  monero_utils_free(json);
  json = NULL;
  EXPECT_ERR(monero_wallet_sweep_output(a, "{\"keyImage\":\"00\",\"destinations\":[{\"address\":\"" ADDRESS "\"}],\"relay\":false}", &json));
  CHECK(json == NULL);
  EXPECT_OK(monero_wallet_sweep_unlocked(a, "{\"accountIndex\":0,\"destinations\":[{\"address\":\"" ADDRESS "\"}],\"relay\":false}", &json));
  CHECK(is_list(json) && has(json, "\"fee\":"));
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
    EXPECT_OK(monero_wallet_set_daemon_connection(v, daemon_uri, "", "", "", true, false));
    EXPECT_OK(monero_wallet_sync(v, NULL, NULL, &json));
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_wallet_create_txs(v, config, &json));
    CHECK(json != NULL && has(json, "\"unsignedTxHex\""));
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
  CHECK(is_list(json));
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

  // the server stops with the last wallet, so this comes last
  EXPECT_OK(monero_wallet_rpc_stop(rpc));
  monero_wallet_free(rpc);
  monero_utils_free(json);
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

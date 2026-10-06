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

// needs a regtest monerod, for example the one in docker-compose.yml in this folder:
//
//   docker compose -f tests/integration/docker-compose.yml up -d
//   MONERO_C_TEST_DAEMON_URI=http://127.0.0.1:18081 ./build/tests/integration/monero_c_daemon_integration_tests
//
// the tests mine blocks, so never point them at a real node.
// run the node with --fixed-difficulty=1, as the compose file does. Otherwise the difficulty grows with every block.
// without MONERO_C_TEST_DAEMON_URI, or on a node that isn't regtest, the program exits with 77 (skipped)

#define EXIT_SKIPPED 77

#define ZERO_HASH "0000000000000000000000000000000000000000000000000000000000000000"

static const char* ADDRESS = "46BeWrHpwXmHDpDEUmZBWZfoQpdc6HaERCNmx1pEYL2rAcuwufPN9rXHHtyUA4QVy66qeFQkn6sfK8aHYjA3jk3o1Bv16em";
static const char* const ZERO_HASHES[] = {ZERO_HASH};

#if defined(_WIN32)

int main(void) {
  printf("skipped: the live daemon tests run on POSIX systems only\n");
  return EXIT_SKIPPED;
}

#else

#include <pthread.h>
#include <unistd.h>

static volatile int g_callbacks = 0;
static char g_header[4096];

static void on_block_header(void* user_data, const char* header_json) {
  (void)user_data;
  snprintf(g_header, sizeof(g_header), "%s", header_json);
  g_callbacks++;
}

// true if the JSON object has the key at its top level (monero-cpp writes keys compactly)
static int has_key(const char* json, const char* key) {
  char pattern[128];
  if (json == NULL) return 0;
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  return strstr(json, pattern) != NULL;
}

static int is_array(const char* json) {
  return json != NULL && json[0] == '[';
}

static uint64_t current_height(monero_daemon* daemon) {
  uint64_t height = 0;
  EXPECT_OK(monero_daemon_get_height(daemon, &height));
  return height;
}

static int is_regtest(monero_daemon* daemon) {
  char* json = NULL;
  int regtest = 0;
  if (monero_daemon_get_info(daemon, &json) == MONERO_OK && json != NULL) {
    regtest = strstr(json, "\"isRegtest\":true") != NULL;
  }
  monero_utils_free(json);
  return regtest;
}

// ---------------------------------- INFO ------------------------------------

static void test_info(monero_daemon* daemon) {
  char* json = NULL;

  EXPECT_OK(monero_daemon_get_version(daemon, &json));
  CHECK(has_key(json, "number"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_info(daemon, &json));
  CHECK(has_key(json, "height"));
  CHECK(has_key(json, "isRegtest"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_sync_info(daemon, &json));
  CHECK(has_key(json, "targetHeight"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_network_stats(daemon, &json));
  CHECK(has_key(json, "totalBytesIn"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_hard_fork_info(daemon, &json));
  CHECK(has_key(json, "version"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_alt_chains(daemon, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_alt_block_hashes(daemon, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  bool trusted = false;
  EXPECT_OK(monero_daemon_is_trusted(daemon, &trusted));

  char* hash = NULL;
  EXPECT_OK(monero_daemon_get_block_hash(daemon, 0, &hash));
  CHECK(hash != NULL && strlen(hash) == 64);
  monero_utils_free(hash);
}

// -------------------------------- MINING ------------------------------------

static void test_mining(monero_daemon* daemon) {
  char* json = NULL;

  EXPECT_OK(monero_daemon_get_block_template(daemon, ADDRESS, NULL, &json));
  CHECK(json != NULL);
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_miner_data(daemon, &json));
  CHECK(json != NULL);
  monero_utils_free(json);
  json = NULL;

  char* pow = NULL;
  EXPECT_OK(monero_daemon_calculate_pow(daemon, 16, 1, "00", ZERO_HASH, &pow));
  monero_utils_free(pow);

  EXPECT_OK(monero_daemon_get_mining_status(daemon, &json));
  CHECK(has_key(json, "isActive"));
  monero_utils_free(json);
  json = NULL;

  // the hash rate can only be shown while mining
  EXPECT_ERR(monero_daemon_set_log_hash_rate(daemon, true));

  // stop soon, so the node doesn't keep mining while the other tests run
  uint64_t threads = 1;
  EXPECT_OK(monero_daemon_start_mining(daemon, ADDRESS, &threads, NULL, NULL));
  EXPECT_OK(monero_daemon_set_log_hash_rate(daemon, true));
  EXPECT_OK(monero_daemon_set_log_hash_rate(daemon, false));
  // the daemon takes a few seconds to start its miner thread, and stop_mining() waits for it
  sleep(3);
  EXPECT_OK(monero_daemon_stop_mining(daemon));

  EXPECT_ERR(monero_daemon_submit_block(daemon, "00"));
  const char* const blobs[] = {"00"};
  EXPECT_ERR(monero_daemon_submit_blocks(daemon, blobs, 1));
  EXPECT_ERR(monero_daemon_add_auxiliary_pow(daemon, "00", "[]", &json));
  monero_utils_free(json);
  json = NULL;
}

// --------------------------------- BLOCKS -----------------------------------

static void test_blocks(monero_daemon* daemon) {
  uint64_t before = current_height(daemon);
  char* json = NULL;
  EXPECT_OK(monero_daemon_generate_blocks(daemon, ADDRESS, 3, NULL, NULL, &json));
  CHECK(has_key(json, "blockHashes"));
  monero_utils_free(json);
  json = NULL;

  uint64_t top = current_height(daemon);
  CHECK(top == before + 3);
  if (top == 0) return;

  char* tip = NULL;
  EXPECT_OK(monero_daemon_get_block_hash(daemon, top - 1, &tip));
  char* genesis = NULL;
  EXPECT_OK(monero_daemon_get_block_hash(daemon, 0, &genesis));
  const char* const history[] = {tip, genesis};

  EXPECT_OK(monero_daemon_get_last_block_header(daemon, &json));
  CHECK(has_key(json, "height"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_block_header_by_height(daemon, top - 1, &json));
  CHECK(has_key(json, "hash"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_block_header_by_hash(daemon, tip, &json));
  CHECK(has_key(json, "hash"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_block_headers_by_range(daemon, 0, top - 1, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_block_by_height(daemon, top - 1, &json));
  CHECK(has_key(json, "hash"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_block_by_hash(daemon, tip, &json));
  CHECK(has_key(json, "hash"));
  monero_utils_free(json);
  json = NULL;

  uint64_t heights[] = {0, top - 1};
  EXPECT_OK(monero_daemon_get_blocks_by_height(daemon, heights, 2, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  uint64_t start = 0;
  uint64_t end = top - 1;
  EXPECT_OK(monero_daemon_get_blocks_by_range(daemon, &start, &end, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_blocks_by_range_chunked(daemon, &start, &end, NULL, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_blocks_by_hash(daemon, history, 2, 0, false, 0, &json));
  CHECK(json != NULL);
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_block_hashes(daemon, history, 2, &json));
  CHECK(json != NULL);
  monero_utils_free(json);
  json = NULL;

  monero_utils_free(tip);
  monero_utils_free(genesis);
}

// ------------------------------ TRANSACTIONS --------------------------------

static void test_transactions(monero_daemon* daemon) {
  char* json = NULL;

  EXPECT_OK(monero_daemon_get_tx(daemon, ZERO_HASH, false, &json));
  CHECK(json == NULL);  // unknown transaction: no result, no error

  EXPECT_OK(monero_daemon_get_txs(daemon, ZERO_HASHES, 1, false, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  char* hex = NULL;
  EXPECT_OK(monero_daemon_get_tx_hex(daemon, ZERO_HASH, false, &hex));
  CHECK(hex == NULL);

  EXPECT_OK(monero_daemon_get_tx_hexes(daemon, ZERO_HASHES, 1, false, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  uint64_t top = current_height(daemon);
  if (top >= 2) {
    EXPECT_OK(monero_daemon_get_miner_tx_sum(daemon, 0, 2, &json));
    CHECK(json != NULL);
    monero_utils_free(json);
    json = NULL;
  }

  EXPECT_OK(monero_daemon_get_fee_estimate(daemon, 0, &json));
  CHECK(has_key(json, "fee"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_submit_tx_hex(daemon, "00", false, &json));
  CHECK(has_key(json, "isGood"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_ERR(monero_daemon_relay_tx_by_hash(daemon, ZERO_HASH));
  EXPECT_ERR(monero_daemon_relay_txs_by_hash(daemon, ZERO_HASHES, 1));
}

// ------------------------------- TX POOL ------------------------------------

static void test_tx_pool(monero_daemon* daemon) {
  char* json = NULL;

  EXPECT_OK(monero_daemon_get_tx_pool(daemon, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_tx_pool_hashes(daemon, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  // monero-cpp's RPC client doesn't implement this call
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool_backlog(daemon, &json), "monero_daemon::get_tx_pool_backlog(): not supported");

  EXPECT_OK(monero_daemon_get_tx_pool_stats(daemon, &json));
  CHECK(has_key(json, "numTxs"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_flush_tx_pool(daemon));
  EXPECT_OK(monero_daemon_flush_tx_pool_hashes(daemon, ZERO_HASHES, 1));
  EXPECT_OK(monero_daemon_flush_tx_pool_hash(daemon, ZERO_HASH));
}

// ------------------------- KEY IMAGES AND OUTPUTS ---------------------------

static void test_key_images_and_outputs(monero_daemon* daemon) {
  monero_key_image_spent_status status = MONERO_KEY_IMAGE_CONFIRMED;
  EXPECT_OK(monero_daemon_get_key_image_spent_status(daemon, ZERO_HASH, &status));
  CHECK(status == MONERO_KEY_IMAGE_NOT_SPENT);

  monero_key_image_spent_status* statuses = NULL;
  size_t count = 0;
  EXPECT_OK(monero_daemon_get_key_image_spent_statuses(daemon, ZERO_HASHES, 1, &statuses, &count));
  CHECK(count == 1 && statuses != NULL && statuses[0] == MONERO_KEY_IMAGE_NOT_SPENT);
  monero_utils_free(statuses);

  uint64_t* indices = NULL;
  EXPECT_ERR(monero_daemon_get_output_indices(daemon, ZERO_HASH, &indices, &count));
  monero_utils_free(indices);

  char* json = NULL;
  EXPECT_ERR(monero_daemon_get_outputs(daemon, "[]", &json));
  monero_utils_free(json);
  json = NULL;

  uint64_t amounts[] = {0};
  EXPECT_OK(monero_daemon_get_output_histogram(daemon, amounts, 1, NULL, NULL, NULL, NULL, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_output_distribution(daemon, amounts, 1, NULL, NULL, NULL, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
}

// ----------------------------- BANDWIDTH, PEERS -----------------------------

static void test_bandwidth_and_peers(monero_daemon* daemon) {
  int32_t limit = 0;
  EXPECT_OK(monero_daemon_get_download_limit(daemon, &limit));
  EXPECT_OK(monero_daemon_set_download_limit(daemon, 1024, &limit));
  EXPECT_OK(monero_daemon_reset_download_limit(daemon, &limit));
  EXPECT_OK(monero_daemon_get_upload_limit(daemon, &limit));
  EXPECT_OK(monero_daemon_set_upload_limit(daemon, 1024, &limit));
  EXPECT_OK(monero_daemon_reset_upload_limit(daemon, &limit));

  char* json = NULL;
  EXPECT_OK(monero_daemon_get_peers(daemon, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_known_peers(daemon, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_get_public_peers(daemon, false, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_set_outgoing_peer_limit(daemon, 8));
  EXPECT_OK(monero_daemon_set_incoming_peer_limit(daemon, 8));

  EXPECT_OK(monero_daemon_get_peer_bans(daemon, &json));
  CHECK(is_array(json));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_set_peer_bans(daemon, "[]"));
  EXPECT_OK(monero_daemon_set_peer_ban(daemon, "{\"host\":\"1.2.3.4\",\"ip\":0,\"seconds\":60,\"isBanned\":true}"));
  EXPECT_OK(monero_daemon_get_peer_ban(daemon, "1.2.3.4", &json));
  CHECK(json != NULL && strstr(json, "1.2.3.4") != NULL);
  monero_utils_free(json);
  json = NULL;

  // a peer that isn't banned still gets a ban object, with isBanned false
  EXPECT_OK(monero_daemon_get_peer_ban(daemon, "10.0.0.1", &json));
  CHECK(json != NULL && strstr(json, "\"isBanned\":false") != NULL);
  monero_utils_free(json);
  json = NULL;
}

// ------------------------------- LISTENERS ----------------------------------

static void test_listener_receives_blocks(monero_daemon* daemon) {
  monero_daemon_listener_callbacks callbacks = {NULL, on_block_header};
  monero_daemon_listener* listener = NULL;
  EXPECT_OK(monero_daemon_listener_create(&callbacks, &listener));
  EXPECT_OK(monero_daemon_add_listener(daemon, listener));

  // the first poll only records the tip, so earlier blocks are not announced
  sleep(2);
  g_callbacks = 0;

  char* json = NULL;
  EXPECT_OK(monero_daemon_generate_blocks(daemon, ADDRESS, 2, NULL, NULL, &json));
  monero_utils_free(json);

  // monero-cpp polls every 10 seconds
  for (int i = 0; i < 30 && g_callbacks == 0; i++) sleep(1);
  CHECK(g_callbacks > 0);
  CHECK(has_key(g_header, "height"));

  monero_daemon_listener** listeners = NULL;
  size_t count = 0;
  EXPECT_OK(monero_daemon_get_listeners(daemon, &listeners, &count));
  CHECK(count == 1 && listeners != NULL && listeners[0] == listener);
  monero_utils_free(listeners);

  EXPECT_OK(monero_daemon_remove_listener(daemon, listener));
  monero_daemon_listener_free(listener);

  // monero_daemon_remove_listeners() removes every registered listener at once
  monero_daemon_listener* first = NULL;
  monero_daemon_listener* second = NULL;
  EXPECT_OK(monero_daemon_listener_create(&callbacks, &first));
  EXPECT_OK(monero_daemon_listener_create(&callbacks, &second));
  EXPECT_OK(monero_daemon_add_listener(daemon, first));
  EXPECT_OK(monero_daemon_add_listener(daemon, second));
  EXPECT_OK(monero_daemon_remove_listeners(daemon));

  EXPECT_OK(monero_daemon_get_listeners(daemon, &listeners, &count));
  CHECK(count == 0);
  monero_utils_free(listeners);

  monero_daemon_listener_free(first);
  monero_daemon_listener_free(second);
}

struct wait_arguments {
  monero_daemon* daemon;
  char* header;
  int ok;
};

static void* wait_for_block(void* arg) {
  struct wait_arguments* args = (struct wait_arguments*)arg;
  args->ok = monero_daemon_wait_for_next_block_header(args->daemon, &args->header) == MONERO_OK;
  return NULL;
}

// the waiting call doesn't block other calls on the handle. The block is mined from this thread meanwhile
static void test_wait_for_next_block_header(monero_daemon* daemon) {
  struct wait_arguments args = {daemon, NULL, 0};
  pthread_t waiter;
  CHECK(pthread_create(&waiter, NULL, wait_for_block, &args) == 0);

  sleep(2);  // give the waiter time to register its listener
  char* json = NULL;
  monero_result generated = monero_daemon_generate_blocks(daemon, ADDRESS, 1, NULL, NULL, &json);
  EXPECT_OK(generated);
  monero_utils_free(json);

  if (generated != MONERO_OK) {
    pthread_detach(waiter);  // the waiter might never return, so don't join it
    return;
  }
  CHECK(pthread_join(waiter, NULL) == 0);
  CHECK(args.ok);
  CHECK(has_key(args.header, "height"));
  monero_utils_free(args.header);
}

// ---------------------------------- ADMIN -----------------------------------

static void test_admin(monero_daemon* daemon) {
  EXPECT_OK(monero_daemon_set_log_level(daemon, 1));
  char* categories = NULL;
  EXPECT_OK(monero_daemon_set_log_categories(daemon, "", &categories));
  monero_utils_free(categories);

  EXPECT_OK(monero_daemon_set_bootstrap_daemon(daemon, "", "", "", ""));
  EXPECT_OK(monero_daemon_remove_bootstrap_daemon(daemon));

  char* json = NULL;
  EXPECT_OK(monero_daemon_prune_blockchain(daemon, false, &json));
  CHECK(json != NULL);
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_daemon_save_blockchain(daemon));
  EXPECT_OK(monero_daemon_flush_cache(daemon, false));

  uint64_t before = current_height(daemon);
  uint64_t after = 0;
  EXPECT_OK(monero_daemon_pop_blocks(daemon, 1, &after));
  CHECK(after + 1 == before);
}

// ------------------------------- CONNECTION ---------------------------------

static void test_rpc_connection(const char* uri) {
  char config[256];
  char* json = NULL;
  uint8_t* data = NULL;
  size_t len = 0;
  bool changed = false;
  monero_optional_bool status = MONERO_OPTIONAL_BOOL_UNSET;
  monero_rpc_connection* connection = NULL;
  snprintf(config, sizeof(config), "{\"uri\":\"%s\",\"timeoutMs\":30000}", uri);
  EXPECT_OK(monero_rpc_connection_create(config, &connection));
  if (connection == NULL) return;

  EXPECT_OK(monero_rpc_connection_check_connection(connection, NULL, &changed));
  CHECK(changed);
  EXPECT_OK(monero_rpc_connection_is_online(connection, &status));
  CHECK(status == MONERO_OPTIONAL_BOOL_TRUE);
  EXPECT_OK(monero_rpc_connection_is_authenticated(connection, &status));
  CHECK(status == MONERO_OPTIONAL_BOOL_TRUE);
  EXPECT_OK(monero_rpc_connection_is_connected(connection, &status));
  CHECK(status == MONERO_OPTIONAL_BOOL_TRUE);
  EXPECT_OK(monero_rpc_connection_serialize(connection, &json));
  CHECK(has_key(json, "responseTime"));
  monero_utils_free(json);
  json = NULL;

  // numbers and bools of the response are not strings
  EXPECT_OK(monero_rpc_connection_send_json_request(connection, "get_block_count", NULL, NULL, &json));
  CHECK(json != NULL && strstr(json, "\"count\":") != NULL && strstr(json, "\"count\":\"") == NULL);
  CHECK(json != NULL && strstr(json, "\"status\":\"OK\"") != NULL);
  monero_utils_free(json);
  json = NULL;
  EXPECT_OK(monero_rpc_connection_send_json_request(connection, "get_block_header_by_height", "{\"height\":0}", NULL, &json));
  CHECK(has_key(json, "block_header"));
  CHECK(json != NULL && strstr(json, "\"orphan_status\":false") != NULL);
  monero_utils_free(json);
  json = NULL;
  EXPECT_ERR(monero_rpc_connection_send_json_request(connection, "no_such_method", NULL, NULL, &json));
  CHECK(json == NULL);

  EXPECT_OK(monero_rpc_connection_send_path_request(connection, "get_height", NULL, NULL, &json));
  CHECK(has_key(json, "height"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_rpc_connection_send_binary_request(connection, "get_blocks_by_height.bin", "{\"heights\":[0]}", NULL, &data, &len));
  CHECK(data != NULL && len > 0);
  if (data != NULL) {
    EXPECT_OK(monero_utils_binary_blocks_to_json(data, len, &json));
    CHECK(has_key(json, "blocks"));
    monero_utils_free(json);
    json = NULL;
  }
  monero_utils_free(data);

  // a daemon from the connection uses it, and the status is shared
  monero_daemon* daemon = NULL;
  EXPECT_OK(monero_daemon_connect_with(connection, &daemon));
  monero_rpc_connection_free(connection);
  if (daemon == NULL) return;
  CHECK(current_height(daemon) > 0);
  EXPECT_OK(monero_daemon_get_rpc_connection(daemon, &json));
  CHECK(json != NULL && strstr(json, "\"isOnline\":true") != NULL);
  monero_utils_free(json);
  EXPECT_OK(monero_daemon_set_poll_period(daemon, 500));
  monero_daemon_free(daemon);
}

// ---------------------------------- MAIN ------------------------------------

int main(void) {
  const char* uri = getenv("MONERO_C_TEST_DAEMON_URI");
  if (uri == NULL || uri[0] == '\0') {
    printf("skipped: set MONERO_C_TEST_DAEMON_URI to a regtest monerod\n");
    return EXIT_SKIPPED;
  }

  monero_daemon* daemon = NULL;
  // 30 seconds per request, so an unresponsive node fails the test instead of hanging it
  if (monero_daemon_connect(uri, "", "", "", 30000, &daemon) != MONERO_OK || daemon == NULL) {
    fprintf(stderr, "cannot create a handle for %s: %s\n", uri, monero_last_error());
    return EXIT_FAILURE;
  }
  if (!is_regtest(daemon)) {
    printf("skipped: %s is not a regtest node; these tests generate blocks\n", uri);
    monero_daemon_free(daemon);
    return EXIT_SKIPPED;
  }

  test_info(daemon);
  test_mining(daemon);
  test_blocks(daemon);
  test_transactions(daemon);
  test_tx_pool(daemon);
  test_key_images_and_outputs(daemon);
  test_bandwidth_and_peers(daemon);
  test_listener_receives_blocks(daemon);
  test_wait_for_next_block_header(daemon);
  test_admin(daemon);
  test_rpc_connection(uri);
  // monero_daemon_stop() isn't called here, since it would shut the node down for the next run

  monero_daemon_free(daemon);
  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif

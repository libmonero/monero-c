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

#if !defined(_WIN32)
#include <pthread.h>
#endif

// unit tests for the daemon binding: argument checks, the mapping of monero-cpp errors,
// listener handles and the per-thread error. None of them needs a running daemon: each call
// either fails before any request is sent, or targets a closed port

static const char* UNREACHABLE_URI = "http://127.0.0.1:1";
static const char* const STRINGS[] = {"00"};
static const uint64_t NUMBERS[] = {0};

// connecting succeeds even though nothing listens on the port: the failure shows up
// in the calls that query the daemon
static monero_daemon* create_unreachable_daemon(void) {
  monero_daemon* daemon = NULL;
  if (monero_daemon_connect(UNREACHABLE_URI, "", "", "", 0, &daemon) != MONERO_OK) return NULL;
  return daemon;
}

// ------------------------------- ARGUMENTS ----------------------------------

static void test_connect_validates_arguments(void) {
  monero_daemon* daemon = NULL;
  EXPECT_ERR_MSG(monero_daemon_connect(NULL, "", "", "", 0, &daemon), "uri must not be empty");
  CHECK(daemon == NULL);
  EXPECT_ERR_MSG(monero_daemon_connect("", "", "", "", 0, &daemon), "uri must not be empty");
  CHECK(daemon == NULL);
  EXPECT_ERR_MSG(monero_daemon_connect(UNREACHABLE_URI, "", "", "", 0, NULL), "out_daemon must not be null");
  EXPECT_OK(monero_daemon_connect(UNREACHABLE_URI, "", "", "", 0, &daemon));
  CHECK(daemon != NULL);
  monero_daemon_free(daemon);
}

static void test_free_null_is_noop(void) {
  monero_daemon_free(NULL);
  monero_daemon_listener_free(NULL);
}

// every function that takes a daemon must reject NULL with the same message
static void test_every_function_rejects_null_daemon(void) {
  char* json = NULL;
  char* text = NULL;
  bool flag = false;
  uint64_t number = 0;
  int32_t limit = 0;
  size_t count = 0;
  monero_daemon_listener* listener = NULL;
  monero_daemon_listener** listeners = NULL;
  monero_key_image_spent_status status = MONERO_KEY_IMAGE_NOT_SPENT;
  monero_key_image_spent_status* statuses = NULL;
  uint64_t* indices = NULL;

  // listeners
  EXPECT_ERR_MSG(monero_daemon_add_listener(NULL, listener), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_remove_listener(NULL, listener), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_listeners(NULL, &listeners, &count), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_remove_listeners(NULL), "daemon must not be null");

  // general
  EXPECT_ERR_MSG(monero_daemon_get_version(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_is_trusted(NULL, &flag), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_height(NULL, &number), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_hash(NULL, 0, &text), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_info(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_sync_info(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_network_stats(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_hard_fork_info(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_alt_chains(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_alt_block_hashes(NULL, &json), "daemon must not be null");

  // mining
  EXPECT_ERR_MSG(monero_daemon_get_block_template(NULL, "addr", NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_miner_data(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_calculate_pow(NULL, 16, 1, "00", "00", &text), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_add_auxiliary_pow(NULL, "00", "[]", &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_mining_status(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_start_mining(NULL, "addr", NULL, NULL, NULL), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_stop_mining(NULL), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_generate_blocks(NULL, "addr", 1, NULL, NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_submit_block(NULL, "00"), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_submit_blocks(NULL, STRINGS, 1), "daemon must not be null");

  // blocks
  EXPECT_ERR_MSG(monero_daemon_get_last_block_header(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_header_by_hash(NULL, "00", &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_header_by_height(NULL, 0, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_headers_by_range(NULL, 0, 0, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_by_hash(NULL, "00", &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_blocks_by_hash(NULL, STRINGS, 1, 0, false, 0, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_by_height(NULL, 0, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_blocks_by_height(NULL, NUMBERS, 1, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_blocks_by_range(NULL, NULL, NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_blocks_by_range_chunked(NULL, NULL, NULL, NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_hashes(NULL, STRINGS, 1, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_wait_for_next_block_header(NULL, &json), "daemon must not be null");

  // transactions
  EXPECT_ERR_MSG(monero_daemon_get_tx(NULL, "00", false, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_txs(NULL, STRINGS, 1, false, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_hex(NULL, "00", false, &text), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_hexes(NULL, STRINGS, 1, false, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_miner_tx_sum(NULL, 0, 1, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_fee_estimate(NULL, 0, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_submit_tx_hex(NULL, "00", false, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_relay_tx_by_hash(NULL, "00"), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_relay_txs_by_hash(NULL, STRINGS, 1), "daemon must not be null");

  // transaction pool
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool_hashes(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool_backlog(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool_stats(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_flush_tx_pool(NULL), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_flush_tx_pool_hashes(NULL, STRINGS, 1), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_flush_tx_pool_hash(NULL, "00"), "daemon must not be null");

  // key images and outputs
  EXPECT_ERR_MSG(monero_daemon_get_key_image_spent_status(NULL, "00", &status), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_key_image_spent_statuses(NULL, STRINGS, 1, &statuses, &count), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_output_indices(NULL, "00", &indices, &count), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_outputs(NULL, "[]", &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_output_histogram(NULL, NUMBERS, 1, NULL, NULL, NULL, NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_output_distribution(NULL, NUMBERS, 1, NULL, NULL, NULL, &json), "daemon must not be null");

  // bandwidth
  EXPECT_ERR_MSG(monero_daemon_get_download_limit(NULL, &limit), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_download_limit(NULL, 1, &limit), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_reset_download_limit(NULL, &limit), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_upload_limit(NULL, &limit), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_upload_limit(NULL, 1, &limit), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_reset_upload_limit(NULL, &limit), "daemon must not be null");

  // peers
  EXPECT_ERR_MSG(monero_daemon_get_peers(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_known_peers(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_public_peers(NULL, false, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_outgoing_peer_limit(NULL, 1), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_incoming_peer_limit(NULL, 1), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_peer_bans(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_peer_bans(NULL, "[]"), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_peer_ban(NULL, "{}"), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_peer_ban(NULL, "1.2.3.4", &json), "daemon must not be null");

  // admin
  EXPECT_ERR_MSG(monero_daemon_prune_blockchain(NULL, false, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_save_blockchain(NULL), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_pop_blocks(NULL, 1, &number), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_flush_cache(NULL, false), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_bootstrap_daemon(NULL, "", "", "", ""), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_remove_bootstrap_daemon(NULL), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_log_hash_rate(NULL, false), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_log_level(NULL, 1), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_log_categories(NULL, "", &text), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_check_for_update(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_download_update(NULL, "", &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_stop(NULL), "daemon must not be null");
}

// with a valid daemon, every output pointer is required
static void test_null_outputs_are_rejected(monero_daemon* daemon) {
  size_t count = 0;
  monero_daemon_listener** listeners = NULL;
  monero_key_image_spent_status* statuses = NULL;
  uint64_t* indices = NULL;

  EXPECT_ERR_MSG(monero_daemon_get_listeners(daemon, NULL, &count), "out_listeners must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_listeners(daemon, &listeners, NULL), "out_count must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_version(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_is_trusted(daemon, NULL), "out_trusted must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_height(daemon, NULL), "out_height must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_hash(daemon, 0, NULL), "out_hash must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_info(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_sync_info(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_network_stats(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_hard_fork_info(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_alt_chains(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_alt_block_hashes(daemon, NULL), "out_json must not be null");

  EXPECT_ERR_MSG(monero_daemon_get_block_template(daemon, "addr", NULL, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_miner_data(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_calculate_pow(daemon, 16, 1, "00", "00", NULL), "out_pow_hash must not be null");
  EXPECT_ERR_MSG(monero_daemon_add_auxiliary_pow(daemon, "00", "[]", NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_mining_status(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_generate_blocks(daemon, "addr", 1, NULL, NULL, NULL), "out_json must not be null");

  EXPECT_ERR_MSG(monero_daemon_get_last_block_header(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_header_by_hash(daemon, "00", NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_header_by_height(daemon, 0, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_headers_by_range(daemon, 0, 0, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_by_hash(daemon, "00", NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_blocks_by_hash(daemon, STRINGS, 1, 0, false, 0, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_by_height(daemon, 0, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_blocks_by_height(daemon, NUMBERS, 1, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_blocks_by_range(daemon, NULL, NULL, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_blocks_by_range_chunked(daemon, NULL, NULL, NULL, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_hashes(daemon, STRINGS, 1, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_wait_for_next_block_header(daemon, NULL), "out_json must not be null");

  EXPECT_ERR_MSG(monero_daemon_get_tx(daemon, "00", false, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_txs(daemon, STRINGS, 1, false, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_hex(daemon, "00", false, NULL), "out_tx_hex must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_hexes(daemon, STRINGS, 1, false, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_miner_tx_sum(daemon, 0, 1, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_fee_estimate(daemon, 0, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_submit_tx_hex(daemon, "00", false, NULL), "out_json must not be null");

  EXPECT_ERR_MSG(monero_daemon_get_tx_pool(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool_hashes(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool_backlog(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool_stats(daemon, NULL), "out_json must not be null");

  EXPECT_ERR_MSG(monero_daemon_get_key_image_spent_status(daemon, "00", NULL), "out_status must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_key_image_spent_statuses(daemon, STRINGS, 1, NULL, &count), "out_statuses must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_key_image_spent_statuses(daemon, STRINGS, 1, &statuses, NULL), "out_count must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_output_indices(daemon, "00", NULL, &count), "out_indices must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_output_indices(daemon, "00", &indices, NULL), "out_count must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_outputs(daemon, "[]", NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_output_histogram(daemon, NUMBERS, 1, NULL, NULL, NULL, NULL, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_output_distribution(daemon, NUMBERS, 1, NULL, NULL, NULL, NULL), "out_json must not be null");

  EXPECT_ERR_MSG(monero_daemon_get_download_limit(daemon, NULL), "out_limit must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_download_limit(daemon, 1, NULL), "out_limit must not be null");
  EXPECT_ERR_MSG(monero_daemon_reset_download_limit(daemon, NULL), "out_limit must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_upload_limit(daemon, NULL), "out_limit must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_upload_limit(daemon, 1, NULL), "out_limit must not be null");
  EXPECT_ERR_MSG(monero_daemon_reset_upload_limit(daemon, NULL), "out_limit must not be null");

  EXPECT_ERR_MSG(monero_daemon_get_peers(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_known_peers(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_public_peers(daemon, false, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_peer_bans(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_peer_ban(daemon, "1.2.3.4", NULL), "out_json must not be null");

  EXPECT_ERR_MSG(monero_daemon_prune_blockchain(daemon, false, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_pop_blocks(daemon, 1, NULL), "out_height must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_log_categories(daemon, "", NULL), "out_categories must not be null");
  EXPECT_ERR_MSG(monero_daemon_check_for_update(daemon, NULL), "out_json must not be null");
  EXPECT_ERR_MSG(monero_daemon_download_update(daemon, "", NULL), "out_json must not be null");
}

// -------------------------------- ERRORS ------------------------------------

// on error, string and array outputs are reset and scalar outputs keep their value
static void test_outputs_are_reset_on_error(monero_daemon* daemon) {
  char* json = (char*)&g_checks;  // any non-NULL value: the call has to reset it
  EXPECT_ERR(monero_daemon_get_tx_pool_backlog(daemon, &json));
  CHECK(json == NULL);

  json = (char*)&g_checks;
  EXPECT_ERR(monero_daemon_get_info(daemon, &json));  // the daemon can't be reached
  CHECK(json == NULL);

  uint64_t height = 42;
  EXPECT_ERR(monero_daemon_get_height(daemon, &height));
  CHECK(height == 42);
}

// a call that fails on a NULL argument resets its outputs too, whichever output it can reach
static void test_null_arguments_reset_outputs(void) {
  char* json = POISON_PTR;
  monero_daemon* daemon = POISON_PTR;
  monero_daemon_listener* listener = POISON_PTR;
  monero_daemon_listener** listeners = POISON_PTR;
  monero_key_image_spent_status* statuses = POISON_PTR;
  uint64_t* indices = POISON_PTR;
  size_t count = 7;

  EXPECT_ERR(monero_daemon_get_info(NULL, &json));
  CHECK(json == NULL);
  EXPECT_ERR(monero_daemon_connect_with(NULL, &daemon));
  CHECK(daemon == NULL);
  EXPECT_ERR(monero_daemon_listener_create(NULL, &listener));
  CHECK(listener == NULL);

  EXPECT_ERR(monero_daemon_get_listeners(NULL, &listeners, &count));
  CHECK(listeners == NULL && count == 0);

  statuses = POISON_PTR;
  count = 7;
  EXPECT_ERR(monero_daemon_get_key_image_spent_statuses(NULL, STRINGS, 1, &statuses, &count));
  CHECK(statuses == NULL && count == 0);

  // a NULL output next to a valid one: the valid one is still reset
  indices = POISON_PTR;
  EXPECT_ERR(monero_daemon_get_output_indices(NULL, "00", &indices, NULL));
  CHECK(indices == NULL);
  count = 7;
  EXPECT_ERR(monero_daemon_get_output_indices(NULL, "00", NULL, &count));
  CHECK(count == 0);
}

// the binding's own checks and the messages monero-cpp throws both reach the caller
static void test_errors_from_the_binding_and_monero_cpp(monero_daemon* daemon) {
  char* json = NULL;
  monero_key_image_spent_status* statuses = NULL;
  size_t count = 0;

  // monero-cpp's base class throws before any request, because the RPC client doesn't implement this call
  EXPECT_ERR_MSG(monero_daemon_get_tx_pool_backlog(daemon, &json), "monero_daemon::get_tx_pool_backlog(): not supported");
  CHECK(json == NULL);

  // malformed JSON, or JSON that isn't an array, is rejected before the daemon is involved
  EXPECT_ERR_MSG(monero_daemon_set_peer_bans(daemon, "not json"), "expected a JSON array");
  EXPECT_ERR_MSG(monero_daemon_set_peer_bans(daemon, "{}"), "expected a JSON array");
  EXPECT_ERR_MSG(monero_daemon_get_outputs(daemon, "{}", &json), "expected a JSON array");
  CHECK(json == NULL);

  // a NULL element or a NULL array with a nonzero count is rejected before the daemon is involved
  const char* const with_null[] = {NULL};
  EXPECT_ERR_MSG(monero_daemon_get_key_image_spent_statuses(daemon, with_null, 1, &statuses, &count), "array element must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_block_hashes(daemon, NULL, 1, &json), "array must not be null");
  EXPECT_ERR_MSG(monero_daemon_get_output_histogram(daemon, NULL, 1, NULL, NULL, NULL, NULL, &json), "array must not be null");

  // zero length is valid, so the call must not fail with the null array error
  monero_result result = monero_daemon_get_blocks_by_height(daemon, NULL, 0, &json);
  CHECK(result == MONERO_OK || strcmp(monero_last_error(), "array must not be null") != 0);
  monero_utils_free(json);
  json = NULL;

  // the daemon can't be reached: the message from monero-cpp reaches the caller
  EXPECT_ERR(monero_daemon_get_peer_ban(daemon, "1.2.3.4", &json));
  CHECK(json == NULL);
  CHECK(strlen(monero_last_error()) > 0);
}

// ------------------------------- LISTENERS ----------------------------------

static void check_registered(monero_daemon* daemon, size_t expected_count, monero_daemon_listener* expected) {
  monero_daemon_listener** listeners = NULL;
  size_t count = 0;
  EXPECT_OK(monero_daemon_get_listeners(daemon, &listeners, &count));
  CHECK(count == expected_count);
  if (expected != NULL && count == 1 && listeners != NULL) CHECK(listeners[0] == expected);
  monero_utils_free(listeners);
}

static void test_listener_handles(monero_daemon* daemon) {
  monero_daemon_listener_callbacks callbacks = {NULL, NULL};
  monero_daemon_listener* first = NULL;
  monero_daemon_listener* second = NULL;

  EXPECT_ERR_MSG(monero_daemon_listener_create(NULL, &first), "callbacks must not be null");
  EXPECT_ERR_MSG(monero_daemon_listener_create(&callbacks, NULL), "out_listener must not be null");
  EXPECT_OK(monero_daemon_listener_create(&callbacks, &first));
  EXPECT_OK(monero_daemon_listener_create(&callbacks, &second));
  CHECK(first != NULL && second != NULL && first != second);

  EXPECT_ERR_MSG(monero_daemon_add_listener(daemon, NULL), "listener must not be null");
  EXPECT_ERR_MSG(monero_daemon_add_listener(NULL, first), "daemon must not be null");

  // the daemon hands back the very handle that was registered
  EXPECT_OK(monero_daemon_add_listener(daemon, first));
  check_registered(daemon, 1, first);

  // registering the same listener again doesn't duplicate it
  EXPECT_OK(monero_daemon_add_listener(daemon, first));
  check_registered(daemon, 1, first);

  EXPECT_OK(monero_daemon_remove_listener(daemon, first));
  check_registered(daemon, 0, NULL);

  // freeing a registered listener unregisters it first
  EXPECT_OK(monero_daemon_add_listener(daemon, first));
  monero_daemon_listener_free(first);
  first = NULL;
  check_registered(daemon, 0, NULL);

  EXPECT_OK(monero_daemon_add_listener(daemon, second));
  EXPECT_OK(monero_daemon_remove_listeners(daemon));
  check_registered(daemon, 0, NULL);

  monero_daemon_listener_free(second);
}

// ---------------------------- PER-THREAD ERROR ------------------------------

#if !defined(_WIN32)
static volatile int g_worker_saw_its_own_error = 0;

static void* failing_worker(void* arg) {
  monero_daemon* daemon = (monero_daemon*)arg;
  // fails before any request: the output pointer is NULL
  monero_result result = monero_daemon_get_block_hash(daemon, 0, NULL);
  g_worker_saw_its_own_error = result == MONERO_ERROR && strcmp(monero_last_error(), "out_hash must not be null") == 0;
  return NULL;
}

static void test_last_error_is_per_thread(monero_daemon* daemon) {
  uint64_t height = 0;
  EXPECT_ERR_MSG(monero_daemon_get_height(NULL, &height), "daemon must not be null");

  pthread_t worker;
  CHECK(pthread_create(&worker, NULL, failing_worker, daemon) == 0);
  CHECK(pthread_join(worker, NULL) == 0);
  CHECK(g_worker_saw_its_own_error);

  // the worker's error didn't replace this thread's error
  CHECK(strcmp(monero_last_error(), "daemon must not be null") == 0);
}
#endif

// ---------------------------------- MAIN ------------------------------------

static void test_listener_registered_with_one_daemon(void) {
  monero_daemon* first = create_unreachable_daemon();
  monero_daemon* second = create_unreachable_daemon();
  CHECK(first != NULL && second != NULL);
  monero_daemon_listener_callbacks callbacks = {NULL, NULL};
  monero_daemon_listener* listener = NULL;
  EXPECT_OK(monero_daemon_listener_create(&callbacks, &listener));

  EXPECT_OK(monero_daemon_add_listener(first, listener));
  EXPECT_OK(monero_daemon_add_listener(first, listener));
  EXPECT_ERR_MSG(monero_daemon_add_listener(second, listener), "listener is already registered with another daemon");
  check_registered(first, 1, listener);
  check_registered(second, 0, NULL);

  // freeing the listener unregisters it from the daemon it is registered with
  monero_daemon_listener_free(listener);
  check_registered(first, 0, NULL);

  monero_daemon_free(second);
  monero_daemon_free(first);
}

int main(void) {
  monero_daemon* daemon = create_unreachable_daemon();
  if (daemon == NULL) {
    fprintf(stderr, "cannot create the test daemon handle\n");
    return EXIT_FAILURE;
  }

  test_connect_validates_arguments();
  test_free_null_is_noop();
  test_every_function_rejects_null_daemon();
  test_null_outputs_are_rejected(daemon);
  test_outputs_are_reset_on_error(daemon);
  test_null_arguments_reset_outputs();
  test_errors_from_the_binding_and_monero_cpp(daemon);
  test_listener_handles(daemon);
  test_listener_registered_with_one_daemon();
#if !defined(_WIN32)
  test_last_error_is_per_thread(daemon);
#endif

  monero_daemon_free(daemon);
  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

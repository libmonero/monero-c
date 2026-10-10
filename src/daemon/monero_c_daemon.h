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

#ifndef MONERO_C_DAEMON_H
#define MONERO_C_DAEMON_H

#include "utils/monero_c_utils.h"
#include "common/monero_c_rpc_connection.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------ DAEMON RPC ----------------------------------
// each function wraps the monero_daemon method of the same name (monero-cpp's
// monero_daemon interface). Results are JSON in monero-cpp's serializable_struct
// format unless the parameter says otherwise. A JSON argument can't nest deeper than
// 64 levels. On error, string and array outputs are set to NULL (counts to 0) and scalar
// outputs are left unchanged. The thread safety rules are documented on monero_daemon and
// monero_daemon_listener

/**
 * Opaque handle to a daemon RPC client. Create it with monero_daemon_connect()
 * and release it with monero_daemon_free(). Calls return MONERO_ERROR if the daemon
 * can't be reached or answers with an error.
 *
 * @par Thread safety
 * monero-cpp serializes calls on one handle, so they don't run in parallel. For parallel
 * requests, use one handle per thread. Don't call monero_daemon_free() while another
 * thread uses the handle.
 */
typedef struct monero_daemon monero_daemon;

/**
 * Opaque listener that receives the daemon's notifications. Create it with
 * monero_daemon_listener_create() and release it with monero_daemon_listener_free().
 *
 * @par Thread safety
 * The listener has no lock of its own, so don't add, remove or free it from two threads at
 * once. Different listeners on one daemon can be managed from different threads.
 */
typedef struct monero_daemon_listener monero_daemon_listener;

/**
 * Called with the JSON of each new block header. The string is valid only during the call.
 * The callback runs on a thread owned by monero-cpp. Inside it, monero_daemon_remove_listener()
 * and monero_daemon_remove_listeners() are safe. Don't free the listener or the daemon from
 * inside it. Free them after the callback returns.
 *
 * @param user_data is the user_data of the monero_daemon_listener_callbacks
 * @param header_json is the JSON-serialized monero_block_header
 */
typedef void (*monero_daemon_on_block_header_fn)(void* user_data, const char* header_json);

/**
 * Callbacks of a daemon listener. The struct is copied by monero_daemon_listener_create().
 */
typedef struct monero_daemon_listener_callbacks {
  void* user_data;  /**< is passed to the callback, and monero_c doesn't read it */
  monero_daemon_on_block_header_fn on_block_header;  /**< is called with each new block header, or NULL to skip */
} monero_daemon_listener_callbacks;

/**
 * Spent status of a key image. The values match monero-cpp's monero_key_image_spent_status.
 */
typedef enum monero_key_image_spent_status {
  MONERO_KEY_IMAGE_NOT_SPENT = 0,
  MONERO_KEY_IMAGE_CONFIRMED = 1,
  MONERO_KEY_IMAGE_TX_POOL = 2
} monero_key_image_spent_status;

// ------------------------------ CONNECTION ----------------------------------

/**
 * Create a daemon RPC client and checks the connection, which can take a while if the
 * daemon is slow to answer. It still succeeds if the daemon is unreachable. The calls that
 * query the daemon report the failure.
 *
 * @param uri is the daemon RPC URI, e.g. "http://127.0.0.1:18081"
 * @param username is the RPC username, or "" for none
 * @param password is the RPC password, or "" for none
 * @param proxy_uri is the proxy to connect through, or "" for none
 * @param timeout_ms is the RPC timeout in milliseconds, or 0 for the default
 * @param out_daemon receives the new handle, released with monero_daemon_free()
 * @return MONERO_OK on success, or MONERO_ERROR if uri is empty (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_connect(const char* uri, const char* username, const char* password, const char* proxy_uri, uint32_t timeout_ms, monero_daemon** out_daemon);

/**
 * Create a daemon RPC client that shares a connection, with its proxy, timeout and TLS
 * settings. It checks the connection if it isn't online yet, and succeeds even if the
 * daemon is unreachable.
 *
 * @param connection is the connection to the daemon. It can be freed after the call
 * @param out_daemon receives the new handle, released with monero_daemon_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_daemon_connect_with(monero_rpc_connection* connection, monero_daemon** out_daemon);

/**
 * Release a handle returned by monero_daemon_connect(). Passing NULL does nothing.
 *
 * @param daemon is the handle to release
 */
MONERO_EXPORT void monero_daemon_free(monero_daemon* daemon);

/**
 * Get the connection to the daemon.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives the JSON-serialized monero_rpc_connection, with the password. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_daemon_get_rpc_connection(monero_daemon* daemon, char** out_json);

/**
 * Set how often the listeners poll the daemon for new blocks.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param period_ms is the poll period in milliseconds
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_daemon_set_poll_period(monero_daemon* daemon, uint64_t period_ms);

// -------------------------------- LISTENERS ---------------------------------

/**
 * Create a listener that forwards the daemon's notifications to the callbacks.
 *
 * @param callbacks are the callbacks to call. They are copied.
 * @param out_listener receives the new listener, released with monero_daemon_listener_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_listener_create(const monero_daemon_listener_callbacks* callbacks, monero_daemon_listener** out_listener);

/**
 * Release a listener. If the listener is still registered with a daemon, it is
 * unregistered first. Passing NULL does nothing.
 *
 * @param listener is the listener to release
 */
MONERO_EXPORT void monero_daemon_listener_free(monero_daemon_listener* listener);

/**
 * Register a listener. Adding a listener that is already registered does nothing.
 * A listener can be registered with one daemon at a time, so adding it to another daemon fails until it is removed from the first one.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param listener is the listener to register
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_add_listener(monero_daemon* daemon, monero_daemon_listener* listener);

/**
 * Unregister a listener. Returns after any callback of that listener in progress has finished.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param listener is the listener to unregister
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_remove_listener(monero_daemon* daemon, monero_daemon_listener* listener);

/**
 * Get the listeners registered with the daemon.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_listeners receives an array of listener handles the caller still owns. Free the array with monero_utils_free()
 * @param out_count receives the number of handles in out_listeners
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_listeners(monero_daemon* daemon, monero_daemon_listener*** out_listeners, size_t* out_count);

/**
 * Unregister all listeners of the daemon. Returns after callbacks in progress have finished.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_remove_listeners(monero_daemon* daemon);

// ---------------------------------- GENERAL ---------------------------------

/**
 * Get the daemon's version.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_version, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_version(monero_daemon* daemon, char** out_json);

/**
 * Indicates if the daemon is trusted or untrusted.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_trusted receives true if the daemon is trusted, false otherwise
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_is_trusted(monero_daemon* daemon, bool* out_trusted);

/**
 * Get the number of blocks in the longest chain known to the node.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_height receives the number of blocks
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_height(monero_daemon* daemon, uint64_t* out_height);

/**
 * Get a block's hash by its height.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param height is the height of the block
 * @param out_hash receives the block's hash, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_block_hash(monero_daemon* daemon, uint64_t height, char** out_hash);

/**
 * Get general information about the state of the node and the network.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_daemon_info, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_info(monero_daemon* daemon, char** out_json);

/**
 * Get synchronization information.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_daemon_sync_info, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_sync_info(monero_daemon* daemon, char** out_json);

/**
 * Get network (bandwidth) statistics since the daemon started.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_daemon_network_stats, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_network_stats(monero_daemon* daemon, char** out_json);

/**
 * Look up information regarding hard fork voting and readiness.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_hard_fork_info, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_hard_fork_info(monero_daemon* daemon, char** out_json);

/**
 * Get the alternative chains seen by the node.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON array of monero_alt_chain, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_alt_chains(monero_daemon* daemon, char** out_json);

/**
 * Get the known block hashes that are not on the main chain.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON array of hex strings, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_alt_block_hashes(monero_daemon* daemon, char** out_json);

// ---------------------------------- MINING ----------------------------------

/**
 * Get a block template for mining a new block.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param wallet_address is the address that receives the miner transaction if the block is mined
 * @param reserve_size is the reserve size, or NULL for none
 * @param out_json receives a JSON-serialized monero_block_template, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_block_template(monero_daemon* daemon, const char* wallet_address, const int32_t* reserve_size, char** out_json);

/**
 * Get the data needed to build a block template, for pools that assemble their own.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_miner_data, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_miner_data(monero_daemon* daemon, char** out_json);

/**
 * Calculate the proof-of-work hash of a mined block.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param major_version is the block's major version
 * @param height is the block's height
 * @param block_blob is the block's blob to hash
 * @param seed_hash is the seed hash used to select the RandomX dataset and cache
 * @param out_pow_hash receives the proof-of-work hash, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_calculate_pow(monero_daemon* daemon, uint32_t major_version, uint64_t height, const char* block_blob, const char* seed_hash, char** out_pow_hash);

/**
 * Add auxiliary proof-of-work to a block template for merge mining. The Merkle root of the
 * updated template commits to the auxiliary blocks.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param block_template_blob is the block template blob to add auxiliary PoW to
 * @param aux_pow_json is a JSON array of monero_auxiliary_pow, one per auxiliary chain
 * @param out_json receives a JSON-serialized monero_add_auxiliary_pow_result, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_add_auxiliary_pow(monero_daemon* daemon, const char* block_template_blob, const char* aux_pow_json, char** out_json);

/**
 * Get the daemon's mining status.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_mining_status, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_mining_status(monero_daemon* daemon, char** out_json);

/**
 * Start mining.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param address is the address that receives the rewards if the daemon mines a block
 * @param num_threads is the number of mining threads, or NULL for the daemon's default
 * @param is_background specifies if the miner runs in the background, or NULL for the daemon's default
 * @param ignore_battery specifies if the battery state is ignored, or NULL for the daemon's default
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_start_mining(monero_daemon* daemon, const char* address, const uint64_t* num_threads, const bool* is_background, const bool* ignore_battery);

/**
 * Stop mining.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_stop_mining(monero_daemon* daemon);

/**
 * Generate blocks to a wallet address. Regtest only.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param wallet_address is the address that receives the miner transactions
 * @param num_blocks is the number of blocks to generate
 * @param prev_block_hash is the hash of the block to build on, or NULL to build on the tip
 * @param starting_nonce is the starting nonce, or NULL for the daemon's default
 * @param out_json receives a JSON-serialized monero_generate_blocks_result, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_generate_blocks(monero_daemon* daemon, const char* wallet_address, uint64_t num_blocks, const char* prev_block_hash, const uint32_t* starting_nonce, char** out_json);

/**
 * Submit a mined block to the network.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param block_blob is the mined block to submit
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_submit_block(monero_daemon* daemon, const char* block_blob);

/**
 * Submit mined blocks to the network.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param block_blobs are the mined blocks to submit
 * @param num_block_blobs is the number of elements in block_blobs
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_submit_blocks(monero_daemon* daemon, const char* const* block_blobs, size_t num_block_blobs);

// ----------------------------------- BLOCKS ---------------------------------

/**
 * Get the header of the last block.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_block_header, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_last_block_header(monero_daemon* daemon, char** out_json);

/**
 * Get a block header by its hash.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param block_hash is the hash of the block
 * @param out_json receives a JSON-serialized monero_block_header, or NULL if the daemon has no such block. Free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_block_header_by_hash(monero_daemon* daemon, const char* block_hash, char** out_json);

/**
 * Get a block header by its height.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param height is the height of the block
 * @param out_json receives a JSON-serialized monero_block_header, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_block_header_by_height(monero_daemon* daemon, uint64_t height, char** out_json);

/**
 * Get block headers in the given height range.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param start_height is the lower bound of the range, inclusive
 * @param end_height is the upper bound of the range, inclusive
 * @param out_json receives a JSON array of monero_block_header, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_block_headers_by_range(monero_daemon* daemon, uint64_t start_height, uint64_t end_height, char** out_json);

/**
 * Get a block by its hash.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param block_hash is the hash of the block
 * @param out_json receives a JSON-serialized monero_block, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_block_by_hash(monero_daemon* daemon, const char* block_hash, char** out_json);

/**
 * Get blocks by hash. The hashes form a short chain history: the first 10 are consecutive,
 * then the gap doubles, and the last one is always the genesis block.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param block_hashes is the chain history
 * @param num_block_hashes is the number of elements in block_hashes
 * @param start_height is the height to resume from, used as-is when non-zero
 * @param prune specifies if the returned blocks should be pruned
 * @param max_block_count caps the number of blocks returned in one call, or 0 for the daemon's default
 * @param out_json receives a JSON-serialized monero_get_blocks_by_hash_result, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_blocks_by_hash(monero_daemon* daemon, const char* const* block_hashes, size_t num_block_hashes, uint64_t start_height, bool prune, uint64_t max_block_count, char** out_json);

/**
 * Get a block by its height.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param height is the height of the block
 * @param out_json receives a JSON-serialized monero_block, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_block_by_height(monero_daemon* daemon, uint64_t height, char** out_json);

/**
 * Get blocks at the given heights.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param heights are the heights of the blocks
 * @param num_heights is the number of elements in heights
 * @param out_json receives a JSON array of monero_block, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_blocks_by_height(monero_daemon* daemon, const uint64_t* heights, size_t num_heights, char** out_json);

/**
 * Get blocks in the given height range. A range of more than 100000 blocks fails, so use
 * monero_daemon_get_blocks_by_range_chunked() for a longer one.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param start_height is the lower bound of the range, inclusive, or NULL for none
 * @param end_height is the upper bound of the range, inclusive, or NULL for none
 * @param out_json receives a JSON array of monero_block, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_blocks_by_range(monero_daemon* daemon, const uint64_t* start_height, const uint64_t* end_height, char** out_json);

/**
 * Get blocks in the given height range, in chunked requests.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param start_height is the lower bound of the range, inclusive, or NULL for none
 * @param end_height is the upper bound of the range, inclusive, or NULL for none
 * @param max_chunk_size is the maximum size in bytes of each request, or NULL for the default. A larger block fails the call.
 * @param out_json receives a JSON array of monero_block, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_blocks_by_range_chunked(monero_daemon* daemon, const uint64_t* start_height, const uint64_t* end_height, const uint64_t* max_chunk_size, char** out_json);

/**
 * Get block hashes from a chain history, using the same format as monero_daemon_get_blocks_by_hash().
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param block_hashes is the chain history. The last hash must be the genesis block.
 * @param num_block_hashes is the number of elements in block_hashes
 * @param out_json receives a JSON-serialized monero_get_block_hashes_result, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_block_hashes(monero_daemon* daemon, const char* const* block_hashes, size_t num_block_hashes, char** out_json);

/**
 * Wait for the next block and return its header. Only the calling thread blocks.
 * Other calls on the same handle can run meanwhile. Fails if no block arrives within 30 minutes.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_block_header, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_wait_for_next_block_header(monero_daemon* daemon, char** out_json);

// ---------------------------- TRANSACTIONS ----------------------------------

/**
 * Get a transaction by its hash.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hash is the hash of the transaction
 * @param prune specifies if the returned transaction should be pruned
 * @param out_json receives a JSON-serialized monero_tx, or NULL if the daemon doesn't have it. Free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_tx(monero_daemon* daemon, const char* tx_hash, bool prune, char** out_json);

/**
 * Get transactions by their hashes.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hashes are the hashes of the transactions
 * @param num_tx_hashes is the number of elements in tx_hashes
 * @param prune specifies if the returned transactions should be pruned
 * @param out_json receives a JSON array of the transactions found, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_txs(monero_daemon* daemon, const char* const* tx_hashes, size_t num_tx_hashes, bool prune, char** out_json);

/**
 * Get a transaction's hex by its hash.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hash is the hash of the transaction
 * @param prune specifies if the returned hex should be pruned
 * @param out_tx_hex receives the transaction hex, or NULL if the daemon doesn't have it. Free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_tx_hex(monero_daemon* daemon, const char* tx_hash, bool prune, char** out_tx_hex);

/**
 * Get transaction hexes by their hashes.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hashes are the hashes of the transactions
 * @param num_tx_hashes is the number of elements in tx_hashes
 * @param prune specifies if the returned hexes should be pruned
 * @param out_json receives a JSON array of hex strings, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_tx_hexes(monero_daemon* daemon, const char* const* tx_hashes, size_t num_tx_hashes, bool prune, char** out_json);

/**
 * Get the total emissions and fees from the genesis block up to the given height.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param height is the height to start the sum at
 * @param num_blocks is the number of blocks to include in the sum
 * @param out_json receives a JSON-serialized monero_miner_tx_sum, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_miner_tx_sum(monero_daemon* daemon, uint64_t height, uint64_t num_blocks, char** out_json);

/**
 * Get the mining fee estimates per kB.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param grace_blocks is the number of grace blocks to use for the estimate
 * @param out_json receives a JSON-serialized monero_fee_estimate, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_fee_estimate(monero_daemon* daemon, uint64_t grace_blocks, char** out_json);

/**
 * Submit a transaction to the daemon's pool.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hex is the raw transaction hex to submit
 * @param do_not_relay specifies if the transaction should not be relayed
 * @param out_json receives a JSON-serialized monero_submit_tx_result, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_submit_tx_hex(monero_daemon* daemon, const char* tx_hex, bool do_not_relay, char** out_json);

/**
 * Relay a transaction by its hash.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hash is the hash of the transaction to relay
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_relay_tx_by_hash(monero_daemon* daemon, const char* tx_hash);

/**
 * Relay transactions by their hashes.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hashes are the hashes of the transactions to relay
 * @param num_tx_hashes is the number of elements in tx_hashes
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_relay_txs_by_hash(monero_daemon* daemon, const char* const* tx_hashes, size_t num_tx_hashes);

// ------------------------------- TX POOL ------------------------------------

/**
 * Get the valid transactions seen by the node but not yet mined into a block.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON array of monero_tx, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_tx_pool(monero_daemon* daemon, char** out_json);

/**
 * Get the hashes of the transactions in the transaction pool.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON array of hex strings, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_tx_pool_hashes(monero_daemon* daemon, char** out_json);

/**
 * Get the transaction pool backlog. monero-cpp doesn't implement it, so this always fails.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON array of monero_tx_backlog_entry, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_tx_pool_backlog(monero_daemon* daemon, char** out_json);

/**
 * Get statistics about the transaction pool.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_tx_pool_stats, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_tx_pool_stats(monero_daemon* daemon, char** out_json);

/**
 * Flush all transactions from the transaction pool.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_flush_tx_pool(monero_daemon* daemon);

/**
 * Flush transactions from the transaction pool by their hashes.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hashes are the hashes of the transactions to flush
 * @param num_tx_hashes is the number of elements in tx_hashes
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_flush_tx_pool_hashes(monero_daemon* daemon, const char* const* tx_hashes, size_t num_tx_hashes);

/**
 * Flush a single transaction from the transaction pool.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hash is the hash of the transaction to flush
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_flush_tx_pool_hash(monero_daemon* daemon, const char* tx_hash);

// --------------------------- KEY IMAGES AND OUTPUTS -------------------------

/**
 * Get the spent status of a key image.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param key_image is the key image hex
 * @param out_status receives the spent status
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_key_image_spent_status(monero_daemon* daemon, const char* key_image, monero_key_image_spent_status* out_status);

/**
 * Get the spent status of each of the given key images.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param key_images are the key image hex strings
 * @param num_key_images is the number of elements in key_images
 * @param out_statuses receives an array of spent statuses, in order, freed with monero_utils_free()
 * @param out_count receives the number of elements in out_statuses
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_key_image_spent_statuses(monero_daemon* daemon, const char* const* key_images, size_t num_key_images, monero_key_image_spent_status** out_statuses, size_t* out_count);

/**
 * Get the global output index of each output in a transaction.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param tx_hash is the hash of the transaction
 * @param out_indices receives the output indices, in order, freed with monero_utils_free()
 * @param out_count receives the number of elements in out_indices
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_output_indices(monero_daemon* daemon, const char* tx_hash, uint64_t** out_indices, size_t* out_count);

/**
 * Get outputs identified by amount and index.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param outputs_json is a JSON array of monero_output, each identified by amount and index
 * @param out_json receives a JSON array of the outputs found, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_outputs(monero_daemon* daemon, const char* outputs_json, char** out_json);

/**
 * Get a histogram of output amounts: for each amount, the number of outputs on the chain.
 * RingCT outputs count as amount 0.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param amounts are the amounts to make the histogram with
 * @param num_amounts is the number of elements in amounts
 * @param min_count is the minimum count to include, or NULL for none
 * @param max_count is the maximum count to include, or NULL for none
 * @param is_unlocked selects the lock state of the outputs to count, or NULL for both
 * @param recent_cutoff is the recent cutoff, or NULL for none
 * @param out_json receives a JSON array of monero_output_histogram_entry, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_output_histogram(monero_daemon* daemon, const uint64_t* amounts, size_t num_amounts, const int32_t* min_count, const int32_t* max_count, const bool* is_unlocked, const int32_t* recent_cutoff, char** out_json);

/**
 * Create an output distribution for the given amounts.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param amounts are the amounts to make the distribution with
 * @param num_amounts is the number of elements in amounts
 * @param is_cumulative specifies if the results are cumulative, or NULL for the daemon's default
 * @param start_height is the lower bound of the range, inclusive, or NULL for none
 * @param end_height is the upper bound of the range, inclusive, or NULL for none
 * @param out_json receives a JSON array of monero_output_distribution_entry, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_output_distribution(monero_daemon* daemon, const uint64_t* amounts, size_t num_amounts, const bool* is_cumulative, const uint64_t* start_height, const uint64_t* end_height, char** out_json);

// ------------------------------- BANDWIDTH ----------------------------------

/**
 * Get the download bandwidth limit.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_limit receives the limit
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_download_limit(monero_daemon* daemon, int32_t* out_limit);

/**
 * Set the download bandwidth limit.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param limit is the limit to set, or -1 to reset it to the default
 * @param out_limit receives the limit in effect after setting it
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_download_limit(monero_daemon* daemon, int32_t limit, int32_t* out_limit);

/**
 * Reset the download bandwidth limit to the default.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_limit receives the limit in effect after resetting it
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_reset_download_limit(monero_daemon* daemon, int32_t* out_limit);

/**
 * Get the upload bandwidth limit.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_limit receives the limit
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_upload_limit(monero_daemon* daemon, int32_t* out_limit);

/**
 * Set the upload bandwidth limit.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param limit is the limit to set, or -1 to reset it to the default
 * @param out_limit receives the limit in effect after setting it
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_upload_limit(monero_daemon* daemon, int32_t limit, int32_t* out_limit);

/**
 * Reset the upload bandwidth limit to the default.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_limit receives the limit in effect after resetting it
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_reset_upload_limit(monero_daemon* daemon, int32_t* out_limit);

// ---------------------------------- PEERS -----------------------------------

/**
 * Get the peers with active incoming or outgoing connections to the node.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON array of monero_peer, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_peers(monero_daemon* daemon, char** out_json);

/**
 * Get all known peers, including their last known online status.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON array of monero_peer, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_known_peers(monero_daemon* daemon, char** out_json);

/**
 * Get the public nodes known to the daemon.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param include_offline specifies if offline nodes are included
 * @param out_json receives a JSON array of monero_peer, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_public_peers(monero_daemon* daemon, bool include_offline, char** out_json);

/**
 * Limits the number of outgoing peers.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param limit is the maximum number of outgoing peers
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_outgoing_peer_limit(monero_daemon* daemon, int32_t limit);

/**
 * Limits the number of incoming peers.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param limit is the maximum number of incoming peers
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_incoming_peer_limit(monero_daemon* daemon, int32_t limit);

/**
 * Get the peer bans.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON array of monero_ban, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_peer_bans(monero_daemon* daemon, char** out_json);

/**
 * Bans peer nodes.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param bans_json is a JSON array of monero_ban to apply
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_peer_bans(monero_daemon* daemon, const char* bans_json);

/**
 * Bans a peer node.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param ban_json is a JSON-serialized monero_ban to apply
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_peer_ban(monero_daemon* daemon, const char* ban_json);

/**
 * Get the ban status of a peer node.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param address is the address of the peer, e.g. "1.2.3.4" or "1.2.3.4:18080"
 * @param out_json receives a JSON-serialized monero_ban, whose isBanned is false if the peer isn't banned. Free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_get_peer_ban(monero_daemon* daemon, const char* address, char** out_json);

// ---------------------------------- ADMIN -----------------------------------

/**
 * Prunes the blockchain.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param check specifies if the pruning is checked
 * @param out_json receives a JSON-serialized monero_prune_result, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_prune_blockchain(monero_daemon* daemon, bool check, char** out_json);

/**
 * Saves (flushes) the blockchain to disk.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_save_blockchain(monero_daemon* daemon);

/**
 * Pops (removes) blocks from the top of the blockchain.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param num_blocks is the number of blocks to pop
 * @param out_height receives the blockchain height after popping the blocks
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_pop_blocks(monero_daemon* daemon, uint64_t num_blocks, uint64_t* out_height);

/**
 * Flush the daemon's invalid block and transaction caches.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param bad_blocks specifies if the bad blocks cache is flushed too
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_flush_cache(monero_daemon* daemon, bool bad_blocks);

/**
 * Set the bootstrap daemon the node uses to serve requests while it is not fully synced.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param address is the bootstrap daemon's address (host:port), "auto" to select a public node, or "" to disable it
 * @param username is the username to authenticate with, or "" for none
 * @param password is the password to authenticate with, or "" for none
 * @param proxy is the proxy used to reach the bootstrap daemon, or "" for none
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_bootstrap_daemon(monero_daemon* daemon, const char* address, const char* username, const char* password, const char* proxy);

/**
 * Disables the bootstrap daemon, so the node no longer falls back to it.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_remove_bootstrap_daemon(monero_daemon* daemon);

/**
 * Show or hide the mining hash rate in the daemon's console log. The daemon must be mining
 * (see monero_daemon_start_mining()), otherwise the call fails.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param is_visible specifies if the hash rate is logged
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_log_hash_rate(monero_daemon* daemon, bool is_visible);

/**
 * Set the daemon's log level.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param level is the log level, from 0 (least verbose) to 4 (most verbose)
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_log_level(monero_daemon* daemon, int32_t level);

/**
 * Set the daemon's log categories, e.g. "*:WARNING,net.p2p:DEBUG".
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param categories are the log categories to set, or "" to reset them to the default
 * @param out_categories receives the log categories in effect, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_set_log_categories(monero_daemon* daemon, const char* categories, char** out_categories);

/**
 * Check for an update.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param out_json receives a JSON-serialized monero_daemon_update_check_result, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_check_for_update(monero_daemon* daemon, char** out_json);

/**
 * Downloads an update.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @param path is the path to download the update to, or "" for the daemon's default
 * @param out_json receives a JSON-serialized monero_daemon_update_download_result, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_download_update(monero_daemon* daemon, const char* path, char** out_json);

/**
 * Safely disconnects from and shuts down the daemon.
 *
 * @param daemon is the handle returned by monero_daemon_connect()
 * @return MONERO_OK on success, or MONERO_ERROR (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_daemon_stop(monero_daemon* daemon);

#ifdef __cplusplus
}
#endif

#endif // MONERO_C_DAEMON_H

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

#include "daemon/monero_c_daemon.h"
#include "common/monero_c_common.h"
#include "daemon/monero_daemon_model.h"
#include "daemon/monero_daemon_rpc.h"
#include "utils/gen_utils.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <boost/optional.hpp>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// state behind a monero_daemon handle
struct monero_daemon {
  std::shared_ptr<monero::monero_daemon_rpc> rpc;
};

namespace {

using monero_c::array_of;
using monero_c::dup_array;
using monero_c::dup_string;
using monero_c::guard;
using monero_c::json_of_list;
using monero_c::json_of_strings;
using monero_c::optional_of;
using monero_c::require;
using monero_c::safe_str;
using monero_c::set_last_error;
using monero_c::string_array;

// splits a JSON array into its elements, each serialized again as a JSON document
std::vector<std::string> split_json_array(const std::string& json) {
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  if (doc.HasParseError() || !doc.IsArray()) throw std::invalid_argument("expected a JSON array");

  std::vector<std::string> items;
  for (const auto& value : doc.GetArray()) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    items.push_back(buffer.GetString());
  }
  return items;
}

// deserializes each element of a JSON array into a monero-cpp object
template <class T>
std::vector<std::shared_ptr<T>> parse_list(const std::string& json) {
  std::vector<std::shared_ptr<T>> items;
  for (const std::string& item : split_json_array(json)) {
    items.push_back(gen_utils::deserialize<T>(item));
  }
  return items;
}

// monero-cpp's monero_tx_backlog_entry has no fields yet, so each entry is an empty object
std::string json_of_backlog(const std::vector<monero::monero_tx_backlog_entry>& entries) {
  std::string json = "[";
  for (size_t i = 0; i < entries.size(); i++) {
    if (i > 0) json += ",";
    json += "{}";
  }
  return json + "]";
}

// serializes one result object. A null result (the daemon has no such item) becomes NULL
template <class T>
char* dup_json(const std::shared_ptr<T>& item) {
  return item ? dup_string(item->serialize()) : nullptr;
}

// serializes one result object held by value
template <class T>
char* dup_json_value(const T& item) {
  return dup_string(item.serialize());
}

// converts monero-cpp's key image status to the C enum, which has the same values
monero_key_image_spent_status to_c_status(monero::monero_key_image_spent_status status) {
  return static_cast<monero_key_image_spent_status>(static_cast<int>(status));
}

// forwards the daemon's block header notifications to the C callbacks of a listener
class listener_adapter : public monero::monero_daemon_listener {
public:
  ::monero_daemon_listener* owner = nullptr;  // the C handle, not the base class of the same name
  monero_daemon_listener_callbacks callbacks = {nullptr, nullptr};

  void on_block_header(const std::shared_ptr<monero::monero_block_header>& header) override {
    monero::monero_daemon_listener::on_block_header(header);
    if (callbacks.on_block_header == nullptr || !header) return;
    std::string json = header->serialize();
    callbacks.on_block_header(callbacks.user_data, json.c_str());
  }
};

} // namespace

struct monero_daemon_listener {
  listener_adapter adapter;
  std::weak_ptr<monero::monero_daemon_rpc> registered_with;
  std::mutex registration_mutex; // guards registered_with, so the check and the registration in add_listener are one step
};

extern "C" {

// ------------------------------ CONNECTION ----------------------------------

monero_result monero_daemon_connect(const char* uri, const char* username, const char* password, const char* proxy_uri, uint32_t timeout_ms, monero_daemon** out_daemon) {
  if (!require(out_daemon, "out_daemon")) return MONERO_ERROR;
  *out_daemon = nullptr;
  std::string uri_str = safe_str(uri);
  if (uri_str.empty()) { set_last_error("uri must not be empty"); return MONERO_ERROR; }
  return guard([&] {
    boost::optional<uint32_t> timeout;
    if (timeout_ms != 0) timeout = timeout_ms;
    std::unique_ptr<monero_daemon> daemon(new monero_daemon());
    daemon->rpc = std::make_shared<monero::monero_daemon_rpc>(uri_str, safe_str(username), safe_str(password), safe_str(proxy_uri), std::string(), timeout);
    *out_daemon = daemon.release();
  });
}

monero_result monero_daemon_connect_with(::monero_rpc_connection* connection, monero_daemon** out_daemon) {
  if (!require(connection, "connection") || !require(out_daemon, "out_daemon")) return MONERO_ERROR;
  *out_daemon = nullptr;
  return guard([&] {
    std::unique_ptr<monero_daemon> daemon(new monero_daemon());
    daemon->rpc = std::make_shared<monero::monero_daemon_rpc>(monero_c::connection_of(connection));
    *out_daemon = daemon.release();
  });
}

void monero_daemon_free(monero_daemon* daemon) {
  delete daemon;
}

monero_result monero_daemon_get_rpc_connection(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(daemon->rpc->get_rpc_connection()->serialize()); });
}

monero_result monero_daemon_set_poll_period(monero_daemon* daemon, uint64_t period_ms) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->set_poll_period_in_ms(period_ms); });
}

// -------------------------------- LISTENERS ---------------------------------

monero_result monero_daemon_listener_create(const monero_daemon_listener_callbacks* callbacks, monero_daemon_listener** out_listener) {
  if (!require(callbacks, "callbacks") || !require(out_listener, "out_listener")) return MONERO_ERROR;
  *out_listener = nullptr;
  return guard([&] {
    std::unique_ptr<monero_daemon_listener> listener(new monero_daemon_listener());
    listener->adapter.owner = listener.get();
    listener->adapter.callbacks = *callbacks;
    *out_listener = listener.release();
  });
}

void monero_daemon_listener_free(monero_daemon_listener* listener) {
  if (listener == nullptr) return;
  std::shared_ptr<monero::monero_daemon_rpc> rpc;
  {
    std::lock_guard<std::mutex> lock(listener->registration_mutex);
    rpc = listener->registered_with.lock();
  }
  if (rpc) {
    try {
      rpc->remove_listener(listener->adapter);
    } catch (...) {
      // release the listener either way, since the caller can't act on a failed removal
    }
  }
  delete listener;
}

monero_result monero_daemon_add_listener(monero_daemon* daemon, monero_daemon_listener* listener) {
  if (!require(daemon, "daemon") || !require(listener, "listener")) return MONERO_ERROR;
  return guard([&] {
    // free unregisters from one daemon only, so a listener can't be registered with two
    std::lock_guard<std::mutex> lock(listener->registration_mutex);
    std::shared_ptr<monero::monero_daemon_rpc> current = listener->registered_with.lock();
    if (current && current != daemon->rpc) throw std::runtime_error("listener is already registered with another daemon");
    daemon->rpc->add_listener(listener->adapter);
    listener->registered_with = daemon->rpc;
  });
}

monero_result monero_daemon_remove_listener(monero_daemon* daemon, monero_daemon_listener* listener) {
  if (!require(daemon, "daemon") || !require(listener, "listener")) return MONERO_ERROR;
  return guard([&] {
    // remove_listener waits for callbacks, so the lock is taken after it returns
    daemon->rpc->remove_listener(listener->adapter);
    std::lock_guard<std::mutex> lock(listener->registration_mutex);
    if (listener->registered_with.lock() == daemon->rpc) listener->registered_with.reset();
  });
}

monero_result monero_daemon_get_listeners(monero_daemon* daemon, monero_daemon_listener*** out_listeners, size_t* out_count) {
  if (!require(daemon, "daemon") || !require(out_listeners, "out_listeners") || !require(out_count, "out_count")) return MONERO_ERROR;
  *out_listeners = nullptr;
  *out_count = 0;
  return guard([&] {
    std::vector<monero_daemon_listener*> handles;
    for (monero::monero_daemon_listener* listener : daemon->rpc->get_listeners()) {
      listener_adapter* adapter = dynamic_cast<listener_adapter*>(listener);
      if (adapter != nullptr) handles.push_back(adapter->owner);
    }
    *out_listeners = dup_array(handles, out_count);
  });
}

monero_result monero_daemon_remove_listeners(monero_daemon* daemon) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->remove_listeners(); });
}

// ---------------------------------- GENERAL ---------------------------------

monero_result monero_daemon_get_version(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json_value(daemon->rpc->get_version()); });
}

monero_result monero_daemon_is_trusted(monero_daemon* daemon, bool* out_trusted) {
  if (!require(daemon, "daemon") || !require(out_trusted, "out_trusted")) return MONERO_ERROR;
  return guard([&] { *out_trusted = daemon->rpc->is_trusted(); });
}

monero_result monero_daemon_get_height(monero_daemon* daemon, uint64_t* out_height) {
  if (!require(daemon, "daemon") || !require(out_height, "out_height")) return MONERO_ERROR;
  return guard([&] { *out_height = daemon->rpc->get_height(); });
}

monero_result monero_daemon_get_block_hash(monero_daemon* daemon, uint64_t height, char** out_hash) {
  if (!require(daemon, "daemon") || !require(out_hash, "out_hash")) return MONERO_ERROR;
  *out_hash = nullptr;
  return guard([&] { *out_hash = dup_string(daemon->rpc->get_block_hash(height)); });
}

monero_result monero_daemon_get_info(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_info()); });
}

monero_result monero_daemon_get_sync_info(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_sync_info()); });
}

monero_result monero_daemon_get_network_stats(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_network_stats()); });
}

monero_result monero_daemon_get_hard_fork_info(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_hard_fork_info()); });
}

monero_result monero_daemon_get_alt_chains(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_alt_chains())); });
}

monero_result monero_daemon_get_alt_block_hashes(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_strings(daemon->rpc->get_alt_block_hashes())); });
}

// ---------------------------------- MINING ----------------------------------

monero_result monero_daemon_get_block_template(monero_daemon* daemon, const char* wallet_address, const int32_t* reserve_size, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_block_template(safe_str(wallet_address), optional_of(reserve_size))); });
}

monero_result monero_daemon_get_miner_data(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_miner_data()); });
}

monero_result monero_daemon_calculate_pow(monero_daemon* daemon, uint32_t major_version, uint64_t height, const char* block_blob, const char* seed_hash, char** out_pow_hash) {
  if (!require(daemon, "daemon") || !require(out_pow_hash, "out_pow_hash")) return MONERO_ERROR;
  *out_pow_hash = nullptr;
  return guard([&] { *out_pow_hash = dup_string(daemon->rpc->calculate_pow(major_version, height, safe_str(block_blob), safe_str(seed_hash))); });
}

monero_result monero_daemon_add_auxiliary_pow(monero_daemon* daemon, const char* block_template_blob, const char* aux_pow_json, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] {
    *out_json = dup_json(daemon->rpc->add_auxiliary_pow(safe_str(block_template_blob), parse_list<monero::monero_auxiliary_pow>(safe_str(aux_pow_json))));
  });
}

monero_result monero_daemon_get_mining_status(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_mining_status()); });
}

monero_result monero_daemon_start_mining(monero_daemon* daemon, const char* address, const uint64_t* num_threads, const bool* is_background, const bool* ignore_battery) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->start_mining(safe_str(address), optional_of(num_threads), optional_of(is_background), optional_of(ignore_battery)); });
}

monero_result monero_daemon_stop_mining(monero_daemon* daemon) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->stop_mining(); });
}

monero_result monero_daemon_generate_blocks(monero_daemon* daemon, const char* wallet_address, uint64_t num_blocks, const char* prev_block_hash, const uint32_t* starting_nonce, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] {
    boost::optional<std::string> prev_hash;
    if (prev_block_hash != nullptr) prev_hash = std::string(prev_block_hash);
    *out_json = dup_json(daemon->rpc->generate_blocks(safe_str(wallet_address), num_blocks, prev_hash, optional_of(starting_nonce)));
  });
}

monero_result monero_daemon_submit_block(monero_daemon* daemon, const char* block_blob) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->submit_block(safe_str(block_blob)); });
}

monero_result monero_daemon_submit_blocks(monero_daemon* daemon, const char* const* block_blobs, size_t num_block_blobs) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->submit_blocks(string_array(block_blobs, num_block_blobs)); });
}

// ----------------------------------- BLOCKS ---------------------------------

monero_result monero_daemon_get_last_block_header(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_last_block_header()); });
}

monero_result monero_daemon_get_block_header_by_hash(monero_daemon* daemon, const char* block_hash, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_block_header_by_hash(safe_str(block_hash))); });
}

monero_result monero_daemon_get_block_header_by_height(monero_daemon* daemon, uint64_t height, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_block_header_by_height(height)); });
}

monero_result monero_daemon_get_block_headers_by_range(monero_daemon* daemon, uint64_t start_height, uint64_t end_height, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_block_headers_by_range(start_height, end_height))); });
}

monero_result monero_daemon_get_block_by_hash(monero_daemon* daemon, const char* block_hash, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_block_by_hash(safe_str(block_hash))); });
}

monero_result monero_daemon_get_blocks_by_hash(monero_daemon* daemon, const char* const* block_hashes, size_t num_block_hashes, uint64_t start_height, bool prune, uint64_t max_block_count, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] {
    *out_json = dup_json(daemon->rpc->get_blocks_by_hash(string_array(block_hashes, num_block_hashes), start_height, prune, max_block_count));
  });
}

monero_result monero_daemon_get_block_by_height(monero_daemon* daemon, uint64_t height, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_block_by_height(height)); });
}

monero_result monero_daemon_get_blocks_by_height(monero_daemon* daemon, const uint64_t* heights, size_t num_heights, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_blocks_by_height(array_of(heights, num_heights)))); });
}

monero_result monero_daemon_get_blocks_by_range(monero_daemon* daemon, const uint64_t* start_height, const uint64_t* end_height, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_blocks_by_range(optional_of(start_height), optional_of(end_height)))); });
}

monero_result monero_daemon_get_blocks_by_range_chunked(monero_daemon* daemon, const uint64_t* start_height, const uint64_t* end_height, const uint64_t* max_chunk_size, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] {
    *out_json = dup_string(json_of_list(daemon->rpc->get_blocks_by_range_chunked(optional_of(start_height), optional_of(end_height), optional_of(max_chunk_size))));
  });
}

monero_result monero_daemon_get_block_hashes(monero_daemon* daemon, const char* const* block_hashes, size_t num_block_hashes, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_block_hashes(string_array(block_hashes, num_block_hashes))); });
}

monero_result monero_daemon_wait_for_next_block_header(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->wait_for_next_block_header()); });
}

// ---------------------------- TRANSACTIONS ----------------------------------

monero_result monero_daemon_get_tx(monero_daemon* daemon, const char* tx_hash, bool prune, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_tx(safe_str(tx_hash), prune)); });
}

monero_result monero_daemon_get_txs(monero_daemon* daemon, const char* const* tx_hashes, size_t num_tx_hashes, bool prune, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_txs(string_array(tx_hashes, num_tx_hashes), prune))); });
}

monero_result monero_daemon_get_tx_hex(monero_daemon* daemon, const char* tx_hash, bool prune, char** out_tx_hex) {
  if (!require(daemon, "daemon") || !require(out_tx_hex, "out_tx_hex")) return MONERO_ERROR;
  *out_tx_hex = nullptr;
  return guard([&] {
    boost::optional<std::string> hex = daemon->rpc->get_tx_hex(safe_str(tx_hash), prune);
    *out_tx_hex = hex ? dup_string(*hex) : nullptr;
  });
}

monero_result monero_daemon_get_tx_hexes(monero_daemon* daemon, const char* const* tx_hashes, size_t num_tx_hashes, bool prune, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_strings(daemon->rpc->get_tx_hexes(string_array(tx_hashes, num_tx_hashes), prune))); });
}

monero_result monero_daemon_get_miner_tx_sum(monero_daemon* daemon, uint64_t height, uint64_t num_blocks, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_miner_tx_sum(height, num_blocks)); });
}

monero_result monero_daemon_get_fee_estimate(monero_daemon* daemon, uint64_t grace_blocks, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_fee_estimate(grace_blocks)); });
}

monero_result monero_daemon_submit_tx_hex(monero_daemon* daemon, const char* tx_hex, bool do_not_relay, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->submit_tx_hex(safe_str(tx_hex), do_not_relay)); });
}

monero_result monero_daemon_relay_tx_by_hash(monero_daemon* daemon, const char* tx_hash) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->relay_tx_by_hash(safe_str(tx_hash)); });
}

monero_result monero_daemon_relay_txs_by_hash(monero_daemon* daemon, const char* const* tx_hashes, size_t num_tx_hashes) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->relay_txs_by_hash(string_array(tx_hashes, num_tx_hashes)); });
}

// -------------------------------- TX POOL -----------------------------------

monero_result monero_daemon_get_tx_pool(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_tx_pool())); });
}

monero_result monero_daemon_get_tx_pool_hashes(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_strings(daemon->rpc->get_tx_pool_hashes())); });
}

monero_result monero_daemon_get_tx_pool_backlog(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_backlog(daemon->rpc->get_tx_pool_backlog())); });
}

monero_result monero_daemon_get_tx_pool_stats(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_tx_pool_stats()); });
}

monero_result monero_daemon_flush_tx_pool(monero_daemon* daemon) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->flush_tx_pool(); });
}

monero_result monero_daemon_flush_tx_pool_hashes(monero_daemon* daemon, const char* const* tx_hashes, size_t num_tx_hashes) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->flush_tx_pool(string_array(tx_hashes, num_tx_hashes)); });
}

monero_result monero_daemon_flush_tx_pool_hash(monero_daemon* daemon, const char* tx_hash) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->flush_tx_pool(safe_str(tx_hash)); });
}

// --------------------------- KEY IMAGES AND OUTPUTS -------------------------

monero_result monero_daemon_get_key_image_spent_status(monero_daemon* daemon, const char* key_image, monero_key_image_spent_status* out_status) {
  if (!require(daemon, "daemon") || !require(out_status, "out_status")) return MONERO_ERROR;
  return guard([&] { *out_status = to_c_status(daemon->rpc->get_key_image_spent_status(safe_str(key_image))); });
}

monero_result monero_daemon_get_key_image_spent_statuses(monero_daemon* daemon, const char* const* key_images, size_t num_key_images, monero_key_image_spent_status** out_statuses, size_t* out_count) {
  if (!require(daemon, "daemon") || !require(out_statuses, "out_statuses") || !require(out_count, "out_count")) return MONERO_ERROR;
  *out_statuses = nullptr;
  *out_count = 0;
  return guard([&] {
    std::vector<monero_key_image_spent_status> statuses;
    for (monero::monero_key_image_spent_status status : daemon->rpc->get_key_image_spent_statuses(string_array(key_images, num_key_images))) {
      statuses.push_back(to_c_status(status));
    }
    *out_statuses = dup_array(statuses, out_count);
  });
}

monero_result monero_daemon_get_output_indices(monero_daemon* daemon, const char* tx_hash, uint64_t** out_indices, size_t* out_count) {
  if (!require(daemon, "daemon") || !require(out_indices, "out_indices") || !require(out_count, "out_count")) return MONERO_ERROR;
  *out_indices = nullptr;
  *out_count = 0;
  return guard([&] { *out_indices = dup_array(daemon->rpc->get_output_indices(safe_str(tx_hash)), out_count); });
}

monero_result monero_daemon_get_outputs(monero_daemon* daemon, const char* outputs_json, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] {
    std::vector<monero::monero_output> outputs;
    for (const auto& output : parse_list<monero::monero_output>(safe_str(outputs_json))) outputs.push_back(*output);
    *out_json = dup_string(json_of_list(daemon->rpc->get_outputs(outputs)));
  });
}

monero_result monero_daemon_get_output_histogram(monero_daemon* daemon, const uint64_t* amounts, size_t num_amounts, const int32_t* min_count, const int32_t* max_count, const bool* is_unlocked, const int32_t* recent_cutoff, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] {
    *out_json = dup_string(json_of_list(daemon->rpc->get_output_histogram(array_of(amounts, num_amounts), optional_of(min_count), optional_of(max_count), optional_of(is_unlocked), optional_of(recent_cutoff))));
  });
}

monero_result monero_daemon_get_output_distribution(monero_daemon* daemon, const uint64_t* amounts, size_t num_amounts, const bool* is_cumulative, const uint64_t* start_height, const uint64_t* end_height, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] {
    *out_json = dup_string(json_of_list(daemon->rpc->get_output_distribution(array_of(amounts, num_amounts), optional_of(is_cumulative), optional_of(start_height), optional_of(end_height))));
  });
}

// ---------------------------------- BANDWIDTH -------------------------------

monero_result monero_daemon_get_download_limit(monero_daemon* daemon, int32_t* out_limit) {
  if (!require(daemon, "daemon") || !require(out_limit, "out_limit")) return MONERO_ERROR;
  return guard([&] { *out_limit = daemon->rpc->get_download_limit(); });
}

monero_result monero_daemon_set_download_limit(monero_daemon* daemon, int32_t limit, int32_t* out_limit) {
  if (!require(daemon, "daemon") || !require(out_limit, "out_limit")) return MONERO_ERROR;
  return guard([&] { *out_limit = daemon->rpc->set_download_limit(limit); });
}

monero_result monero_daemon_reset_download_limit(monero_daemon* daemon, int32_t* out_limit) {
  if (!require(daemon, "daemon") || !require(out_limit, "out_limit")) return MONERO_ERROR;
  return guard([&] { *out_limit = daemon->rpc->reset_download_limit(); });
}

monero_result monero_daemon_get_upload_limit(monero_daemon* daemon, int32_t* out_limit) {
  if (!require(daemon, "daemon") || !require(out_limit, "out_limit")) return MONERO_ERROR;
  return guard([&] { *out_limit = daemon->rpc->get_upload_limit(); });
}

monero_result monero_daemon_set_upload_limit(monero_daemon* daemon, int32_t limit, int32_t* out_limit) {
  if (!require(daemon, "daemon") || !require(out_limit, "out_limit")) return MONERO_ERROR;
  return guard([&] { *out_limit = daemon->rpc->set_upload_limit(limit); });
}

monero_result monero_daemon_reset_upload_limit(monero_daemon* daemon, int32_t* out_limit) {
  if (!require(daemon, "daemon") || !require(out_limit, "out_limit")) return MONERO_ERROR;
  return guard([&] { *out_limit = daemon->rpc->reset_upload_limit(); });
}

// ----------------------------------- PEERS ----------------------------------

monero_result monero_daemon_get_peers(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_peers())); });
}

monero_result monero_daemon_get_known_peers(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_known_peers())); });
}

monero_result monero_daemon_get_public_peers(monero_daemon* daemon, bool include_offline, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_public_peers(include_offline))); });
}

monero_result monero_daemon_set_outgoing_peer_limit(monero_daemon* daemon, int32_t limit) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->set_outgoing_peer_limit(limit); });
}

monero_result monero_daemon_set_incoming_peer_limit(monero_daemon* daemon, int32_t limit) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->set_incoming_peer_limit(limit); });
}

monero_result monero_daemon_get_peer_bans(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_string(json_of_list(daemon->rpc->get_peer_bans())); });
}

monero_result monero_daemon_set_peer_bans(monero_daemon* daemon, const char* bans_json) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->set_peer_bans(parse_list<monero::monero_ban>(safe_str(bans_json))); });
}

monero_result monero_daemon_set_peer_ban(monero_daemon* daemon, const char* ban_json) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->set_peer_ban(gen_utils::deserialize<monero::monero_ban>(safe_str(ban_json))); });
}

monero_result monero_daemon_get_peer_ban(monero_daemon* daemon, const char* address, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->get_peer_ban(safe_str(address))); });
}

// ----------------------------------- ADMIN ----------------------------------

monero_result monero_daemon_prune_blockchain(monero_daemon* daemon, bool check, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->prune_blockchain(check)); });
}

monero_result monero_daemon_save_blockchain(monero_daemon* daemon) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->save_blockchain(); });
}

monero_result monero_daemon_pop_blocks(monero_daemon* daemon, uint64_t num_blocks, uint64_t* out_height) {
  if (!require(daemon, "daemon") || !require(out_height, "out_height")) return MONERO_ERROR;
  return guard([&] { *out_height = daemon->rpc->pop_blocks(num_blocks); });
}

monero_result monero_daemon_flush_cache(monero_daemon* daemon, bool bad_blocks) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->flush_cache(bad_blocks); });
}

monero_result monero_daemon_set_bootstrap_daemon(monero_daemon* daemon, const char* address, const char* username, const char* password, const char* proxy) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->set_bootstrap_daemon(safe_str(address), safe_str(username), safe_str(password), safe_str(proxy)); });
}

monero_result monero_daemon_remove_bootstrap_daemon(monero_daemon* daemon) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->remove_bootstrap_daemon(); });
}

monero_result monero_daemon_set_log_hash_rate(monero_daemon* daemon, bool is_visible) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->set_log_hash_rate(is_visible); });
}

monero_result monero_daemon_set_log_level(monero_daemon* daemon, int32_t level) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->set_log_level(level); });
}

monero_result monero_daemon_set_log_categories(monero_daemon* daemon, const char* categories, char** out_categories) {
  if (!require(daemon, "daemon") || !require(out_categories, "out_categories")) return MONERO_ERROR;
  *out_categories = nullptr;
  return guard([&] { *out_categories = dup_string(daemon->rpc->set_log_categories(safe_str(categories))); });
}

monero_result monero_daemon_check_for_update(monero_daemon* daemon, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->check_for_update()); });
}

monero_result monero_daemon_download_update(monero_daemon* daemon, const char* path, char** out_json) {
  if (!require(daemon, "daemon") || !require(out_json, "out_json")) return MONERO_ERROR;
  *out_json = nullptr;
  return guard([&] { *out_json = dup_json(daemon->rpc->download_update(safe_str(path))); });
}

monero_result monero_daemon_stop(monero_daemon* daemon) {
  if (!require(daemon, "daemon")) return MONERO_ERROR;
  return guard([&] { daemon->rpc->stop(); });
}

} // extern "C"

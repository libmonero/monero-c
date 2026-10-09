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

#include "wallet/monero_c_wallet.h"
#include "common/monero_c_common.h"
#include "wallet/monero_wallet_full.h"
#include "wallet/monero_wallet_keys.h"
#include "wallet/monero_wallet_rpc.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <boost/optional.hpp>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using monero_c::array_of;
using monero_c::check_json_depth;
using monero_c::check_mnemonic_length;
using monero_c::dup_string;
using monero_c::guard;
using monero_c::json_of_list;
using monero_c::json_of_strings;
using monero_c::network_of;
using monero_c::optional_of;
using monero_c::require;
using monero_c::reset_out;
using monero_c::safe_str;
using monero_c::string_array;

// holds the base class, so full and keys-only wallets share one handle type
// monero-cpp has `using namespace monero`, so the C wallet names are written with ::
struct monero_wallet {
  std::shared_ptr<monero::monero_wallet> wallet;
  bool regtest = false;  // monero-cpp has no regtest network, so the handle keeps the flag
};

namespace {

// forwards the wallet's notifications to the C callbacks of a listener
class wallet_listener_adapter : public monero::monero_wallet_listener {
public:
  ::monero_wallet_listener* owner = nullptr;  // the C handle, not the base class of the same name
  monero_wallet_listener_callbacks callbacks = {};

  void on_sync_progress(uint64_t height, uint64_t start_height, uint64_t end_height, double percent_done, const std::string& message) override {
    if (callbacks.on_sync_progress == nullptr) return;
    callbacks.on_sync_progress(callbacks.user_data, height, start_height, end_height, percent_done, message.c_str());
  }

  void on_new_block(uint64_t height) override {
    if (callbacks.on_new_block == nullptr) return;
    callbacks.on_new_block(callbacks.user_data, height);
  }

  void on_balances_changed(uint64_t new_balance, uint64_t new_unlocked_balance) override {
    if (callbacks.on_balances_changed == nullptr) return;
    callbacks.on_balances_changed(callbacks.user_data, new_balance, new_unlocked_balance);
  }

  void on_output_received(const monero::monero_output_wallet& output) override {
    if (callbacks.on_output_received == nullptr) return;
    std::string json = output.serialize();
    callbacks.on_output_received(callbacks.user_data, json.c_str());
  }

  void on_output_spent(const monero::monero_output_wallet& output) override {
    if (callbacks.on_output_spent == nullptr) return;
    std::string json = output.serialize();
    callbacks.on_output_spent(callbacks.user_data, json.c_str());
  }
};

// copies a mnemonic argument, which can't be longer than a valid one
std::string mnemonic_of(const char* mnemonic) {
  std::string text(mnemonic);
  check_mnemonic_length(text);
  return text;
}

// monero-cpp reads most fields with get(), so every field is set, empty when unused
monero::monero_wallet_config make_config(const char* path, const char* password, int32_t network_type, const char* language) {
  monero::monero_wallet_config config;
  config.m_path = safe_str(path);
  config.m_password = safe_str(password);
  config.m_network_type = network_of<monero::monero_network_type>(network_type);
  std::string language_str = safe_str(language);
  config.m_language = language_str.empty() ? std::string("English") : language_str;
  config.m_server = nullptr;
  config.m_is_trusted_daemon = false;
  config.m_seed = std::string();
  config.m_seed_offset = std::string();
  config.m_primary_address = std::string();
  config.m_private_view_key = std::string();
  config.m_private_spend_key = std::string();
  config.m_is_multisig = false;
  config.m_regtest = network_type == MONERO_UTILS_NETWORK_REGTEST;
  return config;
}

// takes ownership of a wallet returned by monero-cpp, created or opened on network_type
::monero_wallet* wrap(std::unique_ptr<monero::monero_wallet> wallet, int32_t network_type) {
  std::unique_ptr<::monero_wallet> handle(new ::monero_wallet());
  handle->wallet = std::shared_ptr<monero::monero_wallet>(std::move(wallet));
  handle->regtest = network_type == MONERO_UTILS_NETWORK_REGTEST;
  return handle.release();
}

// monero-cpp's signature key enum has the same values as the C enum
monero::monero_message_signature_type to_signature_type(int32_t signature_type) {
  if (signature_type < MONERO_MESSAGE_SIGN_WITH_SPEND_KEY || signature_type > MONERO_MESSAGE_SIGN_WITH_VIEW_KEY) throw std::runtime_error("unknown signature type");
  return static_cast<monero::monero_message_signature_type>(signature_type);
}

// parses a JSON array into one monero-cpp model per element. The message is thrown if json is not an array
template <class T>
std::vector<std::shared_ptr<T>> parse_list(const std::string& json, const char* message) {
  check_json_depth(json);
  rapidjson::Document doc;
  if (doc.Parse(json.c_str()).HasParseError() || !doc.IsArray()) throw std::runtime_error(message);
  std::vector<std::shared_ptr<T>> items;
  for (const auto& item : doc.GetArray()) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    item.Accept(writer);
    items.push_back(T::deserialize(buffer.GetString()));
  }
  return items;
}

std::vector<std::shared_ptr<monero::monero_key_image>> parse_key_images(const std::string& json) {
  return parse_list<monero::monero_key_image>(json, "key images must be a JSON array");
}

// the metadata of a transaction created without relaying. monero-cpp can't deserialize a created transaction
// (it has an outgoingTransfer), so only the metadata is read
std::string tx_metadata_of(const rapidjson::Value& tx) {
  if (!tx.IsObject()) throw std::runtime_error("tx must be a JSON object");
  auto it = tx.FindMember("metadata");
  if (it == tx.MemberEnd() || !it->value.IsString() || it->value.GetStringLength() == 0) throw std::runtime_error("tx metadata is not initialized");
  return it->value.GetString();
}

std::string tx_metadata_of_json(const std::string& json) {
  check_json_depth(json);
  rapidjson::Document doc;
  if (doc.Parse(json.c_str()).HasParseError()) throw std::runtime_error("tx must be a JSON object");
  return tx_metadata_of(doc);
}

std::vector<std::string> tx_metadatas_of_json(const std::string& json) {
  check_json_depth(json);
  rapidjson::Document doc;
  if (doc.Parse(json.c_str()).HasParseError() || !doc.IsArray()) throw std::runtime_error("txs must be a JSON array");
  std::vector<std::string> metadatas;
  for (const auto& tx : doc.GetArray()) metadatas.push_back(tx_metadata_of(tx));
  return metadatas;
}

// parses a JSON object into a monero-cpp model. The name is the argument, for the error message
template <class T>
std::shared_ptr<T> model_json(const char* json, const char* name) {
  const char* p = json;
  while (p != nullptr && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  if (p == nullptr || *p != '{') throw std::runtime_error(std::string(name) + " must be a JSON object");
  std::string text(json);
  check_json_depth(text);
  std::shared_ptr<T> model = T::deserialize(text);
  if (model == nullptr) throw std::runtime_error(std::string(name) + " must be a JSON object");
  return model;
}

// parses an optional query. NULL gives the default query, which matches everything
template <class T>
std::shared_ptr<T> query_of(const char* json) {
  return json != nullptr ? model_json<T>(json, "query") : std::make_shared<T>();
}

// shares a wallet with a new handle, for a wallet that is also owned by another handle
::monero_wallet* wrap_shared(std::shared_ptr<monero::monero_wallet> wallet) {
  std::unique_ptr<::monero_wallet> handle(new ::monero_wallet());
  handle->wallet = std::move(wallet);
  return handle.release();
}

// the RPC methods are only on monero_wallet_rpc, so the handle must hold an RPC wallet
monero::monero_wallet_rpc& as_rpc(::monero_wallet* wallet) {
  monero::monero_wallet_rpc* rpc = dynamic_cast<monero::monero_wallet_rpc*>(wallet->wallet.get());
  if (rpc == nullptr) throw std::runtime_error("not an RPC wallet");
  return *rpc;
}

// a connection to a monero-wallet-rpc server, to open or create wallets on it
std::shared_ptr<monero::monero_wallet_rpc> rpc_client(const char* uri, const char* username, const char* password) {
  return std::make_shared<monero::monero_wallet_rpc>(std::string(uri), safe_str(username), safe_str(password));
}

// the config of a wallet that monero-wallet-rpc creates, with the name, the password and the language
std::shared_ptr<monero::monero_wallet_config> rpc_config(const char* name, const char* wallet_password, const char* language) {
  auto config = std::make_shared<monero::monero_wallet_config>();
  config->m_path = std::string(name);
  config->m_password = safe_str(wallet_password);
  if (language != nullptr && language[0] != '\0') config->m_language = std::string(language);
  return config;
}

// creates a wallet on a monero-wallet-rpc server and returns a handle to it
::monero_wallet* create_rpc_wallet(const char* uri, const char* username, const char* password, const std::shared_ptr<monero::monero_wallet_config>& config) {
  std::shared_ptr<monero::monero_wallet_rpc> rpc = rpc_client(uri, username, password);
  rpc->create_wallet(config);
  return wrap_shared(rpc);
}

} // namespace

struct monero_wallet_listener {
  wallet_listener_adapter adapter;
  std::weak_ptr<monero::monero_wallet> registered_with;
  std::mutex registration_mutex;  // guards registered_with, never held across a monero-cpp call
};

namespace {

// monero-cpp adds the listener for a sync and removes it after, so a listener that the handle
// already tracks uses the plain sync. Otherwise the removal would unregister it
monero::monero_sync_result sync_with_listener(::monero_wallet* wallet, ::monero_wallet_listener* listener, const uint64_t* start_height) {
  std::shared_ptr<monero::monero_wallet> previous;
  {
    std::lock_guard<std::mutex> lock(listener->registration_mutex);
    previous = listener->registered_with.lock();
  }
  if (previous && previous != wallet->wallet) throw std::runtime_error("listener is already registered with another wallet");
  if (previous) return start_height ? wallet->wallet->sync(*start_height) : wallet->wallet->sync();
  return start_height ? wallet->wallet->sync(*start_height, listener->adapter) : wallet->wallet->sync(listener->adapter);
}

} // namespace

extern "C" {

// -------------------------------- WALLET ------------------------------------

monero_result monero_wallet_create_random(const char* path, const char* password, int32_t network_type, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(path, "path") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_wallet_config config = make_config(path, password, network_type, language);
    std::unique_ptr<monero::monero_wallet_full> wallet(monero::monero_wallet_full::create_wallet(config));
    // a new wallet starts from an estimate of the current date, which is past the height of a regtest chain
    if (network_type == MONERO_UTILS_NETWORK_REGTEST) {
      wallet->set_restore_height(0);
      wallet->save();
    }
    *out_wallet = wrap(std::move(wallet), network_type);
  });
}

monero_result monero_wallet_create_from_seed(const char* path, const char* password, int32_t network_type, const char* seed, const char* seed_offset, uint64_t restore_height, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(path, "path") || !require(seed, "seed") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_wallet_config config = make_config(path, password, network_type, language);
    config.m_seed = mnemonic_of(seed);
    config.m_seed_offset = safe_str(seed_offset);
    config.m_restore_height = restore_height;
    *out_wallet = wrap(std::unique_ptr<monero::monero_wallet_full>(monero::monero_wallet_full::create_wallet(config)), network_type);
  });
}

monero_result monero_wallet_create_from_keys(const char* path, const char* password, int32_t network_type, const char* address, const char* private_view_key, const char* private_spend_key, uint64_t restore_height, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(path, "path") || !require(address, "address") || !require(private_view_key, "private_view_key") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_wallet_config config = make_config(path, password, network_type, language);
    config.m_primary_address = std::string(address);
    config.m_private_view_key = std::string(private_view_key);
    config.m_private_spend_key = safe_str(private_spend_key);
    config.m_restore_height = restore_height;
    *out_wallet = wrap(std::unique_ptr<monero::monero_wallet_full>(monero::monero_wallet_full::create_wallet(config)), network_type);
  });
}

monero_result monero_wallet_open(const char* path, const char* password, int32_t network_type, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(path, "path") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    *out_wallet = wrap(std::unique_ptr<monero::monero_wallet_full>(monero::monero_wallet_full::open_wallet(std::string(path), safe_str(password), network_of<monero::monero_network_type>(network_type), network_type == MONERO_UTILS_NETWORK_REGTEST)), network_type);
  });
}

monero_result monero_wallet_exists(const char* path, bool* out_exists) {
  if (!require(path, "path") || !require(out_exists, "out_exists")) return MONERO_ERROR;
  return guard([&] { *out_exists = monero::monero_wallet_full::wallet_exists(std::string(path)); });
}

monero_result monero_wallet_get_seed_languages(char** out_json) {
  reset_out(out_json);
  if (!require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_strings(monero::monero_wallet_full::get_seed_languages())); });
}

monero_result monero_wallet_keys_create_random(int32_t network_type, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_wallet_config config = make_config(nullptr, nullptr, network_type, language);
    *out_wallet = wrap(std::unique_ptr<monero::monero_wallet>(monero::monero_wallet_keys::create_wallet_random(config)), network_type);
  });
}

monero_result monero_wallet_keys_create_from_seed(int32_t network_type, const char* seed, const char* seed_offset, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(seed, "seed") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_wallet_config config = make_config(nullptr, nullptr, network_type, language);
    config.m_seed = mnemonic_of(seed);
    config.m_seed_offset = safe_str(seed_offset);
    *out_wallet = wrap(std::unique_ptr<monero::monero_wallet>(monero::monero_wallet_keys::create_wallet_from_seed(config)), network_type);
  });
}

monero_result monero_wallet_keys_create_from_keys(int32_t network_type, const char* address, const char* private_view_key, const char* private_spend_key, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(address, "address") || !require(private_view_key, "private_view_key") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_wallet_config config = make_config(nullptr, nullptr, network_type, language);
    config.m_primary_address = std::string(address);
    config.m_private_view_key = std::string(private_view_key);
    config.m_private_spend_key = safe_str(private_spend_key);
    *out_wallet = wrap(std::unique_ptr<monero::monero_wallet>(monero::monero_wallet_keys::create_wallet_from_keys(config)), network_type);
  });
}

monero_result monero_wallet_save(::monero_wallet* wallet) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->save(); });
}

monero_result monero_wallet_close(::monero_wallet* wallet, bool save) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->close(save); });
}

void monero_wallet_free(::monero_wallet* wallet) {
  delete wallet;
}

monero_result monero_wallet_get_network_type(::monero_wallet* wallet, monero_utils_network_type* out_network_type) {
  if (!require(wallet, "wallet") || !require(out_network_type, "out_network_type")) return MONERO_ERROR;
  return guard([&] {
    // monero-cpp runs regtest as mainnet with a flag, so the handle tells regtest apart
    *out_network_type = wallet->regtest ? MONERO_UTILS_NETWORK_REGTEST : static_cast<monero_utils_network_type>(wallet->wallet->get_network_type());
  });
}

monero_result monero_wallet_is_view_only(::monero_wallet* wallet, bool* out_view_only) {
  if (!require(wallet, "wallet") || !require(out_view_only, "out_view_only")) return MONERO_ERROR;
  return guard([&] { *out_view_only = wallet->wallet->is_view_only(); });
}

monero_result monero_wallet_is_multisig(::monero_wallet* wallet, bool* out_multisig) {
  if (!require(wallet, "wallet") || !require(out_multisig, "out_multisig")) return MONERO_ERROR;
  return guard([&] { *out_multisig = wallet->wallet->is_multisig(); });
}

monero_result monero_wallet_get_seed(::monero_wallet* wallet, char** out_seed) {
  reset_out(out_seed);
  if (!require(wallet, "wallet") || !require(out_seed, "out_seed")) return MONERO_ERROR;
  return guard([&] { *out_seed = dup_string(wallet->wallet->get_seed()); });
}

monero_result monero_wallet_get_seed_language(::monero_wallet* wallet, char** out_language) {
  reset_out(out_language);
  if (!require(wallet, "wallet") || !require(out_language, "out_language")) return MONERO_ERROR;
  return guard([&] { *out_language = dup_string(wallet->wallet->get_seed_language()); });
}

monero_result monero_wallet_get_primary_address(::monero_wallet* wallet, char** out_address) {
  reset_out(out_address);
  if (!require(wallet, "wallet") || !require(out_address, "out_address")) return MONERO_ERROR;
  return guard([&] { *out_address = dup_string(wallet->wallet->get_primary_address()); });
}

monero_result monero_wallet_get_private_view_key(::monero_wallet* wallet, char** out_key) {
  reset_out(out_key);
  if (!require(wallet, "wallet") || !require(out_key, "out_key")) return MONERO_ERROR;
  return guard([&] { *out_key = dup_string(wallet->wallet->get_private_view_key()); });
}

monero_result monero_wallet_get_private_spend_key(::monero_wallet* wallet, char** out_key) {
  reset_out(out_key);
  if (!require(wallet, "wallet") || !require(out_key, "out_key")) return MONERO_ERROR;
  return guard([&] { *out_key = dup_string(wallet->wallet->get_private_spend_key()); });
}

monero_result monero_wallet_get_height(::monero_wallet* wallet, uint64_t* out_height) {
  if (!require(wallet, "wallet") || !require(out_height, "out_height")) return MONERO_ERROR;
  return guard([&] { *out_height = wallet->wallet->get_height(); });
}

monero_result monero_wallet_get_balance(::monero_wallet* wallet, uint64_t* out_balance) {
  if (!require(wallet, "wallet") || !require(out_balance, "out_balance")) return MONERO_ERROR;
  return guard([&] { *out_balance = wallet->wallet->get_balance(); });
}

monero_result monero_wallet_get_unlocked_balance(::monero_wallet* wallet, uint64_t* out_balance) {
  if (!require(wallet, "wallet") || !require(out_balance, "out_balance")) return MONERO_ERROR;
  return guard([&] { *out_balance = wallet->wallet->get_unlocked_balance(); });
}

monero_result monero_wallet_get_account_balance(::monero_wallet* wallet, uint32_t account_idx, uint64_t* out_balance) {
  if (!require(wallet, "wallet") || !require(out_balance, "out_balance")) return MONERO_ERROR;
  return guard([&] { *out_balance = wallet->wallet->get_balance(account_idx); });
}

monero_result monero_wallet_get_subaddress_balance(::monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, uint64_t* out_balance) {
  if (!require(wallet, "wallet") || !require(out_balance, "out_balance")) return MONERO_ERROR;
  return guard([&] { *out_balance = wallet->wallet->get_balance(account_idx, subaddress_idx); });
}

monero_result monero_wallet_get_account_unlocked_balance(::monero_wallet* wallet, uint32_t account_idx, uint64_t* out_balance) {
  if (!require(wallet, "wallet") || !require(out_balance, "out_balance")) return MONERO_ERROR;
  return guard([&] { *out_balance = wallet->wallet->get_unlocked_balance(account_idx); });
}

monero_result monero_wallet_get_subaddress_unlocked_balance(::monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, uint64_t* out_balance) {
  if (!require(wallet, "wallet") || !require(out_balance, "out_balance")) return MONERO_ERROR;
  return guard([&] { *out_balance = wallet->wallet->get_unlocked_balance(account_idx, subaddress_idx); });
}

monero_result monero_wallet_get_address(::monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, char** out_address) {
  reset_out(out_address);
  if (!require(wallet, "wallet") || !require(out_address, "out_address")) return MONERO_ERROR;
  return guard([&] { *out_address = dup_string(wallet->wallet->get_address(account_idx, subaddress_idx)); });
}

monero_result monero_wallet_get_integrated_address(::monero_wallet* wallet, const char* standard_address, const char* payment_id, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->get_integrated_address(safe_str(standard_address), safe_str(payment_id)).serialize()); });
}

monero_result monero_wallet_decode_integrated_address(::monero_wallet* wallet, const char* integrated_address, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(integrated_address, "integrated_address") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->decode_integrated_address(std::string(integrated_address)).serialize()); });
}

monero_result monero_wallet_get_account(::monero_wallet* wallet, uint32_t account_idx, bool include_subaddresses, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->get_account(account_idx, include_subaddresses).serialize()); });
}

monero_result monero_wallet_get_subaddresses(::monero_wallet* wallet, uint32_t account_idx, const uint32_t* subaddress_indices, size_t num_subaddress_indices, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    auto indices = array_of(subaddress_indices, num_subaddress_indices, "subaddress_indices");
    *out_json = dup_string(json_of_list(wallet->wallet->get_subaddresses(account_idx, indices)));
  });
}

monero_result monero_wallet_sign_message(::monero_wallet* wallet, const char* message, int32_t signature_type, uint32_t account_idx, uint32_t subaddress_idx, char** out_signature) {
  reset_out(out_signature);
  if (!require(wallet, "wallet") || !require(message, "message") || !require(out_signature, "out_signature")) return MONERO_ERROR;
  return guard([&] { *out_signature = dup_string(wallet->wallet->sign_message(std::string(message), to_signature_type(signature_type), account_idx, subaddress_idx)); });
}

monero_result monero_wallet_verify_message(::monero_wallet* wallet, const char* message, const char* address, const char* signature, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(message, "message") || !require(address, "address") || !require(signature, "signature") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->verify_message(std::string(message), std::string(address), std::string(signature)).serialize()); });
}

monero_result monero_wallet_add_address_book_entry(::monero_wallet* wallet, const char* address, const char* description, uint64_t* out_index) {
  if (!require(wallet, "wallet") || !require(address, "address") || !require(out_index, "out_index")) return MONERO_ERROR;
  return guard([&] { *out_index = wallet->wallet->add_address_book_entry(std::string(address), safe_str(description)); });
}

monero_result monero_wallet_edit_address_book_entry(::monero_wallet* wallet, uint64_t index, const char* address, const char* description) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->edit_address_book_entry(index, address != nullptr, safe_str(address), description != nullptr, safe_str(description)); });
}

monero_result monero_wallet_delete_address_book_entry(::monero_wallet* wallet, uint64_t index) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->delete_address_book_entry(index); });
}

monero_result monero_wallet_get_address_book_entries(::monero_wallet* wallet, const uint64_t* indices, size_t num_indices, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_list(wallet->wallet->get_address_book_entries(array_of(indices, num_indices, "indices")))); });
}

monero_result monero_wallet_change_password(::monero_wallet* wallet, const char* old_password, const char* new_password) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->change_password(safe_str(old_password), safe_str(new_password)); });
}

monero_result monero_wallet_create_account(::monero_wallet* wallet, const char* label, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->create_account(safe_str(label)).serialize()); });
}

monero_result monero_wallet_create_subaddress(::monero_wallet* wallet, uint32_t account_idx, const char* label, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->create_subaddress(account_idx, safe_str(label)).serialize()); });
}

monero_result monero_wallet_get_accounts(::monero_wallet* wallet, bool include_subaddresses, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_list(wallet->wallet->get_accounts(include_subaddresses))); });
}

monero_result monero_wallet_get_address_index(::monero_wallet* wallet, const char* address, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(address, "address") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->get_address_index(std::string(address)).serialize()); });
}

monero_result monero_wallet_get_subaddress(::monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->get_subaddress(account_idx, subaddress_idx).serialize()); });
}

monero_result monero_wallet_set_subaddress_label(::monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, const char* label) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->set_subaddress_label(account_idx, subaddress_idx, safe_str(label)); });
}

monero_result monero_wallet_set_account_tag_label(::monero_wallet* wallet, const char* tag, const char* label) {
  if (!require(wallet, "wallet") || !require(tag, "tag")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->set_account_tag_label(std::string(tag), safe_str(label)); });
}

monero_result monero_wallet_get_account_tags(::monero_wallet* wallet, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_list(wallet->wallet->get_account_tags())); });
}

monero_result monero_wallet_tag_accounts(::monero_wallet* wallet, const char* tag, const uint32_t* account_indices, size_t num_account_indices) {
  if (!require(wallet, "wallet") || !require(tag, "tag")) return MONERO_ERROR;
  return guard([&] {
    auto accounts = array_of(account_indices, num_account_indices, "account_indices");
    wallet->wallet->tag_accounts(std::string(tag), accounts);
  });
}

monero_result monero_wallet_untag_accounts(::monero_wallet* wallet, const uint32_t* account_indices, size_t num_account_indices) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] {
    auto accounts = array_of(account_indices, num_account_indices, "account_indices");
    wallet->wallet->untag_accounts(accounts);
  });
}

monero_result monero_wallet_get_attribute(::monero_wallet* wallet, const char* key, bool* out_found, char** out_value) {
  reset_out(out_value);
  if (!require(wallet, "wallet") || !require(key, "key") || !require(out_found, "out_found") || !require(out_value, "out_value")) return MONERO_ERROR;
  return guard([&] {
    std::string value;
    *out_found = wallet->wallet->get_attribute(std::string(key), value);
    if (*out_found) *out_value = dup_string(value);
  });
}

monero_result monero_wallet_set_attribute(::monero_wallet* wallet, const char* key, const char* value) {
  if (!require(wallet, "wallet") || !require(key, "key")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->set_attribute(std::string(key), safe_str(value)); });
}

monero_result monero_wallet_get_default_fee_priority(::monero_wallet* wallet, ::monero_tx_priority* out_priority) {
  if (!require(wallet, "wallet") || !require(out_priority, "out_priority")) return MONERO_ERROR;
  return guard([&] { *out_priority = static_cast<::monero_tx_priority>(wallet->wallet->get_default_fee_priority()); });
}

monero_result monero_wallet_get_path(::monero_wallet* wallet, char** out_path) {
  reset_out(out_path);
  if (!require(wallet, "wallet") || !require(out_path, "out_path")) return MONERO_ERROR;
  return guard([&] { *out_path = dup_string(wallet->wallet->get_path()); });
}

monero_result monero_wallet_get_public_view_key(::monero_wallet* wallet, char** out_key) {
  reset_out(out_key);
  if (!require(wallet, "wallet") || !require(out_key, "out_key")) return MONERO_ERROR;
  return guard([&] { *out_key = dup_string(wallet->wallet->get_public_view_key()); });
}

monero_result monero_wallet_get_public_spend_key(::monero_wallet* wallet, char** out_key) {
  reset_out(out_key);
  if (!require(wallet, "wallet") || !require(out_key, "out_key")) return MONERO_ERROR;
  return guard([&] { *out_key = dup_string(wallet->wallet->get_public_spend_key()); });
}

monero_result monero_wallet_get_version(::monero_wallet* wallet, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->get_version().serialize()); });
}

monero_result monero_wallet_get_restore_height(::monero_wallet* wallet, uint64_t* out_height) {
  if (!require(wallet, "wallet") || !require(out_height, "out_height")) return MONERO_ERROR;
  return guard([&] { *out_height = wallet->wallet->get_restore_height(); });
}

monero_result monero_wallet_set_restore_height(::monero_wallet* wallet, uint64_t restore_height) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->set_restore_height(restore_height); });
}

monero_result monero_wallet_is_closed(::monero_wallet* wallet, bool* out_closed) {
  if (!require(wallet, "wallet") || !require(out_closed, "out_closed")) return MONERO_ERROR;
  return guard([&] { *out_closed = wallet->wallet->is_closed(); });
}

monero_result monero_wallet_export_key_images(::monero_wallet* wallet, bool all, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->export_key_images(all)->serialize()); });
}

monero_result monero_wallet_import_key_images(::monero_wallet* wallet, const char* key_images_json, uint64_t offset, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(key_images_json, "key_images_json") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->import_key_images(parse_key_images(key_images_json), offset)->serialize()); });
}

monero_result monero_wallet_export_outputs(::monero_wallet* wallet, bool all, char** out_hex) {
  reset_out(out_hex);
  if (!require(wallet, "wallet") || !require(out_hex, "out_hex")) return MONERO_ERROR;
  return guard([&] { *out_hex = dup_string(wallet->wallet->export_outputs(all)); });
}

monero_result monero_wallet_import_outputs(::monero_wallet* wallet, const char* outputs_hex, int32_t* out_num_imported) {
  if (!require(wallet, "wallet") || !require(outputs_hex, "outputs_hex") || !require(out_num_imported, "out_num_imported")) return MONERO_ERROR;
  return guard([&] { *out_num_imported = wallet->wallet->import_outputs(std::string(outputs_hex)); });
}

monero_result monero_wallet_freeze_output(::monero_wallet* wallet, const char* key_image) {
  if (!require(wallet, "wallet") || !require(key_image, "key_image")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->freeze_output(std::string(key_image)); });
}

monero_result monero_wallet_thaw_output(::monero_wallet* wallet, const char* key_image) {
  if (!require(wallet, "wallet") || !require(key_image, "key_image")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->thaw_output(std::string(key_image)); });
}

monero_result monero_wallet_is_output_frozen(::monero_wallet* wallet, const char* key_image, bool* out_frozen) {
  if (!require(wallet, "wallet") || !require(key_image, "key_image") || !require(out_frozen, "out_frozen")) return MONERO_ERROR;
  return guard([&] { *out_frozen = wallet->wallet->is_output_frozen(std::string(key_image)); });
}

monero_result monero_wallet_get_tx_note(::monero_wallet* wallet, const char* tx_hash, char** out_note) {
  reset_out(out_note);
  if (!require(wallet, "wallet") || !require(tx_hash, "tx_hash") || !require(out_note, "out_note")) return MONERO_ERROR;
  return guard([&] { *out_note = dup_string(wallet->wallet->get_tx_note(std::string(tx_hash))); });
}

monero_result monero_wallet_set_tx_note(::monero_wallet* wallet, const char* tx_hash, const char* note) {
  if (!require(wallet, "wallet") || !require(tx_hash, "tx_hash")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->set_tx_note(std::string(tx_hash), safe_str(note)); });
}

monero_result monero_wallet_get_tx_notes(::monero_wallet* wallet, const char* const* tx_hashes, size_t num_tx_hashes, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_strings(wallet->wallet->get_tx_notes(string_array(tx_hashes, num_tx_hashes, "tx_hashes")))); });
}

monero_result monero_wallet_set_tx_notes(::monero_wallet* wallet, const char* const* tx_hashes, const char* const* notes, size_t num_tx_hashes) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->set_tx_notes(string_array(tx_hashes, num_tx_hashes, "tx_hashes"), string_array(notes, num_tx_hashes, "notes")); });
}

monero_result monero_wallet_set_daemon_connection(::monero_wallet* wallet, const char* uri, const char* username, const char* password, const char* proxy_uri, bool is_trusted, bool ssl_verify) {
  if (!require(wallet, "wallet") || !require(uri, "uri")) return MONERO_ERROR;
  return guard([&] {
    // the connection object carries ssl_verify, the uri overload of monero-cpp doesn't take it
    std::shared_ptr<monero::monero_rpc_connection> connection = std::make_shared<monero::monero_rpc_connection>(std::string(uri), safe_str(username), safe_str(password), safe_str(proxy_uri));
    connection->m_ssl_verify = ssl_verify;
    wallet->wallet->set_daemon_connection(connection, boost::optional<bool>(is_trusted));
  });
}

monero_result monero_wallet_get_daemon_connection(::monero_wallet* wallet, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    std::shared_ptr<monero::monero_rpc_connection> connection = wallet->wallet->get_daemon_connection();
    if (connection) *out_json = dup_string(connection->serialize());
  });
}

monero_result monero_wallet_is_daemon_trusted(::monero_wallet* wallet, bool* out_trusted) {
  if (!require(wallet, "wallet") || !require(out_trusted, "out_trusted")) return MONERO_ERROR;
  return guard([&] { *out_trusted = wallet->wallet->is_daemon_trusted(); });
}

// -------------------------------- LISTENERS ---------------------------------

monero_result monero_wallet_listener_create(const monero_wallet_listener_callbacks* callbacks, ::monero_wallet_listener** out_listener) {
  reset_out(out_listener);
  if (!require(callbacks, "callbacks") || !require(out_listener, "out_listener")) return MONERO_ERROR;
  return guard([&] {
    std::unique_ptr<::monero_wallet_listener> listener(new ::monero_wallet_listener());
    listener->adapter.owner = listener.get();
    listener->adapter.callbacks = *callbacks;
    *out_listener = listener.release();
  });
}

void monero_wallet_listener_free(::monero_wallet_listener* listener) {
  if (listener == nullptr) return;
  std::shared_ptr<monero::monero_wallet> wallet;
  {
    std::lock_guard<std::mutex> lock(listener->registration_mutex);
    wallet = listener->registered_with.lock();
  }
  if (wallet) {
    try {
      wallet->remove_listener(listener->adapter);
    } catch (...) {
      // release the listener even if the removal fails
    }
  }
  delete listener;
}

monero_result monero_wallet_add_listener(::monero_wallet* wallet, ::monero_wallet_listener* listener) {
  if (!require(wallet, "wallet") || !require(listener, "listener")) return MONERO_ERROR;
  return guard([&] {
    // reserve the wallet under the lock, so one listener can't join two wallets
    std::shared_ptr<monero::monero_wallet> previous;
    {
      std::lock_guard<std::mutex> lock(listener->registration_mutex);
      previous = listener->registered_with.lock();
      if (previous && previous != wallet->wallet) throw std::runtime_error("listener is already registered with another wallet");
      listener->registered_with = wallet->wallet;
    }
    try {
      wallet->wallet->add_listener(listener->adapter);
    } catch (...) {
      if (!previous) {
        std::lock_guard<std::mutex> lock(listener->registration_mutex);
        if (listener->registered_with.lock() == wallet->wallet) listener->registered_with.reset();
      }
      throw;
    }
  });
}

monero_result monero_wallet_remove_listener(::monero_wallet* wallet, ::monero_wallet_listener* listener) {
  if (!require(wallet, "wallet") || !require(listener, "listener")) return MONERO_ERROR;
  return guard([&] {
    // monero-cpp notifies under its own lock, so this runs without registration_mutex
    wallet->wallet->remove_listener(listener->adapter);
    std::lock_guard<std::mutex> lock(listener->registration_mutex);
    if (listener->registered_with.lock() == wallet->wallet) listener->registered_with.reset();
  });
}

monero_result monero_wallet_remove_listeners(::monero_wallet* wallet) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] {
    for (monero::monero_wallet_listener* listener : wallet->wallet->get_listeners()) {
      wallet->wallet->remove_listener(*listener);
      // listeners that don't come from the C ABI have no handle to reset
      auto* adapter = dynamic_cast<wallet_listener_adapter*>(listener);
      if (adapter == nullptr) continue;
      std::lock_guard<std::mutex> lock(adapter->owner->registration_mutex);
      if (adapter->owner->registered_with.lock() == wallet->wallet) adapter->owner->registered_with.reset();
    }
  });
}

// -------------------------------- DAEMON ------------------------------------

monero_result monero_wallet_sync(::monero_wallet* wallet, const uint64_t* start_height, ::monero_wallet_listener* listener, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    monero::monero_sync_result result = listener ? sync_with_listener(wallet, listener, start_height) : (start_height ? wallet->wallet->sync(*start_height) : wallet->wallet->sync());
    *out_json = dup_string(result.serialize());
  });
}

monero_result monero_wallet_start_syncing(::monero_wallet* wallet, uint64_t sync_period_ms) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->start_syncing(sync_period_ms); });
}

monero_result monero_wallet_stop_syncing(::monero_wallet* wallet) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->stop_syncing(); });
}

monero_result monero_wallet_is_connected_to_daemon(::monero_wallet* wallet, bool* out_connected) {
  if (!require(wallet, "wallet") || !require(out_connected, "out_connected")) return MONERO_ERROR;
  return guard([&] { *out_connected = wallet->wallet->is_connected_to_daemon(); });
}

monero_result monero_wallet_is_daemon_synced(::monero_wallet* wallet, bool* out_synced) {
  if (!require(wallet, "wallet") || !require(out_synced, "out_synced")) return MONERO_ERROR;
  return guard([&] { *out_synced = wallet->wallet->is_daemon_synced(); });
}

monero_result monero_wallet_is_synced(::monero_wallet* wallet, bool* out_synced) {
  if (!require(wallet, "wallet") || !require(out_synced, "out_synced")) return MONERO_ERROR;
  return guard([&] { *out_synced = wallet->wallet->is_synced(); });
}

monero_result monero_wallet_get_daemon_height(::monero_wallet* wallet, uint64_t* out_height) {
  if (!require(wallet, "wallet") || !require(out_height, "out_height")) return MONERO_ERROR;
  return guard([&] { *out_height = wallet->wallet->get_daemon_height(); });
}

monero_result monero_wallet_get_daemon_max_peer_height(::monero_wallet* wallet, uint64_t* out_height) {
  if (!require(wallet, "wallet") || !require(out_height, "out_height")) return MONERO_ERROR;
  return guard([&] { *out_height = wallet->wallet->get_daemon_max_peer_height(); });
}

monero_result monero_wallet_wait_for_next_block(::monero_wallet* wallet, uint64_t* out_height) {
  if (!require(wallet, "wallet") || !require(out_height, "out_height")) return MONERO_ERROR;
  return guard([&] { *out_height = wallet->wallet->wait_for_next_block(); });
}

monero_result monero_wallet_scan_txs(::monero_wallet* wallet, const char* const* tx_hashes, size_t num_tx_hashes) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->scan_txs(string_array(tx_hashes, num_tx_hashes, "tx_hashes")); });
}

monero_result monero_wallet_rescan_blockchain(::monero_wallet* wallet) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->rescan_blockchain(); });
}

monero_result monero_wallet_rescan_spent(::monero_wallet* wallet) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->rescan_spent(); });
}

monero_result monero_wallet_get_height_by_date(::monero_wallet* wallet, uint16_t year, uint8_t month, uint8_t day, uint64_t* out_height) {
  if (!require(wallet, "wallet") || !require(out_height, "out_height")) return MONERO_ERROR;
  return guard([&] { *out_height = wallet->wallet->get_height_by_date(year, month, day); });
}

monero_result monero_wallet_start_mining(::monero_wallet* wallet, const uint64_t* num_threads, const bool* background_mining, const bool* ignore_battery) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->start_mining(optional_of(num_threads), optional_of(background_mining), optional_of(ignore_battery)); });
}

monero_result monero_wallet_stop_mining(::monero_wallet* wallet) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->stop_mining(); });
}

// ------------------------------ TRANSACTIONS ---------------------------------

monero_result monero_wallet_get_txs(::monero_wallet* wallet, const char* query_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    auto txs = query_json ? wallet->wallet->get_txs(*model_json<monero::monero_tx_query>(query_json, "query")) : wallet->wallet->get_txs();
    *out_json = dup_string(json_of_list(txs));
  });
}

monero_result monero_wallet_get_transfers(::monero_wallet* wallet, const char* query_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_list(wallet->wallet->get_transfers(*query_of<monero::monero_transfer_query>(query_json)))); });
}

monero_result monero_wallet_get_outputs(::monero_wallet* wallet, const char* query_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_list(wallet->wallet->get_outputs(*query_of<monero::monero_output_query>(query_json)))); });
}

monero_result monero_wallet_create_tx(::monero_wallet* wallet, const char* config_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(config_json, "config_json") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->create_tx(*model_json<monero::monero_tx_config>(config_json, "config"))->serialize()); });
}

monero_result monero_wallet_create_txs(::monero_wallet* wallet, const char* config_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(config_json, "config_json") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    // the transactions share one tx set, which holds the unsigned hex that sign_txs() takes
    auto txs = wallet->wallet->create_txs(*model_json<monero::monero_tx_config>(config_json, "config"));
    if (txs.empty() || txs[0]->m_tx_set == nullptr) throw std::runtime_error("no transactions were created");
    *out_json = dup_string(txs[0]->m_tx_set->serialize());
  });
}

monero_result monero_wallet_relay_tx(::monero_wallet* wallet, const char* tx_metadata, char** out_hash) {
  reset_out(out_hash);
  if (!require(wallet, "wallet") || !require(tx_metadata, "tx_metadata") || !require(out_hash, "out_hash")) return MONERO_ERROR;
  return guard([&] { *out_hash = dup_string(wallet->wallet->relay_tx(std::string(tx_metadata))); });
}

monero_result monero_wallet_relay_tx_json(::monero_wallet* wallet, const char* tx_json, char** out_hash) {
  reset_out(out_hash);
  if (!require(wallet, "wallet") || !require(tx_json, "tx_json") || !require(out_hash, "out_hash")) return MONERO_ERROR;
  return guard([&] { *out_hash = dup_string(wallet->wallet->relay_tx(tx_metadata_of_json(tx_json))); });
}

monero_result monero_wallet_relay_txs(::monero_wallet* wallet, const char* const* tx_metadatas, size_t num_tx_metadatas, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_strings(wallet->wallet->relay_txs(string_array(tx_metadatas, num_tx_metadatas, "tx_metadatas")))); });
}

monero_result monero_wallet_relay_txs_json(::monero_wallet* wallet, const char* txs_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(txs_json, "txs_json") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_strings(wallet->wallet->relay_txs(tx_metadatas_of_json(txs_json)))); });
}

monero_result monero_wallet_submit_txs(::monero_wallet* wallet, const char* signed_tx_hex, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(signed_tx_hex, "signed_tx_hex") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_strings(wallet->wallet->submit_txs(std::string(signed_tx_hex)))); });
}

monero_result monero_wallet_sign_txs(::monero_wallet* wallet, const char* unsigned_tx_hex, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(unsigned_tx_hex, "unsigned_tx_hex") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->sign_txs(std::string(unsigned_tx_hex)).serialize()); });
}

monero_result monero_wallet_describe_tx_set(::monero_wallet* wallet, const char* tx_set_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(tx_set_json, "tx_set_json") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->describe_tx_set(*model_json<monero::monero_tx_set>(tx_set_json, "tx_set")).serialize()); });
}

monero_result monero_wallet_sweep_output(::monero_wallet* wallet, const char* config_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(config_json, "config_json") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->sweep_output(*model_json<monero::monero_tx_config>(config_json, "config"))->serialize()); });
}

monero_result monero_wallet_sweep_dust(::monero_wallet* wallet, bool relay, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_list(wallet->wallet->sweep_dust(relay))); });
}

monero_result monero_wallet_sweep_unlocked(::monero_wallet* wallet, const char* config_json, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(config_json, "config_json") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_list(wallet->wallet->sweep_unlocked(*model_json<monero::monero_tx_config>(config_json, "config")))); });
}

monero_result monero_wallet_move_to(::monero_wallet* wallet, const char* path, const char* password) {
  if (!require(wallet, "wallet") || !require(path, "path")) return MONERO_ERROR;
  return guard([&] { wallet->wallet->move_to(std::string(path), safe_str(password)); });
}

monero_result monero_wallet_get_payment_uri(::monero_wallet* wallet, const char* config_json, char** out_uri) {
  reset_out(out_uri);
  if (!require(wallet, "wallet") || !require(config_json, "config_json") || !require(out_uri, "out_uri")) return MONERO_ERROR;
  return guard([&] { *out_uri = dup_string(wallet->wallet->get_payment_uri(*model_json<monero::monero_tx_config>(config_json, "config"))); });
}

monero_result monero_wallet_parse_payment_uri(::monero_wallet* wallet, const char* uri, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(uri, "uri") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->parse_payment_uri(std::string(uri))->serialize()); });
}

monero_result monero_wallet_get_tx_key(::monero_wallet* wallet, const char* tx_hash, char** out_key) {
  reset_out(out_key);
  if (!require(wallet, "wallet") || !require(tx_hash, "tx_hash") || !require(out_key, "out_key")) return MONERO_ERROR;
  return guard([&] { *out_key = dup_string(wallet->wallet->get_tx_key(std::string(tx_hash))); });
}

monero_result monero_wallet_check_tx_key(::monero_wallet* wallet, const char* tx_hash, const char* tx_key, const char* address, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(tx_hash, "tx_hash") || !require(tx_key, "tx_key") || !require(address, "address") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->check_tx_key(std::string(tx_hash), std::string(tx_key), std::string(address))->serialize()); });
}

monero_result monero_wallet_get_tx_proof(::monero_wallet* wallet, const char* tx_hash, const char* address, const char* message, char** out_signature) {
  reset_out(out_signature);
  if (!require(wallet, "wallet") || !require(tx_hash, "tx_hash") || !require(address, "address") || !require(out_signature, "out_signature")) return MONERO_ERROR;
  return guard([&] { *out_signature = dup_string(wallet->wallet->get_tx_proof(std::string(tx_hash), std::string(address), safe_str(message))); });
}

monero_result monero_wallet_check_tx_proof(::monero_wallet* wallet, const char* tx_hash, const char* address, const char* message, const char* signature, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(tx_hash, "tx_hash") || !require(address, "address") || !require(signature, "signature") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->check_tx_proof(std::string(tx_hash), std::string(address), safe_str(message), std::string(signature))->serialize()); });
}

monero_result monero_wallet_get_spend_proof(::monero_wallet* wallet, const char* tx_hash, const char* message, char** out_signature) {
  reset_out(out_signature);
  if (!require(wallet, "wallet") || !require(tx_hash, "tx_hash") || !require(out_signature, "out_signature")) return MONERO_ERROR;
  return guard([&] { *out_signature = dup_string(wallet->wallet->get_spend_proof(std::string(tx_hash), safe_str(message))); });
}

monero_result monero_wallet_check_spend_proof(::monero_wallet* wallet, const char* tx_hash, const char* message, const char* signature, bool* out_good) {
  if (!require(wallet, "wallet") || !require(tx_hash, "tx_hash") || !require(signature, "signature") || !require(out_good, "out_good")) return MONERO_ERROR;
  return guard([&] { *out_good = wallet->wallet->check_spend_proof(std::string(tx_hash), safe_str(message), std::string(signature)); });
}

monero_result monero_wallet_get_reserve_proof_wallet(::monero_wallet* wallet, const char* message, char** out_signature) {
  reset_out(out_signature);
  if (!require(wallet, "wallet") || !require(out_signature, "out_signature")) return MONERO_ERROR;
  return guard([&] { *out_signature = dup_string(wallet->wallet->get_reserve_proof_wallet(safe_str(message))); });
}

monero_result monero_wallet_get_reserve_proof_account(::monero_wallet* wallet, uint32_t account_idx, uint64_t amount, const char* message, char** out_signature) {
  reset_out(out_signature);
  if (!require(wallet, "wallet") || !require(out_signature, "out_signature")) return MONERO_ERROR;
  return guard([&] { *out_signature = dup_string(wallet->wallet->get_reserve_proof_account(account_idx, amount, safe_str(message))); });
}

monero_result monero_wallet_check_reserve_proof(::monero_wallet* wallet, const char* address, const char* message, const char* signature, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(address, "address") || !require(signature, "signature") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->check_reserve_proof(std::string(address), safe_str(message), std::string(signature))->serialize()); });
}

// -------------------------------- MULTISIG ----------------------------------

monero_result monero_wallet_is_multisig_import_needed(::monero_wallet* wallet, bool* out_needed) {
  if (!require(wallet, "wallet") || !require(out_needed, "out_needed")) return MONERO_ERROR;
  return guard([&] { *out_needed = wallet->wallet->is_multisig_import_needed(); });
}

monero_result monero_wallet_get_multisig_info(::monero_wallet* wallet, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->get_multisig_info().serialize()); });
}

monero_result monero_wallet_prepare_multisig(::monero_wallet* wallet, char** out_multisig_hex) {
  reset_out(out_multisig_hex);
  if (!require(wallet, "wallet") || !require(out_multisig_hex, "out_multisig_hex")) return MONERO_ERROR;
  return guard([&] { *out_multisig_hex = dup_string(wallet->wallet->prepare_multisig()); });
}

monero_result monero_wallet_make_multisig(::monero_wallet* wallet, const char* const* multisig_hexes, size_t num_multisig_hexes, int32_t threshold, const char* password, char** out_multisig_hex) {
  reset_out(out_multisig_hex);
  if (!require(wallet, "wallet") || !require(out_multisig_hex, "out_multisig_hex")) return MONERO_ERROR;
  return guard([&] { *out_multisig_hex = dup_string(wallet->wallet->make_multisig(string_array(multisig_hexes, num_multisig_hexes, "multisig_hexes"), threshold, safe_str(password))); });
}

monero_result monero_wallet_exchange_multisig_keys(::monero_wallet* wallet, const char* const* multisig_hexes, size_t num_multisig_hexes, const char* password, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->exchange_multisig_keys(string_array(multisig_hexes, num_multisig_hexes, "multisig_hexes"), safe_str(password)).serialize()); });
}

monero_result monero_wallet_export_multisig_hex(::monero_wallet* wallet, char** out_multisig_hex) {
  reset_out(out_multisig_hex);
  if (!require(wallet, "wallet") || !require(out_multisig_hex, "out_multisig_hex")) return MONERO_ERROR;
  return guard([&] { *out_multisig_hex = dup_string(wallet->wallet->export_multisig_hex()); });
}

monero_result monero_wallet_import_multisig_hex(::monero_wallet* wallet, const char* const* multisig_hexes, size_t num_multisig_hexes, bool refresh_after_import, int32_t* out_num_imported) {
  if (!require(wallet, "wallet") || !require(out_num_imported, "out_num_imported")) return MONERO_ERROR;
  return guard([&] { *out_num_imported = wallet->wallet->import_multisig_hex(string_array(multisig_hexes, num_multisig_hexes, "multisig_hexes"), refresh_after_import); });
}

monero_result monero_wallet_sign_multisig_tx_hex(::monero_wallet* wallet, const char* multisig_tx_hex, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(multisig_tx_hex, "multisig_tx_hex") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(wallet->wallet->sign_multisig_tx_hex(std::string(multisig_tx_hex)).serialize()); });
}

monero_result monero_wallet_submit_multisig_tx_hex(::monero_wallet* wallet, const char* signed_multisig_tx_hex, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(signed_multisig_tx_hex, "signed_multisig_tx_hex") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_strings(wallet->wallet->submit_multisig_tx_hex(std::string(signed_multisig_tx_hex)))); });
}

// -------------------------------- RPC WALLETS -------------------------------

monero_result monero_wallet_rpc_connect(::monero_rpc_connection* connection, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(connection, "connection") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] { *out_wallet = wrap_shared(std::make_shared<monero::monero_wallet_rpc>(monero_c::connection_of(connection))); });
}

monero_result monero_wallet_rpc_open_wallet(::monero_wallet* wallet, const char* name, const char* wallet_password) {
  if (!require(wallet, "wallet") || !require(name, "name")) return MONERO_ERROR;
  return guard([&] { as_rpc(wallet).open_wallet(std::string(name), safe_str(wallet_password)); });
}

monero_result monero_wallet_rpc_create_wallet(::monero_wallet* wallet, const char* config_json) {
  if (!require(wallet, "wallet") || !require(config_json, "config_json")) return MONERO_ERROR;
  return guard([&] { as_rpc(wallet).create_wallet(model_json<monero::monero_wallet_config>(config_json, "config")); });
}

monero_result monero_wallet_rpc_open(const char* uri, const char* username, const char* password, const char* name, const char* wallet_password, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(uri, "uri") || !require(name, "name") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    std::shared_ptr<monero::monero_wallet_rpc> rpc = rpc_client(uri, username, password);
    rpc->open_wallet(std::string(name), safe_str(wallet_password));
    *out_wallet = wrap_shared(rpc);
  });
}

monero_result monero_wallet_rpc_create_random(const char* uri, const char* username, const char* password, const char* name, const char* wallet_password, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(uri, "uri") || !require(name, "name") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] { *out_wallet = create_rpc_wallet(uri, username, password, rpc_config(name, wallet_password, language)); });
}

monero_result monero_wallet_rpc_create_from_seed(const char* uri, const char* username, const char* password, const char* name, const char* wallet_password, const char* seed, const char* seed_offset, uint64_t restore_height, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(uri, "uri") || !require(name, "name") || !require(seed, "seed") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    auto config = rpc_config(name, wallet_password, language);
    config->m_seed = mnemonic_of(seed);
    config->m_seed_offset = safe_str(seed_offset);
    config->m_restore_height = restore_height;
    *out_wallet = create_rpc_wallet(uri, username, password, config);
  });
}

monero_result monero_wallet_rpc_create_from_keys(const char* uri, const char* username, const char* password, const char* name, const char* wallet_password, const char* address, const char* private_view_key, const char* private_spend_key, uint64_t restore_height, const char* language, ::monero_wallet** out_wallet) {
  reset_out(out_wallet);
  if (!require(uri, "uri") || !require(name, "name") || !require(address, "address") || !require(private_view_key, "private_view_key") || !require(out_wallet, "out_wallet")) return MONERO_ERROR;
  return guard([&] {
    auto config = rpc_config(name, wallet_password, language);
    config->m_primary_address = std::string(address);
    config->m_private_view_key = std::string(private_view_key);
    config->m_private_spend_key = safe_str(private_spend_key);
    config->m_restore_height = restore_height;
    *out_wallet = create_rpc_wallet(uri, username, password, config);
  });
}

monero_result monero_wallet_rpc_stop(::monero_wallet* wallet) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { as_rpc(wallet).stop(); });
}

monero_result monero_wallet_rpc_get_seed_languages(::monero_wallet* wallet, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_strings(as_rpc(wallet).get_seed_languages())); });
}

monero_result monero_wallet_rpc_get_connection(::monero_wallet* wallet, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(as_rpc(wallet).get_rpc_connection()->serialize()); });
}

monero_result monero_wallet_rpc_set_poll_period(::monero_wallet* wallet, uint64_t period_ms) {
  if (!require(wallet, "wallet")) return MONERO_ERROR;
  return guard([&] { as_rpc(wallet).set_poll_period_in_ms(period_ms); });
}

monero_result monero_wallet_rpc_get_balances(::monero_wallet* wallet, const uint32_t* account_idx, const uint32_t* subaddress_idx, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    std::shared_ptr<monero::monero_subaddress> balances = as_rpc(wallet).get_balances(optional_of(account_idx), optional_of(subaddress_idx));
    *out_json = dup_string(balances ? balances->serialize() : std::string("null"));
  });
}

monero_result monero_wallet_rpc_get_account(::monero_wallet* wallet, uint32_t account_idx, bool include_subaddresses, bool skip_balances, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(as_rpc(wallet).get_account(account_idx, include_subaddresses, skip_balances).serialize()); });
}

monero_result monero_wallet_rpc_get_accounts(::monero_wallet* wallet, bool include_subaddresses, const char* tag, bool skip_balances, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] { *out_json = dup_string(json_of_list(as_rpc(wallet).get_accounts(include_subaddresses, safe_str(tag), skip_balances))); });
}

monero_result monero_wallet_rpc_get_subaddresses(::monero_wallet* wallet, uint32_t account_idx, const uint32_t* subaddress_indices, size_t num_subaddress_indices, bool skip_balances, char** out_json) {
  reset_out(out_json);
  if (!require(wallet, "wallet") || !require(out_json, "out_json")) return MONERO_ERROR;
  return guard([&] {
    auto indices = array_of(subaddress_indices, num_subaddress_indices, "subaddress_indices");
    *out_json = dup_string(json_of_list(as_rpc(wallet).get_subaddresses(account_idx, indices, skip_balances)));
  });
}

monero_result monero_wallet_rpc_set_daemon_connection(::monero_wallet* wallet, const char* connection_json, bool is_trusted, const char* ssl_options_json) {
  if (!require(wallet, "wallet") || !require(connection_json, "connection_json")) return MONERO_ERROR;
  return guard([&] {
    boost::optional<monero::ssl_options> options;
    if (ssl_options_json != nullptr && ssl_options_json[0] != '\0') options = *model_json<monero::ssl_options>(ssl_options_json, "ssl_options");
    as_rpc(wallet).set_daemon_connection(model_json<monero::monero_rpc_connection>(connection_json, "connection"), is_trusted, options);
  });
}

} // extern "C"

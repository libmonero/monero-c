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
 *
 * Parts of this file are originally copyright (c) woodser
 *
 * Parts of this file are originally copyright (c) 2014-2019, The Monero Project
 *
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *
 * All rights reserved.
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers
 */

#include "monero_c_utils.h"
#include "common/monero_c_common.h"
#include "utils/monero_utils.h"
#include "crypto/hash.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace {

using monero_c::dup_string;
using monero_c::guard;
using monero_c::safe_str;
using monero_c::set_last_error;

std::string safe_bin(const uint8_t* data, size_t len) {
  return data ? std::string(reinterpret_cast<const char*>(data), len) : std::string();
}

bool to_network_type(int32_t v, monero_network_type& out) {
  if (v < MONERO_UTILS_NETWORK_MAINNET || v > MONERO_UTILS_NETWORK_STAGENET) return false;
  out = static_cast<monero_network_type>(v);
  return true;
}

uint8_t* dup_buffer(const std::string& bin, size_t* out_len) {
  uint8_t* out = static_cast<uint8_t*>(std::malloc(bin.empty() ? 1 : bin.size()));
  if (out == nullptr) throw std::bad_alloc();
  if (!bin.empty()) std::memcpy(out, bin.data(), bin.size());
  *out_len = bin.size();
  return out;
}

using binary_to_json_fn = void (*)(const std::string&, std::string&);

// shared by the monero_utils_binary*_to_json wrappers below, which differ
// only in which monero_utils converter they call
monero_result binary_to_json_impl(binary_to_json_fn fn, const uint8_t* data, size_t len, char** out_json) {
  if (out_json == nullptr) { set_last_error("out_json must not be null"); return MONERO_ERROR; }
  *out_json = nullptr;
  return guard([&] {
    std::string json;
    fn(safe_bin(data, len), json);
    *out_json = dup_string(json);
  });
}

} // namespace

extern "C" {

// ---------------------------- ERROR / MEMORY --------------------------------

const char* monero_last_error(void) {
  return monero_c::last_error().c_str();
}

void monero_utils_free(void* ptr) {
  std::free(ptr);
}

// --------------------------------- LOGGING ----------------------------------

void monero_utils_set_log_level(int32_t level) {
  monero_utils::set_log_level(static_cast<int>(level));
}

void monero_utils_set_log_categories(const char* categories) {
  monero_utils::set_log_categories(safe_str(categories));
}

void monero_utils_configure_logging(const char* path, bool console) {
  monero_utils::configure_logging(safe_str(path), console);
}

// -------------------------------- VALIDATION --------------------------------

bool monero_utils_is_valid_address(const char* address, int32_t network_type) {
  monero_network_type nt;
  if (!to_network_type(network_type, nt)) return false;
  return monero_utils::is_valid_address(safe_str(address), nt);
}

bool monero_utils_is_valid_private_view_key(const char* private_view_key) {
  return monero_utils::is_valid_private_view_key(safe_str(private_view_key));
}

bool monero_utils_is_valid_private_spend_key(const char* private_spend_key) {
  return monero_utils::is_valid_private_spend_key(safe_str(private_spend_key));
}

bool monero_utils_is_valid_public_view_key(const char* public_view_key) {
  return monero_utils::is_valid_public_view_key(safe_str(public_view_key));
}

bool monero_utils_is_valid_public_spend_key(const char* public_spend_key) {
  return monero_utils::is_valid_public_spend_key(safe_str(public_spend_key));
}

bool monero_utils_is_valid_payment_id(const char* payment_id) {
  return monero_utils::is_valid_payment_id(safe_str(payment_id));
}

bool monero_utils_is_valid_mnemonic(const char* mnemonic, const char* language) {
  return monero_utils::is_valid_mnemonic(safe_str(mnemonic), safe_str(language));
}

bool monero_utils_is_valid_language(const char* language) {
  return monero_utils::is_valid_language(safe_str(language));
}

monero_result monero_utils_validate_address(const char* address, int32_t network_type) {
  monero_network_type nt;
  if (!to_network_type(network_type, nt)) { set_last_error("invalid network_type"); return MONERO_ERROR; }
  return guard([&] { monero_utils::validate_address(safe_str(address), nt); });
}

monero_result monero_utils_validate_private_view_key(const char* private_view_key) {
  return guard([&] { monero_utils::validate_private_view_key(safe_str(private_view_key)); });
}

monero_result monero_utils_validate_private_spend_key(const char* private_spend_key) {
  return guard([&] { monero_utils::validate_private_spend_key(safe_str(private_spend_key)); });
}

monero_result monero_utils_validate_public_view_key(const char* public_view_key) {
  return guard([&] { monero_utils::validate_public_view_key(safe_str(public_view_key)); });
}

monero_result monero_utils_validate_public_spend_key(const char* public_spend_key) {
  return guard([&] { monero_utils::validate_public_spend_key(safe_str(public_spend_key)); });
}

monero_result monero_utils_validate_payment_id(const char* payment_id) {
  return guard([&] { monero_utils::validate_payment_id(safe_str(payment_id)); });
}

monero_result monero_utils_validate_mnemonic(const char* mnemonic, const char* language) {
  return guard([&] { monero_utils::validate_mnemonic(safe_str(mnemonic), safe_str(language)); });
}

// --------------------------- INTEGRATED ADDRESS / URIS ----------------------

monero_result monero_utils_get_integrated_address(int32_t network_type, const char* standard_address, const char* payment_id, char** out_json) {
  if (out_json == nullptr) { set_last_error("out_json must not be null"); return MONERO_ERROR; }
  *out_json = nullptr;
  monero_network_type nt;
  if (!to_network_type(network_type, nt)) { set_last_error("invalid network_type"); return MONERO_ERROR; }
  return guard([&] {
    monero_integrated_address addr = monero_utils::get_integrated_address(nt, safe_str(standard_address), safe_str(payment_id));
    *out_json = dup_string(addr.serialize());
  });
}

monero_result monero_utils_get_payment_uri(const char* tx_config_json, int32_t network_type, char** out_uri) {
  if (out_uri == nullptr) { set_last_error("out_uri must not be null"); return MONERO_ERROR; }
  *out_uri = nullptr;
  monero_network_type nt;
  if (!to_network_type(network_type, nt)) { set_last_error("invalid network_type"); return MONERO_ERROR; }
  return guard([&] {
    std::shared_ptr<monero_tx_config> config = monero_tx_config::deserialize(safe_str(tx_config_json));
    *out_uri = dup_string(monero_utils::get_payment_uri(*config, nt));
  });
}

monero_result monero_utils_parse_payment_uri(const char* uri, int32_t network_type, char** out_tx_config_json) {
  if (out_tx_config_json == nullptr) { set_last_error("out_tx_config_json must not be null"); return MONERO_ERROR; }
  *out_tx_config_json = nullptr;
  monero_network_type nt;
  if (!to_network_type(network_type, nt)) { set_last_error("invalid network_type"); return MONERO_ERROR; }
  return guard([&] {
    std::shared_ptr<monero_tx_config> config = monero_utils::parse_payment_uri(safe_str(uri), nt);
    *out_tx_config_json = dup_string(config->serialize());
  });
}

// ----------------------------- JSON / BINARY RPC ----------------------------

monero_result monero_utils_json_to_binary(const char* json, uint8_t** out_data, size_t* out_len) {
  if (out_data == nullptr || out_len == nullptr) { set_last_error("out_data/out_len must not be null"); return MONERO_ERROR; }
  *out_data = nullptr;
  *out_len = 0;
  return guard([&] {
    std::string bin;
    monero_utils::json_to_binary(safe_str(json), bin);
    *out_data = dup_buffer(bin, out_len);
  });
}

monero_result monero_utils_binary_to_json(const uint8_t* data, size_t len, char** out_json) {
  return binary_to_json_impl(monero_utils::binary_to_json, data, len, out_json);
}

monero_result monero_utils_binary_blocks_to_json(const uint8_t* data, size_t len, char** out_json) {
  return binary_to_json_impl(monero_utils::binary_blocks_to_json, data, len, out_json);
}

monero_result monero_utils_binary_blocks_fast_to_json(const uint8_t* data, size_t len, char** out_json) {
  return binary_to_json_impl(monero_utils::binary_blocks_fast_to_json, data, len, out_json);
}

// -------------------------------- AMOUNTS / IDS -----------------------------

monero_result monero_utils_xmr_to_atomic_units(double amount_xmr, uint64_t* out_atomic_units) {
  if (out_atomic_units == nullptr) { set_last_error("out_atomic_units must not be null"); return MONERO_ERROR; }
  return guard([&] { *out_atomic_units = monero_utils::xmr_to_atomic_units(amount_xmr); });
}

double monero_utils_atomic_units_to_xmr(uint64_t amount_atomic_units) {
  return monero_utils::atomic_units_to_xmr(amount_atomic_units);
}

bool monero_utils_parse_payment_id_long(const char* payment_id_str, uint8_t out_payment_id[32]) {
  static_assert(sizeof(crypto::hash) == 32, "crypto::hash is expected to be 32 bytes");
  crypto::hash h;
  if (!monero_utils::parse_payment_id_long(safe_str(payment_id_str), h)) return false;
  if (out_payment_id != nullptr) std::memcpy(out_payment_id, &h, sizeof(h));
  return true;
}

bool monero_utils_parse_payment_id_short(const char* payment_id_str, uint8_t out_payment_id[8]) {
  static_assert(sizeof(crypto::hash8) == 8, "crypto::hash8 is expected to be 8 bytes");
  crypto::hash8 h;
  if (!monero_utils::parse_payment_id_short(safe_str(payment_id_str), h)) return false;
  if (out_payment_id != nullptr) std::memcpy(out_payment_id, &h, sizeof(h));
  return true;
}

// monero-cpp only exposes parse_payment_id_long/short, which return a bool and an out-ref.
// the is_valid_ and validate_ wrappers match the checks of downstream bindings such as
// monero-python's PyMoneroUtils, including the "Invalid {long,short} payment id" messages

bool monero_utils_is_valid_payment_id_long(const char* payment_id_str) {
  crypto::hash h;
  return monero_utils::parse_payment_id_long(safe_str(payment_id_str), h);
}

bool monero_utils_is_valid_payment_id_short(const char* payment_id_str) {
  crypto::hash8 h;
  return monero_utils::parse_payment_id_short(safe_str(payment_id_str), h);
}

monero_result monero_utils_validate_payment_id_long(const char* payment_id_str) {
  return guard([&] {
    crypto::hash h;
    if (!monero_utils::parse_payment_id_long(safe_str(payment_id_str), h)) throw std::runtime_error("Invalid long payment id");
  });
}

monero_result monero_utils_validate_payment_id_short(const char* payment_id_str) {
  return guard([&] {
    crypto::hash8 h;
    if (!monero_utils::parse_payment_id_short(safe_str(payment_id_str), h)) throw std::runtime_error("Invalid short payment id");
  });
}

int32_t monero_utils_get_ring_size(void) {
  return monero_utils::RING_SIZE;
}

} // extern "C"

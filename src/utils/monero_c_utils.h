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

#ifndef MONERO_C_UTILS_H
#define MONERO_C_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef MONERO_C_BUILD
    #define MONERO_EXPORT __declspec(dllexport)
  #else
    #define MONERO_EXPORT __declspec(dllimport)
  #endif
#else
  #define MONERO_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------- RESULT CODES -------------------------------

// Returned by every function that can fail. On MONERO_ERROR, call
// monero_utils_last_error() for a human-readable message.
typedef enum monero_result {
  MONERO_OK = 0,
  MONERO_ERROR = -1
} monero_result;

// Mirrors monero_network_type in monero-cpp (daemon/monero_daemon_model.h):
// values must stay numerically identical since they're cast directly across the bridge.
typedef enum monero_utils_network_type {
  MONERO_UTILS_NETWORK_MAINNET = 0,
  MONERO_UTILS_NETWORK_TESTNET = 1,
  MONERO_UTILS_NETWORK_STAGENET = 2
} monero_utils_network_type;

// ---------------------------- ERROR / MEMORY --------------------------------

/**
 * Returns the message for the most recent MONERO_ERROR result on the calling thread.
 *
 * @return the error message, or "" if none. The pointer is owned by monero-c
 *   and stays valid only until the next monero_utils_* call on this thread;
 *   copy it if you need to keep it. Unlike other monero_utils_* out params,
 *   do not pass this pointer to monero_utils_free().
 */
MONERO_EXPORT const char* monero_utils_last_error(void);

/**
 * Frees a string or byte buffer returned by any monero_utils_* function.
 *
 * @param ptr is the pointer previously returned by a monero_utils_* function
 */
MONERO_EXPORT void monero_utils_free(void* ptr);

// --------------------------------- LOGGING ----------------------------------

/**
 * Sets the log level, which selects a default set of category filters
 * (0 = warnings/errors only, 4 = full trace); see monero_utils_set_log_categories()
 * for finer-grained control.
 *
 * @param level is the log level, 0 (least verbose) through 4 (most verbose)
 */
MONERO_EXPORT void monero_utils_set_log_level(int32_t level);

/**
 * Sets the log categories filter directly, overriding whatever
 * monero_utils_set_log_level() last selected.
 *
 * @param categories is a comma-separated list of "category:level" pairs
 *   (e.g. "*:WARNING,net:FATAL"); prefix with '+' to append to the current
 *   filter, or '-' to remove matching categories from it
 */
MONERO_EXPORT void monero_utils_set_log_categories(const char* categories);

/**
 * Configures where log output is written.
 *
 * @param path is the log file's base path
 * @param console is whether to also write log output to the console
 */
MONERO_EXPORT void monero_utils_configure_logging(const char* path, bool console);

// -------------------------------- VALIDATION --------------------------------
// Pure predicates: never fail, false just means "invalid".

/**
 * Checks whether an address is valid for a given network.
 *
 * @param address is the address to validate
 * @param network_type is the network to validate the address against
 * @return true if the address is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_address(const char* address, int32_t network_type);

/**
 * Checks whether a private view key is valid.
 *
 * @param private_view_key is the private view key to validate
 * @return true if the private view key is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_private_view_key(const char* private_view_key);

/**
 * Checks whether a private spend key is valid.
 *
 * @param private_spend_key is the private spend key to validate
 * @return true if the private spend key is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_private_spend_key(const char* private_spend_key);

/**
 * Checks whether a public view key is valid.
 *
 * @param public_view_key is the public view key to validate
 * @return true if the public view key is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_public_view_key(const char* public_view_key);

/**
 * Checks whether a public spend key is valid.
 *
 * @param public_spend_key is the public spend key to validate
 * @return true if the public spend key is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_public_spend_key(const char* public_spend_key);

/**
 * Checks whether a payment id is valid.
 *
 * @param payment_id is the payment id to validate, as 16 or 64 hex characters
 * @return true if the payment id is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_payment_id(const char* payment_id);

/**
 * Checks whether a mnemonic seed is valid.
 *
 * @param mnemonic is the mnemonic seed to validate
 * @param language restricts validation to that seed language, or "" to accept any language
 * @return true if the mnemonic is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_mnemonic(const char* mnemonic, const char* language);

/**
 * Indicates if the given language is valid.
 *
 * @param language is the language to validate (case-sensitive, e.g. "English")
 * @return true if the language is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_language(const char* language);

// Same checks as above, but returning MONERO_ERROR with a descriptive message
// (via monero_utils_last_error()) instead of just `false`.

/**
 * Validates an address for a given network, like monero_utils_is_valid_address()
 * but with a descriptive error instead of just false.
 *
 * @param address is the address to validate
 * @param network_type is the network to validate the address against
 * @return MONERO_OK if the address is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_address(const char* address, int32_t network_type);

/**
 * Validates a private view key, like monero_utils_is_valid_private_view_key()
 * but with a descriptive error instead of just false.
 *
 * @param private_view_key is the private view key to validate
 * @return MONERO_OK if the private view key is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_private_view_key(const char* private_view_key);

/**
 * Validates a private spend key, like monero_utils_is_valid_private_spend_key()
 * but with a descriptive error instead of just false.
 *
 * @param private_spend_key is the private spend key to validate
 * @return MONERO_OK if the private spend key is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_private_spend_key(const char* private_spend_key);

/**
 * Validates a public view key, like monero_utils_is_valid_public_view_key()
 * but with a descriptive error instead of just false.
 *
 * @param public_view_key is the public view key to validate
 * @return MONERO_OK if the public view key is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_public_view_key(const char* public_view_key);

/**
 * Validates a public spend key, like monero_utils_is_valid_public_spend_key()
 * but with a descriptive error instead of just false.
 *
 * @param public_spend_key is the public spend key to validate
 * @return MONERO_OK if the public spend key is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_public_spend_key(const char* public_spend_key);

/**
 * Validates a payment id, like monero_utils_is_valid_payment_id() but with a
 * descriptive error instead of just false.
 *
 * @param payment_id is the payment id to validate, as 16 or 64 hex characters
 * @return MONERO_OK if the payment id is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_payment_id(const char* payment_id);

/**
 * Validates a mnemonic seed, like monero_utils_is_valid_mnemonic() but with a
 * descriptive error instead of just false.
 *
 * @param mnemonic is the mnemonic seed to validate
 * @param language restricts validation to that seed language, or "" to accept any language
 * @return MONERO_OK if the mnemonic is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_mnemonic(const char* mnemonic, const char* language);

// --------------------------- INTEGRATED ADDRESS / URIS ----------------------
// Structured in/out values cross the bridge as JSON (monero-cpp's own
// serializable_struct::serialize()/::deserialize() format), so the C side
// doesn't need to mirror every C++ model struct field by field.

/**
 * Builds an integrated address from a standard address and payment id.
 *
 * @param network_type is the network to validate the address against
 * @param standard_address is the base address to integrate the payment id into
 * @param payment_id is the payment id to integrate, or "" to generate a random one
 * @param out_json receives a JSON-serialized monero_integrated_address; free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the address or payment id is invalid
 */
MONERO_EXPORT monero_result monero_utils_get_integrated_address(int32_t network_type, const char* standard_address, const char* payment_id, char** out_json);

/**
 * Creates a payment URI from a transaction configuration.
 *
 * @param tx_config_json is a JSON-serialized monero_tx_config with exactly one destination (address + amount)
 * @param network_type is the network to format the URI's address for
 * @param out_uri receives the payment URI string; free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the transaction config is invalid
 */
MONERO_EXPORT monero_result monero_utils_get_payment_uri(const char* tx_config_json, int32_t network_type, char** out_uri);

/**
 * Parses a payment URI into a transaction configuration.
 *
 * @param uri is the payment URI to parse
 * @param network_type is the network to validate the URI's address against
 * @param out_tx_config_json receives a JSON-serialized monero_tx_config parsed from the URI; free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the URI is invalid
 */
MONERO_EXPORT monero_result monero_utils_parse_payment_uri(const char* uri, int32_t network_type, char** out_tx_config_json);

// ----------------------------- JSON / BINARY RPC ----------------------------
// Binary payloads (as used by monerod's .bin RPC endpoints) are not
// null-terminated text, so they cross the bridge as explicit (data, length) pairs.

/**
 * Converts a JSON string to its binary-encoded (portable storage format) equivalent.
 *
 * @param json is the JSON string to convert
 * @param out_data receives the binary-encoded buffer; free with monero_utils_free()
 * @param out_len receives the length of the binary-encoded buffer
 * @return MONERO_OK on success, or MONERO_ERROR if the JSON is invalid
 */
MONERO_EXPORT monero_result monero_utils_json_to_binary(const char* json, uint8_t** out_data, size_t* out_len);

/**
 * Converts a binary-encoded (portable storage format) buffer to its JSON equivalent.
 *
 * @param data is the binary-encoded buffer to convert
 * @param len is the length of the binary-encoded buffer
 * @param out_json receives the decoded JSON string; free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the buffer is invalid
 */
MONERO_EXPORT monero_result monero_utils_binary_to_json(const uint8_t* data, size_t len, char** out_json);

/**
 * Decodes a get_blocks.bin-style response.
 *
 * @param data is the binary-encoded buffer to decode
 * @param len is the length of the binary-encoded buffer
 * @param out_json receives the decoded JSON string; see monero_utils_binary_to_json(). Free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the buffer is invalid
 */
MONERO_EXPORT monero_result monero_utils_binary_blocks_to_json(const uint8_t* data, size_t len, char** out_json);

/**
 * Decodes a get_blocks_by_height.bin-style response.
 *
 * @param data is the binary-encoded buffer to decode
 * @param len is the length of the binary-encoded buffer
 * @param out_json receives the decoded JSON string; see monero_utils_binary_to_json(). Free with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the buffer is invalid
 */
MONERO_EXPORT monero_result monero_utils_binary_blocks_fast_to_json(const uint8_t* data, size_t len, char** out_json);

// -------------------------------- AMOUNTS / IDS -----------------------------

/**
 * Converts an amount in XMR to atomic units.
 *
 * @param amount_xmr is the amount in XMR to convert
 * @param out_atomic_units receives the amount in atomic units (1 XMR = 10^12)
 * @return MONERO_OK on success, or MONERO_ERROR if amount_xmr is negative, non-finite, or too large to represent in atomic units
 */
MONERO_EXPORT monero_result monero_utils_xmr_to_atomic_units(double amount_xmr, uint64_t* out_atomic_units);

/**
 * Converts an amount in atomic units to XMR.
 *
 * @param amount_atomic_units is the amount in atomic units to convert
 * @return the amount in XMR
 */
MONERO_EXPORT double monero_utils_atomic_units_to_xmr(uint64_t amount_atomic_units);

/**
 * Parses a long payment ID from a string.
 *
 * @param payment_id_str is the string to parse
 * @param out_payment_id receives the parsed 32-byte payment id if valid
 * @return true if the payment ID is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_parse_payment_id_long(const char* payment_id_str, uint8_t out_payment_id[32]);

/**
 * Parses a short payment ID from a string.
 *
 * @param payment_id_str is the string to parse
 * @param out_payment_id receives the parsed 8-byte payment id if valid
 * @return true if the payment ID is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_parse_payment_id_short(const char* payment_id_str, uint8_t out_payment_id[8]);

/**
 * Checks whether a long payment id is valid.
 *
 * @param payment_id_str is the payment id to validate, as 64 hex characters
 * @return true if the payment id is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_payment_id_long(const char* payment_id_str);

/**
 * Checks whether a short payment id is valid.
 *
 * @param payment_id_str is the payment id to validate, as 16 hex characters
 * @return true if the payment id is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_payment_id_short(const char* payment_id_str);

/**
 * Validates a long payment id, like monero_utils_is_valid_payment_id_long()
 * but with a descriptive error instead of just false.
 *
 * @param payment_id_str is the payment id to validate, as 64 hex characters
 * @return MONERO_OK if the payment id is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_payment_id_long(const char* payment_id_str);

/**
 * Validates a short payment id, like monero_utils_is_valid_payment_id_short()
 * but with a descriptive error instead of just false.
 *
 * @param payment_id_str is the payment id to validate, as 16 hex characters
 * @return MONERO_OK if the payment id is valid, or MONERO_ERROR otherwise (see monero_utils_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_payment_id_short(const char* payment_id_str);

/**
 * Returns the network-enforced ring size.
 *
 * @return the ring size (monero_utils::RING_SIZE)
 */
MONERO_EXPORT int32_t monero_utils_get_ring_size(void);

#ifdef __cplusplus
}
#endif

#endif // MONERO_C_UTILS_H

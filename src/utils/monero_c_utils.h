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

// On Windows a program that uses the DLL includes the headers as they are, and the functions are imported from it. A program that links the
// static library, libmonero_c.a, has to define MONERO_C_STATIC before it includes them, or it looks for __imp_ symbols that the library does
// not have. The target monero_c-static of CMake defines it for the programs that link it
#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(MONERO_C_STATIC)
    #define MONERO_EXPORT
  #elif defined(MONERO_C_BUILD)
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

/**
 * Result of a call, returned by every function that can fail. On MONERO_ERROR, call monero_last_error()
 * for a human-readable message. The pointer outputs of a failed call are set to NULL and the
 * counts to 0, even when the failure is a NULL argument. Scalar outputs are left unchanged.
 */
typedef enum monero_result {
  MONERO_OK = 0,  /**< the call succeeded */
  MONERO_ERROR = -1  /**< the call failed, see monero_last_error() and monero_last_error_code() */
} monero_result;

/**
 * Kind of failure that monero_last_error_code() reports. The kind comes from the type of the exception that ended the call, not
 * from its text, so a failure that monero-cpp reports with its generic exception is MONERO_ERROR_GENERIC. That includes a daemon that
 * doesn't answer ("Network error: no response from ..."), a file that doesn't exist, a mnemonic that isn't valid and a wallet that is
 * closed: a program that has to tell them apart reads the message until monero-cpp has an exception of its own for each. More kinds
 * can be added before 1.0, so treat a value that you don't know as MONERO_ERROR_GENERIC.
 */
typedef enum monero_error_code {
  MONERO_ERROR_NONE = 0,              /**< no call has failed on this thread yet */
  MONERO_ERROR_GENERIC = 1,           /**< any other failure */
  MONERO_ERROR_INVALID_ARGUMENT = 2,  /**< a NULL pointer, a value out of range, or a text that doesn't parse */
  MONERO_ERROR_OUT_OF_MEMORY = 3,     /**< a memory allocation failed */
  MONERO_ERROR_RPC = 4,               /**< the daemon or the wallet RPC server answered with an error, or with an HTTP status that is not OK */
  MONERO_ERROR_IO = 5                 /**< the system reported an error of a file or a socket */
} monero_error_code;

/**
 * Network of an address or a wallet. The values mirror monero_network_type in monero-cpp (daemon/monero_daemon_model.h) and must
 * stay numerically identical, since they are cast directly across the bridge. REGTEST has no monero-cpp value. It maps to mainnet,
 * whose address format a regtest node uses, and the wallets on it are created with the regtest flag of monero-cpp.
 */
typedef enum monero_utils_network_type {
  MONERO_UTILS_NETWORK_MAINNET = 0,  /**< the main network */
  MONERO_UTILS_NETWORK_TESTNET = 1,  /**< the test network */
  MONERO_UTILS_NETWORK_STAGENET = 2,  /**< the stage network */
  MONERO_UTILS_NETWORK_REGTEST = 3  /**< a private regtest chain, with the addresses of mainnet */
} monero_utils_network_type;

// ---------------------------- ERROR / MEMORY --------------------------------

/**
 * Return the message of the most recent MONERO_ERROR result on the calling thread, from any
 * monero_c function. A successful call doesn't clear it, so read it only after MONERO_ERROR.
 *
 * @return the error message, or "" if none. The pointer belongs to monero-c and stays valid
 *   until the next call that fails on this thread. Copy it to keep it. Unlike the other
 *   strings that monero_c returns, don't pass it to monero_utils_free().
 */
MONERO_EXPORT const char* monero_last_error(void);

/**
 * Return the kind of the most recent MONERO_ERROR result on the calling thread, from any monero_c
 * function. It is set with the message of monero_last_error(), so read it right after the failing call,
 * before any other call on the thread. A successful call doesn't clear it, so read it only after
 * MONERO_ERROR.
 *
 * @return the kind of the failure, or MONERO_ERROR_NONE if no call has failed on this thread
 */
MONERO_EXPORT monero_error_code monero_last_error_code(void);

/**
 * Return the code of the RPC error that is the most recent MONERO_ERROR result on the calling thread, from any monero_c function.
 * The code is the one that the daemon or the wallet RPC server sent in its JSON-RPC error (for example -2 when a block height is
 * too big for monerod), or the HTTP status when the server answered with a status that is not OK (401 for a login that it
 * refuses). It is -1 when the error has no code. Like monero_last_error_code(), it is set with the message, so read it right after
 * the failing call, before any other call on the thread, and only after MONERO_ERROR.
 *
 * @return the code of the RPC error, or 0 if the most recent failure on this thread is not MONERO_ERROR_RPC, or none has failed yet
 */
MONERO_EXPORT int32_t monero_last_error_rpc_code(void);

/**
 * Free a string, a byte buffer or an array that a monero_c function returned. Passing NULL does nothing.
 *
 * @param ptr is the pointer that the function returned, or NULL
 */
MONERO_EXPORT void monero_utils_free(void* ptr);

/**
 * Overwrite a secret with zeros and free it, so it doesn't stay in freed memory. Use it instead of
 * monero_utils_free() for the strings that hold a seed or a private key: monero_wallet_get_seed(),
 * monero_wallet_get_private_view_key(), monero_wallet_get_private_spend_key() and monero_wallet_get_tx_key().
 * The copies that the caller made of the secret are the caller's to overwrite.
 *
 * @param secret is a string that one of those functions returned, or NULL
 */
MONERO_EXPORT void monero_utils_free_secret(char* secret);

// ---------------------------------- VERSION ---------------------------------

/**
 * Major version of the C ABI. The version is major.minor.patch, like the releases of monero-cpp. While the major version is 0, a change
 * of the minor version can break callers. From 1, only a change of the major version breaks callers.
 */
#define MONERO_C_ABI_VERSION_MAJOR 0

/** Minor version of the C ABI. It grows when functions are added, and before 1.0 when a change breaks callers. */
#define MONERO_C_ABI_VERSION_MINOR 3

/** Patch version of the C ABI. It changes for fixes that leave the ABI as it is. */
#define MONERO_C_ABI_VERSION_PATCH 0

/**
 * Get the version of the C ABI of the loaded library. A binding compares it with the version it
 * was written for, to detect a library that doesn't match.
 *
 * @param out_major receives the major version
 * @param out_minor receives the minor version
 * @param out_patch receives the patch version
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_utils_get_abi_version(uint32_t* out_major, uint32_t* out_minor, uint32_t* out_patch);

// --------------------------------- LOGGING ----------------------------------

/**
 * Set the log level, which picks default category filters (0 = warnings and errors only,
 * 4 = full trace). Use monero_utils_set_log_categories() for finer control.
 *
 * @param level is the log level, 0 (least verbose) through 4 (most verbose)
 */
MONERO_EXPORT void monero_utils_set_log_level(int32_t level);

/**
 * Set the log categories filter directly, overriding whatever
 * monero_utils_set_log_level() last selected.
 *
 * @param categories is a comma-separated list of "category:level" pairs
 *   (e.g. "*:WARNING,net:FATAL"). Prefix with '+' to append to the current filter, or
 *   '-' to remove matching categories from it
 */
MONERO_EXPORT void monero_utils_set_log_categories(const char* categories);

/**
 * Configure where log output is written.
 *
 * @param path is the log file's base path
 * @param console is whether to also write log output to the console
 */
MONERO_EXPORT void monero_utils_configure_logging(const char* path, bool console);

// -------------------------------- VALIDATION --------------------------------
// pure predicates: never fail, false just means "invalid"

/**
 * Check whether an address is valid for a given network.
 *
 * @param address is the address to validate
 * @param network_type is the network to validate the address against
 * @return true if the address is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_address(const char* address, int32_t network_type);

/**
 * Check whether a private view key is valid.
 *
 * @param private_view_key is the private view key to validate
 * @return true if the private view key is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_private_view_key(const char* private_view_key);

/**
 * Check whether a private spend key is valid.
 *
 * @param private_spend_key is the private spend key to validate
 * @return true if the private spend key is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_private_spend_key(const char* private_spend_key);

/**
 * Check whether a public view key is valid.
 *
 * @param public_view_key is the public view key to validate
 * @return true if the public view key is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_public_view_key(const char* public_view_key);

/**
 * Check whether a public spend key is valid.
 *
 * @param public_spend_key is the public spend key to validate
 * @return true if the public spend key is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_public_spend_key(const char* public_spend_key);

/**
 * Check whether a payment id is valid.
 *
 * @param payment_id is the payment id to validate, as 16 or 64 hex characters
 * @return true if the payment id is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_payment_id(const char* payment_id);

/**
 * Check whether a mnemonic seed is valid. A mnemonic of more than 4096 bytes is never valid.
 *
 * @param mnemonic is the mnemonic seed to validate
 * @param language restricts validation to that seed language, or "" to accept any language
 * @return true if the mnemonic is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_mnemonic(const char* mnemonic, const char* language);

/**
 * Check whether a seed language is valid.
 *
 * @param language is the language to validate (case-sensitive, e.g. "English")
 * @return true if the language is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_language(const char* language);

// same checks as above, but on failure they return MONERO_ERROR and set monero_last_error()

/**
 * Validate an address for a given network, like monero_utils_is_valid_address()
 * but reports the reason through monero_last_error() on failure.
 *
 * @param address is the address to validate
 * @param network_type is the network to validate the address against
 * @return MONERO_OK if the address is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_address(const char* address, int32_t network_type);

/**
 * Validate a private view key, like monero_utils_is_valid_private_view_key()
 * but reports the reason through monero_last_error() on failure.
 *
 * @param private_view_key is the private view key to validate
 * @return MONERO_OK if the private view key is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_private_view_key(const char* private_view_key);

/**
 * Validate a private spend key, like monero_utils_is_valid_private_spend_key()
 * but reports the reason through monero_last_error() on failure.
 *
 * @param private_spend_key is the private spend key to validate
 * @return MONERO_OK if the private spend key is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_private_spend_key(const char* private_spend_key);

/**
 * Validate a public view key, like monero_utils_is_valid_public_view_key()
 * but reports the reason through monero_last_error() on failure.
 *
 * @param public_view_key is the public view key to validate
 * @return MONERO_OK if the public view key is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_public_view_key(const char* public_view_key);

/**
 * Validate a public spend key, like monero_utils_is_valid_public_spend_key()
 * but reports the reason through monero_last_error() on failure.
 *
 * @param public_spend_key is the public spend key to validate
 * @return MONERO_OK if the public spend key is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_public_spend_key(const char* public_spend_key);

/**
 * Validate a payment id, like monero_utils_is_valid_payment_id() but reports the reason
 * through monero_last_error() on failure.
 *
 * @param payment_id is the payment id to validate, as 16 or 64 hex characters
 * @return MONERO_OK if the payment id is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_payment_id(const char* payment_id);

/**
 * Validate a mnemonic seed, like monero_utils_is_valid_mnemonic() but reports the reason
 * through monero_last_error() on failure. A mnemonic of more than 4096 bytes fails.
 *
 * @param mnemonic is the mnemonic seed to validate
 * @param language restricts validation to that seed language, or "" to accept any language
 * @return MONERO_OK if the mnemonic is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_mnemonic(const char* mnemonic, const char* language);

// --------------------------- INTEGRATED ADDRESS / URIS ----------------------
// structured in/out values cross the bridge as JSON (monero-cpp's own
// serializable_struct::serialize()/::deserialize() format), so the C side
// doesn't need to mirror every C++ model struct field by field

/**
 * Build an integrated address from a standard address and payment id.
 *
 * @param network_type is the network to validate the address against
 * @param standard_address is the base address to integrate the payment id into
 * @param payment_id is the payment id to integrate, or "" to generate a random one
 * @param out_json receives a JSON-serialized monero_integrated_address, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the address or payment id is invalid
 */
MONERO_EXPORT monero_result monero_utils_get_integrated_address(int32_t network_type, const char* standard_address, const char* payment_id, char** out_json);

/**
 * Create a payment URI from a transaction configuration.
 *
 * @param tx_config_json is a JSON-serialized monero_tx_config with exactly one destination (address + amount)
 * @param network_type is the network to format the URI's address for
 * @param out_uri receives the payment URI string, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the transaction config is invalid
 */
MONERO_EXPORT monero_result monero_utils_get_payment_uri(const char* tx_config_json, int32_t network_type, char** out_uri);

/**
 * Parse a payment URI into a transaction configuration.
 *
 * @param uri is the payment URI to parse
 * @param network_type is the network to validate the URI's address against
 * @param out_tx_config_json receives a JSON-serialized monero_tx_config parsed from the URI, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the URI is invalid
 */
MONERO_EXPORT monero_result monero_utils_parse_payment_uri(const char* uri, int32_t network_type, char** out_tx_config_json);

// ----------------------------- JSON / BINARY RPC ----------------------------
// binary payloads (as used by monerod's .bin RPC endpoints) are not
// null-terminated text, so they cross the bridge as explicit (data, length) pairs

/**
 * Convert a JSON string to its binary-encoded (portable storage format) equivalent.
 *
 * @param json is the JSON string to convert
 * @param out_data receives the binary-encoded buffer, freed with monero_utils_free()
 * @param out_len receives the length of the binary-encoded buffer
 * @return MONERO_OK on success, or MONERO_ERROR if the JSON is invalid
 */
MONERO_EXPORT monero_result monero_utils_json_to_binary(const char* json, uint8_t** out_data, size_t* out_len);

/**
 * Convert a binary-encoded (portable storage format) buffer to its JSON equivalent.
 *
 * @param data is the binary-encoded buffer to convert
 * @param len is the length of the binary-encoded buffer
 * @param out_json receives the decoded JSON string, freed with monero_utils_free()
 * @return MONERO_OK on success, or MONERO_ERROR if the buffer is invalid
 */
MONERO_EXPORT monero_result monero_utils_binary_to_json(const uint8_t* data, size_t len, char** out_json);

/**
 * Decode a get_blocks_by_height.bin-style response. The JSON has "blocks" (each block as a JSON string), "txs" (the
 * transactions of each block), "block_hashes", "status" and "untrusted".
 *
 * @param data is the binary-encoded buffer to decode
 * @param len is the length of the binary-encoded buffer
 * @param out_json receives the decoded JSON string, freed with monero_utils_free(). See monero_utils_binary_to_json().
 * @return MONERO_OK on success, or MONERO_ERROR if the buffer is invalid
 */
MONERO_EXPORT monero_result monero_utils_binary_blocks_to_json(const uint8_t* data, size_t len, char** out_json);

/**
 * Decode a get_blocks.bin-style response. The JSON has what monero_utils_binary_blocks_to_json() gives, and
 * "start_height" and "current_height" too.
 *
 * @param data is the binary-encoded buffer to decode
 * @param len is the length of the binary-encoded buffer
 * @param out_json receives the decoded JSON string, freed with monero_utils_free(). See monero_utils_binary_to_json().
 * @return MONERO_OK on success, or MONERO_ERROR if the buffer is invalid
 */
MONERO_EXPORT monero_result monero_utils_binary_blocks_fast_to_json(const uint8_t* data, size_t len, char** out_json);

// -------------------------------- AMOUNTS / IDS -----------------------------

/**
 * Convert an amount in XMR to atomic units.
 *
 * @param amount_xmr is the amount in XMR to convert
 * @param out_atomic_units receives the amount in atomic units (1 XMR = 10^12)
 * @return MONERO_OK on success, or MONERO_ERROR if amount_xmr is negative, non-finite, or too large to represent in atomic units
 */
MONERO_EXPORT monero_result monero_utils_xmr_to_atomic_units(double amount_xmr, uint64_t* out_atomic_units);

/**
 * Convert an amount in atomic units to XMR.
 *
 * @param amount_atomic_units is the amount in atomic units to convert
 * @return the amount in XMR
 */
MONERO_EXPORT double monero_utils_atomic_units_to_xmr(uint64_t amount_atomic_units);

/**
 * Parse a long payment ID from a string.
 *
 * @param payment_id_str is the string to parse
 * @param out_payment_id receives the parsed 32-byte payment id if valid
 * @return true if the payment ID is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_parse_payment_id_long(const char* payment_id_str, uint8_t out_payment_id[32]);

/**
 * Parse a short payment ID from a string.
 *
 * @param payment_id_str is the string to parse
 * @param out_payment_id receives the parsed 8-byte payment id if valid
 * @return true if the payment ID is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_parse_payment_id_short(const char* payment_id_str, uint8_t out_payment_id[8]);

/**
 * Check whether a long payment id is valid.
 *
 * @param payment_id_str is the payment id to validate, as 64 hex characters
 * @return true if the payment id is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_payment_id_long(const char* payment_id_str);

/**
 * Check whether a short payment id is valid.
 *
 * @param payment_id_str is the payment id to validate, as 16 hex characters
 * @return true if the payment id is valid, false otherwise
 */
MONERO_EXPORT bool monero_utils_is_valid_payment_id_short(const char* payment_id_str);

/**
 * Validate a long payment id, like monero_utils_is_valid_payment_id_long()
 * but reports the reason through monero_last_error() on failure.
 *
 * @param payment_id_str is the payment id to validate, as 64 hex characters
 * @return MONERO_OK if the payment id is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_payment_id_long(const char* payment_id_str);

/**
 * Validate a short payment id, like monero_utils_is_valid_payment_id_short()
 * but reports the reason through monero_last_error() on failure.
 *
 * @param payment_id_str is the payment id to validate, as 16 hex characters
 * @return MONERO_OK if the payment id is valid, or MONERO_ERROR otherwise (see monero_last_error())
 */
MONERO_EXPORT monero_result monero_utils_validate_payment_id_short(const char* payment_id_str);

/**
 * Get the ring size that the network enforces.
 *
 * @return the ring size, 16 (monero_utils::RING_SIZE)
 */
MONERO_EXPORT int32_t monero_utils_get_ring_size(void);

// ----------------------------------- PATHS ----------------------------------

/**
 * Get the default directory of the shared ring database of a network, where wallet2 keeps the rings of the key images that it
 * spent. It is the .shared-ringdb directory next to the data directory of monerod (~/.shared-ringdb on Linux and macOS, with
 * testnet or stagenet below it for those networks), as monero-wallet-cli and monero-wallet-rpc choose it. The path is built from
 * the home of the user, and the directory is not made. Regtest gives the path of mainnet, since a regtest wallet is a mainnet one.
 *
 * @param network_type is the monero_utils_network_type of the wallet
 * @param out_path receives the path, freed with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR if network_type is not one of the enum
 */
MONERO_EXPORT monero_result monero_utils_get_default_ringdb_path(int32_t network_type, char** out_path);

#ifdef __cplusplus
}
#endif

#endif // MONERO_C_UTILS_H

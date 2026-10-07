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

#ifndef MONERO_C_WALLET_H
#define MONERO_C_WALLET_H

#include "utils/monero_c_utils.h"
#include "common/monero_c_rpc_connection.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------- WALLET ------------------------------------
// wraps the monero_wallet methods of the same name. On error the call returns MONERO_ERROR,
// monero_last_error() gives the message, string outputs are NULL and scalar outputs are unchanged

/**
 * Opaque handle to a wallet. Full wallets come from monero_wallet_create_random(),
 * monero_wallet_create_from_seed(), monero_wallet_create_from_keys() and monero_wallet_open().
 * Keys-only wallets come from the monero_wallet_keys_create_*() functions. Release the handle
 * with monero_wallet_free().
 *
 * @par Thread safety
 * Don't use one handle from two threads at once. Listener callbacks run inside monero-cpp, so
 * don't free the handle or a listener from a callback.
 */
typedef struct monero_wallet monero_wallet;

/**
 * Opaque listener that receives the wallet's notifications. Create it with
 * monero_wallet_listener_create() and release it with monero_wallet_listener_free().
 *
 * @par Thread safety
 * Don't add, remove or free the same listener from two threads at once. Adding one listener to
 * two wallets from two threads is safe, and one of the adds fails.
 */
typedef struct monero_wallet_listener monero_wallet_listener;

/**
 * Called with the progress of a sync. The message is valid only during the call.
 *
 * @param user_data is the user_data of the monero_wallet_listener_callbacks
 * @param height is the height of the synced block
 * @param start_height is the starting height of the sync request
 * @param end_height is the ending height of the sync request
 * @param percent_done is the sync progress as a percentage
 * @param message is a human-readable description of the progress
 */
typedef void (*monero_wallet_on_sync_progress_fn)(void* user_data, uint64_t height, uint64_t start_height, uint64_t end_height, double percent_done, const char* message);

/**
 * Called when a new block is processed.
 *
 * @param user_data is the user_data of the monero_wallet_listener_callbacks
 * @param height is the height of the new block
 */
typedef void (*monero_wallet_on_new_block_fn)(void* user_data, uint64_t height);

/**
 * Called when the balances of the wallet change.
 *
 * @param user_data is the user_data of the monero_wallet_listener_callbacks
 * @param new_balance is the new balance in atomic units
 * @param new_unlocked_balance is the new unlocked balance in atomic units
 */
typedef void (*monero_wallet_on_balances_changed_fn)(void* user_data, uint64_t new_balance, uint64_t new_unlocked_balance);

/**
 * Called when the wallet receives an output. The JSON is valid only during the call.
 *
 * @param user_data is the user_data of the monero_wallet_listener_callbacks
 * @param output_json is the JSON-serialized monero_output_wallet
 */
typedef void (*monero_wallet_on_output_received_fn)(void* user_data, const char* output_json);

/**
 * Called when the wallet spends an output. The JSON is valid only during the call.
 *
 * @param user_data is the user_data of the monero_wallet_listener_callbacks
 * @param output_json is the JSON-serialized monero_output_wallet
 */
typedef void (*monero_wallet_on_output_spent_fn)(void* user_data, const char* output_json);

/**
 * Callbacks of a wallet listener. The struct is copied by monero_wallet_listener_create().
 * A NULL callback is skipped.
 */
typedef struct monero_wallet_listener_callbacks {
  void* user_data;
  monero_wallet_on_sync_progress_fn on_sync_progress;
  monero_wallet_on_new_block_fn on_new_block;
  monero_wallet_on_balances_changed_fn on_balances_changed;
  monero_wallet_on_output_received_fn on_output_received;
  monero_wallet_on_output_spent_fn on_output_spent;
} monero_wallet_listener_callbacks;

/**
 * Create a wallet with a new random seed and save it to disk. The keys are written to path + ".keys".
 * The wallet scans from an estimate of the current height, or from 0 on regtest.
 *
 * @param path is the path of the wallet file
 * @param password encrypts the wallet files. NULL or "" for no password
 * @param network_type is the monero_utils_network_type of the wallet
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle, released with monero_wallet_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_create_random(const char* path, const char* password, int32_t network_type, const char* language, monero_wallet** out_wallet);

/**
 * Restore a wallet from its mnemonic seed and save it to disk.
 *
 * @param path is the path of the wallet file
 * @param password encrypts the wallet files. NULL or "" for no password
 * @param network_type is the monero_utils_network_type of the wallet
 * @param seed is the mnemonic seed
 * @param seed_offset is the passphrase that extends the seed. NULL or "" for none
 * @param restore_height is the first block to scan. 0 scans from the genesis block
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle, released with monero_wallet_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_create_from_seed(const char* path, const char* password, int32_t network_type, const char* seed, const char* seed_offset, uint64_t restore_height, const char* language, monero_wallet** out_wallet);

/**
 * Restore a wallet from its keys and save it to disk. Without a private spend key the wallet
 * is view-only.
 *
 * @param path is the path of the wallet file
 * @param password encrypts the wallet files. NULL or "" for no password
 * @param network_type is the monero_utils_network_type of the wallet
 * @param address is the primary address
 * @param private_view_key is the private view key, in hex
 * @param private_spend_key is the private spend key, in hex. NULL or "" for a view-only wallet
 * @param restore_height is the first block to scan. 0 scans from the genesis block
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle, released with monero_wallet_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_create_from_keys(const char* path, const char* password, int32_t network_type, const char* address, const char* private_view_key, const char* private_spend_key, uint64_t restore_height, const char* language, monero_wallet** out_wallet);

/**
 * Open a wallet from its files on disk.
 *
 * @param path is the path of the wallet file
 * @param password decrypts the wallet files. NULL or "" for no password
 * @param network_type is the monero_utils_network_type of the wallet
 * @param out_wallet receives the handle, released with monero_wallet_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_open(const char* path, const char* password, int32_t network_type, monero_wallet** out_wallet);

/**
 * Check if a wallet exists at the given path. It looks for the keys file, path + ".keys".
 *
 * @param path is the path of the wallet file
 * @param out_exists receives true if the wallet exists
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_exists(const char* path, bool* out_exists);

/**
 * Get the languages a mnemonic seed can be written in.
 *
 * @param out_json receives a JSON array of language names. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_seed_languages(char** out_json);

/**
 * Create a keys-only wallet with a new random seed. It has no files, so monero_wallet_save() and
 * monero_wallet_close(wallet, true) fail on it. It also has no height, sync or transactions.
 *
 * @param network_type is the monero_utils_network_type of the wallet
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle, released with monero_wallet_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_keys_create_random(int32_t network_type, const char* language, monero_wallet** out_wallet);

/**
 * Restore a keys-only wallet from its mnemonic seed.
 *
 * @param network_type is the monero_utils_network_type of the wallet
 * @param seed is the mnemonic seed
 * @param seed_offset is the passphrase that extends the seed. NULL or "" for none
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle, released with monero_wallet_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_keys_create_from_seed(int32_t network_type, const char* seed, const char* seed_offset, const char* language, monero_wallet** out_wallet);

/**
 * Restore a keys-only wallet from its keys. Without a private spend key the wallet is view-only.
 *
 * @param network_type is the monero_utils_network_type of the wallet
 * @param address is the primary address
 * @param private_view_key is the private view key, in hex
 * @param private_spend_key is the private spend key, in hex. NULL or "" for a view-only wallet
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle, released with monero_wallet_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_keys_create_from_keys(int32_t network_type, const char* address, const char* private_view_key, const char* private_spend_key, const char* language, monero_wallet** out_wallet);

/**
 * Save the wallet to disk.
 *
 * @param wallet is the wallet handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_save(monero_wallet* wallet);

/**
 * Close the wallet. Its methods fail afterwards, but the handle must still be freed.
 *
 * @param wallet is the wallet handle
 * @param save saves the wallet to disk before closing it
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_close(monero_wallet* wallet, bool save);

/**
 * Release a wallet handle. A wallet that is still open is closed without saving, so call
 * monero_wallet_save() first to keep the changes.
 *
 * @param wallet is the handle to release
 */
MONERO_EXPORT void monero_wallet_free(monero_wallet* wallet);

/**
 * Get the network of the wallet.
 *
 * @param wallet is the wallet handle
 * @param out_network_type receives the network
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_network_type(monero_wallet* wallet, monero_utils_network_type* out_network_type);

/**
 * Check if the wallet is view-only, which means it has no private spend key.
 *
 * @param wallet is the wallet handle
 * @param out_view_only receives true for a view-only wallet
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_view_only(monero_wallet* wallet, bool* out_view_only);

/**
 * Check if the wallet is a multisig wallet. Keys-only wallets don't support multisig, so this fails
 * for them.
 *
 * @param wallet is the wallet handle
 * @param out_multisig receives true for a multisig wallet
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_multisig(monero_wallet* wallet, bool* out_multisig);

/**
 * Get the mnemonic seed of the wallet. Fails for a view-only wallet.
 *
 * @param wallet is the wallet handle
 * @param out_seed receives the seed. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_seed(monero_wallet* wallet, char** out_seed);

/**
 * Get the language of the mnemonic seed.
 *
 * @param wallet is the wallet handle
 * @param out_language receives the language name. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_seed_language(monero_wallet* wallet, char** out_language);

/**
 * Get the primary address of the wallet.
 *
 * @param wallet is the wallet handle
 * @param out_address receives the address. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_primary_address(monero_wallet* wallet, char** out_address);

/**
 * Get the private view key of the wallet, in hex.
 *
 * @param wallet is the wallet handle
 * @param out_key receives the key. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_private_view_key(monero_wallet* wallet, char** out_key);

/**
 * Get the private spend key of the wallet, in hex. Fails for a view-only wallet.
 *
 * @param wallet is the wallet handle
 * @param out_key receives the key. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_private_spend_key(monero_wallet* wallet, char** out_key);

/**
 * Get the height of the blocks the wallet has synced to.
 *
 * @param wallet is the wallet handle
 * @param out_height receives the height
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_height(monero_wallet* wallet, uint64_t* out_height);

/**
 * Get the balance of the wallet, including funds that are still locked.
 *
 * @param wallet is the wallet handle
 * @param out_balance receives the balance in atomic units
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_balance(monero_wallet* wallet, uint64_t* out_balance);

/**
 * Get the unlocked balance of the wallet, the part that can be spent now.
 *
 * @param wallet is the wallet handle
 * @param out_balance receives the unlocked balance in atomic units
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_unlocked_balance(monero_wallet* wallet, uint64_t* out_balance);

/**
 * Get the balance of an account, including funds that are still locked.
 * Keys-only wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the account index
 * @param out_balance receives the balance in atomic units
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_account_balance(monero_wallet* wallet, uint32_t account_idx, uint64_t* out_balance);

/**
 * Get the balance of a subaddress, including funds that are still locked.
 * Keys-only wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the account index
 * @param subaddress_idx is the subaddress index within the account
 * @param out_balance receives the balance in atomic units
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_subaddress_balance(monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, uint64_t* out_balance);

/**
 * Get the unlocked balance of an account, the part that can be spent now.
 * Keys-only wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the account index
 * @param out_balance receives the unlocked balance in atomic units
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_account_unlocked_balance(monero_wallet* wallet, uint32_t account_idx, uint64_t* out_balance);

/**
 * Get the unlocked balance of a subaddress, the part that can be spent now.
 * Keys-only wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the account index
 * @param subaddress_idx is the subaddress index within the account
 * @param out_balance receives the unlocked balance in atomic units
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_subaddress_unlocked_balance(monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, uint64_t* out_balance);

/**
 * Key that signs a message. Values match monero-cpp's monero_message_signature_type.
 */
typedef enum monero_message_signature_type {
  MONERO_MESSAGE_SIGN_WITH_SPEND_KEY = 0,
  MONERO_MESSAGE_SIGN_WITH_VIEW_KEY = 1
} monero_message_signature_type;

/**
 * Get the address of an account and subaddress. Account 0, subaddress 0 is the primary address.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the account index
 * @param subaddress_idx is the subaddress index within the account
 * @param out_address receives the address. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_address(monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, char** out_address);

/**
 * Get the integrated address of a standard address and a payment ID.
 *
 * @param wallet is the wallet handle
 * @param standard_address is the standard address. NULL or "" for the primary address
 * @param payment_id is the payment ID, in hex. NULL or "" for a random one
 * @param out_json receives the JSON-serialized monero_integrated_address. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_integrated_address(monero_wallet* wallet, const char* standard_address, const char* payment_id, char** out_json);

/**
 * Decode an integrated address into its standard address and payment ID.
 *
 * @param wallet is the wallet handle
 * @param integrated_address is the integrated address
 * @param out_json receives the JSON-serialized monero_integrated_address. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_decode_integrated_address(monero_wallet* wallet, const char* integrated_address, char** out_json);

/**
 * Get an account, with or without its subaddresses.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the account index
 * @param include_subaddresses is true to include the subaddresses of the account
 * @param out_json receives the JSON-serialized monero_account. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_account(monero_wallet* wallet, uint32_t account_idx, bool include_subaddresses, char** out_json);

/**
 * Get the subaddresses of an account. With no indices, a full wallet returns all of them. Keys-only
 * wallets can't enumerate their subaddresses, so they need the indices.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the account index
 * @param subaddress_indices are the subaddress indices, or NULL for all of them
 * @param num_subaddress_indices is the number of elements in subaddress_indices, 0 for all of them
 * @param out_json receives a JSON array of monero_subaddress. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_subaddresses(monero_wallet* wallet, uint32_t account_idx, const uint32_t* subaddress_indices, size_t num_subaddress_indices, char** out_json);

/**
 * Sign a message with the spend key or the view key of an account and subaddress.
 *
 * @param wallet is the wallet handle
 * @param message is the message to sign
 * @param signature_type is the monero_message_signature_type, the key that signs the message
 * @param account_idx is the account index
 * @param subaddress_idx is the subaddress index within the account
 * @param out_signature receives the signature. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_sign_message(monero_wallet* wallet, const char* message, int32_t signature_type, uint32_t account_idx, uint32_t subaddress_idx, char** out_signature);

/**
 * Verify a signature made by monero_wallet_sign_message() for an address. A signature that doesn't
 * match is not an error. The result has isGood set to false.
 *
 * @param wallet is the wallet handle
 * @param message is the signed message
 * @param address is the address that signed the message
 * @param signature is the signature
 * @param out_json receives the JSON-serialized monero_message_signature_result. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_verify_message(monero_wallet* wallet, const char* message, const char* address, const char* signature, char** out_json);

/**
 * Fee priority of a transaction. Values match monero-cpp's monero_tx_priority.
 */
typedef enum monero_tx_priority {
  MONERO_TX_PRIORITY_DEFAULT = 0,
  MONERO_TX_PRIORITY_UNIMPORTANT = 1,
  MONERO_TX_PRIORITY_NORMAL = 2,
  MONERO_TX_PRIORITY_ELEVATED = 3
} monero_tx_priority;

/**
 * Add an address to the address book.
 *
 * @param wallet is the wallet handle
 * @param address is the address to add
 * @param description is the description of the address. NULL or "" for none
 * @param out_index receives the index of the new entry
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_add_address_book_entry(monero_wallet* wallet, const char* address, const char* description, uint64_t* out_index);

/**
 * Edit an address book entry. A NULL address or description is left unchanged.
 *
 * @param wallet is the wallet handle
 * @param index is the index of the entry
 * @param address is the new address, or NULL
 * @param description is the new description, or NULL
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_edit_address_book_entry(monero_wallet* wallet, uint64_t index, const char* address, const char* description);

/**
 * Delete an address book entry.
 *
 * @param wallet is the wallet handle
 * @param index is the index of the entry
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_delete_address_book_entry(monero_wallet* wallet, uint64_t index);

/**
 * Get the address book entries with the given indices. With no indices, all of them are returned.
 *
 * @param wallet is the wallet handle
 * @param indices are the indices of the entries
 * @param num_indices is the number of elements in indices
 * @param out_json receives a JSON array of monero_address_book_entry. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_address_book_entries(monero_wallet* wallet, const uint64_t* indices, size_t num_indices, char** out_json);

/**
 * Change the password of the wallet files.
 *
 * @param wallet is the wallet handle
 * @param old_password is the current password
 * @param new_password is the new password
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_change_password(monero_wallet* wallet, const char* old_password, const char* new_password);

/**
 * Create an account, with the next account index.
 *
 * @param wallet is the wallet handle
 * @param label is the label of the account. NULL or "" for none
 * @param out_json receives the JSON-serialized monero_account. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_create_account(monero_wallet* wallet, const char* label, char** out_json);

/**
 * Create a subaddress in an account.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the index of the account
 * @param label is the label of the subaddress. NULL or "" for none
 * @param out_json receives the JSON-serialized monero_subaddress. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_create_subaddress(monero_wallet* wallet, uint32_t account_idx, const char* label, char** out_json);

/**
 * Get the accounts of the wallet.
 *
 * @param wallet is the wallet handle
 * @param include_subaddresses is true to include the subaddresses of each account
 * @param out_json receives a JSON array of monero_account. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_accounts(monero_wallet* wallet, bool include_subaddresses, char** out_json);

/**
 * Get the subaddress of an address.
 *
 * @param wallet is the wallet handle
 * @param address is the address
 * @param out_json receives the JSON-serialized monero_subaddress. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_address_index(monero_wallet* wallet, const char* address, char** out_json);

/**
 * Get a subaddress by its indices. Full wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the index of the account
 * @param subaddress_idx is the index of the subaddress within the account
 * @param out_json receives the JSON-serialized monero_subaddress. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_subaddress(monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, char** out_json);

/**
 * Set the label of a subaddress.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the index of the account
 * @param subaddress_idx is the index of the subaddress within the account
 * @param label is the new label. NULL or "" to clear it
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_set_subaddress_label(monero_wallet* wallet, uint32_t account_idx, uint32_t subaddress_idx, const char* label);

/**
 * Set the label of an account tag. Full wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param tag is the tag
 * @param label is the new label
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_set_account_tag_label(monero_wallet* wallet, const char* tag, const char* label);

/**
 * Get the account tags of the wallet. Full wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param out_json receives a JSON array of monero_account_tag. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_account_tags(monero_wallet* wallet, char** out_json);

/**
 * Tag accounts with a tag. Full wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param tag is the tag
 * @param account_indices are the indices of the accounts
 * @param num_account_indices is the number of elements in account_indices
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_tag_accounts(monero_wallet* wallet, const char* tag, const uint32_t* account_indices, size_t num_account_indices);

/**
 * Remove the tags of accounts. Full wallets don't support it.
 *
 * @param wallet is the wallet handle
 * @param account_indices are the indices of the accounts
 * @param num_account_indices is the number of elements in account_indices
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_untag_accounts(monero_wallet* wallet, const uint32_t* account_indices, size_t num_account_indices);

/**
 * Get an attribute, a key and value that the wallet stores with its files.
 *
 * @param wallet is the wallet handle
 * @param key is the key of the attribute
 * @param out_found receives true if the attribute exists
 * @param out_value receives the value, or NULL if it doesn't exist. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_attribute(monero_wallet* wallet, const char* key, bool* out_found, char** out_value);

/**
 * Set an attribute.
 *
 * @param wallet is the wallet handle
 * @param key is the key of the attribute
 * @param value is the value of the attribute
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_set_attribute(monero_wallet* wallet, const char* key, const char* value);

/**
 * Get the default priority of the fee of the transactions.
 *
 * @param wallet is the wallet handle
 * @param out_priority receives the priority
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_default_fee_priority(monero_wallet* wallet, monero_tx_priority* out_priority);

/**
 * Get the path of the wallet file.
 *
 * @param wallet is the wallet handle
 * @param out_path receives the path. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_path(monero_wallet* wallet, char** out_path);

/**
 * Get the public view key of the wallet, in hex.
 *
 * @param wallet is the wallet handle
 * @param out_key receives the key. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_public_view_key(monero_wallet* wallet, char** out_key);

/**
 * Get the public spend key of the wallet, in hex.
 *
 * @param wallet is the wallet handle
 * @param out_key receives the key. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_public_spend_key(monero_wallet* wallet, char** out_key);

/**
 * Get the version of the wallet library.
 *
 * @param wallet is the wallet handle
 * @param out_json receives the JSON-serialized monero_version. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_version(monero_wallet* wallet, char** out_json);

/**
 * Get the height from which the wallet scans the blocks.
 *
 * @param wallet is the wallet handle
 * @param out_height receives the height
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_restore_height(monero_wallet* wallet, uint64_t* out_height);

/**
 * Set the height from which the wallet scans the blocks.
 *
 * @param wallet is the wallet handle
 * @param restore_height is the height
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_set_restore_height(monero_wallet* wallet, uint64_t restore_height);

/**
 * Check if the wallet is closed.
 *
 * @param wallet is the wallet handle
 * @param out_closed receives true if the wallet is closed
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_closed(monero_wallet* wallet, bool* out_closed);

/**
 * Export the key images of the wallet.
 *
 * @param wallet is the wallet handle
 * @param all is true to include the key images of the spent outputs
 * @param out_json receives the JSON-serialized monero_key_image_export_result. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_export_key_images(monero_wallet* wallet, bool all, char** out_json);

/**
 * Import key images into the wallet.
 *
 * @param wallet is the wallet handle
 * @param key_images_json is a JSON array of monero_key_image
 * @param offset is the index of the first key image among the wallet's outputs
 * @param out_json receives the JSON-serialized monero_key_image_import_result. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_import_key_images(monero_wallet* wallet, const char* key_images_json, uint64_t offset, char** out_json);

/**
 * Export the outputs of the wallet.
 *
 * @param wallet is the wallet handle
 * @param all is true to include the spent outputs
 * @param out_hex receives the outputs in hex. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_export_outputs(monero_wallet* wallet, bool all, char** out_hex);

/**
 * Import outputs into the wallet.
 *
 * @param wallet is the wallet handle
 * @param outputs_hex are the outputs, in hex
 * @param out_num_imported receives the number of imported outputs
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_import_outputs(monero_wallet* wallet, const char* outputs_hex, int* out_num_imported);

/**
 * Freeze an output, so the wallet doesn't spend it. Fails for an unknown key image.
 *
 * @param wallet is the wallet handle
 * @param key_image is the key image of the output, in hex
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_freeze_output(monero_wallet* wallet, const char* key_image);

/**
 * Thaw a frozen output. Fails for an unknown key image.
 *
 * @param wallet is the wallet handle
 * @param key_image is the key image of the output, in hex
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_thaw_output(monero_wallet* wallet, const char* key_image);

/**
 * Check if an output is frozen. Fails for an unknown key image.
 *
 * @param wallet is the wallet handle
 * @param key_image is the key image of the output, in hex
 * @param out_frozen receives true if the output is frozen
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_output_frozen(monero_wallet* wallet, const char* key_image, bool* out_frozen);

/**
 * Get the note of a transaction.
 *
 * @param wallet is the wallet handle
 * @param tx_hash is the hash of the transaction
 * @param out_note receives the note. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_tx_note(monero_wallet* wallet, const char* tx_hash, char** out_note);

/**
 * Set the note of a transaction.
 *
 * @param wallet is the wallet handle
 * @param tx_hash is the hash of the transaction
 * @param note is the note
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_set_tx_note(monero_wallet* wallet, const char* tx_hash, const char* note);

/**
 * Get the notes of transactions.
 *
 * @param wallet is the wallet handle
 * @param tx_hashes are the hashes of the transactions
 * @param num_tx_hashes is the number of elements in tx_hashes
 * @param out_json receives a JSON array of notes. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_tx_notes(monero_wallet* wallet, const char* const* tx_hashes, size_t num_tx_hashes, char** out_json);

/**
 * Set the notes of transactions. Each note goes with the hash at the same position.
 *
 * @param wallet is the wallet handle
 * @param tx_hashes are the hashes of the transactions
 * @param notes are the notes
 * @param num_tx_hashes is the number of elements in tx_hashes and notes
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_set_tx_notes(monero_wallet* wallet, const char* const* tx_hashes, const char* const* notes, size_t num_tx_hashes);

/**
 * Set the daemon connection of the wallet.
 *
 * @param wallet is the wallet handle
 * @param uri is the URI of the daemon
 * @param username is the username of the daemon. NULL or "" for none
 * @param password is the password of the daemon. NULL or "" for none
 * @param proxy_uri is the URI of a proxy. NULL or "" for none
 * @param is_trusted is true if the daemon is trusted
 * @param ssl_verify is true to verify the daemon's TLS certificate and hostname
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_set_daemon_connection(monero_wallet* wallet, const char* uri, const char* username, const char* password, const char* proxy_uri, bool is_trusted, bool ssl_verify);

/**
 * Get the daemon connection of the wallet.
 *
 * @param wallet is the wallet handle
 * @param out_json receives the JSON-serialized monero_rpc_connection, or NULL if there is none. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_daemon_connection(monero_wallet* wallet, char** out_json);

/**
 * Check if the daemon is trusted.
 *
 * @param wallet is the wallet handle
 * @param out_trusted receives true if the daemon is trusted
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_daemon_trusted(monero_wallet* wallet, bool* out_trusted);

/**
 * Create a listener from its callbacks. The callbacks are copied, and the listener is released
 * with monero_wallet_listener_free().
 *
 * @param callbacks are the callbacks to call
 * @param out_listener receives the listener
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_listener_create(const monero_wallet_listener_callbacks* callbacks, monero_wallet_listener** out_listener);

/**
 * Release a listener. If the listener is registered with a wallet, it is removed first.
 *
 * @param listener is the listener to release
 */
MONERO_EXPORT void monero_wallet_listener_free(monero_wallet_listener* listener);

/**
 * Register a listener with a wallet. Adding it again to the same wallet does nothing. A listener
 * belongs to one wallet at a time, so adding it to another wallet fails until it is removed from
 * the first one.
 *
 * @param wallet is the wallet handle
 * @param listener is the listener to register
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_add_listener(monero_wallet* wallet, monero_wallet_listener* listener);

/**
 * Unregister a listener from a wallet.
 *
 * @param wallet is the wallet handle
 * @param listener is the listener to unregister
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_remove_listener(monero_wallet* wallet, monero_wallet_listener* listener);

/**
 * Unregister all the listeners of a wallet.
 *
 * @param wallet is the wallet handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_remove_listeners(monero_wallet* wallet);

// ------------------------------- DAEMON ------------------------------------
// these methods need a daemon connection, see monero_wallet_set_daemon_connection()

/**
 * Synchronize the wallet with the daemon in the calling thread.
 *
 * @param wallet is the wallet handle
 * @param start_height is the height to sync from, ignored if it is less than the last processed block. NULL for none
 * @param listener receives the notifications of this sync. NULL for none. Free it after the call returns
 * @param out_json receives the JSON-serialized monero_sync_result. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_sync(monero_wallet* wallet, const uint64_t* start_height, monero_wallet_listener* listener, char** out_json);

/**
 * Start a background thread that syncs the wallet with the daemon. Stop it with monero_wallet_stop_syncing().
 *
 * @param wallet is the wallet handle
 * @param sync_period_ms is the maximum time between two syncs in milliseconds
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_start_syncing(monero_wallet* wallet, uint64_t sync_period_ms);

/**
 * Stop the background thread that syncs the wallet.
 *
 * @param wallet is the wallet handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_stop_syncing(monero_wallet* wallet);

/**
 * Check if the wallet is connected to a daemon.
 *
 * @param wallet is the wallet handle
 * @param out_connected receives true if the wallet is connected
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_connected_to_daemon(monero_wallet* wallet, bool* out_connected);

/**
 * Check if the daemon of the wallet is synced with the network.
 *
 * @param wallet is the wallet handle
 * @param out_synced receives true if the daemon is synced
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_daemon_synced(monero_wallet* wallet, bool* out_synced);

/**
 * Check if the wallet is synced with the daemon.
 *
 * @param wallet is the wallet handle
 * @param out_synced receives true if the wallet is synced
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_synced(monero_wallet* wallet, bool* out_synced);

/**
 * Get the height the daemon of the wallet is synced to.
 *
 * @param wallet is the wallet handle
 * @param out_height receives the height
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_daemon_height(monero_wallet* wallet, uint64_t* out_height);

/**
 * Get the maximum height of the peers the daemon of the wallet is connected to.
 *
 * @param wallet is the wallet handle
 * @param out_height receives the height
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_daemon_max_peer_height(monero_wallet* wallet, uint64_t* out_height);

/**
 * Wait until the next block is added to the chain. It blocks until a block arrives, and the wallet
 * must be syncing in the background with monero_wallet_start_syncing(), or it never returns.
 *
 * @param wallet is the wallet handle
 * @param out_height receives the height of the new block
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_wait_for_next_block(monero_wallet* wallet, uint64_t* out_height);

/**
 * Scan transactions by their hashes.
 *
 * @param wallet is the wallet handle
 * @param tx_hashes are the hashes of the transactions
 * @param num_tx_hashes is the number of elements in tx_hashes
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_scan_txs(monero_wallet* wallet, const char* const* tx_hashes, size_t num_tx_hashes);

/**
 * Rescan the blockchain from scratch. This discards local data that the blockchain can't restore,
 * like destination addresses, tx keys and tx notes.
 *
 * @param wallet is the wallet handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rescan_blockchain(monero_wallet* wallet);

/**
 * Rescan the blockchain for spent outputs. It needs a trusted daemon.
 *
 * @param wallet is the wallet handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rescan_spent(monero_wallet* wallet);

/**
 * Get the approximate height of the blockchain at a date. It is a conservative estimate for scanning.
 *
 * @param wallet is the wallet handle
 * @param year is the year
 * @param month is the month, from 1 to 12
 * @param day is the day, from 1 to 31
 * @param out_height receives the height
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_height_by_date(monero_wallet* wallet, uint16_t year, uint8_t month, uint8_t day, uint64_t* out_height);

/**
 * Start mining on the daemon. It needs a trusted daemon.
 *
 * @param wallet is the wallet handle
 * @param num_threads is the number of mining threads. NULL for one thread
 * @param background_mining is true to mine in the background. NULL for false
 * @param ignore_battery is true to mine on battery power. NULL for false
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_start_mining(monero_wallet* wallet, const uint64_t* num_threads, const bool* background_mining, const bool* ignore_battery);

/**
 * Stop mining on the daemon.
 *
 * @param wallet is the wallet handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_stop_mining(monero_wallet* wallet);

// ---------------------------- TRANSACTIONS ---------------------------------
// the JSON of a query or a config has the same fields as the monero-cpp model it describes
// NULL as a query means every item

/**
 * Get the transactions of the wallet.
 *
 * @param wallet is the wallet handle
 * @param query_json is the JSON-serialized monero_tx_query. NULL for all transactions
 * @param out_json receives a JSON array of monero_tx_wallet. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_txs(monero_wallet* wallet, const char* query_json, char** out_json);

/**
 * Get the incoming and outgoing transfers of the wallet.
 *
 * @param wallet is the wallet handle
 * @param query_json is the JSON-serialized monero_transfer_query. NULL for all transfers
 * @param out_json receives a JSON array of monero_incoming_transfer and monero_outgoing_transfer. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_transfers(monero_wallet* wallet, const char* query_json, char** out_json);

/**
 * Get the outputs of the wallet that it can spend.
 *
 * @param wallet is the wallet handle
 * @param query_json is the JSON-serialized monero_output_query. NULL for all outputs
 * @param out_json receives a JSON array of monero_output_wallet. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_outputs(monero_wallet* wallet, const char* query_json, char** out_json);

/**
 * Create a transaction that transfers funds from the wallet. It is relayed only if the config says so.
 *
 * @param wallet is the wallet handle
 * @param config_json is the JSON-serialized monero_tx_config
 * @param out_json receives the JSON-serialized monero_tx_wallet. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_create_tx(monero_wallet* wallet, const char* config_json, char** out_json);

/**
 * Create one or more transactions that transfer funds from the wallet. A view-only wallet
 * creates the unsigned hex that monero_wallet_sign_txs() takes, in the tx set.
 *
 * @param wallet is the wallet handle
 * @param config_json is the JSON-serialized monero_tx_config
 * @param out_json receives the JSON-serialized monero_tx_set with the transactions. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_create_txs(monero_wallet* wallet, const char* config_json, char** out_json);

/**
 * Relay a transaction that was created without relaying.
 *
 * @param wallet is the wallet handle
 * @param tx_metadata is the metadata of the transaction
 * @param out_hash receives the hash of the relayed transaction. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_relay_tx(monero_wallet* wallet, const char* tx_metadata, char** out_hash);

/**
 * Relay a transaction created without relaying, from its JSON. Only the metadata is used, so the output of
 * monero_wallet_create_tx() can be passed as it is.
 *
 * @param wallet is the wallet handle
 * @param tx_json is the JSON-serialized monero_tx_wallet with its metadata
 * @param out_hash receives the hash of the relayed transaction. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_relay_tx_json(monero_wallet* wallet, const char* tx_json, char** out_hash);

/**
 * Relay transactions that were created without relaying.
 *
 * @param wallet is the wallet handle
 * @param tx_metadatas are the metadata of the transactions
 * @param num_tx_metadatas is the number of elements in tx_metadatas
 * @param out_json receives a JSON array of the hashes of the relayed transactions. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_relay_txs(monero_wallet* wallet, const char* const* tx_metadatas, size_t num_tx_metadatas, char** out_json);

/**
 * Relay transactions created without relaying, from a JSON array. Only their metadata is used.
 *
 * @param wallet is the wallet handle
 * @param txs_json is a JSON array of monero_tx_wallet with their metadata
 * @param out_json receives a JSON array of the hashes of the relayed transactions. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_relay_txs_json(monero_wallet* wallet, const char* txs_json, char** out_json);

/**
 * Submit signed transactions of a view-only wallet.
 *
 * @param wallet is the wallet handle
 * @param signed_tx_hex is the signed transaction hex, from monero_wallet_sign_txs()
 * @param out_json receives a JSON array of the hashes of the submitted transactions. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_submit_txs(monero_wallet* wallet, const char* signed_tx_hex, char** out_json);

/**
 * Sign unsigned transactions of a view-only wallet with the full wallet that created them.
 *
 * @param wallet is the wallet handle
 * @param unsigned_tx_hex is the unsigned transaction hex, from when the transactions were created
 * @param out_json receives the JSON-serialized monero_tx_set with the signed transactions. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_sign_txs(monero_wallet* wallet, const char* unsigned_tx_hex, char** out_json);

/**
 * Describe a transaction set with unsigned or multisig hex as structured transactions.
 *
 * @param wallet is the wallet handle
 * @param tx_set_json is the JSON-serialized monero_tx_set
 * @param out_json receives the JSON-serialized monero_tx_set with the structured transactions. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_describe_tx_set(monero_wallet* wallet, const char* tx_set_json, char** out_json);

/**
 * Sweep an output with a given key image into the wallet.
 *
 * @param wallet is the wallet handle
 * @param config_json is the JSON-serialized monero_tx_config of the sweep
 * @param out_json receives the JSON-serialized monero_tx_wallet. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_sweep_output(monero_wallet* wallet, const char* config_json, char** out_json);

/**
 * Sweep the unmixable dust outputs back to the wallet, so they are easier to spend and mix.
 *
 * @param wallet is the wallet handle
 * @param relay is true to relay the transactions
 * @param out_json receives a JSON array of monero_tx_wallet. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_sweep_dust(monero_wallet* wallet, bool relay, char** out_json);

/**
 * Sweep the unlocked funds of the wallet.
 *
 * @param wallet is the wallet handle
 * @param config_json is the JSON-serialized monero_tx_config of the sweep
 * @param out_json receives a JSON array of monero_tx_wallet. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_sweep_unlocked(monero_wallet* wallet, const char* config_json, char** out_json);

/**
 * Move the wallet files to another path. The wallet is saved there with the new password.
 *
 * @param wallet is the wallet handle
 * @param path is the new path of the wallet file
 * @param password is the new password of the wallet files
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_move_to(monero_wallet* wallet, const char* path, const char* password);

/**
 * Create a payment URI from a transaction config.
 *
 * @param wallet is the wallet handle
 * @param config_json is the JSON-serialized monero_tx_config
 * @param out_uri receives the payment URI. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_payment_uri(monero_wallet* wallet, const char* config_json, char** out_uri);

/**
 * Parse a payment URI into a transaction config.
 *
 * @param wallet is the wallet handle
 * @param uri is the payment URI
 * @param out_json receives the JSON-serialized monero_tx_config. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_parse_payment_uri(monero_wallet* wallet, const char* uri, char** out_json);

/**
 * Get the secret key of a transaction from its hash.
 *
 * @param wallet is the wallet handle
 * @param tx_hash is the hash of the transaction
 * @param out_key receives the secret key. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_tx_key(monero_wallet* wallet, const char* tx_hash, char** out_key);

/**
 * Check a transaction in the blockchain with its secret key.
 *
 * @param wallet is the wallet handle
 * @param tx_hash is the hash of the transaction
 * @param tx_key is the secret key of the transaction
 * @param address is the destination address of the transaction
 * @param out_json receives the JSON-serialized monero_check_tx. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_check_tx_key(monero_wallet* wallet, const char* tx_hash, const char* tx_key, const char* address, char** out_json);

/**
 * Get a signature that proves a transaction sent to an address.
 *
 * @param wallet is the wallet handle
 * @param tx_hash is the hash of the transaction
 * @param address is the destination address of the transaction
 * @param message is a message to include in the signature. NULL or "" for none
 * @param out_signature receives the signature. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_tx_proof(monero_wallet* wallet, const char* tx_hash, const char* address, const char* message, char** out_signature);

/**
 * Check a transaction proof, the signature made by monero_wallet_get_tx_proof().
 *
 * @param wallet is the wallet handle
 * @param tx_hash is the hash of the transaction
 * @param address is the destination address of the transaction
 * @param message is the message included in the signature. NULL or "" for none
 * @param signature is the signature to check
 * @param out_json receives the JSON-serialized monero_check_tx. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_check_tx_proof(monero_wallet* wallet, const char* tx_hash, const char* address, const char* message, const char* signature, char** out_json);

/**
 * Get a signature that proves a spend of a transaction. It doesn't need the destination address.
 *
 * @param wallet is the wallet handle
 * @param tx_hash is the hash of the transaction
 * @param message is a message to include in the signature. NULL or "" for none
 * @param out_signature receives the signature. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_spend_proof(monero_wallet* wallet, const char* tx_hash, const char* message, char** out_signature);

/**
 * Check a spend proof, the signature made by monero_wallet_get_spend_proof().
 *
 * @param wallet is the wallet handle
 * @param tx_hash is the hash of the transaction
 * @param message is the message included in the signature. NULL or "" for none
 * @param signature is the signature to check
 * @param out_good receives true if the signature is good
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_check_spend_proof(monero_wallet* wallet, const char* tx_hash, const char* message, const char* signature, bool* out_good);

/**
 * Get a signature that proves the whole balance of the wallet.
 *
 * @param wallet is the wallet handle
 * @param message is a message to include in the signature. NULL or "" for none
 * @param out_signature receives the signature. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_reserve_proof_wallet(monero_wallet* wallet, const char* message, char** out_signature);

/**
 * Get a signature that proves an amount available in an account.
 *
 * @param wallet is the wallet handle
 * @param account_idx is the index of the account
 * @param amount is the minimum amount to prove, in atomic units
 * @param message is a message to include in the signature. NULL or "" for none
 * @param out_signature receives the signature. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_reserve_proof_account(monero_wallet* wallet, uint32_t account_idx, uint64_t amount, const char* message, char** out_signature);

/**
 * Check a reserve proof, the signature made by monero_wallet_get_reserve_proof_wallet() or monero_wallet_get_reserve_proof_account().
 *
 * @param wallet is the wallet handle
 * @param address is the address of the wallet
 * @param message is the message included in the signature. NULL or "" for none
 * @param signature is the signature to check
 * @param out_json receives the JSON-serialized monero_check_reserve. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_check_reserve_proof(monero_wallet* wallet, const char* address, const char* message, const char* signature, char** out_json);

// ------------------------------- MULTISIG ----------------------------------
// the multisig hex is shared between the participants, in the order they were made

/**
 * Check if importing multisig data is needed for a correct balance.
 *
 * @param wallet is the wallet handle
 * @param out_needed receives true if the import is needed
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_is_multisig_import_needed(monero_wallet* wallet, bool* out_needed);

/**
 * Get the multisig info of the wallet.
 *
 * @param wallet is the wallet handle
 * @param out_json receives the JSON-serialized monero_multisig_info. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_get_multisig_info(monero_wallet* wallet, char** out_json);

/**
 * Get the multisig hex to share with the participants, to start making a multisig wallet.
 *
 * @param wallet is the wallet handle
 * @param out_multisig_hex receives the hex. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_prepare_multisig(monero_wallet* wallet, char** out_multisig_hex);

/**
 * Make the wallet multisig from the multisig hex of the other participants.
 *
 * @param wallet is the wallet handle
 * @param multisig_hexes are the multisig hex of each participant
 * @param num_multisig_hexes is the number of elements in multisig_hexes
 * @param threshold is the number of signatures needed to sign a transfer
 * @param password is the password of the wallet
 * @param out_multisig_hex receives the hex to share with the participants, if another round is needed. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_make_multisig(monero_wallet* wallet, const char* const* multisig_hexes, size_t num_multisig_hexes, int threshold, const char* password, char** out_multisig_hex);

/**
 * Exchange the multisig hex with the other participants. Repeat it with the new multisig hex of each
 * participant until the result has an address. A 2 of 3 wallet needs two exchanges after make.
 *
 * @param wallet is the wallet handle
 * @param multisig_hexes are the multisig hex of each participant
 * @param num_multisig_hexes is the number of elements in multisig_hexes
 * @param password is the password of the wallet
 * @param out_json receives the JSON-serialized monero_multisig_init_result. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_exchange_multisig_keys(monero_wallet* wallet, const char* const* multisig_hexes, size_t num_multisig_hexes, const char* password, char** out_json);

/**
 * Export the multisig info of the wallet as hex, for the other participants.
 *
 * @param wallet is the wallet handle
 * @param out_multisig_hex receives the hex. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_export_multisig_hex(monero_wallet* wallet, char** out_multisig_hex);

/**
 * Import the multisig info of the other participants, as hex.
 *
 * @param wallet is the wallet handle
 * @param multisig_hexes are the multisig hex of each participant
 * @param num_multisig_hexes is the number of elements in multisig_hexes
 * @param refresh_after_import is true to refresh the wallet after the import
 * @param out_num_imported receives the number of imported outputs
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_import_multisig_hex(monero_wallet* wallet, const char* const* multisig_hexes, size_t num_multisig_hexes, bool refresh_after_import, int* out_num_imported);

/**
 * Sign multisig transactions, given as the hex shared when they were created.
 *
 * @param wallet is the wallet handle
 * @param multisig_tx_hex is the multisig transaction hex
 * @param out_json receives the JSON-serialized monero_multisig_sign_result. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_sign_multisig_tx_hex(monero_wallet* wallet, const char* multisig_tx_hex, char** out_json);

/**
 * Submit signed multisig transactions, given as hex.
 *
 * @param wallet is the wallet handle
 * @param signed_multisig_tx_hex is the signed multisig transaction hex, from monero_wallet_sign_multisig_tx_hex()
 * @param out_json receives a JSON array of the hashes of the submitted transactions. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_submit_multisig_tx_hex(monero_wallet* wallet, const char* signed_multisig_tx_hex, char** out_json);

// -------------------------------- RPC WALLETS --------------------------------
// a wallet on a monero-wallet-rpc server. It has the same handle as the other wallets, so the
// methods above work on it. The functions below need an RPC wallet and fail for the others

/**
 * Connect to a monero-wallet-rpc server through a shared connection, with its proxy, timeout
 * and TLS settings. No wallet is open until monero_wallet_rpc_open_wallet() or
 * monero_wallet_rpc_create_wallet(). Free the handle with monero_wallet_free().
 *
 * @param connection is the connection to the server. It can be freed after the call
 * @param out_wallet receives the handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_connect(monero_rpc_connection* connection, monero_wallet** out_wallet);

/**
 * Open a wallet on the server of an RPC wallet.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param name is the name of the wallet on the server
 * @param wallet_password is the password of the wallet. NULL or "" for none
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_open_wallet(monero_wallet* wallet, const char* name, const char* wallet_password);

/**
 * Create a wallet on the server of an RPC wallet and open it. A config with a seed restores the
 * wallet from the seed, a config with keys restores it from the keys, and a config with neither
 * creates a random wallet. The server has its own network, so the config has no network type.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param config_json is the JSON-serialized monero_wallet_config, with the wallet name in "path"
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_create_wallet(monero_wallet* wallet, const char* config_json);

/**
 * Open a wallet on a monero-wallet-rpc server. Free the handle with monero_wallet_free().
 *
 * @param uri is the URI of the server
 * @param username is the username of the server. NULL or "" for none
 * @param password is the password of the server. NULL or "" for none
 * @param name is the name of the wallet on the server
 * @param wallet_password is the password of the wallet. NULL or "" for none
 * @param out_wallet receives the handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_open(const char* uri, const char* username, const char* password, const char* name, const char* wallet_password, monero_wallet** out_wallet);

/**
 * Create a random wallet on a monero-wallet-rpc server. Free the handle with monero_wallet_free().
 *
 * @param uri is the URI of the server
 * @param username is the username of the server. NULL or "" for none
 * @param password is the password of the server. NULL or "" for none
 * @param name is the name of the new wallet on the server
 * @param wallet_password is the password of the wallet. NULL or "" for none
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_create_random(const char* uri, const char* username, const char* password, const char* name, const char* wallet_password, const char* language, monero_wallet** out_wallet);

/**
 * Restore a wallet from its mnemonic seed on a monero-wallet-rpc server. Free the handle with monero_wallet_free().
 *
 * @param uri is the URI of the server
 * @param username is the username of the server. NULL or "" for none
 * @param password is the password of the server. NULL or "" for none
 * @param name is the name of the new wallet on the server
 * @param wallet_password is the password of the wallet. NULL or "" for none
 * @param seed is the mnemonic seed
 * @param seed_offset is the passphrase that extends the seed. NULL or "" for none
 * @param restore_height is the first block to scan. 0 scans from the genesis block
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_create_from_seed(const char* uri, const char* username, const char* password, const char* name, const char* wallet_password, const char* seed, const char* seed_offset, uint64_t restore_height, const char* language, monero_wallet** out_wallet);

/**
 * Restore a wallet from its keys on a monero-wallet-rpc server. Without a private spend key the wallet is view-only.
 * Free the handle with monero_wallet_free().
 *
 * @param uri is the URI of the server
 * @param username is the username of the server. NULL or "" for none
 * @param password is the password of the server. NULL or "" for none
 * @param name is the name of the new wallet on the server
 * @param wallet_password is the password of the wallet. NULL or "" for none
 * @param address is the primary address
 * @param private_view_key is the private view key, in hex
 * @param private_spend_key is the private spend key, in hex. NULL or "" for a view-only wallet
 * @param restore_height is the first block to scan. 0 scans from the genesis block
 * @param language is the seed language. NULL for English
 * @param out_wallet receives the handle
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_create_from_keys(const char* uri, const char* username, const char* password, const char* name, const char* wallet_password, const char* address, const char* private_view_key, const char* private_spend_key, uint64_t restore_height, const char* language, monero_wallet** out_wallet);

/**
 * Save and close the wallet and stop the monero-wallet-rpc server. Calls on the handle fail afterwards.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_stop(monero_wallet* wallet);

/**
 * Get the seed languages that the monero-wallet-rpc server supports.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param out_json receives a JSON array of language names. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_get_seed_languages(monero_wallet* wallet, char** out_json);

/**
 * Get the connection to the monero-wallet-rpc server.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param out_json receives the JSON-serialized monero_rpc_connection. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_get_connection(monero_wallet* wallet, char** out_json);

/**
 * Set how often the RPC wallet polls the server for changes. Polling starts with the first call.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param period_ms is the poll period in milliseconds
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_set_poll_period(monero_wallet* wallet, uint64_t period_ms);

/**
 * Get the balances of an account or a subaddress, from the server.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param account_idx is the account index. NULL for none
 * @param subaddress_idx is the subaddress index within the account. NULL for none
 * @param out_json receives the JSON-serialized monero_subaddress with the balances. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_get_balances(monero_wallet* wallet, const uint32_t* account_idx, const uint32_t* subaddress_idx, char** out_json);

/**
 * Get an account from the server, with or without its subaddresses and balances.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param account_idx is the account index
 * @param include_subaddresses is true to include the subaddresses of the account
 * @param skip_balances is true to skip the balances, which is faster
 * @param out_json receives the JSON-serialized monero_account. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_get_account(monero_wallet* wallet, uint32_t account_idx, bool include_subaddresses, bool skip_balances, char** out_json);

/**
 * Get the accounts of the wallet from the server, optionally only those with a tag.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param include_subaddresses is true to include the subaddresses of each account
 * @param tag is the tag of the accounts. NULL or "" for all of them
 * @param skip_balances is true to skip the balances, which is faster
 * @param out_json receives a JSON array of monero_account. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_get_accounts(monero_wallet* wallet, bool include_subaddresses, const char* tag, bool skip_balances, char** out_json);

/**
 * Get the subaddresses of an account from the server, with the given indices.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param account_idx is the account index
 * @param subaddress_indices are the subaddress indices, or NULL for all of them
 * @param num_subaddress_indices is the number of elements in subaddress_indices, 0 for all of them
 * @param skip_balances is true to skip the balances, which is faster
 * @param out_json receives a JSON array of monero_subaddress. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_get_subaddresses(monero_wallet* wallet, uint32_t account_idx, const uint32_t* subaddress_indices, size_t num_subaddress_indices, bool skip_balances, char** out_json);

/**
 * Set the daemon connection of an RPC wallet, with the SSL options of the connection.
 *
 * @param wallet is the wallet handle of an RPC wallet
 * @param connection_json is the JSON-serialized monero_rpc_connection of the daemon
 * @param is_trusted is true if the daemon is trusted
 * @param ssl_options_json is the JSON-serialized ssl_options. NULL or empty for none
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_wallet_rpc_set_daemon_connection(monero_wallet* wallet, const char* connection_json, bool is_trusted, const char* ssl_options_json);

#ifdef __cplusplus
}
#endif

#endif // MONERO_C_WALLET_H

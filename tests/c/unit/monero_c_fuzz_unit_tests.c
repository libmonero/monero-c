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

#include "monero_c_fuzz.h"
#include "monero_c_test.h"

#include <time.h>

// gives each function that reads JSON, hex or binary from its caller a few hundred inputs that are wrong in ways that a caller or a peer can
// make them: a number at the edge of its type, a value of the wrong type, a string with a NUL escape or bytes that aren't UTF-8, a size that
// is too large, a text that stops in the middle. The inputs are the mutations of a valid one, from a generator with a fixed seed, so a failure
// happens again. A call has to return, and an error has to leave its outputs reset. The wallets are offline and the daemon is a closed port, so
// the calls that need a node fail where they ask for it, after they have parsed what they were given.
// MONERO_C_FUZZ_ITERATIONS and MONERO_C_FUZZ_SEED change the run, and MONERO_C_FUZZ_VERBOSE prints each input before its call, so the last line
// is the input of a crash

#define MAINNET MONERO_UTILS_NETWORK_MAINNET
#define SEED "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex"
#define ADDRESS "48W9YHwPzRz9aPTeXCA6kmSpW6HsvmWx578jj3of2gT3JwZzwTf33amESBoNDkL6SVK34Q2HTKqgYbGyE1hBws3wCrcBDR2"
#define VIEW_KEY "e8c2288181bad9ec410d7322efd65f663c6da57bd1d1198636278a039743a600"
#define HEX64 "abababababababababababababababababababababababababababababababab"
#define CLOSED_PORT "http://127.0.0.1:1"

#define MAX_INPUT 65536

typedef struct fixtures {
  monero_wallet* full;
  monero_wallet* keys;
  monero_wallet* rpc;
  monero_daemon* daemon;
  monero_rpc_connection* connection;
} fixtures;

typedef enum input_kind { INPUT_TEXT, INPUT_HEX, INPUT_BYTES } input_kind;

typedef void (*target_fn)(const fixtures* fx, const char* input, size_t length);

typedef struct target {
  const char* name;
  target_fn call;
  input_kind kind;
  const char* seed;  // what is mutated. For hex, the magic bytes of the blob, which are in hex and are followed by random bytes
  size_t iterations;  // 0 for the iterations of the run
} target;

static fuzz_rng g_rng;
static int g_verbose = 0;
static const char* g_target = "";

// checks what every call has to do, whatever the input: it returns OK or ERROR, an error has a message, and a failed call has reset its output
static void check_call(monero_result result, const void* output) {
  g_checks++;
  if (result != MONERO_OK && result != MONERO_ERROR) {
    g_failures++;
    fprintf(stderr, "FAIL %s: the result is %d\n", g_target, (int) result);
  }
  if (result == MONERO_ERROR) {
    g_checks++;
    if (monero_last_error() == NULL || monero_last_error()[0] == '\0') {
      g_failures++;
      fprintf(stderr, "FAIL %s: no message for the error\n", g_target);
    }
    g_checks++;
    if (output != NULL) {
      g_failures++;
      fprintf(stderr, "FAIL %s: a failed call didn't reset its output\n", g_target);
    }
  }
}

// frees what a call returned, unless the call failed to reset it, which check_call() has reported
static void release(void* pointer) {
  if (pointer != NULL && pointer != POISON_PTR) monero_utils_free(pointer);
}

// the calls that return a string
#define STRING_TARGET(NAME, CALL) \
  static void NAME(const fixtures* fx, const char* input, size_t length) { \
    char* out = POISON_PTR; \
    (void) fx; (void) input; (void) length; \
    monero_result result = CALL; \
    check_call(result, result == MONERO_OK ? NULL : out); \
    release(out); \
  }

// the calls that return nothing
#define VOID_TARGET(NAME, CALL) \
  static void NAME(const fixtures* fx, const char* input, size_t length) { \
    (void) fx; (void) input; (void) length; \
    monero_result result = CALL; \
    check_call(result, NULL); \
  }

// ----------------------------------- UTILS -----------------------------------

STRING_TARGET(utils_get_payment_uri, monero_utils_get_payment_uri(input, MAINNET, &out))
STRING_TARGET(utils_parse_payment_uri, monero_utils_parse_payment_uri(input, MAINNET, &out))
STRING_TARGET(utils_get_integrated_address, monero_utils_get_integrated_address(MAINNET, input, "abababababababab", &out))
STRING_TARGET(utils_get_integrated_address_id, monero_utils_get_integrated_address(MAINNET, ADDRESS, input, &out))
STRING_TARGET(utils_binary_to_json, monero_utils_binary_to_json((const uint8_t*) input, length, &out))
STRING_TARGET(utils_binary_blocks_to_json, monero_utils_binary_blocks_to_json((const uint8_t*) input, length, &out))
STRING_TARGET(utils_binary_blocks_fast_to_json, monero_utils_binary_blocks_fast_to_json((const uint8_t*) input, length, &out))

static void utils_json_to_binary(const fixtures* fx, const char* input, size_t length) {
  uint8_t* data = POISON_PTR;
  size_t size = 7;
  (void) fx;
  (void) length;
  monero_result result = monero_utils_json_to_binary(input, &data, &size);
  check_call(result, result == MONERO_OK ? NULL : data);
  if (result == MONERO_OK) {
    // what it wrote reads back as JSON
    char* json = POISON_PTR;
    result = monero_utils_binary_to_json(data, size, &json);
    check_call(result, result == MONERO_OK ? NULL : json);
    release(json);
    release(data);
  }
}

// every validator takes any text, and the ones that return a bool say no
static void utils_validators(const fixtures* fx, const char* input, size_t length) {
  (void) fx;
  (void) length;
  (void) monero_utils_is_valid_address(input, MAINNET);
  (void) monero_utils_is_valid_private_view_key(input);
  (void) monero_utils_is_valid_private_spend_key(input);
  (void) monero_utils_is_valid_public_view_key(input);
  (void) monero_utils_is_valid_public_spend_key(input);
  (void) monero_utils_is_valid_payment_id(input);
  (void) monero_utils_is_valid_payment_id_long(input);
  (void) monero_utils_is_valid_payment_id_short(input);
  (void) monero_utils_is_valid_language(input);
  (void) monero_utils_is_valid_mnemonic(input, "English");
  (void) monero_utils_is_valid_mnemonic(SEED, input);
  check_call(monero_utils_validate_address(input, MAINNET), NULL);
  check_call(monero_utils_validate_private_view_key(input), NULL);
  check_call(monero_utils_validate_private_spend_key(input), NULL);
  check_call(monero_utils_validate_public_view_key(input), NULL);
  check_call(monero_utils_validate_public_spend_key(input), NULL);
  check_call(monero_utils_validate_payment_id(input), NULL);
  check_call(monero_utils_validate_payment_id_long(input), NULL);
  check_call(monero_utils_validate_payment_id_short(input), NULL);
  check_call(monero_utils_validate_mnemonic(input, "English"), NULL);
  uint8_t id[32];
  (void) monero_utils_parse_payment_id_long(input, id);
  (void) monero_utils_parse_payment_id_short(input, id);
}

// ------------------------------ RPC CONNECTION -------------------------------

static void connection_create(const fixtures* fx, const char* input, size_t length) {
  monero_rpc_connection* connection = POISON_PTR;
  (void) fx;
  (void) length;
  monero_result result = monero_rpc_connection_create(input, &connection);
  check_call(result, result == MONERO_OK ? NULL : connection);
  if (result == MONERO_OK) {
    // a connection that was made from any text serializes and answers its questions
    char* json = POISON_PTR;
    bool flag = false;
    monero_optional_bool status = MONERO_OPTIONAL_BOOL_UNSET;
    check_call(monero_rpc_connection_serialize(connection, &json), json);
    release(json);
    check_call(monero_rpc_connection_is_onion(connection, &flag), NULL);
    check_call(monero_rpc_connection_is_i2p(connection, &flag), NULL);
    check_call(monero_rpc_connection_is_online(connection, &status), NULL);
    monero_rpc_connection_free(connection);
  }
}

STRING_TARGET(connection_send_json, monero_rpc_connection_send_json_request(fx->connection, "get_info", input, NULL, &out))
STRING_TARGET(connection_send_path, monero_rpc_connection_send_path_request(fx->connection, "get_height", input, NULL, &out))

static void connection_send_binary(const fixtures* fx, const char* input, size_t length) {
  uint8_t* data = POISON_PTR;
  size_t size = 7;
  (void) length;
  monero_result result = monero_rpc_connection_send_binary_request(fx->connection, "get_blocks.bin", input, NULL, &data, &size);
  check_call(result, result == MONERO_OK ? NULL : data);
  release(data);
}

static void connection_methods(const fixtures* fx, const char* input, size_t length) {
  char* out = POISON_PTR;
  (void) length;
  check_call(monero_rpc_connection_set_attribute(fx->connection, input, input), NULL);
  monero_result result = monero_rpc_connection_get_attribute(fx->connection, input, &out);
  check_call(result, result == MONERO_OK ? NULL : out);
  release(out);
  check_call(monero_rpc_connection_set_credentials(fx->connection, input, input), NULL);
}

// ------------------------------- FULL WALLETS --------------------------------

STRING_TARGET(wallet_get_txs, monero_wallet_get_txs(fx->full, input, &out))
STRING_TARGET(wallet_get_transfers, monero_wallet_get_transfers(fx->full, input, &out))
STRING_TARGET(wallet_get_outputs, monero_wallet_get_outputs(fx->full, input, &out))
STRING_TARGET(keys_get_txs, monero_wallet_get_txs(fx->keys, input, &out))
STRING_TARGET(wallet_create_tx, monero_wallet_create_tx(fx->full, input, &out))
STRING_TARGET(wallet_create_txs, monero_wallet_create_txs(fx->full, input, &out))
STRING_TARGET(wallet_sweep_output, monero_wallet_sweep_output(fx->full, input, &out))
STRING_TARGET(wallet_sweep_unlocked, monero_wallet_sweep_unlocked(fx->full, input, &out))
STRING_TARGET(wallet_get_payment_uri, monero_wallet_get_payment_uri(fx->full, input, &out))
STRING_TARGET(wallet_parse_payment_uri, monero_wallet_parse_payment_uri(fx->full, input, &out))
STRING_TARGET(wallet_import_key_images, monero_wallet_import_key_images(fx->full, input, 0, &out))
STRING_TARGET(wallet_describe_tx_set, monero_wallet_describe_tx_set(fx->full, input, &out))
STRING_TARGET(wallet_relay_tx_json, monero_wallet_relay_tx_json(fx->full, input, &out))
STRING_TARGET(wallet_relay_txs_json, monero_wallet_relay_txs_json(fx->full, input, &out))
STRING_TARGET(wallet_decode_integrated_address, monero_wallet_decode_integrated_address(fx->full, input, &out))
STRING_TARGET(wallet_get_address_index, monero_wallet_get_address_index(fx->full, input, &out))
STRING_TARGET(wallet_sign_message, monero_wallet_sign_message(fx->full, input, 0, 0, 0, &out))
STRING_TARGET(wallet_verify_message, monero_wallet_verify_message(fx->full, "message", ADDRESS, input, &out))
STRING_TARGET(wallet_verify_message_address, monero_wallet_verify_message(fx->full, "message", input, "SigV2xx", &out))
STRING_TARGET(wallet_get_tx_note, monero_wallet_get_tx_note(fx->full, input, &out))
STRING_TARGET(wallet_get_tx_key, monero_wallet_get_tx_key(fx->full, input, &out))
STRING_TARGET(wallet_check_tx_key, monero_wallet_check_tx_key(fx->full, HEX64, input, ADDRESS, &out))
STRING_TARGET(wallet_check_tx_proof, monero_wallet_check_tx_proof(fx->full, HEX64, ADDRESS, "message", input, &out))
STRING_TARGET(wallet_check_reserve_proof, monero_wallet_check_reserve_proof(fx->full, ADDRESS, "message", input, &out))
STRING_TARGET(wallet_get_integrated_address, monero_wallet_get_integrated_address(fx->full, ADDRESS, input, &out))
STRING_TARGET(wallet_create_account, monero_wallet_create_account(fx->full, input, &out))
STRING_TARGET(wallet_get_tx_notes, monero_wallet_get_tx_notes(fx->full, &input, 1, &out))

static void wallet_check_spend_proof(const fixtures* fx, const char* input, size_t length) {
  bool good = false;
  (void) length;
  check_call(monero_wallet_check_spend_proof(fx->full, HEX64, "message", input, &good), NULL);
}

static void wallet_attributes(const fixtures* fx, const char* input, size_t length) {
  char* out = POISON_PTR;
  bool found = false;
  uint64_t index = 0;
  (void) length;
  check_call(monero_wallet_set_attribute(fx->full, input, input), NULL);
  monero_result result = monero_wallet_get_attribute(fx->full, input, &found, &out);
  check_call(result, result == MONERO_OK ? NULL : out);
  release(out);
  check_call(monero_wallet_set_tx_note(fx->full, input, input), NULL);
  check_call(monero_wallet_add_address_book_entry(fx->full, input, input, &index), NULL);
  check_call(monero_wallet_set_account_tag_label(fx->full, input, input), NULL);
  check_call(monero_wallet_freeze_output(fx->full, input), NULL);
  check_call(monero_wallet_is_output_frozen(fx->full, input, &found), NULL);
}

// the hex of what the wallet reads back from a file or a peer
static void wallet_import_outputs(const fixtures* fx, const char* input, size_t length) {
  int32_t imported = 7;
  (void) length;
  monero_result result = monero_wallet_import_outputs(fx->full, input, &imported);
  check_call(result, NULL);
  g_checks++;
  if (result == MONERO_ERROR && imported != 7) {
    g_failures++;
    fprintf(stderr, "FAIL %s: the count changed in a failed call\n", g_target);
  }
}

STRING_TARGET(wallet_submit_txs, monero_wallet_submit_txs(fx->full, input, &out))
STRING_TARGET(wallet_sign_txs, monero_wallet_sign_txs(fx->full, input, &out))
STRING_TARGET(wallet_relay_tx, monero_wallet_relay_tx(fx->full, input, &out))
STRING_TARGET(wallet_sign_multisig_tx_hex, monero_wallet_sign_multisig_tx_hex(fx->full, input, &out))
STRING_TARGET(wallet_submit_multisig_tx_hex, monero_wallet_submit_multisig_tx_hex(fx->full, input, &out))
STRING_TARGET(wallet_make_multisig, monero_wallet_make_multisig(fx->full, &input, 1, 2, "password", &out))
STRING_TARGET(wallet_exchange_multisig_keys, monero_wallet_exchange_multisig_keys(fx->full, &input, 1, "password", &out))

static void wallet_import_multisig_hex(const fixtures* fx, const char* input, size_t length) {
  int32_t imported = 7;
  const char* hexes[2] = {input, input};
  (void) length;
  check_call(monero_wallet_import_multisig_hex(fx->full, hexes, 2, false, &imported), NULL);
}

// a config creates a wallet, which is in memory and has no password, so it is made quickly
static void wallet_create(const fixtures* fx, const char* input, size_t length) {
  monero_wallet* wallet = POISON_PTR;
  (void) fx;
  (void) length;
  monero_result result = monero_wallet_create(input, &wallet);
  check_call(result, result == MONERO_OK ? NULL : wallet);
  if (result == MONERO_OK) monero_wallet_free(wallet);
}

VOID_TARGET(wallet_rpc_create_wallet_json, monero_wallet_rpc_create_wallet(fx->rpc, input))

static void wallet_rpc_set_daemon_connection(const fixtures* fx, const char* input, size_t length) {
  (void) length;
  check_call(monero_wallet_rpc_set_daemon_connection(fx->rpc, input, false, NULL), NULL);
  check_call(monero_wallet_rpc_set_daemon_connection(fx->rpc, "{\"uri\":\"" CLOSED_PORT "\"}", false, input), NULL);
}

static void keys_create(const fixtures* fx, const char* input, size_t length) {
  monero_wallet* wallet = POISON_PTR;
  (void) fx;
  (void) length;
  monero_result result = monero_wallet_keys_create_from_keys(MAINNET, input, VIEW_KEY, NULL, NULL, &wallet);
  check_call(result, result == MONERO_OK ? NULL : wallet);
  if (result == MONERO_OK) monero_wallet_free(wallet);
  wallet = POISON_PTR;
  result = monero_wallet_keys_create_from_seed(MAINNET, input, NULL, NULL, &wallet);
  check_call(result, result == MONERO_OK ? NULL : wallet);
  if (result == MONERO_OK) monero_wallet_free(wallet);
}

// the text of a path: where a wallet is, which is a name that the file system takes or doesn't
static void wallet_paths(const fixtures* fx, const char* input, size_t length) {
  monero_wallet* wallet = POISON_PTR;
  bool exists = false;
  (void) fx;
  (void) length;
  check_call(monero_wallet_exists(input, &exists), NULL);
  monero_result result = monero_wallet_open(input, "password", MAINNET, &wallet);
  check_call(result, result == MONERO_OK ? NULL : wallet);
  if (result == MONERO_OK) monero_wallet_free(wallet);
}

// ------------------------------------ DAEMON ----------------------------------

STRING_TARGET(daemon_add_auxiliary_pow, monero_daemon_add_auxiliary_pow(fx->daemon, "aabb", input, &out))
STRING_TARGET(daemon_get_outputs, monero_daemon_get_outputs(fx->daemon, input, &out))

static void daemon_set_peer_bans(const fixtures* fx, const char* input, size_t length) {
  (void) length;
  check_call(monero_daemon_set_peer_bans(fx->daemon, input), NULL);
}

static void daemon_set_peer_ban(const fixtures* fx, const char* input, size_t length) {
  (void) length;
  check_call(monero_daemon_set_peer_ban(fx->daemon, input), NULL);
}

static void daemon_hashes(const fixtures* fx, const char* input, size_t length) {
  char* out = POISON_PTR;
  (void) length;
  monero_result result = monero_daemon_get_block_header_by_hash(fx->daemon, input, &out);
  check_call(result, result == MONERO_OK ? NULL : out);
  release(out);
  out = POISON_PTR;
  result = monero_daemon_get_tx_hex(fx->daemon, input, false, &out);
  check_call(result, result == MONERO_OK ? NULL : out);
  release(out);
  check_call(monero_daemon_relay_tx_by_hash(fx->daemon, input), NULL);
  check_call(monero_daemon_flush_tx_pool_hash(fx->daemon, input), NULL);
  check_call(monero_daemon_set_bootstrap_daemon(fx->daemon, input, input, input, input), NULL);
}

// ------------------------------------ TABLE ------------------------------------

#define TX_CONFIG "{\"accountIndex\":0,\"subaddressIndices\":[0,1],\"destinations\":[{\"address\":\"" ADDRESS "\",\"amount\":\"1000000\"}],\"priority\":2,\"relay\":false,\"canSplit\":false,\"subtractFeeFrom\":[0],\"ringSize\":16,\"fee\":\"100\",\"note\":\"n\",\"recipientName\":\"r\",\"belowAmount\":\"100\",\"sweepEachSubaddress\":false,\"keyImage\":\"" HEX64 "\"}"
#define OUTPUT_QUERY "{\"accountIndex\":0,\"subaddressIndex\":0,\"subaddressIndices\":[0],\"amount\":\"1\",\"minAmount\":\"0\",\"maxAmount\":\"100\",\"isSpent\":false,\"isFrozen\":false,\"keyImage\":{\"hex\":\"" HEX64 "\",\"signature\":\"" HEX64 "\"},\"index\":1,\"stealthPublicKey\":\"" HEX64 "\",\"txQuery\":{\"isConfirmed\":true,\"hashes\":[\"" HEX64 "\"],\"height\":1}}"
#define TRANSFER_QUERY "{\"isIncoming\":true,\"address\":\"" ADDRESS "\",\"subaddressIndex\":0,\"subaddressIndices\":[0,1],\"hasDestinations\":false,\"amount\":\"1\",\"accountIndex\":0,\"txQuery\":{\"isConfirmed\":true,\"hashes\":[\"" HEX64 "\"],\"minHeight\":0,\"maxHeight\":5}}"
#define TX_QUERY "{\"isConfirmed\":true,\"inTxPool\":false,\"isMinerTx\":false,\"isLocked\":false,\"isOutgoing\":true,\"isIncoming\":true,\"hashes\":[\"" HEX64 "\",\"" HEX64 "\"],\"hasPaymentId\":true,\"paymentIds\":[\"abababababababab\"],\"height\":3,\"minHeight\":0,\"maxHeight\":9,\"includeOutputs\":true,\"fee\":\"10\",\"numConfirmations\":2,\"transferQuery\":{\"isIncoming\":true,\"address\":\"" ADDRESS "\",\"subaddressIndex\":0,\"amount\":\"1\"},\"inputQuery\":{\"isSpent\":true,\"minAmount\":\"1\"},\"outputQuery\":{\"accountIndex\":0,\"isFrozen\":false}}"
#define WALLET_CONFIG "{\"networkType\":0,\"password\":\"password\",\"seed\":\"" SEED "\",\"seedOffset\":\"\",\"restoreHeight\":0,\"language\":\"English\",\"isMultisig\":false,\"saveCurrent\":false,\"accountLookahead\":1,\"subaddressLookahead\":2,\"server\":{\"uri\":\"" CLOSED_PORT "\",\"username\":\"u\",\"password\":\"p\",\"sslVerify\":false,\"timeoutMs\":500,\"priority\":1,\"zmqUri\":\"\",\"proxyUri\":\"\"},\"isTrustedDaemon\":false}"
#define KEYS_CONFIG "{\"networkType\":0,\"primaryAddress\":\"" ADDRESS "\",\"privateViewKey\":\"" VIEW_KEY "\",\"restoreHeight\":0}"
#define CONNECTION_JSON "{\"uri\":\"" CLOSED_PORT "\",\"username\":\"u\",\"password\":\"p\",\"proxyUri\":\"\",\"sslVerify\":true,\"zmqUri\":\"tcp://127.0.0.1:1\",\"priority\":1,\"timeoutMs\":1000}"
#define KEY_IMAGES "[{\"hex\":\"" HEX64 "\",\"signature\":\"" HEX64 HEX64 "\"},{\"hex\":\"" HEX64 "\",\"signature\":\"" HEX64 HEX64 "\"}]"
#define TX_SET "{\"txSets\":[{\"unsignedTxHex\":\"abababababababab\",\"signedTxHex\":\"cdcdcdcdcdcdcdcd\",\"multisigTxHex\":\"efefefefefefefef\",\"txs\":[{\"hash\":\"" HEX64 "\",\"metadata\":\"aabb\"}]}]}"
#define TX_JSON "{\"metadata\":\"aabbcc\",\"hash\":\"" HEX64 "\",\"fee\":\"10\",\"outgoingTransfer\":{\"amount\":\"1\",\"accountIndex\":0}}"
#define TXS_JSON "{\"txSets\":[{\"txs\":[" TX_JSON "]}]}"
#define BANS "[{\"host\":\"1.2.3.4\",\"ip\":16909060,\"isBanned\":true,\"seconds\":100},{\"host\":\"\",\"isBanned\":false}]"
#define BAN "{\"host\":\"1.2.3.4\",\"ip\":16909060,\"isBanned\":true,\"seconds\":100}"
#define OUTPUTS "[{\"index\":1,\"amount\":\"0\",\"stealthPublicKey\":\"" HEX64 "\",\"keyImage\":{\"hex\":\"" HEX64 "\"},\"tx\":{\"hash\":\"" HEX64 "\"}}]"
#define AUX_POW "[{\"hash\":\"" HEX64 "\",\"id\":\"" HEX64 "\"}]"
#define PAYLOAD "{\"a\":[1,2,{\"b\":null}],\"c\":\"d\",\"e\":1.5,\"f\":true,\"g\":{\"h\":{\"i\":[]}},\"blob\":\"aaaaaaaaaaaaaaaaaaaa\"}"
#define SSL_OPTIONS "{\"sslAllowAnyCert\":true,\"sslCertificatePath\":\"x\",\"sslPrivateKeyPath\":\"y\"}"

static const target g_targets[] = {
  // text
  {"utils_get_payment_uri", utils_get_payment_uri, INPUT_TEXT, "{\"destinations\":[{\"address\":\"" ADDRESS "\",\"amount\":\"1500000000000\"}],\"recipientName\":\"bob\",\"note\":\"thanks\",\"paymentId\":null}", 0},
  {"utils_parse_payment_uri", utils_parse_payment_uri, INPUT_TEXT, "monero:" ADDRESS "?tx_amount=1.5&recipient_name=bob%20b&tx_description=thanks%21", 0},
  {"utils_get_integrated_address", utils_get_integrated_address, INPUT_TEXT, ADDRESS, 0},
  {"utils_get_integrated_address_id", utils_get_integrated_address_id, INPUT_TEXT, "abababababababab", 0},
  {"utils_json_to_binary", utils_json_to_binary, INPUT_TEXT, PAYLOAD, 0},
  {"utils_validators", utils_validators, INPUT_TEXT, ADDRESS, 0},
  {"utils_validators_mnemonic", utils_validators, INPUT_TEXT, SEED, 0},
  {"utils_validators_key", utils_validators, INPUT_TEXT, VIEW_KEY, 0},
  {"connection_create", connection_create, INPUT_TEXT, CONNECTION_JSON, 0},
  {"connection_send_json", connection_send_json, INPUT_TEXT, PAYLOAD, 0},
  {"connection_send_path", connection_send_path, INPUT_TEXT, PAYLOAD, 0},
  {"connection_send_binary", connection_send_binary, INPUT_TEXT, PAYLOAD, 0},
  {"connection_methods", connection_methods, INPUT_TEXT, "key", 0},
  {"wallet_get_txs", wallet_get_txs, INPUT_TEXT, TX_QUERY, 0},
  {"wallet_get_transfers", wallet_get_transfers, INPUT_TEXT, TRANSFER_QUERY, 0},
  {"wallet_get_outputs", wallet_get_outputs, INPUT_TEXT, OUTPUT_QUERY, 0},
  {"keys_get_txs", keys_get_txs, INPUT_TEXT, TX_QUERY, 0},
  {"wallet_create_tx", wallet_create_tx, INPUT_TEXT, TX_CONFIG, 0},
  {"wallet_create_txs", wallet_create_txs, INPUT_TEXT, TX_CONFIG, 0},
  {"wallet_sweep_output", wallet_sweep_output, INPUT_TEXT, "{\"keyImage\":\"" HEX64 "\",\"destinations\":[{\"address\":\"" ADDRESS "\"}],\"relay\":false}", 0},
  {"wallet_sweep_unlocked", wallet_sweep_unlocked, INPUT_TEXT, "{\"accountIndex\":0,\"destinations\":[{\"address\":\"" ADDRESS "\"}],\"relay\":false}", 0},
  {"wallet_get_payment_uri", wallet_get_payment_uri, INPUT_TEXT, "{\"destinations\":[{\"address\":\"" ADDRESS "\",\"amount\":\"1500000000000\"}],\"recipientName\":\"bob\",\"note\":\"thanks\"}", 0},
  {"wallet_parse_payment_uri", wallet_parse_payment_uri, INPUT_TEXT, "monero:" ADDRESS "?tx_amount=1.5&recipient_name=bob%20b&tx_description=thanks%21", 0},
  {"wallet_import_key_images", wallet_import_key_images, INPUT_TEXT, KEY_IMAGES, 0},
  {"wallet_describe_tx_set", wallet_describe_tx_set, INPUT_TEXT, TX_SET, 0},
  {"wallet_relay_tx_json", wallet_relay_tx_json, INPUT_TEXT, TX_JSON, 0},
  {"wallet_relay_txs_json", wallet_relay_txs_json, INPUT_TEXT, TXS_JSON, 0},
  {"wallet_decode_integrated_address", wallet_decode_integrated_address, INPUT_TEXT, ADDRESS, 0},
  {"wallet_get_address_index", wallet_get_address_index, INPUT_TEXT, ADDRESS, 0},
  {"wallet_sign_message", wallet_sign_message, INPUT_TEXT, "a message to sign", 0},
  {"wallet_verify_message", wallet_verify_message, INPUT_TEXT, "SigV2" HEX64 HEX64, 0},
  {"wallet_verify_message_address", wallet_verify_message_address, INPUT_TEXT, ADDRESS, 0},
  {"wallet_get_tx_note", wallet_get_tx_note, INPUT_TEXT, HEX64, 0},
  {"wallet_get_tx_key", wallet_get_tx_key, INPUT_TEXT, HEX64, 0},
  {"wallet_check_tx_key", wallet_check_tx_key, INPUT_TEXT, HEX64, 0},
  {"wallet_check_tx_proof", wallet_check_tx_proof, INPUT_TEXT, "InProofV2" HEX64 HEX64, 0},
  {"wallet_check_spend_proof", wallet_check_spend_proof, INPUT_TEXT, "SpendProofV1" HEX64 HEX64, 0},
  {"wallet_check_reserve_proof", wallet_check_reserve_proof, INPUT_TEXT, "ReserveProofV2" HEX64 HEX64 HEX64, 0},
  {"wallet_get_integrated_address", wallet_get_integrated_address, INPUT_TEXT, "abababababababab", 0},
  {"wallet_create_account", wallet_create_account, INPUT_TEXT, "a label", 0},
  {"wallet_get_tx_notes", wallet_get_tx_notes, INPUT_TEXT, HEX64, 0},
  {"wallet_attributes", wallet_attributes, INPUT_TEXT, HEX64, 0},
  {"wallet_create", wallet_create, INPUT_TEXT, WALLET_CONFIG, 8},
  {"wallet_create_from_keys", wallet_create, INPUT_TEXT, KEYS_CONFIG, 8},
  {"keys_create", keys_create, INPUT_TEXT, SEED, 0},
  {"wallet_paths", wallet_paths, INPUT_TEXT, "monero_c_fuzz_missing_wallet", 0},
  {"wallet_rpc_create_wallet", wallet_rpc_create_wallet_json, INPUT_TEXT, "{\"path\":\"name\",\"password\":\"password\",\"language\":\"English\",\"seed\":\"" SEED "\",\"restoreHeight\":0}", 0},
  {"wallet_rpc_set_daemon_connection", wallet_rpc_set_daemon_connection, INPUT_TEXT, CONNECTION_JSON, 0},
  {"wallet_rpc_ssl_options", wallet_rpc_set_daemon_connection, INPUT_TEXT, SSL_OPTIONS, 0},
  {"daemon_add_auxiliary_pow", daemon_add_auxiliary_pow, INPUT_TEXT, AUX_POW, 0},
  {"daemon_get_outputs", daemon_get_outputs, INPUT_TEXT, OUTPUTS, 0},
  {"daemon_set_peer_bans", daemon_set_peer_bans, INPUT_TEXT, BANS, 0},
  {"daemon_set_peer_ban", daemon_set_peer_ban, INPUT_TEXT, BAN, 0},
  {"daemon_hashes", daemon_hashes, INPUT_TEXT, HEX64, 0},
  // hex, which is the magic bytes of a blob that wallet2 reads and what follows them
  {"wallet_import_outputs", wallet_import_outputs, INPUT_HEX, "4d6f6e65726f206f757470757420657870" "6f727404", 0},
  {"wallet_sign_txs", wallet_sign_txs, INPUT_HEX, "4d6f6e65726f20756e7369676e656420747820736574" "05", 0},
  {"wallet_submit_txs", wallet_submit_txs, INPUT_HEX, "4d6f6e65726f207369676e656420747820736574" "05", 0},
  {"wallet_relay_tx", wallet_relay_tx, INPUT_HEX, "011673657269616c697a6174696f6e3a3a61726368697665", 0},
  {"wallet_sign_multisig_tx_hex", wallet_sign_multisig_tx_hex, INPUT_HEX, "4d6f6e65726f206d756c7469736967207369676e656420747820736574" "01", 0},
  {"wallet_submit_multisig_tx_hex", wallet_submit_multisig_tx_hex, INPUT_HEX, "4d6f6e65726f206d756c7469736967207369676e656420747820736574" "01", 0},
  {"wallet_make_multisig", wallet_make_multisig, INPUT_HEX, "4d756c74697369674d", 0},
  {"wallet_exchange_multisig_keys", wallet_exchange_multisig_keys, INPUT_HEX, "4d756c74697369674d", 0},
  {"wallet_import_multisig_hex", wallet_import_multisig_hex, INPUT_HEX, "4d6f6e65726f206d756c746973696720657870" "6f727401", 0},
  // binary: the storage of epee with a valid body, mutated
  {"utils_binary_to_json", utils_binary_to_json, INPUT_BYTES, "", 0},
  {"utils_binary_blocks_to_json", utils_binary_blocks_to_json, INPUT_BYTES, "", 0},
  {"utils_binary_blocks_fast_to_json", utils_binary_blocks_fast_to_json, INPUT_BYTES, "", 0},
};

// a random blob that starts with the bytes of the hex, as the seed of a hex target
static size_t blob_seed(const char* magic, char* out, size_t cap) {
  uint8_t body[1024];
  size_t length = strlen(magic);
  size_t body_length = fuzz_below(&g_rng, sizeof(body));
  if (length + 2 * body_length + 1 > cap) body_length = (cap - length - 1) / 2;
  for (size_t i = 0; i < body_length; i++) body[i] = (uint8_t) fuzz_below(&g_rng, 256);
  memcpy(out, magic, length);
  fuzz_to_hex(body, body_length, out + length);
  return length + 2 * body_length;
}

// the bytes of a valid response of monerod, to which the mutations of a binary target are applied: the storage of epee that json_to_binary writes
static size_t binary_seed(uint8_t* out, size_t cap) {
  static const char* const bodies[] = {
    "{\"blocks\":[],\"status\":\"OK\"}",
    "{\"blocks\":[{\"block\":\"aabbcc\",\"txs\":[{\"blob\":\"aa\",\"prunable_hash\":\"bb\"}]}],\"output_indices\":[{\"indices\":[{\"indices\":[1,2,3]}]}],\"status\":\"OK\",\"untrusted\":false,\"start_height\":1,\"current_height\":2}",
    "{\"hashes\":\"" HEX64 "\",\"start_height\":1,\"status\":\"OK\",\"top_hash\":\"" HEX64 "\"}",
    "{\"a\":{\"b\":[1,2,3],\"c\":\"text\"},\"d\":[\"x\",\"y\"],\"e\":1.5,\"f\":true,\"g\":18446744073709551615}",
  };
  uint8_t* data = NULL;
  size_t size = 0;
  if (monero_utils_json_to_binary(bodies[fuzz_below(&g_rng, sizeof(bodies) / sizeof(bodies[0]))], &data, &size) != MONERO_OK || data == NULL) return 0;
  if (size > cap) size = cap;
  memcpy(out, data, size);
  monero_utils_free(data);
  return size;
}

static void run_target(const target* t, const fixtures* fx, size_t iterations) {
  static char input[MAX_INPUT];
  static char seed[MAX_INPUT];
  static uint8_t bytes[MAX_INPUT];
  static uint8_t body[MAX_INPUT];
  g_target = t->name;
  for (size_t i = 0; i < iterations; i++) {
    size_t length = 0;
    if (t->kind == INPUT_TEXT) {
      // the first input is the valid one
      if (i == 0) {
        length = strlen(t->seed);
        memcpy(input, t->seed, length + 1);
      } else {
        length = fuzz_mutate_text(&g_rng, t->seed, input, sizeof(input));
      }
    } else if (t->kind == INPUT_HEX) {
      // one input in four is the magic bytes of the blob and random bytes, as they are
      size_t seed_length = blob_seed(t->seed, seed, sizeof(seed));
      if (i % 4 == 0) {
        length = seed_length;
        memcpy(input, seed, length + 1);
      } else {
        length = fuzz_mutate_hex(&g_rng, seed, input, sizeof(input));
      }
    } else {
      size_t body_length = binary_seed(body, sizeof(body));
      length = i == 0 ? body_length : fuzz_mutate_bytes(&g_rng, body, body_length, bytes, sizeof(bytes));
      if (i == 0) memcpy(bytes, body, body_length);
      // the call gets the bytes as they are, with a NUL after them that isn't counted
      memcpy(input, bytes, length);
      input[length] = '\0';
    }
    if (g_verbose) fprintf(stderr, "%s #%lu (%lu bytes): %.*s\n", t->name, (unsigned long) i, (unsigned long) length, (int) (length > 400 ? 400 : length), t->kind == INPUT_BYTES ? "(binary)" : input);
    t->call(fx, input, length);
  }
}

int main(void) {
  const char* path = "monero_c_fuzz_wallet";
  fixtures fx;
  monero_wallet* full = NULL;
  monero_wallet* keys = NULL;
  monero_wallet* rpc = NULL;
  monero_daemon* daemon = NULL;
  monero_rpc_connection* connection = NULL;
  size_t iterations = fuzz_iterations(150);
  size_t count = sizeof(g_targets) / sizeof(g_targets[0]);

  g_verbose = getenv("MONERO_C_FUZZ_VERBOSE") != NULL;
  fuzz_seed(&g_rng, fuzz_seed_value());
  remove_files(path);
  EXPECT_OK(monero_wallet_create_from_seed(path, "password", MAINNET, SEED, NULL, 0, NULL, &full));
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &keys));
  EXPECT_OK(monero_rpc_connection_create("{\"uri\":\"" CLOSED_PORT "\",\"timeoutMs\":1000}", &connection));
  EXPECT_OK(monero_wallet_rpc_connect(connection, &rpc));
  EXPECT_OK(monero_daemon_connect_with(connection, &daemon));
  if (full == NULL || keys == NULL || rpc == NULL || daemon == NULL || connection == NULL) {
    fprintf(stderr, "the fixtures were not created\n");
    return EXIT_FAILURE;
  }
  fx.full = full;
  fx.keys = keys;
  fx.rpc = rpc;
  fx.daemon = daemon;
  fx.connection = connection;

  for (size_t i = 0; i < count; i++) {
    size_t n = g_targets[i].iterations != 0 && g_targets[i].iterations < iterations ? g_targets[i].iterations : iterations;
    clock_t start = clock();
    run_target(&g_targets[i], &fx, n);
    if (g_verbose) fprintf(stderr, "%s took %.2f s\n", g_targets[i].name, (double) (clock() - start) / CLOCKS_PER_SEC);
  }
  printf("%lu targets, %lu inputs for each, seed %llu\n", (unsigned long) count, (unsigned long) iterations, (unsigned long long) fuzz_seed_value());

  monero_wallet_free(rpc);
  monero_wallet_free(keys);
  monero_wallet_free(full);
  monero_daemon_free(daemon);
  monero_rpc_connection_free(connection);
  remove_files(path);

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

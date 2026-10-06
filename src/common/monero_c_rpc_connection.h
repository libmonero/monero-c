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

#ifndef MONERO_C_RPC_CONNECTION_H
#define MONERO_C_RPC_CONNECTION_H

#include "utils/monero_c_utils.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------- RPC CONNECTION -------------------------------
// each function wraps the monero_rpc_connection method of the same name. On error,
// string and buffer outputs are set to NULL and scalar outputs are left unchanged

/**
 * Opaque handle to a connection to a monerod or monero-wallet-rpc server. Create it with
 * monero_rpc_connection_create() and release it with monero_rpc_connection_free().
 * Daemons and RPC wallets created from the handle share the connection and its status.
 *
 * @par Thread safety
 * monero-cpp locks the connection, so one handle can be used from several threads.
 * Don't call monero_rpc_connection_free() while another thread uses the handle.
 */
typedef struct monero_rpc_connection monero_rpc_connection;

/**
 * Bool that is unset until monero-cpp knows it, e.g. before the first check_connection().
 */
typedef enum monero_optional_bool {
  MONERO_OPTIONAL_BOOL_UNSET = -1,
  MONERO_OPTIONAL_BOOL_FALSE = 0,
  MONERO_OPTIONAL_BOOL_TRUE = 1
} monero_optional_bool;

/**
 * Create a connection. It doesn't contact the server.
 *
 * @param connection_json is the JSON-serialized monero_rpc_connection, e.g. {"uri":"http://127.0.0.1:18081","sslVerify":false}
 * @param out_connection receives the new handle, released with monero_rpc_connection_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_create(const char* connection_json, monero_rpc_connection** out_connection);

/**
 * Release a connection handle. The daemons and RPC wallets that share the connection keep it.
 * Passing NULL does nothing.
 *
 * @param connection is the handle to release
 */
MONERO_EXPORT void monero_rpc_connection_free(monero_rpc_connection* connection);

/**
 * Serialize the connection, with the status that check_connection() sets.
 *
 * @param connection is the connection handle
 * @param out_json receives the JSON-serialized monero_rpc_connection, with the password. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_serialize(monero_rpc_connection* connection, char** out_json);

/**
 * Set the credentials of the connection.
 *
 * @param connection is the connection handle
 * @param username is the username, or "" for none
 * @param password is the password, or "" for none
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_set_credentials(monero_rpc_connection* connection, const char* username, const char* password);

/**
 * Set an attribute of the connection. Attributes are kept in memory only.
 *
 * @param connection is the connection handle
 * @param key is the attribute key
 * @param value is the attribute value
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_set_attribute(monero_rpc_connection* connection, const char* key, const char* value);

/**
 * Get an attribute of the connection.
 *
 * @param connection is the connection handle
 * @param key is the attribute key
 * @param out_value receives the value, "" if the attribute is not set. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_get_attribute(monero_rpc_connection* connection, const char* key, char** out_value);

/**
 * Check if the URI is a Tor onion address.
 *
 * @param connection is the connection handle
 * @param out_is_onion receives true if the URI is a .onion address
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_is_onion(monero_rpc_connection* connection, bool* out_is_onion);

/**
 * Check if the URI is an I2P address.
 *
 * @param connection is the connection handle
 * @param out_is_i2p receives true if the URI is a .b32.i2p address
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_is_i2p(monero_rpc_connection* connection, bool* out_is_i2p);

/**
 * Check if the server was online at the last check_connection().
 *
 * @param connection is the connection handle
 * @param out_is_online receives the status, MONERO_OPTIONAL_BOOL_UNSET before the first check
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_is_online(monero_rpc_connection* connection, monero_optional_bool* out_is_online);

/**
 * Check if the server accepted the credentials at the last check_connection().
 *
 * @param connection is the connection handle
 * @param out_is_authenticated receives the status, also true if the server needs no credentials
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_is_authenticated(monero_rpc_connection* connection, monero_optional_bool* out_is_authenticated);

/**
 * Check if the server was online and authenticated at the last check_connection().
 *
 * @param connection is the connection handle
 * @param out_is_connected receives the status, MONERO_OPTIONAL_BOOL_UNSET before the first check
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_is_connected(monero_rpc_connection* connection, monero_optional_bool* out_is_connected);

/**
 * Check the connection and update its online, authentication and response time status.
 * An unreachable server is not an error. The status becomes offline.
 *
 * @param connection is the connection handle
 * @param timeout_ms is the time to wait for the server in milliseconds, or NULL for the connection's timeout
 * @param out_changed receives true if the online or authentication status changed
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_check_connection(monero_rpc_connection* connection, const uint32_t* timeout_ms, bool* out_changed);

/**
 * Send a JSON-RPC request to /json_rpc. monero-cpp parses the response into a property tree,
 * which has no types, so "true" and "false" become bools and numbers become numbers. Empty
 * arrays and objects become "".
 *
 * @param connection is the connection handle
 * @param method is the JSON-RPC method, e.g. "get_block_count"
 * @param params_json is a JSON object with the parameters, or NULL for none
 * @param timeout_ms is the request timeout in milliseconds, or NULL for the connection's timeout
 * @param out_json receives the "result" of the response. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_send_json_request(monero_rpc_connection* connection, const char* method, const char* params_json, const uint32_t* timeout_ms, char** out_json);

/**
 * Send a JSON request to a path of the server, e.g. "get_height" for /get_height. The response
 * has the same types as in monero_rpc_connection_send_json_request().
 *
 * @param connection is the connection handle
 * @param path is the path without the leading slash
 * @param params_json is a JSON object with the parameters, or NULL for none
 * @param timeout_ms is the request timeout in milliseconds, or NULL for the connection's timeout
 * @param out_json receives the response. Free with monero_utils_free()
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_send_path_request(monero_rpc_connection* connection, const char* path, const char* params_json, const uint32_t* timeout_ms, char** out_json);

/**
 * Send a binary request to a path of the server, e.g. "get_blocks_by_height.bin". The parameters
 * are sent in the portable storage format, and monero_utils_binary_to_json() converts the response.
 *
 * @param connection is the connection handle
 * @param path is the path without the leading slash
 * @param params_json is a JSON object with the parameters, or NULL for none
 * @param timeout_ms is the request timeout in milliseconds, or NULL for the connection's timeout
 * @param out_data receives the binary response. Free with monero_utils_free()
 * @param out_len receives the length of out_data
 * @return MONERO_OK or MONERO_ERROR
 */
MONERO_EXPORT monero_result monero_rpc_connection_send_binary_request(monero_rpc_connection* connection, const char* path, const char* params_json, const uint32_t* timeout_ms, uint8_t** out_data, size_t* out_len);

#ifdef __cplusplus
}
#endif

#endif // MONERO_C_RPC_CONNECTION_H

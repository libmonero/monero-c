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

// unit tests for the RPC connection binding, and for the daemons and RPC wallets created from a
// connection. None needs a server: the connections target a closed port, or a fake server in this
// program that answers with fixed JSON

#define UNREACHABLE "{\"uri\":\"http://127.0.0.1:1\",\"username\":\"user\",\"password\":\"pass\",\"sslVerify\":false,\"timeoutMs\":2000,\"priority\":2}"

static monero_rpc_connection* create_connection(const char* json) {
  monero_rpc_connection* connection = NULL;
  EXPECT_OK(monero_rpc_connection_create(json, &connection));
  CHECK(connection != NULL);
  return connection;
}

static int has(const char* json, const char* text) {
  return json != NULL && strstr(json, text) != NULL;
}

static void test_create_and_serialize(void) {
  monero_rpc_connection* connection = NULL;
  char* json = NULL;
  EXPECT_ERR_MSG(monero_rpc_connection_create(NULL, &connection), "connection_json must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_create(UNREACHABLE, NULL), "out_connection must not be null");
  EXPECT_ERR(monero_rpc_connection_create("not json", &connection));
  CHECK(connection == NULL);
  monero_rpc_connection_free(NULL);

  connection = create_connection(UNREACHABLE);
  if (connection == NULL) return;
  EXPECT_OK(monero_rpc_connection_serialize(connection, &json));
  CHECK(has(json, "\"uri\":\"http://127.0.0.1:1\""));
  CHECK(has(json, "\"username\":\"user\""));
  CHECK(has(json, "\"sslVerify\":false"));
  CHECK(has(json, "\"timeoutMs\":2000"));
  CHECK(has(json, "\"priority\":2"));
  CHECK(!has(json, "isOnline"));
  monero_utils_free(json);
  json = NULL;

  EXPECT_OK(monero_rpc_connection_set_credentials(connection, "other", "secret"));
  EXPECT_OK(monero_rpc_connection_serialize(connection, &json));
  CHECK(has(json, "\"username\":\"other\""));
  monero_utils_free(json);
  json = NULL;
  EXPECT_ERR_MSG(monero_rpc_connection_serialize(NULL, &json), "connection must not be null");
  CHECK(json == NULL);
  monero_rpc_connection_free(connection);
}

static void test_attributes(void) {
  monero_rpc_connection* connection = create_connection(UNREACHABLE);
  char* value = NULL;
  if (connection == NULL) return;
  EXPECT_OK(monero_rpc_connection_get_attribute(connection, "label", &value));
  CHECK(value != NULL && strcmp(value, "") == 0);
  monero_utils_free(value);
  value = NULL;
  EXPECT_OK(monero_rpc_connection_set_attribute(connection, "label", "local node"));
  EXPECT_OK(monero_rpc_connection_get_attribute(connection, "label", &value));
  CHECK(value != NULL && strcmp(value, "local node") == 0);
  monero_utils_free(value);
  EXPECT_ERR_MSG(monero_rpc_connection_set_attribute(connection, NULL, "x"), "key must not be null");
  monero_rpc_connection_free(connection);
}

static void test_address_kinds(void) {
  monero_rpc_connection* onion = create_connection("{\"uri\":\"http://abcdefghij.onion:18081\"}");
  monero_rpc_connection* i2p = create_connection("{\"uri\":\"http://abcdefghij.b32.i2p:18081\"}");
  monero_rpc_connection* plain = create_connection(UNREACHABLE);
  bool flag = false;
  if (onion != NULL) {
    EXPECT_OK(monero_rpc_connection_is_onion(onion, &flag));
    CHECK(flag);
    EXPECT_OK(monero_rpc_connection_is_i2p(onion, &flag));
    CHECK(!flag);
  }
  if (i2p != NULL) {
    EXPECT_OK(monero_rpc_connection_is_i2p(i2p, &flag));
    CHECK(flag);
  }
  if (plain != NULL) {
    EXPECT_OK(monero_rpc_connection_is_onion(plain, &flag));
    CHECK(!flag);
    EXPECT_OK(monero_rpc_connection_is_i2p(plain, &flag));
    CHECK(!flag);
  }
  monero_rpc_connection_free(onion);
  monero_rpc_connection_free(i2p);
  monero_rpc_connection_free(plain);
}

// an unreachable server is not an error: the check sets the status to offline
static void test_check_unreachable(void) {
  monero_rpc_connection* connection = create_connection(UNREACHABLE);
  monero_optional_bool status = MONERO_OPTIONAL_BOOL_TRUE;
  uint32_t timeout_ms = 2000;
  bool changed = false;
  char* json = NULL;
  uint8_t* data = NULL;
  size_t len = 0;
  if (connection == NULL) return;

  EXPECT_OK(monero_rpc_connection_is_online(connection, &status));
  CHECK(status == MONERO_OPTIONAL_BOOL_UNSET);
  EXPECT_OK(monero_rpc_connection_is_connected(connection, &status));
  CHECK(status == MONERO_OPTIONAL_BOOL_UNSET);

  EXPECT_OK(monero_rpc_connection_check_connection(connection, &timeout_ms, &changed));
  CHECK(changed);
  EXPECT_OK(monero_rpc_connection_is_online(connection, &status));
  CHECK(status == MONERO_OPTIONAL_BOOL_FALSE);
  EXPECT_OK(monero_rpc_connection_is_authenticated(connection, &status));
  CHECK(status == MONERO_OPTIONAL_BOOL_UNSET);
  EXPECT_OK(monero_rpc_connection_is_connected(connection, &status));
  CHECK(status == MONERO_OPTIONAL_BOOL_FALSE);
  EXPECT_OK(monero_rpc_connection_check_connection(connection, &timeout_ms, &changed));
  CHECK(!changed);
  EXPECT_OK(monero_rpc_connection_serialize(connection, &json));
  CHECK(has(json, "\"isOnline\":false"));
  monero_utils_free(json);
  json = NULL;

  // the requests fail, and bad parameters fail before anything is sent
  EXPECT_ERR(monero_rpc_connection_send_json_request(connection, "get_block_count", NULL, &timeout_ms, &json));
  CHECK(json == NULL);
  EXPECT_ERR_MSG(monero_rpc_connection_send_json_request(connection, "get_block_count", "[]", &timeout_ms, &json), "params must be a JSON object");
  EXPECT_ERR_MSG(monero_rpc_connection_send_path_request(connection, "get_height", "nope", &timeout_ms, &json), "params must be a JSON object");
  EXPECT_ERR(monero_rpc_connection_send_path_request(connection, "get_height", NULL, &timeout_ms, &json));
  CHECK(json == NULL);
  EXPECT_ERR(monero_rpc_connection_send_binary_request(connection, "get_blocks_by_height.bin", "{\"heights\":[0]}", &timeout_ms, &data, &len));
  CHECK(data == NULL);
  EXPECT_ERR_MSG(monero_rpc_connection_send_json_request(connection, NULL, NULL, NULL, &json), "method must not be null");
  monero_rpc_connection_free(connection);
}

// the daemon shares the connection, so it keeps the TLS setting and outlives the connection handle
static void test_daemon_from_connection(void) {
  monero_rpc_connection* connection = create_connection(UNREACHABLE);
  monero_daemon* daemon = NULL;
  char* json = NULL;
  uint64_t height = 0;
  if (connection == NULL) return;
  EXPECT_ERR_MSG(monero_daemon_connect_with(NULL, &daemon), "connection must not be null");
  EXPECT_OK(monero_daemon_connect_with(connection, &daemon));
  CHECK(daemon != NULL);
  monero_rpc_connection_free(connection);
  if (daemon == NULL) return;

  EXPECT_OK(monero_daemon_get_rpc_connection(daemon, &json));
  CHECK(has(json, "\"uri\":\"http://127.0.0.1:1\""));
  CHECK(has(json, "\"sslVerify\":false"));
  CHECK(has(json, "\"isOnline\":false"));
  monero_utils_free(json);
  EXPECT_OK(monero_daemon_set_poll_period(daemon, 1000));
  EXPECT_ERR(monero_daemon_get_height(daemon, &height));
  EXPECT_ERR_MSG(monero_daemon_get_rpc_connection(NULL, &json), "daemon must not be null");
  EXPECT_ERR_MSG(monero_daemon_set_poll_period(NULL, 1000), "daemon must not be null");
  monero_daemon_free(daemon);
}

// an RPC wallet from a connection has no wallet open, and opening one fails when the server is down
static void test_rpc_wallet_from_connection(void) {
  monero_rpc_connection* connection = create_connection(UNREACHABLE);
  monero_wallet* wallet = NULL;
  char* json = NULL;
  if (connection == NULL) return;
  EXPECT_ERR_MSG(monero_wallet_rpc_connect(NULL, &wallet), "connection must not be null");
  EXPECT_OK(monero_wallet_rpc_connect(connection, &wallet));
  CHECK(wallet != NULL);
  monero_rpc_connection_free(connection);
  if (wallet == NULL) return;

  EXPECT_OK(monero_wallet_rpc_get_connection(wallet, &json));
  CHECK(has(json, "\"sslVerify\":false"));
  monero_utils_free(json);
  EXPECT_ERR(monero_wallet_rpc_open_wallet(wallet, "name", "password"));
  EXPECT_ERR_MSG(monero_wallet_rpc_create_wallet(wallet, "[]"), "config must be a JSON object");
  EXPECT_ERR_MSG(monero_wallet_rpc_open_wallet(wallet, NULL, NULL), "name must not be null");
  monero_wallet_free(wallet);
}

// every function rejects a NULL connection
static void test_every_function_rejects_null_connection(void) {
  char* json = NULL;
  bool flag = false;
  size_t count = 0;
  uint8_t* data = NULL;
  monero_optional_bool status = MONERO_OPTIONAL_BOOL_UNSET;
  EXPECT_ERR_MSG(monero_rpc_connection_serialize(NULL, &json), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_set_credentials(NULL, "x", "x"), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_set_attribute(NULL, "x", "x"), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_get_attribute(NULL, "x", &json), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_is_onion(NULL, &flag), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_is_i2p(NULL, &flag), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_is_online(NULL, &status), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_is_authenticated(NULL, &status), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_is_connected(NULL, &status), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_check_connection(NULL, NULL, &flag), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_send_json_request(NULL, "x", "x", NULL, &json), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_send_path_request(NULL, "x", "x", NULL, &json), "connection must not be null");
  EXPECT_ERR_MSG(monero_rpc_connection_send_binary_request(NULL, "x", "x", NULL, &data, &count), "connection must not be null");
  CHECK(json == NULL && data == NULL);
}

// a call that fails on a NULL argument resets its outputs too
static void test_null_arguments_reset_outputs(void) {
  monero_rpc_connection* connection = POISON_PTR;
  char* json = POISON_PTR;
  uint8_t* data = POISON_PTR;
  size_t count = 7;

  EXPECT_ERR(monero_rpc_connection_create(NULL, &connection));
  CHECK(connection == NULL);
  EXPECT_ERR(monero_rpc_connection_serialize(NULL, &json));
  CHECK(json == NULL);

  EXPECT_ERR(monero_rpc_connection_send_binary_request(NULL, "x", "x", NULL, &data, &count));
  CHECK(data == NULL && count == 0);

  // with the data pointer NULL, the length is still reset
  count = 7;
  EXPECT_ERR(monero_rpc_connection_send_binary_request(NULL, "x", "x", NULL, NULL, &count));
  CHECK(count == 0);

  // a connection that exists, with a NULL path
  monero_rpc_connection* valid = create_connection(UNREACHABLE);
  if (valid == NULL) return;
  json = POISON_PTR;
  EXPECT_ERR(monero_rpc_connection_get_attribute(valid, NULL, &json));
  CHECK(json == NULL);
  json = POISON_PTR;
  EXPECT_ERR(monero_rpc_connection_send_path_request(valid, NULL, "{}", NULL, &json));
  CHECK(json == NULL);
  data = POISON_PTR;
  count = 7;
  EXPECT_ERR(monero_rpc_connection_send_binary_request(valid, NULL, "{}", NULL, &data, &count));
  CHECK(data == NULL && count == 0);
  monero_rpc_connection_free(valid);
}

#if !defined(_WIN32)

#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

// an HTTP server on a local port that answers every request with the body set before the request.
// It serves one client at a time and keeps its socket open, as the epee client expects
typedef struct fake_server {
  int fd;
  int port;
  volatile int stop;
  const char* volatile body;
  char request[4096];  // body of the last request, which may be binary
  size_t request_len;
} fake_server;

// reads one request and keeps its body. Returns 0 when the client closed the connection
static int read_request(fake_server* server, int client) {
  char buffer[8192];
  size_t used = 0;
  const char* end = NULL;
  while (end == NULL) {
    if (used + 1 >= sizeof(buffer)) return 0;
    ssize_t n = recv(client, buffer + used, sizeof(buffer) - 1 - used, 0);
    if (n <= 0) return 0;
    used += (size_t) n;
    buffer[used] = '\0';
    end = strstr(buffer, "\r\n\r\n");
  }
  size_t header_len = (size_t) (end + 4 - buffer);
  size_t body_len = 0;
  const char* length = strstr(buffer, "Content-Length:");
  if (length != NULL) body_len = (size_t) strtoul(length + 15, NULL, 10);
  if (header_len + body_len >= sizeof(buffer) || body_len > sizeof(server->request)) return 0;
  while (used < header_len + body_len) {
    ssize_t n = recv(client, buffer + used, sizeof(buffer) - 1 - used, 0);
    if (n <= 0) return 0;
    used += (size_t) n;
  }
  for (size_t i = 0; i < body_len; i++) server->request[i] = buffer[header_len + i];
  server->request_len = body_len;
  return 1;
}

// true if the body of the last request contains text
static int request_has(const fake_server* server, const char* text) {
  char request[sizeof(server->request) + 1];
  snprintf(request, sizeof(request), "%.*s", (int) server->request_len, server->request);
  return strstr(request, text) != NULL;
}

static void* serve(void* arg) {
  fake_server* server = (fake_server*) arg;
  while (!server->stop) {
    int client = accept(server->fd, NULL, NULL);
    if (client < 0) break;
    while (!server->stop && read_request(server, client)) {
      char header[256];
      const char* body = server->body;
      int body_len = snprintf(NULL, 0, "%s", body);
      int header_len = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n", body_len);
      if (send(client, header, (size_t) header_len, 0) < 0 || send(client, body, (size_t) body_len, 0) < 0) break;
    }
    close(client);
  }
  return NULL;
}

static int start_server(fake_server* server, pthread_t* thread) {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  server->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->fd < 0) return 0;
  if (bind(server->fd, (struct sockaddr*) &addr, sizeof(addr)) != 0 || listen(server->fd, 4) != 0 ||
      getsockname(server->fd, (struct sockaddr*) &addr, &addr_len) != 0) {
    close(server->fd);
    return 0;
  }
  server->port = ntohs(addr.sin_port);
  return pthread_create(thread, NULL, serve, server) == 0;
}

// wakes the accept() of the server with a last connection, then joins it
static void stop_server(fake_server* server, pthread_t thread) {
  struct sockaddr_in addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  server->stop = 1;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t) server->port);
  if (fd >= 0) {
    connect(fd, (struct sockaddr*) &addr, sizeof(addr));
    close(fd);
  }
  pthread_join(thread, NULL);
  close(server->fd);
}

// monero-cpp parses responses into a property tree, which keeps only text, so the types are guessed
static void test_response_types(void) {
  fake_server server = {-1, 0, 0, NULL, {0}, 0};
  pthread_t thread;
  char config[128];
  char* json = NULL;
  monero_rpc_connection* connection = NULL;
  if (!start_server(&server, &thread)) {
    CHECK(!"cannot start the fake server");
    return;
  }
  snprintf(config, sizeof(config), "{\"uri\":\"http://127.0.0.1:%d\",\"timeoutMs\":10000}", server.port);
  connection = create_connection(config);

  if (connection != NULL) {
    server.body = "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"result\":{"
      "\"negative\":-5,\"min\":-9223372036854775808,\"max\":18446744073709551615,\"huge\":184467440737095516160,\"low\":-9223372036854775809,\"inf\":1e999,"
      "\"price\":1.5,\"half\":-0.5,\"one\":1.0,\"small\":1.23e-4,\"large\":2E+3,"
      "\"lead\":\"007\",\"dot\":\"1.\",\"exp\":\"1e\",\"frac\":\"1.e5\",\"minus\":\"-\",\"word\":\"abc\","
      "\"yes\":true,\"no\":false,\"none\":null,\"empty\":[],"
      "\"nested\":[[1,2],[3]],\"objects\":[{\"a\":1},{\"b\":\"x\"}]}}";
    EXPECT_OK(monero_rpc_connection_send_json_request(connection, "any", "{\"value\":1}", NULL, &json));
    CHECK(request_has(&server, "\"method\":\"any\""));
    CHECK(request_has(&server, "\"params\":{\"value\":1}"));
    CHECK(has(json, "\"negative\":-5,"));
    CHECK(has(json, "\"min\":-9223372036854775808,"));
    CHECK(has(json, "\"max\":18446744073709551615,"));
    CHECK(has(json, "\"huge\":\"184467440737095516160\""));
    CHECK(has(json, "\"low\":\"-9223372036854775809\""));
    CHECK(has(json, "\"inf\":\"1e999\""));
    CHECK(has(json, "\"price\":1.5,"));
    CHECK(has(json, "\"half\":-0.5,"));
    CHECK(has(json, "\"one\":1.0,"));
    CHECK(has(json, "\"small\":") && !has(json, "\"small\":\""));
    CHECK(has(json, "\"large\":2000.0,"));
    CHECK(has(json, "\"lead\":\"007\""));
    CHECK(has(json, "\"dot\":\"1.\""));
    CHECK(has(json, "\"exp\":\"1e\""));
    CHECK(has(json, "\"frac\":\"1.e5\""));
    CHECK(has(json, "\"minus\":\"-\""));
    CHECK(has(json, "\"word\":\"abc\""));
    CHECK(has(json, "\"yes\":true,\"no\":false,\"none\":null,"));
    CHECK(has(json, "\"empty\":\"\""));
    CHECK(has(json, "\"nested\":[[1,2],[3]]"));
    CHECK(has(json, "\"objects\":[{\"a\":1},{\"b\":\"x\"}]"));
    monero_utils_free(json);
    json = NULL;

    // a JSON-RPC error fails the call with the server's message
    server.body = "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}";
    EXPECT_ERR_MSG(monero_rpc_connection_send_json_request(connection, "any", NULL, NULL, &json), "Method not found");
    CHECK(json == NULL);

    // a path request returns the whole response, and a JSON-RPC response has none
    server.body = "{\"height\":42,\"status\":\"OK\"}";
    EXPECT_OK(monero_rpc_connection_send_path_request(connection, "get_height", "{\"value\":2}", NULL, &json));
    CHECK(server.request_len == 11 && request_has(&server, "{\"value\":2}"));
    CHECK(has(json, "{\"height\":42,\"status\":\"OK\"}"));
    monero_utils_free(json);
    json = NULL;
    server.body = "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"result\":{}}";
    EXPECT_ERR_MSG(monero_rpc_connection_send_path_request(connection, "get_height", NULL, NULL, &json), "Invalid Monero RPC response");
    CHECK(json == NULL);

    // a binary request sends the parameters in the portable storage format, which starts with its signature
    uint8_t* data = NULL;
    size_t len = 0;
    server.body = "{\"height\":42,\"status\":\"OK\"}";
    EXPECT_OK(monero_rpc_connection_send_binary_request(connection, "get_blocks_by_height.bin", "{\"heights\":[0]}", NULL, &data, &len));
    CHECK(server.request_len > 9 && server.request[0] == 0x01 && server.request[1] == 0x11 && server.request[2] == 0x01 && server.request[3] == 0x01);
    CHECK(data != NULL && len == 27);
    monero_utils_free(data);
  }

  monero_rpc_connection_free(connection);
  stop_server(&server, thread);
}

#endif

int main(void) {
  test_create_and_serialize();
  test_attributes();
  test_address_kinds();
  test_check_unreachable();
  test_daemon_from_connection();
  test_rpc_wallet_from_connection();
  test_every_function_rejects_null_connection();
  test_null_arguments_reset_outputs();
#if !defined(_WIN32)
  // a client that closes its socket while the server writes must not stop the program
  signal(SIGPIPE, SIG_IGN);
  test_response_types();
#endif

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

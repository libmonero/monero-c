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
  EXPECT_ERR_MSG(monero_wallet_request_shutdown(wallet), "request_shutdown() not supported");

  // the array is checked before any request, and a config that nests too deeply before the parser
  json = POISON_PTR;
  EXPECT_ERR_MSG(monero_wallet_rpc_get_subaddresses(wallet, 0, NULL, 1, false, &json), "subaddress_indices must not be null");
  CHECK(json == NULL);
  char* deep = nested_json_object(100000);
  EXPECT_ERR_MSG(monero_wallet_rpc_create_wallet(wallet, deep), "JSON is nested deeper than 64 levels");
  free(deep);
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

// a JSON argument that nests too deeply fails before the parser, which would overflow the stack
static void test_json_depth_limit(void) {
  const char* message = "JSON is nested deeper than 64 levels";
  char* at_limit = nested_json(64);
  char* too_deep = nested_json(65);
  char* huge = nested_json(100000);
  monero_rpc_connection* connection = POISON_PTR;
  char* json = POISON_PTR;

  EXPECT_ERR_MSG(monero_rpc_connection_create(too_deep, &connection), message);
  CHECK(connection == NULL);
  EXPECT_ERR_MSG(monero_rpc_connection_create(huge, &connection), message);

  monero_rpc_connection* valid = create_connection(UNREACHABLE);
  if (valid != NULL) {
    EXPECT_ERR_MSG(monero_rpc_connection_send_json_request(valid, "x", too_deep, NULL, &json), message);
    CHECK(json == NULL);
    EXPECT_ERR_MSG(monero_rpc_connection_send_json_request(valid, "x", huge, NULL, &json), message);
    EXPECT_ERR_MSG(monero_rpc_connection_send_path_request(valid, "x", huge, NULL, &json), message);
    monero_rpc_connection_free(valid);
  }

  // 64 levels pass the check, and are then rejected as the parameters of a request
  monero_rpc_connection* other = create_connection(UNREACHABLE);
  if (other != NULL) {
    EXPECT_ERR(monero_rpc_connection_send_json_request(other, "x", at_limit, NULL, &json));
    CHECK(strcmp(monero_last_error(), message) != 0);
    monero_rpc_connection_free(other);
  }

  free(at_limit);
  free(too_deep);
  free(huge);
}

#if !defined(_WIN32)

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// waits for the milliseconds, and goes on waiting when a signal wakes it up
static void sleep_ms(long milliseconds) {
  struct timespec remaining;
  remaining.tv_sec = milliseconds / 1000;
  remaining.tv_nsec = (milliseconds % 1000) * 1000000L;
  while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
  }
}

// an HTTP server on a local port that answers every request with the body set before the request.
// It serves one client at a time and keeps its socket open, as the epee client expects
typedef struct fake_server {
  int fd;
  int port;
  pthread_mutex_t lock;  // guards the fields below, which the test and the server thread share
  int stop;
  const char* body;
  char request[4096];  // body of the last request, which may be binary
  size_t request_len;
  int requests;  // how many requests were read
  int socks;  // set before the start: the server then takes a SOCKS4a or SOCKS5 CONNECT first, as a proxy does, and answers HTTP through it
  int socks_version;  // the version of the last CONNECT, and the host and the port that it asked for
  char socks_host[256];
  int socks_port;
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
  pthread_mutex_lock(&server->lock);
  for (size_t i = 0; i < body_len; i++) server->request[i] = buffer[header_len + i];
  server->request_len = body_len;
  server->requests++;
  pthread_mutex_unlock(&server->lock);
  return 1;
}

// sets the body that the server answers with. NULL makes it hold the requests without an answer, until it stops
static void set_body(fake_server* server, const char* body) {
  pthread_mutex_lock(&server->lock);
  server->body = body;
  pthread_mutex_unlock(&server->lock);
}

static const char* get_body(fake_server* server) {
  pthread_mutex_lock(&server->lock);
  const char* body = server->body;
  pthread_mutex_unlock(&server->lock);
  return body;
}

// how many requests the server has read
static int request_count(fake_server* server) {
  pthread_mutex_lock(&server->lock);
  int count = server->requests;
  pthread_mutex_unlock(&server->lock);
  return count;
}

static int is_stopped(fake_server* server) {
  pthread_mutex_lock(&server->lock);
  int stop = server->stop;
  pthread_mutex_unlock(&server->lock);
  return stop;
}

// true if the body of the last request contains text
static int request_has(fake_server* server, const char* text) {
  char request[sizeof(server->request) + 1];
  pthread_mutex_lock(&server->lock);
  snprintf(request, sizeof(request), "%.*s", (int) server->request_len, server->request);
  pthread_mutex_unlock(&server->lock);
  return strstr(request, text) != NULL;
}

// the length of the body of the last request
static size_t request_size(fake_server* server) {
  pthread_mutex_lock(&server->lock);
  size_t size = server->request_len;
  pthread_mutex_unlock(&server->lock);
  return size;
}

// true if the body of the last request starts with the prefix
static int request_starts_with(fake_server* server, const unsigned char* prefix, size_t size) {
  pthread_mutex_lock(&server->lock);
  int match = server->request_len >= size && memcmp(server->request, prefix, size) == 0;
  pthread_mutex_unlock(&server->lock);
  return match;
}

// reads a string that ends with a NUL. Returns 0 if the client closed the connection or the string is longer than size
static int recv_text(int client, char* out, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (recv(client, out + i, 1, MSG_WAITALL) != 1) return 0;
    if (out[i] == '\0') return 1;
  }
  return 0;
}

// answers the CONNECT of a SOCKS4a or SOCKS5 client, as Tor does, and keeps the version and the host name that it asked for
static int socks_handshake(fake_server* server, int client) {
  unsigned char buffer[262];
  char host[256] = "";
  int version;
  int port;
  if (recv(client, buffer, 1, MSG_WAITALL) != 1) return 0;
  version = buffer[0];
  if (version == 4) {
    // the command, the port, the address 0.0.0.x that asks the proxy to resolve the name, then the user id and the name
    char user[256];
    if (recv(client, buffer, 7, MSG_WAITALL) != 7 || !recv_text(client, user, sizeof(user)) || !recv_text(client, host, sizeof(host))) return 0;
    port = buffer[1] << 8 | buffer[2];
    unsigned char reply[8] = {0x00, 0x5a, buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6]};
    if (send(client, reply, sizeof(reply), 0) < 0) return 0;
  } else if (version == 5) {
    // the methods, which get no authentication, then the command, a reserved byte, the type 3 for a name, its length, the name and the port
    unsigned char accept_none[2] = {0x05, 0x00};
    unsigned char granted[10] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    if (recv(client, buffer, 1, MSG_WAITALL) != 1) return 0;
    size_t methods = buffer[0];
    if (recv(client, buffer, methods, MSG_WAITALL) != (ssize_t) methods || send(client, accept_none, sizeof(accept_none), 0) < 0) return 0;
    if (recv(client, buffer, 5, MSG_WAITALL) != 5 || buffer[3] != 3) return 0;
    size_t length = buffer[4];
    if (recv(client, host, length, MSG_WAITALL) != (ssize_t) length) return 0;
    host[length] = '\0';
    if (recv(client, buffer, 2, MSG_WAITALL) != 2) return 0;
    port = buffer[0] << 8 | buffer[1];
    if (send(client, granted, sizeof(granted), 0) < 0) return 0;
  } else {
    return 0;
  }
  pthread_mutex_lock(&server->lock);
  server->socks_version = version;
  snprintf(server->socks_host, sizeof(server->socks_host), "%s", host);
  server->socks_port = port;
  pthread_mutex_unlock(&server->lock);
  return 1;
}

// true if the last CONNECT was of this SOCKS version, for this host name and port
static int socks_target_is(fake_server* server, int version, const char* host, int port) {
  pthread_mutex_lock(&server->lock);
  int match = server->socks_version == version && strcmp(server->socks_host, host) == 0 && server->socks_port == port;
  pthread_mutex_unlock(&server->lock);
  return match;
}

static void* serve(void* arg) {
  fake_server* server = (fake_server*) arg;
  while (!is_stopped(server)) {
    int client = accept(server->fd, NULL, NULL);
    if (client < 0) break;
    if (server->socks && !socks_handshake(server, client)) {
      close(client);
      continue;
    }
    while (!is_stopped(server) && read_request(server, client)) {
      char header[256];
      const char* body = get_body(server);
      if (body == NULL) {
        while (!is_stopped(server)) sleep_ms(10);
        break;
      }
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
  server->body = "{}";  // what a request gets before the test sets a body, and not a held request
  pthread_mutex_init(&server->lock, NULL);
  if (pthread_create(thread, NULL, serve, server) != 0) {
    pthread_mutex_destroy(&server->lock);
    close(server->fd);
    return 0;
  }
  return 1;
}

// wakes the accept() of the server with a last connection, then joins it
static void stop_server(fake_server* server, pthread_t thread) {
  struct sockaddr_in addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  pthread_mutex_lock(&server->lock);
  server->stop = 1;
  pthread_mutex_unlock(&server->lock);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t) server->port);
  if (fd >= 0) {
    connect(fd, (struct sockaddr*) &addr, sizeof(addr));
    close(fd);
  }
  pthread_join(thread, NULL);
  pthread_mutex_destroy(&server->lock);
  close(server->fd);
}

// monero-cpp parses responses into a property tree, which keeps only text, so the types are guessed
static void test_response_types(void) {
  fake_server server;
  pthread_t thread;
  char config[128];
  char* json = NULL;
  monero_rpc_connection* connection = NULL;
  memset(&server, 0, sizeof(server));
  if (!start_server(&server, &thread)) {
    CHECK(!"cannot start the fake server");
    return;
  }
  snprintf(config, sizeof(config), "{\"uri\":\"http://127.0.0.1:%d\",\"timeoutMs\":10000}", server.port);
  connection = create_connection(config);

  if (connection != NULL) {
    set_body(&server, "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"result\":{"
      "\"negative\":-5,\"min\":-9223372036854775808,\"max\":18446744073709551615,\"huge\":184467440737095516160,\"low\":-9223372036854775809,\"inf\":1e999,"
      "\"price\":1.5,\"half\":-0.5,\"one\":1.0,\"small\":1.23e-4,\"large\":2E+3,"
      "\"lead\":\"007\",\"dot\":\"1.\",\"exp\":\"1e\",\"frac\":\"1.e5\",\"minus\":\"-\",\"word\":\"abc\","
      "\"yes\":true,\"no\":false,\"none\":null,\"empty\":[],"
      "\"nested\":[[1,2],[3]],\"objects\":[{\"a\":1},{\"b\":\"x\"}]}}");
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
    set_body(&server, "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    EXPECT_ERR_MSG(monero_rpc_connection_send_json_request(connection, "any", NULL, NULL, &json), "Method not found");
    CHECK(json == NULL);

    // a path request returns the whole response, and a JSON-RPC response has none
    set_body(&server, "{\"height\":42,\"status\":\"OK\"}");
    EXPECT_OK(monero_rpc_connection_send_path_request(connection, "get_height", "{\"value\":2}", NULL, &json));
    CHECK(request_size(&server) == 11 && request_has(&server, "{\"value\":2}"));
    CHECK(has(json, "{\"height\":42,\"status\":\"OK\"}"));
    monero_utils_free(json);
    json = NULL;
    set_body(&server, "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"result\":{}}");
    EXPECT_ERR_MSG(monero_rpc_connection_send_path_request(connection, "get_height", NULL, NULL, &json), "Invalid Monero RPC response");
    CHECK(json == NULL);

    // a binary request sends the parameters in the portable storage format, which starts with its signature
    uint8_t* data = NULL;
    size_t len = 0;
    set_body(&server, "{\"height\":42,\"status\":\"OK\"}");
    EXPECT_OK(monero_rpc_connection_send_binary_request(connection, "get_blocks_by_height.bin", "{\"heights\":[0]}", NULL, &data, &len));
    CHECK(request_size(&server) > 9 && request_starts_with(&server, (const unsigned char*) "\x01\x11\x01\x01", 4));
    CHECK(data != NULL && len == 27);
    monero_utils_free(data);
  }

  monero_rpc_connection_free(connection);
  stop_server(&server, thread);
}

// the answer of a regtest monerod to get_block, for the block at height 5, which has only its miner tx
static const char* const GET_BLOCK_RESPONSE =
  "{\"id\":\"0\",\"jsonrpc\":\"2.0\",\"result\":{\"blob\":\"1010a2b1a9d606c34c01246454397230ca260c97e7d22d798e758cd2463b6"
  "a6bafa0b8629d60cc00000000024101ff0501ff8780f0feff07032a88b2d22b44d5df99481dab1d7c821fdb7c83e66c10433516a8cc20ba7b34c99"
  "22401af58de76b561d77c53117e5977f59a6d0d80831bf2b9b51e1eb0f3bb7feff4b70201000000\",\"block_header\":{\"block_size\":85,"
  "\"block_weight\":85,\"cumulative_difficulty\":6,\"cumulative_difficulty_top64\":0,\"depth\":95,\"difficulty\":1,\"diff"
  "iculty_top64\":0,\"hash\":\"9ffadcf6a7fc4612f08910bdd7343933d46feb20f2277c58771cf97f6798f8e1\",\"height\":5,\"long_ter"
  "m_weight\":176470,\"major_version\":16,\"miner_tx_hash\":\"0b4083f547e9b71c4ceb4ef3048a9c8fea2b9013445419d9df4b06b8a96"
  "9c627\",\"minor_version\":16,\"nonce\":0,\"num_txes\":0,\"orphan_status\":false,\"pow_hash\":\"\",\"prev_hash\":\"c34c"
  "01246454397230ca260c97e7d22d798e758cd2463b6a6bafa0b8629d60cc\",\"reward\":35184070099967,\"timestamp\":1791645858,\"wi"
  "de_cumulative_difficulty\":\"0x6\",\"wide_difficulty\":\"0x1\"},\"credits\":0,\"json\":\"{\\n  \\\"major_version\\\": "
  "16, \\n  \\\"minor_version\\\": 16, \\n  \\\"timestamp\\\": 1791645858, \\n  \\\"prev_id\\\": "
  "\\\"c34c01246454397230ca260c97e7d22d798e758cd2463b6a6bafa0b8629d60cc\\\", \\n  \\\"nonce\\\": 0, \\n  "
  "\\\"miner_tx\\\": {\\n    \\\"version\\\": 2, \\n    \\\"unlock_time\\\": 65, \\n    \\\"vin\\\": [ {\\n        "
  "\\\"gen\\\": {\\n          \\\"height\\\": 5\\n        }\\n      }\\n    ], \\n    \\\"vout\\\": [ {\\n        "
  "\\\"amount\\\": 35184070099967, \\n        \\\"target\\\": {\\n          \\\"tagged_key\\\": {\\n            "
  "\\\"key\\\": \\\"2a88b2d22b44d5df99481dab1d7c821fdb7c83e66c10433516a8cc20ba7b34c9\\\", \\n            "
  "\\\"view_tag\\\": \\\"92\\\"\\n          }\\n        }\\n      }\\n    ], \\n    \\\"extra\\\": [ 1, 175, 88, 222, "
  "118, 181, 97, 215, 124, 83, 17, 126, 89, 119, 245, 154, 109, 13, 128, 131, 27, 242, 185, 181, 30, 30, 176, 243, 187, "
  "127, 239, 244, 183, 2, 1, 0\\n    ], \\n    \\\"rct_signatures\\\": {\\n      \\\"type\\\": 0\\n    }\\n  }, \\n  "
  "\\\"tx_hashes\\\": [ ]\\n}\",\"miner_tx_hash\":\"0b4083f547e9b71c4ceb4ef3048a9c8fea2b9013445419d9df4b06b8a969c627\",\""
  "status\":\"OK\",\"top_hash\":\"\",\"untrusted\":false}}";

// the answer to get_transactions for a tx with two inputs and two outputs, as a confirmed tx of block 101. The hex and the signatures are cut, since monero-cpp does not read them
static const char* const GET_TRANSACTIONS_RESPONSE =
  "{\"credits\":0,\"status\":\"OK\",\"top_hash\":\"\",\"txs\":[{\"as_hex\":\"\",\"as_json\":\"{\\\"version\\\":2,\\\"unlo"
  "ck_time\\\":0,\\\"vin\\\":[{\\\"key\\\":{\\\"amount\\\":0,\\\"key_offsets\\\":[0,1,3,2,1,1,4,5,8,2,1,8,1,1,1,1],\\\"k_"
  "image\\\":\\\"f35f0734fd630bbcc175fbeb824436b4094bb5cb2ec3fcdaa0b40a38d994a3f8\\\"}},{\\\"key\\\":{\\\"amount\\\":0,\\"
  "\"key_offsets\\\":[0,6,1,2,2,1,2,2,1,2,5,6,1,1,5,2],\\\"k_image\\\":\\\"d2680fea7ead71e391c7a8e6e0343922ddf7aeb92315ac"
  "25834e5faf7cc9fecd\\\"}}],\\\"vout\\\":[{\\\"amount\\\":0,\\\"target\\\":{\\\"tagged_key\\\":{\\\"key\\\":\\\"dfac00fd"
  "a542ae5d859f4019c32517979a80021f97be262c08dcf05625295040\\\",\\\"view_tag\\\":\\\"3b\\\"}}},{\\\"amount\\\":0,\\\"targ"
  "et\\\":{\\\"tagged_key\\\":{\\\"key\\\":\\\"e432b5ac4a3bebcbdf282666ebd1bee2222f916b1958a3fab16b6e80aa560ab9\\\",\\\"v"
  "iew_tag\\\":\\\"f7\\\"}}}],\\\"extra\\\":[1,118,139,214,199,43,162,62,100,230,87,87,103,83,75,220,156,2,236,22,185,99,"
  "6,231,247,11,130,42,108,222,104,175,110,2,9,1,223,150,226,29,41,199,209,230],\\\"rct_signatures\\\":{\\\"type\\\":6,\\"
  "\"txnFee\\\":2599200000,\\\"outPk\\\":[\\\"a3a1ad610261e70d766f065059950e78a38ed74ca2740f54dcf292b0d3633ab7\\\",\\\"5d"
  "87b0a532b85accae03f25b8b4c3f8766d8aa6c0a62f83a72ce4b9a26818a07\\\"]}}\",\"block_height\":101,\"block_timestamp\":17916"
  "45000,\"confirmations\":1,\"double_spend_seen\":false,\"in_pool\":false,\"output_indices\":[201,202],\"prunable_as_hex"
  "\":\"\",\"prunable_hash\":\"fb547e70cd07dd1f48f59f8b25a555f63423d1d6a35eac929d8956ec8ede2e3c\",\"pruned_as_hex\":\"\","
  "\"tx_hash\":\"52e698a070e263d91a808883c3407cd473bd8a3b36455be231cb3bf3c3336118\"}],\"txs_as_hex\":[\"\"],\"untrusted\""
  ":false}";

// the answer to get_transaction_pool with the same tx, cut the same way
static const char* const GET_TRANSACTION_POOL_RESPONSE =
  "{\"credits\":0,\"spent_key_images\":[],\"status\":\"OK\",\"top_hash\":\"\",\"transactions\":[{\"blob_size\":2166,\"do_"
  "not_relay\":false,\"double_spend_seen\":false,\"fee\":2599200000,\"id_hash\":\"52e698a070e263d91a808883c3407cd473bd8a3"
  "b36455be231cb3bf3c3336118\",\"kept_by_block\":false,\"last_failed_height\":0,\"last_failed_id_hash\":\"000000000000000"
  "0000000000000000000000000000000000000000000000000\",\"last_relayed_time\":1791645862,\"max_used_block_height\":41,\"ma"
  "x_used_block_id_hash\":\"77e2250259021160f76607f1e2eabea8eb2128d30c8334180aead02efdde9776\",\"receive_time\":179164586"
  "2,\"relayed\":true,\"tx_blob\":\"\",\"tx_json\":\"{\\\"version\\\":2,\\\"unlock_time\\\":0,\\\"vin\\\":[{\\\"key\\\":{"
  "\\\"amount\\\":0,\\\"key_offsets\\\":[0,1,3,2,1,1,4,5,8,2,1,8,1,1,1,1],\\\"k_image\\\":\\\"f35f0734fd630bbcc175fbeb824"
  "436b4094bb5cb2ec3fcdaa0b40a38d994a3f8\\\"}},{\\\"key\\\":{\\\"amount\\\":0,\\\"key_offsets\\\":[0,6,1,2,2,1,2,2,1,2,5,"
  "6,1,1,5,2],\\\"k_image\\\":\\\"d2680fea7ead71e391c7a8e6e0343922ddf7aeb92315ac25834e5faf7cc9fecd\\\"}}],\\\"vout\\\":[{"
  "\\\"amount\\\":0,\\\"target\\\":{\\\"tagged_key\\\":{\\\"key\\\":\\\"dfac00fda542ae5d859f4019c32517979a80021f97be262c0"
  "8dcf05625295040\\\",\\\"view_tag\\\":\\\"3b\\\"}}},{\\\"amount\\\":0,\\\"target\\\":{\\\"tagged_key\\\":{\\\"key\\\":"
  "\\\"e432b5ac4a3bebcbdf282666ebd1bee2222f916b1958a3fab16b6e80aa560ab9\\\",\\\"view_tag\\\":\\\"f7\\\"}}}],\\\"extra\\\":"
  "[1,118,139,214,199,43,162,62,100,230,87,87,103,83,75,220,156,2,236,22,185,99,6,231,247,11,130,42,108,222,104,175,110,2"
  ",9,1,223,150,226,29,41,199,209,230],\\\"rct_signatures\\\":{\\\"type\\\":6,\\\"txnFee\\\":2599200000,\\\"outPk\\\":[\\"
  "\"a3a1ad610261e70d766f065059950e78a38ed74ca2740f54dcf292b0d3633ab7\\\",\\\"5d87b0a532b85accae03f25b8b4c3f8766d8aa6c0a6"
  "2f83a72ce4b9a26818a07\\\"]}}\",\"weight\":2166}],\"untrusted\":false}";

#define BLOCK_HASH "9ffadcf6a7fc4612f08910bdd7343933d46feb20f2277c58771cf97f6798f8e1"
#define TX_HASH "52e698a070e263d91a808883c3407cd473bd8a3b36455be231cb3bf3c3336118"

// the proxy of a connection, of a daemon and of a wallet receives the host name of an onion address, which it resolves itself.
// The server answers the CONNECT of a SOCKS proxy before the HTTP request, as Tor does
static void test_proxy(void) {
  fake_server server;
  pthread_t thread;
  char config[256];
  char proxy[64];
  char* json = NULL;
  uint64_t height = 0;
  bool connected = true;
  monero_rpc_connection* connection = NULL;
  monero_daemon* daemon = NULL;
  monero_wallet* wallet = NULL;
  memset(&server, 0, sizeof(server));
  server.socks = 1;
  if (!start_server(&server, &thread)) {
    CHECK(!"cannot start the fake server");
    return;
  }

  // an address without a scheme is SOCKS4a, and the host name goes to the proxy
  snprintf(config, sizeof(config), "{\"uri\":\"http://abcdefghij.onion:18081\",\"proxyUri\":\"127.0.0.1:%d\",\"timeoutMs\":10000}", server.port);
  connection = create_connection(config);
  if (connection != NULL) {
    set_body(&server, "{\"height\":42,\"status\":\"OK\"}");
    EXPECT_OK(monero_rpc_connection_send_path_request(connection, "get_height", NULL, NULL, &json));
    CHECK(has(json, "{\"height\":42,\"status\":\"OK\"}"));
    CHECK(socks_target_is(&server, 4, "abcdefghij.onion", 18081));
    monero_utils_free(json);
    json = NULL;
    monero_rpc_connection_free(connection);
  }

  // a daemon with a socks5 proxy
  snprintf(proxy, sizeof(proxy), "socks5://127.0.0.1:%d", server.port);
  set_body(&server, "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"result\":{\"count\":42,\"status\":\"OK\"}}");
  EXPECT_OK(monero_daemon_connect("http://klmnopqrst.onion:18089", "", "", proxy, 10000, &daemon));
  if (daemon != NULL) {
    EXPECT_OK(monero_daemon_get_height(daemon, &height));
    CHECK(height == 42);
    CHECK(socks_target_is(&server, 5, "klmnopqrst.onion", 18089));
    monero_daemon_free(daemon);
  }

  // a wallet made with the proxy in its config, and one that gets the proxy later
  snprintf(config, sizeof(config), "{\"networkType\":0,\"server\":{\"uri\":\"http://uvwxyzabcd.onion:18081\",\"proxyUri\":\"127.0.0.1:%d\"}}", server.port);
  EXPECT_OK(monero_wallet_create(config, &wallet));
  if (wallet != NULL) {
    CHECK(socks_target_is(&server, 4, "uvwxyzabcd.onion", 18081));
    EXPECT_OK(monero_wallet_set_daemon_connection(wallet, "http://efghijklmn.onion:18081", "", "", proxy, NULL, true));
    EXPECT_OK(monero_wallet_is_connected_to_daemon(wallet, &connected));
    CHECK(socks_target_is(&server, 5, "efghijklmn.onion", 18081));
    monero_wallet_free(wallet);
  }

  stop_server(&server, thread);
}

// monero-cpp builds a block with its miner tx, and a tx with its inputs and outputs, and each of them points back to its parent.
// The daemon calls release them once the JSON is built, and LeakSanitizer fails the run if one of them doesn't
static void test_daemon_models_are_released(void) {
  fake_server server;
  pthread_t thread;
  char config[128];
  char* json = NULL;
  monero_rpc_connection* connection = NULL;
  monero_daemon* daemon = NULL;
  const char* tx_hashes[] = {TX_HASH};
  memset(&server, 0, sizeof(server));
  if (!start_server(&server, &thread)) {
    CHECK(!"cannot start the fake server");
    return;
  }
  set_body(&server, GET_BLOCK_RESPONSE);
  snprintf(config, sizeof(config), "{\"uri\":\"http://127.0.0.1:%d\",\"timeoutMs\":10000}", server.port);
  connection = create_connection(config);
  if (connection != NULL) EXPECT_OK(monero_daemon_connect_with(connection, &daemon));

  if (daemon != NULL) {
    EXPECT_OK(monero_daemon_get_block_by_height(daemon, 5, &json));
    CHECK(has(json, "\"height\":5") && has(json, "\"minerTx\":{") && has(json, "\"outputs\":["));
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_daemon_get_block_by_hash(daemon, BLOCK_HASH, &json));
    CHECK(has(json, "\"hash\":\"" BLOCK_HASH "\"") && has(json, "\"minerTx\":{"));
    monero_utils_free(json);
    json = NULL;

    set_body(&server, GET_TRANSACTIONS_RESPONSE);
    EXPECT_OK(monero_daemon_get_tx(daemon, TX_HASH, false, &json));
    CHECK(has(json, "\"hash\":\"" TX_HASH "\"") && has(json, "\"inputs\":[") && has(json, "\"outputs\":["));
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_daemon_get_txs(daemon, tx_hashes, 1, false, &json));
    CHECK(json != NULL && json[0] == '[' && has(json, "\"inputs\":["));
    monero_utils_free(json);
    json = NULL;

    set_body(&server, GET_TRANSACTION_POOL_RESPONSE);
    EXPECT_OK(monero_daemon_get_tx_pool(daemon, &json));
    CHECK(json != NULL && json[0] == '[' && has(json, "\"hash\":\"" TX_HASH "\"") && has(json, "\"inputs\":["));
    monero_utils_free(json);
    json = NULL;
  }

  monero_daemon_free(daemon);
  monero_rpc_connection_free(connection);
  stop_server(&server, thread);
}

// a sync on a thread, and its result
typedef struct sync_call {
  monero_wallet* wallet;
  volatile int done;
  monero_result result;
  char error[256];
} sync_call;

static void* run_sync(void* arg) {
  sync_call* call = (sync_call*) arg;
  char* json = NULL;
  monero_result result = monero_wallet_sync(call->wallet, NULL, NULL, &json);
  snprintf(call->error, sizeof(call->error), "%s", result == MONERO_OK ? "" : monero_last_error());
  monero_utils_free(json);
  call->result = result;
  call->done = 1;
  return NULL;
}

// a sync that waits for a daemon that never answers returns when another thread asks the wallet to shut down. If it doesn't, the
// server closes the connection after 20 seconds, so that the join can't hang
static void test_request_shutdown_aborts_a_sync(void) {
  fake_server server;
  pthread_t server_thread;
  pthread_t sync_thread;
  char uri[64];
  const char* path = "monero_c_rpc_connection_shutdown";
  monero_wallet* wallet = NULL;
  sync_call call;
  memset(&server, 0, sizeof(server));
  memset(&call, 0, sizeof(call));
  if (!start_server(&server, &server_thread)) {
    CHECK(!"cannot start the fake server");
    return;
  }
  set_body(&server, NULL);
  EXPECT_OK(monero_wallet_create_random(path, "password", MONERO_UTILS_NETWORK_MAINNET, NULL, &wallet));
  if (wallet != NULL) {
    snprintf(uri, sizeof(uri), "http://127.0.0.1:%d", server.port);
    EXPECT_OK(monero_wallet_set_daemon_connection(wallet, uri, "", "", "", NULL, false));
    call.wallet = wallet;
    CHECK(pthread_create(&sync_thread, NULL, run_sync, &call) == 0);

    // the sync is in flight once the server has read its request
    for (int i = 0; i < 1000 && request_count(&server) == 0; i++) sleep_ms(10);
    CHECK(request_count(&server) > 0);
    EXPECT_OK(monero_wallet_request_shutdown(wallet));
    for (int i = 0; i < 2000 && !call.done; i++) sleep_ms(10);
    CHECK(call.done);
    if (!call.done) {
      stop_server(&server, server_thread);
      server.fd = -1;
    }
    pthread_join(sync_thread, NULL);
    CHECK(call.result == MONERO_ERROR && strcmp(call.error, "Wallet is not connected to daemon") == 0);

    // the wallet can be closed once the call has returned
    EXPECT_OK(monero_wallet_close(wallet, false));
    monero_wallet_free(wallet);
    remove_files(path);
  }
  if (server.fd != -1) stop_server(&server, server_thread);
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
  test_json_depth_limit();
#if !defined(_WIN32)
  // a client that closes its socket while the server writes must not stop the program
  signal(SIGPIPE, SIG_IGN);
  test_response_types();
  test_proxy();
  test_daemon_models_are_released();
  test_request_shutdown_aborts_a_sync();
#endif

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

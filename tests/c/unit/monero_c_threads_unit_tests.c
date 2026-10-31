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

#if defined(_WIN32)

int main(void) {
  printf("skipped: the thread tests run on POSIX systems only\n");
  return EXIT_SUCCESS;
}

#else

#include <pthread.h>

// what the library promises for several threads: the functions that take no handle can be called from any thread, each thread has its own
// last error, handles that are not shared don't interfere, and the handles that the headers call safe to share are. Run it under
// ThreadSanitizer to look for data races in monero-c itself (the static libraries of monero-project aren't instrumented)

#define THREADS 6
#define MAINNET MONERO_UTILS_NETWORK_MAINNET
#define PASSWORD "password"
#define SEED "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex"
#define ADDRESS "48W9YHwPzRz9aPTeXCA6kmSpW6HsvmWx578jj3of2gT3JwZzwTf33amESBoNDkL6SVK34Q2HTKqgYbGyE1hBws3wCrcBDR2"
#define VIEW_KEY "e8c2288181bad9ec410d7322efd65f663c6da57bd1d1198636278a039743a600"
#define CLOSED_PORT "{\"uri\":\"http://127.0.0.1:1\",\"timeoutMs\":1000}"

// the threads count what they check, and add it to the totals after they have joined
typedef struct worker {
  int index;
  int checks;
  int failures;
  void* shared;  // what the threads of a test share, if anything
} worker;

#define WORKER_CHECK(w, cond) do { \
  (w)->checks++; \
  if (!(cond)) { \
    (w)->failures++; \
    fprintf(stderr, "FAIL %s:%d (thread %d): %s\n", __FILE__, __LINE__, (w)->index, #cond); \
  } \
} while (0)

// the threads wait here until all have started, so that they work together
typedef struct gate {
  pthread_mutex_t lock;
  pthread_cond_t open;
  int waiting;
  int total;
} gate;

static void gate_init(gate* g, int total) {
  pthread_mutex_init(&g->lock, NULL);
  pthread_cond_init(&g->open, NULL);
  g->waiting = 0;
  g->total = total;
}

static void gate_wait(gate* g) {
  pthread_mutex_lock(&g->lock);
  g->waiting++;
  if (g->waiting >= g->total) pthread_cond_broadcast(&g->open);
  while (g->waiting < g->total) pthread_cond_wait(&g->open, &g->lock);
  pthread_mutex_unlock(&g->lock);
}

static void gate_destroy(gate* g) {
  pthread_mutex_destroy(&g->lock);
  pthread_cond_destroy(&g->open);
}

typedef void* (*worker_fn)(void*);

// runs the function in THREADS threads, and adds what they checked to the totals
static void run_threads(worker_fn fn, void* shared) {
  pthread_t threads[THREADS];
  worker workers[THREADS];
  int started = 0;
  for (int i = 0; i < THREADS; i++) {
    workers[i].index = i;
    workers[i].checks = 0;
    workers[i].failures = 0;
    workers[i].shared = shared;
  }
  for (int i = 0; i < THREADS; i++) {
    if (pthread_create(&threads[i], NULL, fn, &workers[i]) == 0) started++;
    else break;
  }
  CHECK(started == THREADS);
  for (int i = 0; i < started; i++) pthread_join(threads[i], NULL);
  for (int i = 0; i < started; i++) {
    g_checks += workers[i].checks;
    g_failures += workers[i].failures;
  }
}

// ---------------------------------- UTILS ------------------------------------

static gate g_utils_gate;

// the functions without a handle, and the message of the last error that each thread keeps for itself
static void* utils_worker(void* arg) {
  worker* w = (worker*) arg;
  char expected[64];
  gate_wait(&g_utils_gate);
  for (int i = 0; i < 150; i++) {
    char* json = NULL;
    uint8_t* data = NULL;
    size_t length = 0;
    char* round_trip = NULL;
    json = NULL;

    WORKER_CHECK(w, monero_utils_is_valid_address(ADDRESS, MAINNET));
    WORKER_CHECK(w, !monero_utils_is_valid_address("not an address", MAINNET));
    WORKER_CHECK(w, monero_utils_is_valid_private_view_key(VIEW_KEY));
    WORKER_CHECK(w, monero_utils_is_valid_mnemonic(SEED, "English"));
    WORKER_CHECK(w, !monero_utils_is_valid_mnemonic("one two three", "English"));
    WORKER_CHECK(w, monero_utils_get_integrated_address(MAINNET, ADDRESS, "abababababababab", &json) == MONERO_OK && json != NULL);
    monero_utils_free(json);

    WORKER_CHECK(w, monero_utils_json_to_binary("{\"a\":[1,2,3],\"b\":\"text\"}", &data, &length) == MONERO_OK && data != NULL && length > 0);
    WORKER_CHECK(w, monero_utils_binary_to_json(data, length, &round_trip) == MONERO_OK && round_trip != NULL && strstr(round_trip, "\"text\"") != NULL);
    monero_utils_free(data);
    monero_utils_free(round_trip);

    // each thread fails with a message of its own, and reads it back whatever the others do
    snprintf(expected, sizeof(expected), "out_json must not be null");
    if (w->index % 2 == 0) {
      WORKER_CHECK(w, monero_utils_get_integrated_address(MAINNET, ADDRESS, "", NULL) == MONERO_ERROR);
    } else {
      snprintf(expected, sizeof(expected), "unknown network type");
      WORKER_CHECK(w, monero_utils_get_integrated_address(99, ADDRESS, "", &json) == MONERO_ERROR);
    }
    WORKER_CHECK(w, strcmp(monero_last_error(), expected) == 0);
  }
  return NULL;
}

static void test_utils_from_many_threads(void) {
  gate_init(&g_utils_gate, THREADS);
  run_threads(utils_worker, NULL);
  gate_destroy(&g_utils_gate);
}

// --------------------------------- WALLETS -----------------------------------

static gate g_wallet_gate;

// each thread has wallets of its own, which are not shared
static void* wallet_worker(void* arg) {
  worker* w = (worker*) arg;
  char path[64];
  monero_wallet* full = NULL;
  monero_wallet* keys = NULL;
  char* text = NULL;
  snprintf(path, sizeof(path), "monero_c_threads_wallet_%d", w->index);
  remove_files(path);

  gate_wait(&g_wallet_gate);
  WORKER_CHECK(w, monero_wallet_create_from_seed(path, PASSWORD, MAINNET, SEED, NULL, 0, NULL, &full) == MONERO_OK && full != NULL);
  WORKER_CHECK(w, monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &keys) == MONERO_OK && keys != NULL);
  for (int i = 0; i < 30 && full != NULL && keys != NULL; i++) {
    WORKER_CHECK(w, monero_wallet_get_primary_address(full, &text) == MONERO_OK && text != NULL && strcmp(text, ADDRESS) == 0);
    monero_utils_free(text);
    text = NULL;
    WORKER_CHECK(w, monero_wallet_get_primary_address(keys, &text) == MONERO_OK && text != NULL && strcmp(text, ADDRESS) == 0);
    monero_utils_free(text);
    text = NULL;
    WORKER_CHECK(w, monero_wallet_sign_message(full, "message", MONERO_MESSAGE_SIGN_WITH_SPEND_KEY, 0, 0, &text) == MONERO_OK && text != NULL);
    if (text != NULL) {
      char* result = NULL;
      WORKER_CHECK(w, monero_wallet_verify_message(keys, "message", ADDRESS, text, &result) == MONERO_OK && result != NULL && strstr(result, "\"isGood\":true") != NULL);
      monero_utils_free(result);
    }
    monero_utils_free(text);
    text = NULL;
    WORKER_CHECK(w, monero_wallet_create_subaddress(full, 0, "label", &text) == MONERO_OK && text != NULL);
    monero_utils_free(text);
    text = NULL;
    WORKER_CHECK(w, monero_wallet_get_accounts(full, true, &text) == MONERO_OK && text != NULL && strstr(text, "\"label\":\"label\"") != NULL);
    monero_utils_free(text);
    text = NULL;
    // the others fail the same way, and only for this thread
    WORKER_CHECK(w, monero_wallet_get_txs(keys, "{}", &text) == MONERO_ERROR && text == NULL);
    WORKER_CHECK(w, strcmp(monero_last_error(), "get_txs(query) not supported") == 0);
  }
  WORKER_CHECK(w, full == NULL || monero_wallet_save(full) == MONERO_OK);
  WORKER_CHECK(w, full == NULL || monero_wallet_close(full, true) == MONERO_OK);
  monero_wallet_free(full);
  monero_wallet_free(keys);
  remove_files(path);
  return NULL;
}

static void test_wallets_from_many_threads(void) {
  gate_init(&g_wallet_gate, THREADS);
  run_threads(wallet_worker, NULL);
  gate_destroy(&g_wallet_gate);
}

// -------------------------------- CONNECTIONS --------------------------------

typedef struct shared_handles {
  monero_rpc_connection* connection;
  monero_daemon* daemon;
  gate start;
} shared_handles;

// the threads of a test share one connection, which monero-cpp locks, and one daemon, which serializes its calls
static void* connection_worker(void* arg) {
  worker* w = (worker*) arg;
  shared_handles* shared = (shared_handles*) w->shared;
  char key[32];
  char value[32];
  gate_wait(&shared->start);
  snprintf(key, sizeof(key), "key%d", w->index);
  for (int i = 0; i < 40; i++) {
    char* text = NULL;
    bool flag = false;
    monero_optional_bool status = MONERO_OPTIONAL_BOOL_UNSET;
    uint64_t height = 0;
    snprintf(value, sizeof(value), "value%d_%d", w->index, i);
    WORKER_CHECK(w, monero_rpc_connection_set_attribute(shared->connection, key, value) == MONERO_OK);
    WORKER_CHECK(w, monero_rpc_connection_get_attribute(shared->connection, key, &text) == MONERO_OK && text != NULL && strcmp(text, value) == 0);
    monero_utils_free(text);
    text = NULL;
    WORKER_CHECK(w, monero_rpc_connection_serialize(shared->connection, &text) == MONERO_OK && text != NULL);
    monero_utils_free(text);
    WORKER_CHECK(w, monero_rpc_connection_is_onion(shared->connection, &flag) == MONERO_OK);
    WORKER_CHECK(w, monero_rpc_connection_is_online(shared->connection, &status) == MONERO_OK);
    WORKER_CHECK(w, monero_rpc_connection_set_credentials(shared->connection, "user", "password") == MONERO_OK);
    WORKER_CHECK(w, monero_daemon_get_height(shared->daemon, &height) == MONERO_ERROR);
    WORKER_CHECK(w, monero_last_error()[0] != '\0');
    if (i % 10 == 0) {
      // the closed port is not an error of the check, it is the status
      uint32_t timeout_ms = 500;
      WORKER_CHECK(w, monero_rpc_connection_check_connection(shared->connection, &timeout_ms, &flag) == MONERO_OK);
    }
  }
  return NULL;
}

static void test_shared_connection_and_daemon(void) {
  shared_handles shared;
  shared.connection = NULL;
  shared.daemon = NULL;
  EXPECT_OK(monero_rpc_connection_create(CLOSED_PORT, &shared.connection));
  EXPECT_OK(monero_daemon_connect_with(shared.connection, &shared.daemon));
  if (shared.connection == NULL || shared.daemon == NULL) {
    monero_rpc_connection_free(shared.connection);
    monero_daemon_free(shared.daemon);
    return;
  }
  gate_init(&shared.start, THREADS);
  run_threads(connection_worker, &shared);
  gate_destroy(&shared.start);
  monero_daemon_free(shared.daemon);
  monero_rpc_connection_free(shared.connection);
}

// -------------------------------- LISTENERS ----------------------------------

typedef struct listener_handles {
  monero_wallet* wallets[2];
  monero_wallet_listener* contested;
  monero_daemon* daemon;
  gate start;
  int wins;
  pthread_mutex_t lock;
} listener_handles;

// different listeners on one daemon can be added and removed from different threads
static void* daemon_listener_worker(void* arg) {
  worker* w = (worker*) arg;
  listener_handles* shared = (listener_handles*) w->shared;
  monero_daemon_listener_callbacks callbacks = {sizeof(monero_daemon_listener_callbacks), NULL, NULL};
  gate_wait(&shared->start);
  for (int i = 0; i < 40; i++) {
    monero_daemon_listener* listener = NULL;
    monero_daemon_listener** listeners = NULL;
    size_t count = 0;
    WORKER_CHECK(w, monero_daemon_listener_create(&callbacks, &listener) == MONERO_OK && listener != NULL);
    if (listener == NULL) continue;
    WORKER_CHECK(w, monero_daemon_add_listener(shared->daemon, listener) == MONERO_OK);
    WORKER_CHECK(w, monero_daemon_get_listeners(shared->daemon, &listeners, &count) == MONERO_OK && count >= 1);
    monero_utils_free(listeners);
    if (i % 2 == 0) {
      WORKER_CHECK(w, monero_daemon_remove_listener(shared->daemon, listener) == MONERO_OK);
    }
    // a listener that is still added is removed by its free
    monero_daemon_listener_free(listener);
  }
  return NULL;
}

// one listener that two threads add to two wallets: one add works and the other fails, whatever the order
static void* wallet_listener_worker(void* arg) {
  worker* w = (worker*) arg;
  listener_handles* shared = (listener_handles*) w->shared;
  gate_wait(&shared->start);
  if (monero_wallet_add_listener(shared->wallets[w->index % 2], shared->contested) == MONERO_OK) {
    pthread_mutex_lock(&shared->lock);
    shared->wins++;
    pthread_mutex_unlock(&shared->lock);
  } else {
    WORKER_CHECK(w, strcmp(monero_last_error(), "listener is already registered with another wallet") == 0);
  }
  return NULL;
}

static void test_listeners_from_many_threads(void) {
  listener_handles shared;
  const char* paths[2] = {"monero_c_threads_listener_a", "monero_c_threads_listener_b"};
  monero_rpc_connection* connection = NULL;
  monero_wallet_listener_callbacks wallet_callbacks = {sizeof(monero_wallet_listener_callbacks)};
  memset(&shared, 0, sizeof(shared));
  pthread_mutex_init(&shared.lock, NULL);
  EXPECT_OK(monero_rpc_connection_create(CLOSED_PORT, &connection));
  EXPECT_OK(monero_daemon_connect_with(connection, &shared.daemon));
  monero_rpc_connection_free(connection);
  if (shared.daemon != NULL) {
    gate_init(&shared.start, THREADS);
    run_threads(daemon_listener_worker, &shared);
    gate_destroy(&shared.start);
    EXPECT_OK(monero_daemon_remove_listeners(shared.daemon));
    monero_daemon_free(shared.daemon);
  }

  for (int i = 0; i < 2; i++) {
    remove_files(paths[i]);
    EXPECT_OK(monero_wallet_create_from_seed(paths[i], PASSWORD, MAINNET, SEED, NULL, 0, NULL, &shared.wallets[i]));
  }
  EXPECT_OK(monero_wallet_listener_create(&wallet_callbacks, &shared.contested));
  if (shared.wallets[0] != NULL && shared.wallets[1] != NULL && shared.contested != NULL) {
    for (int round = 0; round < 25; round++) {
      shared.wins = 0;
      gate_init(&shared.start, 2);
      // two threads of the pool of THREADS: the others have nothing to do and return
      pthread_t threads[2];
      worker workers[2];
      int started = 0;
      for (int i = 0; i < 2; i++) {
        workers[i].index = i;
        workers[i].checks = 0;
        workers[i].failures = 0;
        workers[i].shared = &shared;
        if (pthread_create(&threads[i], NULL, wallet_listener_worker, &workers[i]) == 0) started++;
        else break;
      }
      CHECK(started == 2);
      for (int i = 0; i < started; i++) {
        pthread_join(threads[i], NULL);
        g_checks += workers[i].checks;
        g_failures += workers[i].failures;
      }
      gate_destroy(&shared.start);
      CHECK(shared.wins == 1);
      EXPECT_OK(monero_wallet_remove_listeners(shared.wallets[0]));
      EXPECT_OK(monero_wallet_remove_listeners(shared.wallets[1]));
    }
  }
  monero_wallet_listener_free(shared.contested);
  monero_wallet_free(shared.wallets[0]);
  monero_wallet_free(shared.wallets[1]);
  for (int i = 0; i < 2; i++) remove_files(paths[i]);
  pthread_mutex_destroy(&shared.lock);
}

int main(void) {
  test_utils_from_many_threads();
  test_wallets_from_many_threads();
  test_shared_connection_and_daemon();
  test_listeners_from_many_threads();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif

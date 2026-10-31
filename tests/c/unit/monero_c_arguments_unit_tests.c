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

#include <stdint.h>
#include <time.h>

// calls every exported function with a NULL in each pointer parameter, one at a time, on a full wallet, a keys wallet, a closed wallet, an
// RPC wallet, a daemon and a connection. The functions come from monero_c_arguments_table.h, which tests/c/generator/generate_argument_tests.py
// writes from the headers, so a new function is in the test as soon as the table is regenerated. A call has to return, and:
//  - a NULL in a pointer that the function requires fails with "<name> must not be null",
//  - a failed call sets the pointers that it returns to NULL and the counts to 0, and leaves the other outputs as they were,
//  - a call that works returns what the caller has to free, and the test frees it.
// A call with valid arguments is not made, since it would close, free or stop the fixture. Set MONERO_C_TEST_VERBOSE to print each call

#define MAX_PARAMS 12

typedef enum arg_kind {
  K_END,
  K_STR,
  K_STRS,
  K_WALLET,
  K_DAEMON,
  K_CONNECTION,
  K_WALLET_LISTENER,
  K_DAEMON_LISTENER,
  K_WALLET_CALLBACKS,
  K_DAEMON_CALLBACKS,
  K_U64,
  K_U32,
  K_I32,
  K_U16,
  K_U8,
  K_BOOL,
  K_SIZE,
  K_DOUBLE,
  K_PU64,
  K_PU32,
  K_PI32,
  K_PBOOL,
  K_PBYTES,
  K_PTR,
  K_OUT_STR,
  K_OUT_BYTES,
  K_OUT_U64S,
  K_OUT_STATUSES,
  K_OUT_LISTENERS,
  K_OUT_WALLET,
  K_OUT_DAEMON,
  K_OUT_CONNECTION,
  K_OUT_WALLET_LISTENER,
  K_OUT_DAEMON_LISTENER,
  K_OUT_U64,
  K_OUT_U32,
  K_OUT_I32,
  K_OUT_BOOL,
  K_OUT_SIZE,
  K_OUT_ENUM,
  K_OUT_BUF32,
  K_OUT_BUF8
} arg_kind;

typedef enum ret_kind { RET_RESULT, RET_OTHER } ret_kind;

typedef struct arg_function {
  const char* name;
  monero_result (*call)(void** args);
  int count;
  arg_kind kinds[MAX_PARAMS];
  const char* names[MAX_PARAMS];
  unsigned required;  // bit i: the function fails with "<names[i]> must not be null" when parameter i is NULL
  ret_kind ret;
} arg_function;

#include "monero_c_arguments_table.h"

#define MAINNET MONERO_UTILS_NETWORK_MAINNET
#define SEED "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex"
#define ADDRESS "48W9YHwPzRz9aPTeXCA6kmSpW6HsvmWx578jj3of2gT3JwZzwTf33amESBoNDkL6SVK34Q2HTKqgYbGyE1hBws3wCrcBDR2"
#define VIEW_KEY "e8c2288181bad9ec410d7322efd65f663c6da57bd1d1198636278a039743a600"
#define HEX64 "abababababababababababababababababababababababababababababababab"
// Windows retries a connection to a closed port of the loopback for about two seconds, and every call that goes to the network pays that, so
// there it is an address that is refused at once
#if defined(_WIN32)
#define CLOSED_PORT "http://0.0.0.0:1"
#define DNS_SERVER "tcp://0.0.0.0"
#else
#define CLOSED_PORT "http://127.0.0.1:1"
#define DNS_SERVER "tcp://127.0.0.1"
#endif
#define CONNECTION_JSON "{\"uri\":\"" CLOSED_PORT "\",\"username\":\"user\",\"password\":\"pass\",\"sslVerify\":false,\"timeoutMs\":1000}"

#define MARK_PTR POISON_PTR
#define MARK_U64 0x5a5a5a5a5a5a5a5aULL
#define MARK_SIZE ((size_t) 77)

typedef struct fixture {
  const char* label;
  monero_wallet* wallet;
  monero_daemon* daemon;
  monero_rpc_connection* connection;
  monero_wallet_listener* wallet_listener;
  monero_daemon_listener* daemon_listener;
} fixture;

typedef union scalar {
  uint64_t u64;
  uint32_t u32;
  int32_t i32;
  uint16_t u16;
  uint8_t u8;
  bool boolean;
  size_t size;
  double real;
  void* ptr;
} scalar;

// the storage behind the arguments of one call
typedef struct call_state {
  void* args[MAX_PARAMS];
  scalar scalars[MAX_PARAMS];
  uint64_t u64s[MAX_PARAMS][2];
  uint32_t u32s[MAX_PARAMS][2];
  int32_t i32s[MAX_PARAMS][2];
  bool bools[MAX_PARAMS][2];
  const char* strs[MAX_PARAMS][3];
  uint8_t bytes[MAX_PARAMS][32];
  monero_wallet_listener_callbacks wallet_callbacks;
  monero_daemon_listener_callbacks daemon_callbacks;
  char path[MAX_PARAMS][64];
} call_state;

// what the arguments of a call are: plausible, the largest numbers, or long texts
typedef enum value_mode { VALUES_VALID, VALUES_LARGEST, VALUES_LONG } value_mode;

#define LONG_TEXT_LENGTH 100000

static int g_verbose = 0;
static int g_unique = 0;
static char* g_long_text = NULL;

#define MAX_PATHS 4096
static char g_paths[MAX_PATHS][64];
static int g_path_count = 0;

static int is_out_pointer(arg_kind kind) {
  return kind >= K_OUT_STR && kind <= K_OUT_DAEMON_LISTENER;
}

static int is_out_scalar(arg_kind kind) {
  return kind >= K_OUT_U64 && kind <= K_OUT_ENUM;
}

static int is_out(arg_kind kind) {
  return is_out_pointer(kind) || is_out_scalar(kind) || kind == K_OUT_BUF32 || kind == K_OUT_BUF8;
}

// a parameter that is a pointer, and can be set to NULL
static int is_pointer(arg_kind kind) {
  return kind == K_STR || kind == K_STRS || (kind >= K_WALLET && kind <= K_DAEMON_CALLBACKS) || (kind >= K_PU64 && kind <= K_PTR) || is_out(kind);
}

static int is_array(arg_kind kind) {
  return kind == K_STRS || kind == K_PU64 || kind == K_PU32 || kind == K_PI32 || kind == K_PBOOL || kind == K_PBYTES;
}

static int is_count(const char* name) {
  return strncmp(name, "num_", 4) == 0 || strcmp(name, "len") == 0;
}

// the text that a parameter gets, so that the other arguments of a call are as valid as they can be without the call doing harm
static const char* benign_string(const char* name, int index, call_state* state) {
  size_t length = text_length(name, 64);
  if (strcmp(name, "path") == 0) {
    snprintf(state->path[index], sizeof(state->path[index]), "monero_c_arguments_path_%d", g_unique++);
    if (g_path_count < MAX_PATHS) snprintf(g_paths[g_path_count++], sizeof(g_paths[0]), "%s", state->path[index]);
    return state->path[index];
  }
  if (strcmp(name, "password") == 0 || strcmp(name, "old_password") == 0 || strcmp(name, "new_password") == 0 || strcmp(name, "wallet_password") == 0) return "password";
  if (strcmp(name, "language") == 0) return "English";
  if (strcmp(name, "seed") == 0 || strcmp(name, "mnemonic") == 0) return SEED;
  if (strcmp(name, "seed_offset") == 0 || strcmp(name, "proxy_uri") == 0 || strcmp(name, "proxy") == 0) return "";
  if (strstr(name, "address") != NULL) return ADDRESS;
  if (strcmp(name, "private_view_key") == 0) return VIEW_KEY;
  if (strcmp(name, "uri") == 0) return CLOSED_PORT;
  if (strcmp(name, "username") == 0) return "user";
  if (strcmp(name, "categories") == 0) return "*:WARNING";
  if (strcmp(name, "payment_id") == 0 || strcmp(name, "payment_id_str") == 0) return "abababababababab";
  if (strstr(name, "hash") != NULL || strstr(name, "key") != NULL || strcmp(name, "key_image") == 0) return HEX64;
  if (length > 5 && strcmp(name + length - 5, "_json") == 0) {
    if (strcmp(name, "bans_json") == 0 || strcmp(name, "outputs_json") == 0 || strcmp(name, "aux_pow_json") == 0 || strcmp(name, "key_images_json") == 0 || strcmp(name, "txs_json") == 0) return "[]";
    if (strcmp(name, "connection_json") == 0) return CONNECTION_JSON;
    return "{}";
  }
  if (strstr(name, "hex") != NULL || strstr(name, "blob") != NULL || strcmp(name, "tx_metadata") == 0) return "aabbcc";
  if (strcmp(name, "name") == 0) return "monero_c_arguments_name";
  return "x";
}

static const char* const* benign_strings(const char* name, int index, call_state* state) {
  const char* first = "x";
  const char* second = "y";
  if (strstr(name, "hash") != NULL || strcmp(name, "key_images") == 0) first = second = HEX64;
  else if (strcmp(name, "notes") == 0) { first = "first note"; second = "second note"; }
  else if (strstr(name, "hex") != NULL || strstr(name, "blob") != NULL || strcmp(name, "tx_metadatas") == 0) { first = "aabb"; second = "ccdd"; }
  state->strs[index][0] = first;
  state->strs[index][1] = second;
  state->strs[index][2] = NULL;
  return state->strs[index];
}

static void set_scalar(const char* name, arg_kind kind, scalar* value, value_mode mode) {
  memset(value, 0, sizeof(*value));
  if (mode == VALUES_LARGEST) {
    switch (kind) {
      case K_U64: value->u64 = UINT64_MAX; return;
      case K_U32: value->u32 = UINT32_MAX; return;
      case K_I32: value->i32 = INT32_MAX; return;
      case K_U16: value->u16 = UINT16_MAX; return;
      case K_U8: value->u8 = UINT8_MAX; return;
      case K_BOOL: value->boolean = true; return;
      case K_DOUBLE: value->real = 1e300; return;
      default: break;
    }
  }
  switch (kind) {
    case K_U64: value->u64 = strstr(name, "period") != NULL ? 1000 : (strstr(name, "idx") != NULL || strstr(name, "index") != NULL || strstr(name, "restore_height") != NULL) ? 0 : 1; break;
    case K_U32: value->u32 = (strstr(name, "idx") != NULL || strstr(name, "index") != NULL) ? 0 : 1; break;
    case K_I32: value->i32 = strcmp(name, "network_type") == 0 ? MAINNET : (strcmp(name, "signature_type") == 0 || strcmp(name, "level") == 0) ? 0 : 2; break;
    case K_U16: value->u16 = 2024; break;
    case K_U8: value->u8 = 1; break;
    case K_BOOL: value->boolean = false; break;
    case K_SIZE: value->size = is_count(name) ? 2 : 1; break;
    case K_DOUBLE: value->real = 1.0; break;
    default: break;
  }
}

// the texts of the long mode keep the name of a file or a server, which a long one only makes fail, or look up
static int keeps_its_text(const char* name) {
  return strcmp(name, "path") == 0 || strcmp(name, "name") == 0 || strstr(name, "uri") != NULL || strcmp(name, "proxy") == 0;
}

static void prepare(const arg_function* function, const fixture* fx, call_state* state, value_mode mode) {
  memset(state, 0, sizeof(*state));
  state->wallet_callbacks.struct_size = sizeof(state->wallet_callbacks);
  state->daemon_callbacks.struct_size = sizeof(state->daemon_callbacks);
  for (int i = 0; i < function->count; i++) {
    const char* name = function->names[i];
    arg_kind kind = function->kinds[i];
    scalar* value = &state->scalars[i];
    void** slot = &state->args[i];
    switch (kind) {
      case K_STR: *slot = (void*) benign_string(name, i, state); break;
      case K_STRS: *slot = (void*) benign_strings(name, i, state); break;
      case K_WALLET: *slot = fx->wallet; break;
      case K_DAEMON: *slot = fx->daemon; break;
      case K_CONNECTION: *slot = fx->connection; break;
      case K_WALLET_LISTENER: *slot = fx->wallet_listener; break;
      case K_DAEMON_LISTENER: *slot = fx->daemon_listener; break;
      case K_WALLET_CALLBACKS: *slot = &state->wallet_callbacks; break;
      case K_DAEMON_CALLBACKS: *slot = &state->daemon_callbacks; break;
      case K_U64: case K_U32: case K_I32: case K_U16: case K_U8: case K_BOOL: case K_SIZE: case K_DOUBLE:
        set_scalar(name, kind, value, mode);
        *slot = value;
        break;
      case K_PU64: state->u64s[i][1] = 1; *slot = state->u64s[i]; break;
      case K_PU32: state->u32s[i][1] = 1; *slot = state->u32s[i]; break;
      case K_PI32: state->i32s[i][1] = 1; *slot = state->i32s[i]; break;
      case K_PBOOL: *slot = state->bools[i]; break;
      case K_PBYTES: *slot = state->bytes[i]; break;
      case K_PTR: *slot = NULL; break;
      case K_OUT_STR: case K_OUT_BYTES: case K_OUT_U64S: case K_OUT_STATUSES: case K_OUT_LISTENERS:
      case K_OUT_WALLET: case K_OUT_DAEMON: case K_OUT_CONNECTION: case K_OUT_WALLET_LISTENER: case K_OUT_DAEMON_LISTENER:
        value->ptr = MARK_PTR;
        *slot = value;
        break;
      case K_OUT_U64: value->u64 = MARK_U64; *slot = value; break;
      case K_OUT_U32: value->u32 = 0x5a5a5a5aU; *slot = value; break;
      case K_OUT_I32: value->i32 = 0x5a5a5a5a; *slot = value; break;
      case K_OUT_BOOL: value->boolean = true; *slot = value; break;
      case K_OUT_SIZE: value->size = MARK_SIZE; *slot = value; break;
      case K_OUT_ENUM: value->i32 = 0x5a5a5a5a; *slot = value; break;
      case K_OUT_BUF32: *slot = state->bytes[i]; break;
      case K_OUT_BUF8: *slot = state->bytes[i]; break;
      case K_END: break;
    }
    if (mode == VALUES_LONG && !keeps_its_text(name)) {
      if (kind == K_STR) *slot = g_long_text;
      if (kind == K_STRS) state->strs[i][0] = state->strs[i][1] = g_long_text;
    }
  }
}

// frees what a call returned, and checks that a failed call returned nothing
static void settle(const arg_function* function, call_state* state, const char* variant, monero_result result) {
  int failed = function->ret == RET_RESULT && result != MONERO_OK;
  for (int i = 0; i < function->count; i++) {
    arg_kind kind = function->kinds[i];
    scalar* value = &state->scalars[i];
    if (!is_out(kind) || state->args[i] == NULL) continue;
    if (is_out_pointer(kind)) {
      if (failed && value->ptr != NULL) {
        g_failures++;
        fprintf(stderr, "FAIL %s (%s): %s was not reset by a failed call\n", function->name, variant, function->names[i]);
      }
      if (value->ptr != NULL && value->ptr != MARK_PTR) {
        switch (kind) {
          case K_OUT_WALLET: monero_wallet_free((monero_wallet*) value->ptr); break;
          case K_OUT_DAEMON: monero_daemon_free((monero_daemon*) value->ptr); break;
          case K_OUT_CONNECTION: monero_rpc_connection_free((monero_rpc_connection*) value->ptr); break;
          case K_OUT_WALLET_LISTENER: monero_wallet_listener_free((monero_wallet_listener*) value->ptr); break;
          case K_OUT_DAEMON_LISTENER: monero_daemon_listener_free((monero_daemon_listener*) value->ptr); break;
          default: monero_utils_free(value->ptr); break;
        }
      }
      g_checks++;
    } else if (is_out_scalar(kind) && failed) {
      int unchanged = 1;
      switch (kind) {
        case K_OUT_U64: unchanged = value->u64 == MARK_U64; break;
        case K_OUT_U32: unchanged = value->u32 == 0x5a5a5a5aU; break;
        case K_OUT_I32: case K_OUT_ENUM: unchanged = value->i32 == 0x5a5a5a5a; break;
        case K_OUT_BOOL: unchanged = value->boolean == true; break;
        case K_OUT_SIZE: unchanged = value->size == 0; break;  // the count of an array that a failed call didn't return
        default: break;
      }
      g_checks++;
      if (!unchanged) {
        g_failures++;
        fprintf(stderr, "FAIL %s (%s): %s changed in a failed call\n", function->name, variant, function->names[i]);
      }
    }
  }
}

static void remove_paths(const arg_function* function, call_state* state) {
  for (int i = 0; i < function->count; i++) {
    if (state->path[i][0] != '\0') remove_files(state->path[i]);
  }
}

// a call that takes seconds says so, so that the log of a slow run shows which functions wait for the network. The clock is time(), which every
// platform has with the same meaning, so a call is reported when it took two seconds or more
static monero_result timed_call(const arg_function* function, void** args) {
  time_t start = time(NULL);
  monero_result result = function->call(args);
  double seconds = difftime(time(NULL), start);
  if (seconds >= 2.0) fprintf(stderr, "slow call: %s took %.0f s\n", function->name, seconds);
  return result;
}

// runs one call with the parameters of null_mask set to NULL. expected is the parameter that has to be rejected by name, or -1
static void run_call(const arg_function* function, const fixture* fx, unsigned null_mask, int expected, const char* variant) {
  call_state state;
  prepare(function, fx, &state, VALUES_VALID);
  for (int i = 0; i < function->count; i++) {
    if ((null_mask & (1u << i)) != 0) state.args[i] = NULL;
  }
  if (g_verbose) fprintf(stderr, "%s [%s] %s\n", function->name, fx->label, variant);
  monero_result result = timed_call(function, state.args);
  if (function->ret == RET_RESULT) {
    g_checks++;
    if (result != MONERO_OK && result != MONERO_ERROR) {
      g_failures++;
      fprintf(stderr, "FAIL %s (%s): the result is %d\n", function->name, variant, (int) result);
    }
    if (result == MONERO_ERROR) {
      g_checks++;
      if (monero_last_error() == NULL || monero_last_error()[0] == '\0') {
        g_failures++;
        fprintf(stderr, "FAIL %s (%s): no message for the error\n", function->name, variant);
      }
    }
    if (expected >= 0) {
      char message[128];
      snprintf(message, sizeof(message), "%s must not be null", function->names[expected]);
      g_checks++;
      if (result != MONERO_ERROR) {
        g_failures++;
        fprintf(stderr, "FAIL %s (%s): a NULL %s was accepted\n", function->name, variant, function->names[expected]);
      } else if (strcmp(monero_last_error(), message) != 0) {
        g_failures++;
        fprintf(stderr, "FAIL %s (%s): expected \"%s\", got \"%s\"\n", function->name, variant, message, monero_last_error());
      }
    }
  }
  settle(function, &state, variant, result);
  remove_paths(function, &state);
}

static void run_function(const arg_function* function, const fixture* fx) {
  char variant[96];
  unsigned pointers = 0;
  int first_array = -1;
  int first_count = -1;

  for (int i = 0; i < function->count; i++) {
    if (is_pointer(function->kinds[i])) pointers |= 1u << i;
    if (is_array(function->kinds[i]) && first_array < 0) first_array = i;
    if (function->kinds[i] == K_SIZE && is_count(function->names[i]) && first_count < 0) first_count = i;
  }

  // a function without parameters is only called
  if (function->count == 0) {
    if (g_verbose) fprintf(stderr, "%s\n", function->name);
    function->call(NULL);
    return;
  }

  // one NULL at a time
  for (int i = 0; i < function->count; i++) {
    if ((pointers & (1u << i)) == 0) continue;
    snprintf(variant, sizeof(variant), "%s is NULL", function->names[i]);
    run_call(function, fx, 1u << i, (function->required & (1u << i)) != 0 ? i : -1, variant);
  }

  // every pointer at once
  if (pointers != 0) run_call(function, fx, pointers, -1, "every pointer is NULL");

  // an array that is NULL with a count of zero is an empty array, and it is not an error
  if (first_array >= 0 && first_count >= 0) {
    call_state state;
    unsigned null_mask = 0;
    prepare(function, fx, &state, VALUES_VALID);
    for (int i = 0; i < function->count; i++) {
      if (is_array(function->kinds[i])) null_mask |= 1u << i;
      if (function->kinds[i] == K_SIZE && is_count(function->names[i])) state.scalars[i].size = 0;
    }
    for (int i = 0; i < function->count; i++) {
      if ((null_mask & (1u << i)) != 0) state.args[i] = NULL;
    }
    if (g_verbose) fprintf(stderr, "%s [%s] empty arrays\n", function->name, fx->label);
    monero_result result = timed_call(function, state.args);
    settle(function, &state, "empty arrays", result);
    remove_paths(function, &state);
  }

  // an array of strings with a NULL in it, which is an error that names the array
  for (int i = 0; i < function->count; i++) {
    if (function->kinds[i] != K_STRS) continue;
    call_state state;
    prepare(function, fx, &state, VALUES_VALID);
    state.strs[i][1] = NULL;
    if (g_verbose) fprintf(stderr, "%s [%s] %s holds a NULL\n", function->name, fx->label, function->names[i]);
    monero_result result = timed_call(function, state.args);
    if ((function->required & (1u << i)) != 0) {
      char message[128];
      snprintf(message, sizeof(message), "%s must not contain NULL", function->names[i]);
      g_checks++;
      if (result != MONERO_ERROR || strcmp(monero_last_error(), message) != 0) {
        g_failures++;
        fprintf(stderr, "FAIL %s: a NULL in %s gave \"%s\"\n", function->name, function->names[i], result == MONERO_ERROR ? monero_last_error() : "no error");
      }
    }
    settle(function, &state, "NULL in an array", result);
    remove_paths(function, &state);
  }
}

// what a call with valid arguments can't do to a fixture: block until a block arrives, free it, or change what the other tests of the process log
static int skipped_with_valid_arguments(const arg_function* function) {
  static const char* const skipped[] = {"monero_daemon_wait_for_next_block_header", "monero_wallet_wait_for_next_block", "monero_utils_configure_logging",
                                        "monero_utils_set_log_level", "monero_utils_set_log_categories"};
  size_t length = text_length(function->name, 64);
  for (size_t i = 0; i < sizeof(skipped) / sizeof(skipped[0]); i++) {
    if (strcmp(function->name, skipped[i]) == 0) return 1;
  }
  if (length > 5 && strcmp(function->name + length - 5, "_free") == 0) return 1;
  return 0;
}

// calls the function once with the arguments of the mode, and checks what a call has to: it returns, and it returns nothing when it fails
static void run_once(const arg_function* function, const fixture* fx, value_mode mode) {
  static const char* const names[] = {"valid arguments", "the largest numbers", "long texts"};
  call_state state;
  prepare(function, fx, &state, mode);
  if (g_verbose) fprintf(stderr, "%s [%s] %s\n", function->name, fx->label, names[mode]);
  monero_result result = timed_call(function, state.args);
  if (function->ret == RET_RESULT) {
    g_checks++;
    if (result != MONERO_OK && result != MONERO_ERROR) {
      g_failures++;
      fprintf(stderr, "FAIL %s (%s): the result is %d\n", function->name, names[mode], (int) result);
    }
    if (result == MONERO_ERROR && (monero_last_error() == NULL || monero_last_error()[0] == '\0')) {
      g_failures++;
      fprintf(stderr, "FAIL %s (%s): no message for the error\n", function->name, names[mode]);
    }
  }
  settle(function, &state, names[mode], result);
  remove_paths(function, &state);
}

static const char* first_handle(const arg_function* function) {
  for (int i = 0; i < function->count; i++) {
    if (function->kinds[i] == K_WALLET) return "wallet";
    if (function->kinds[i] == K_DAEMON) return "daemon";
    if (function->kinds[i] == K_CONNECTION) return "connection";
  }
  return "none";
}

static monero_wallet* create_wallet(const char* path) {
  monero_wallet* wallet = NULL;
  remove_files(path);
  EXPECT_OK(monero_wallet_create_from_seed(path, "password", MAINNET, SEED, NULL, 0, NULL, &wallet));
  CHECK(wallet != NULL);
  return wallet;
}

int main(void) {
  // a long text or a name that is no address is looked up as an OpenAlias name, and the public servers make each lookup wait for its timeout.
  // Pointing the resolver at a port that refuses makes them fail
#if defined(_WIN32)
  _putenv_s("DNS_PUBLIC", DNS_SERVER);
#else
  setenv("DNS_PUBLIC", DNS_SERVER, 1);
#endif
  const char* full_path = "monero_c_arguments_full";
  const char* closed_path = "monero_c_arguments_closed";
  monero_wallet* full = create_wallet(full_path);
  monero_wallet* closed = create_wallet(closed_path);
  monero_wallet* keys = NULL;
  monero_wallet* rpc = NULL;
  monero_daemon* daemon = NULL;
  monero_rpc_connection* connection = NULL;
  monero_wallet_listener* wallet_listener = NULL;
  monero_daemon_listener* daemon_listener = NULL;
  monero_wallet_listener_callbacks wallet_callbacks;
  monero_daemon_listener_callbacks daemon_callbacks;
  fixture fixtures[4];
  const char* labels[4] = {"full wallet", "keys wallet", "closed wallet", "RPC wallet"};
  g_verbose = getenv("MONERO_C_TEST_VERBOSE") != NULL;
  g_long_text = repeated_text('a', LONG_TEXT_LENGTH);
  if (g_long_text == NULL) return EXIT_FAILURE;

  memset(&wallet_callbacks, 0, sizeof(wallet_callbacks));
  memset(&daemon_callbacks, 0, sizeof(daemon_callbacks));
  wallet_callbacks.struct_size = sizeof(wallet_callbacks);
  daemon_callbacks.struct_size = sizeof(daemon_callbacks);
  EXPECT_OK(monero_wallet_keys_create_from_seed(MAINNET, SEED, NULL, NULL, &keys));
  EXPECT_OK(monero_rpc_connection_create(CONNECTION_JSON, &connection));
  EXPECT_OK(monero_wallet_rpc_connect(connection, &rpc));
  EXPECT_OK(monero_daemon_connect_with(connection, &daemon));
  EXPECT_OK(monero_wallet_listener_create(&wallet_callbacks, &wallet_listener));
  EXPECT_OK(monero_daemon_listener_create(&daemon_callbacks, &daemon_listener));
  if (closed != NULL) EXPECT_OK(monero_wallet_close(closed, false));
  if (full == NULL || closed == NULL || keys == NULL || rpc == NULL || daemon == NULL || connection == NULL || wallet_listener == NULL || daemon_listener == NULL) {
    fprintf(stderr, "the fixtures were not created\n");
    return EXIT_FAILURE;
  }

  fixtures[0].wallet = full;
  fixtures[1].wallet = keys;
  fixtures[2].wallet = closed;
  fixtures[3].wallet = rpc;
  for (int i = 0; i < 4; i++) {
    fixtures[i].label = labels[i];
    fixtures[i].daemon = daemon;
    fixtures[i].connection = connection;
    fixtures[i].wallet_listener = wallet_listener;
    fixtures[i].daemon_listener = daemon_listener;
  }

  size_t count = sizeof(g_functions) / sizeof(g_functions[0]);
  for (size_t i = 0; i < count; i++) {
    const arg_function* function = &g_functions[i];
    const char* handle = first_handle(function);
    if (strcmp(handle, "wallet") == 0) {
      for (int k = 0; k < 4; k++) run_function(function, &fixtures[k]);
    } else {
      fixture neutral = fixtures[0];
      neutral.label = handle;
      run_function(function, &neutral);
    }
  }
  printf("%lu functions, with a NULL in each pointer\n", (unsigned long) count);

  // then every function once, with plausible arguments, with the largest numbers and with long texts, on the same fixtures, which the calls
  // above left as they were. A wallet that closes or shuts down goes last in its pass, and the calls after it find a closed wallet
  for (int mode = VALUES_VALID; mode <= VALUES_LONG; mode++) {
    for (int k = 0; k < 4; k++) {
      for (int last = 0; last < 2; last++) {
        for (size_t i = 0; i < count; i++) {
          const arg_function* function = &g_functions[i];
          int deferred = strcmp(function->name, "monero_wallet_close") == 0 || strcmp(function->name, "monero_wallet_request_shutdown") == 0;
          if (strcmp(first_handle(function), "wallet") != 0 || skipped_with_valid_arguments(function) || deferred != last) continue;
          run_once(function, &fixtures[k], (value_mode) mode);
        }
      }
    }
    for (size_t i = 0; i < count; i++) {
      const arg_function* function = &g_functions[i];
      fixture neutral = fixtures[0];
      neutral.label = first_handle(function);
      if (strcmp(neutral.label, "wallet") != 0 && !skipped_with_valid_arguments(function)) run_once(function, &neutral, (value_mode) mode);
    }
  }
  printf("%lu functions, with valid arguments, with the largest numbers and with long texts\n", (unsigned long) count);

  monero_wallet_listener_free(wallet_listener);
  monero_daemon_listener_free(daemon_listener);
  monero_wallet_free(rpc);
  monero_wallet_free(keys);
  monero_wallet_free(closed);
  monero_wallet_free(full);
  monero_daemon_free(daemon);
  monero_rpc_connection_free(connection);
  remove_files(full_path);
  remove_files(closed_path);
  for (int i = 0; i < g_path_count; i++) remove_files(g_paths[i]);
  free(g_long_text);

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

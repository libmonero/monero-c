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

#include <math.h>
#include <stdlib.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

// finds "key":"value" in a compact JSON string and copies value into out.
// only good enough for this test's known output shape, not a general JSON parser
static int extract_json_string(const char* json, const char* key, char* out, size_t out_cap, size_t* out_len) {
  char needle[128];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char* p = strstr(json, needle);
  if (!p) return 0;
  p = strchr(p + strlen(needle), ':');
  if (!p) return 0;
  p = strchr(p, '"');
  if (!p) return 0;
  p++;
  const char* end = strchr(p, '"');
  if (!end) return 0;
  size_t len = (size_t) (end - p);
  if (len >= out_cap) return 0;
  memcpy(out, p, len);
  out[len] = '\0';
  if (out_len) *out_len = len;
  return 1;
}

// ------------------------------- TEST DATA ----------------------------------
// reused from monero-python's tests/config/test_monero_utils.ini (public, funds-free)

static const char* MAINNET_PRIMARY_1 = "42U9v3qs5CjZEePHBZHwuSckQXebuZu299NSmVEmQ41YJZQhKcPyujyMSzpDH4VMMVSBo3U3b54JaNvQLwAjqDhKS3rvM3L";
static const char* MAINNET_PRIMARY_2 = "48ZxX3Y2y5s4nJ8fdz2w65TrTEp9PRsv5J8iHSShkHQcE2V31FhnWptioNst1K9oeDY4KpWZ7v8V2BZNVa4Wdky89iqmPz2";
static const char* MAINNET_SUBADDR_1 = "891TQPrWshJVpnBR4ZMhHiHpLx1PUnMqa3ccV5TJFBbqcJa3DWhjBh2QByCv3Su7WDPTGMHmCKkiVFN2fyGJKwbM1t6G7Ea";
static const char* MAINNET_INTEGRATED_1 = "4CApvrfMgUFZEePHBZHwuSckQXebuZu299NSmVEmQ41YJZQhKcPyujyMSzpDH4VMMVSBo3U3b54JaNvQLwAjqDhKeGLQ9vfRBRKFKnBtVH";
static const char* MAINNET_INVALID_1 = "42ZxX3Y2y5s4nJ8fdz2w65TrTEp9PRsv5J8iHSShkHQcE2V31FhnWptioNst1K9oeDY4KpWZ7v8V2BZNVa4Wdky89iqmPz2";

static const char* TESTNET_PRIMARY_1 = "9tUBnNCkC3UKGygHCwYvAB1FscpjUuq5e9MYJd2rXuiiTjjfVeSVjnbSG5VTnJgBgy9Y7GTLfxpZNMUwNZjGfdFr1z79eV1";
static const char* TESTNET_INVALID_1 = "91UBnNCkC3UKGygHCwYvAB1FscpjUuq5e9MYJd2rXuiiTjjfVeSVjnbSG5VTnJgBgy9Y7GTLfxpZNMUwNZjGfdFr1z79eV1";

static const char* STAGENET_PRIMARY_4 = "58qRVVjZ4KxMX57TH6yWqGcH5AswvZZS494hWHcHPt6cDkP7V8AqxFhi3RKXZueVRgUnk8niQGHSpY5Bm9DjuWn16GDKXpF";
static const char* STAGENET_SUBADDR_4 = "7B9w2xieXjhDumgPX39h1CAYELpsZ7Pe8Wqtr3pVL9jJ5gGDqgxjWt55gTYUCAuhahhM85ajEp6VbQfLDPETt4oT2ZRXa6n";
static const char* STAGENET_INVALID_1 = "518s3obCY2ETeQB3GNAGPK2zRGen5UeW1WzegSizVsmf6z5NvM2GLoN6zzk1vHyzGAAfA8pGhuYAeCFZjHAp59jRVQkunGS";

static const char* PRIVATE_VIEW_KEY = "86cf351d10894769feba29b9e201e12fb100b85bb52fc5825c864eef55c5840d";
static const char* PUBLIC_VIEW_KEY = "99873d76ca874ff1aad676b835dd303abcb21c9911ca8a3d9130abc4544d8a0a";
static const char* PRIVATE_SPEND_KEY = "e9ba887e93620ef9fafdfe0c6d3022949f1c5713cbd9ef631f18a0fb00421dee";
static const char* PUBLIC_SPEND_KEY = "3e48df9e9d8038dbf6f5382fac2becd8686273cda5bd87187e45dca7ec5af37b";
static const char* INVALID_PRIVATE_VIEW_KEY = "5B8s3obCY2ETeQB3GNAGPK2zRGen5UeW1WzegSizVsmf6z5NvM2GLoN6zzk1vHyzGAAfA8pGhuYAeCFZjHAp59jRVQkunGS";

static const char* SEED = "vortex degrees outbreak teeming gimmick school rounded tonic observant injury leech ought problems ahead upcoming ledge textbook cigar atrium trash dunes eavesdrop dullness evolved vortex";

// ------------------------------- VALIDATION ---------------------------------

static void test_address_validation(void) {
  CHECK(monero_utils_is_valid_address(MAINNET_PRIMARY_1, MONERO_UTILS_NETWORK_MAINNET));
  CHECK(monero_utils_is_valid_address(MAINNET_SUBADDR_1, MONERO_UTILS_NETWORK_MAINNET));
  CHECK(monero_utils_is_valid_address(MAINNET_INTEGRATED_1, MONERO_UTILS_NETWORK_MAINNET));
  EXPECT_OK(monero_utils_validate_address(MAINNET_PRIMARY_1, MONERO_UTILS_NETWORK_MAINNET));

  CHECK(monero_utils_is_valid_address(TESTNET_PRIMARY_1, MONERO_UTILS_NETWORK_TESTNET));
  CHECK(!monero_utils_is_valid_address(TESTNET_PRIMARY_1, MONERO_UTILS_NETWORK_MAINNET)); // wrong network

  CHECK(!monero_utils_is_valid_address(NULL, MONERO_UTILS_NETWORK_MAINNET));
  CHECK(!monero_utils_is_valid_address("", MONERO_UTILS_NETWORK_MAINNET));
  CHECK(!monero_utils_is_valid_address(MAINNET_INVALID_1, MONERO_UTILS_NETWORK_MAINNET));
  CHECK(!monero_utils_is_valid_address(TESTNET_INVALID_1, MONERO_UTILS_NETWORK_TESTNET));
  CHECK(!monero_utils_is_valid_address(STAGENET_INVALID_1, MONERO_UTILS_NETWORK_STAGENET));
  EXPECT_ERR(monero_utils_validate_address(MAINNET_INVALID_1, MONERO_UTILS_NETWORK_MAINNET));
  EXPECT_ERR(monero_utils_validate_address(MAINNET_PRIMARY_1, (int32_t) 99)); // invalid network_type
}

static void test_key_validation(void) {
  const char* invalid_hex_64 = "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"; // right length, not hex

  CHECK(monero_utils_is_valid_private_view_key(PRIVATE_VIEW_KEY));
  EXPECT_OK(monero_utils_validate_private_view_key(PRIVATE_VIEW_KEY));
  CHECK(!monero_utils_is_valid_private_view_key(""));
  CHECK(!monero_utils_is_valid_private_view_key(NULL));
  CHECK(!monero_utils_is_valid_private_view_key(INVALID_PRIVATE_VIEW_KEY));
  CHECK(!monero_utils_is_valid_private_view_key(invalid_hex_64));

  CHECK(monero_utils_is_valid_public_view_key(PUBLIC_VIEW_KEY));
  EXPECT_OK(monero_utils_validate_public_view_key(PUBLIC_VIEW_KEY));
  CHECK(!monero_utils_is_valid_public_view_key(""));

  CHECK(monero_utils_is_valid_private_spend_key(PRIVATE_SPEND_KEY));
  EXPECT_OK(monero_utils_validate_private_spend_key(PRIVATE_SPEND_KEY));
  CHECK(!monero_utils_is_valid_private_spend_key(""));
  EXPECT_ERR_MSG(monero_utils_validate_private_spend_key(""), "private spend key expected to be 64 hex characters");

  CHECK(monero_utils_is_valid_public_spend_key(PUBLIC_SPEND_KEY));
  EXPECT_OK(monero_utils_validate_public_spend_key(PUBLIC_SPEND_KEY));
  CHECK(!monero_utils_is_valid_public_spend_key(""));
}

static void test_mnemonic_validation(void) {
  EXPECT_OK(monero_utils_validate_mnemonic(SEED, ""));
  CHECK(monero_utils_is_valid_mnemonic(SEED, ""));
  CHECK(monero_utils_is_valid_mnemonic(SEED, "English"));
  CHECK(!monero_utils_is_valid_mnemonic(SEED, "Spanish")); // wrong language for this seed
  CHECK(!monero_utils_is_valid_mnemonic("invalid monero wallet seed", ""));
  CHECK(!monero_utils_is_valid_mnemonic("", ""));
  CHECK(!monero_utils_is_valid_mnemonic(NULL, ""));
}

// a mnemonic is checked up to 4096 bytes, since a longer one is never valid
static void test_mnemonic_length_limit(void) {
  char* longest = repeated_text('a', 4096);
  char* too_long = repeated_text('a', 4097);
  char* huge = repeated_text('a', 1000000);

  EXPECT_ERR_MSG(monero_utils_validate_mnemonic(longest, ""), "Mnemonic phrased words must be 25");
  EXPECT_ERR_MSG(monero_utils_validate_mnemonic(too_long, ""), "mnemonic is longer than 4096 bytes");
  EXPECT_ERR_MSG(monero_utils_validate_mnemonic(huge, ""), "mnemonic is longer than 4096 bytes");
  CHECK(!monero_utils_is_valid_mnemonic(longest, ""));
  CHECK(!monero_utils_is_valid_mnemonic(too_long, ""));
  CHECK(!monero_utils_is_valid_mnemonic(huge, ""));

  free(longest);
  free(too_long);
  free(huge);
}

static void test_seed_language_validation(void) {
  CHECK(monero_utils_is_valid_language("Italian"));
  CHECK(monero_utils_is_valid_language("English"));
  CHECK(monero_utils_is_valid_language("German"));
  CHECK(!monero_utils_is_valid_language(""));
  CHECK(!monero_utils_is_valid_language("english")); // case-sensitive
  CHECK(!monero_utils_is_valid_language("italian"));
}

static void test_payment_id_validation(void) {
  static const char* valid_ids[] = {
    "43e04076e176b768", "ef35647e9842991c", "8434d5452ad1b0ab",
    "3b5ac230d2666177", "87fdf837b5e6a390", "304e0fa65b9c9e14"
  };
  for (size_t i = 0; i < sizeof(valid_ids) / sizeof(valid_ids[0]); i++) {
    CHECK(monero_utils_is_valid_payment_id(valid_ids[i]));
    EXPECT_OK(monero_utils_validate_payment_id(valid_ids[i]));
  }

  // is_valid_payment_id accepts either a 16 or 64 hex char payment id
  CHECK(monero_utils_is_valid_payment_id("87fdf837b5e6a390ef35647e9842991c8434d5452ad1b0ab304e0fa65b9c9e14"));

  static const char* invalid_ids[] = {
    "", "wijqwnn38y", "87fdf837b5e6a39", "3b5ac230d26661778",
    "304e0fa65b9c9e14304e0fa65b9c9e14",
    "zzzzzzzzzzzzzzzz", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"
  };
  for (size_t i = 0; i < sizeof(invalid_ids) / sizeof(invalid_ids[0]); i++) {
    CHECK(!monero_utils_is_valid_payment_id(invalid_ids[i]));
    EXPECT_ERR_MSG(monero_utils_validate_payment_id(invalid_ids[i]), "payment id expected to be 64 or 16 hex characters");
  }
}

static void test_payment_id_long_short_validation(void) {
  const char* long_id = "87fdf837b5e6a390ef35647e9842991c8434d5452ad1b0ab304e0fa65b9c9e14";
  const char* short_id = "87fdf837b5e6a390";

  CHECK(monero_utils_is_valid_payment_id_long(long_id));
  CHECK(!monero_utils_is_valid_payment_id_long(short_id));
  CHECK(!monero_utils_is_valid_payment_id_long(""));
  CHECK(!monero_utils_is_valid_payment_id_long("wijqwnn38y"));
  EXPECT_OK(monero_utils_validate_payment_id_long(long_id));
  EXPECT_ERR_MSG(monero_utils_validate_payment_id_long(short_id), "Invalid long payment id");
  EXPECT_ERR_MSG(monero_utils_validate_payment_id_long("wijqwnn38y"), "Invalid long payment id");

  CHECK(monero_utils_is_valid_payment_id_short(short_id));
  CHECK(!monero_utils_is_valid_payment_id_short(long_id));
  CHECK(!monero_utils_is_valid_payment_id_short(""));
  EXPECT_OK(monero_utils_validate_payment_id_short(short_id));
  EXPECT_ERR_MSG(monero_utils_validate_payment_id_short(long_id), "Invalid short payment id");

  // parse_payment_id_long/short: same validity, plus the decoded bytes come back
  uint8_t buf32[32];
  uint8_t buf8[8];
  CHECK(monero_utils_parse_payment_id_long(long_id, buf32));
  CHECK(!monero_utils_parse_payment_id_long(short_id, buf32));
  CHECK(monero_utils_parse_payment_id_short(short_id, buf8));
  CHECK(!monero_utils_parse_payment_id_short(long_id, buf8));
}

// ------------------------------ AMOUNTS / IDS -------------------------------

static void test_atomic_unit_conversion(void) {
  uint64_t out = 0;

  EXPECT_OK(monero_utils_xmr_to_atomic_units(1.0, &out));
  CHECK(out == 1000000000000ULL);
  CHECK(monero_utils_atomic_units_to_xmr(1000000000000ULL) == 1.0);

  EXPECT_OK(monero_utils_xmr_to_atomic_units(0.001, &out));
  CHECK(out == 1000000000ULL);

  EXPECT_OK(monero_utils_xmr_to_atomic_units(0.25, &out));
  CHECK(out == 250000000000ULL);
  CHECK(monero_utils_atomic_units_to_xmr(250000000000ULL) == 0.25);

  EXPECT_OK(monero_utils_xmr_to_atomic_units(2.79672619, &out));
  CHECK(out == 2796726190000ULL);
  CHECK(monero_utils_atomic_units_to_xmr(2796726190000ULL) == 2.79672619);
}

static void test_xmr_to_atomic_units_zero(void) {
  uint64_t out = 1;
  EXPECT_OK(monero_utils_xmr_to_atomic_units(0.0, &out));
  CHECK(out == 0);
  out = 1;
  EXPECT_OK(monero_utils_xmr_to_atomic_units(-0.0, &out)); // -0.0 < 0 is false in IEEE 754
  CHECK(out == 0);
}

static void test_xmr_to_atomic_units_rounds_down_to_zero(void) {
  uint64_t out = 1;
  EXPECT_OK(monero_utils_xmr_to_atomic_units(1e-13, &out));
  CHECK(out == 0);
}

static void test_xmr_to_atomic_units_invalid_amount(void) {
  uint64_t out;
  EXPECT_ERR_MSG(monero_utils_xmr_to_atomic_units(-1.0, &out), "amount must be a finite, non-negative number");
  EXPECT_ERR_MSG(monero_utils_xmr_to_atomic_units(NAN, &out), "amount must be a finite, non-negative number");
  EXPECT_ERR_MSG(monero_utils_xmr_to_atomic_units(INFINITY, &out), "amount must be a finite, non-negative number");
  EXPECT_ERR_MSG(monero_utils_xmr_to_atomic_units(-INFINITY, &out), "amount must be a finite, non-negative number");
}

static void test_xmr_to_atomic_units_overflow(void) {
  uint64_t out;
  EXPECT_ERR(monero_utils_xmr_to_atomic_units(18446745.0, &out));
  EXPECT_ERR(monero_utils_xmr_to_atomic_units(2e22, &out));
}

static void test_get_ring_size(void) {
  CHECK(monero_utils_get_ring_size() == 16);
}

static void test_get_abi_version(void) {
  uint32_t major = 99;
  uint32_t minor = 99;
  uint32_t patch = 99;
  EXPECT_OK(monero_utils_get_abi_version(&major, &minor, &patch));
  CHECK(major == MONERO_C_ABI_VERSION_MAJOR);
  CHECK(minor == MONERO_C_ABI_VERSION_MINOR);
  CHECK(patch == MONERO_C_ABI_VERSION_PATCH);
  EXPECT_ERR_MSG(monero_utils_get_abi_version(NULL, &minor, &patch), "out_major must not be null");
  EXPECT_ERR_MSG(monero_utils_get_abi_version(&major, NULL, &patch), "out_minor must not be null");
  EXPECT_ERR_MSG(monero_utils_get_abi_version(&major, &minor, NULL), "out_patch must not be null");
}

// --------------------------- INTEGRATED ADDRESS -----------------------------

static void test_get_integrated_address(void) {
  char* json = NULL;
  char value[256];
  size_t value_len;

  // random payment id
  EXPECT_OK(monero_utils_get_integrated_address(MONERO_UTILS_NETWORK_STAGENET, STAGENET_PRIMARY_4, "", &json));
  CHECK(json != NULL);
  if (json != NULL) {
    CHECK(extract_json_string(json, "standardAddress", value, sizeof(value), &value_len) && strcmp(value, STAGENET_PRIMARY_4) == 0);
    CHECK(extract_json_string(json, "paymentId", value, sizeof(value), &value_len) && value_len == 16);
    CHECK(extract_json_string(json, "integratedAddress", value, sizeof(value), &value_len) && value_len == 106);
    monero_utils_free(json);
    json = NULL;
  }

  // specific payment id
  EXPECT_OK(monero_utils_get_integrated_address(MONERO_UTILS_NETWORK_STAGENET, STAGENET_PRIMARY_4, "03284e41c342f036", &json));
  if (json != NULL) {
    CHECK(extract_json_string(json, "paymentId", value, sizeof(value), &value_len) && strcmp(value, "03284e41c342f036") == 0);
    monero_utils_free(json);
    json = NULL;
  }

  // with a subaddress
  EXPECT_OK(monero_utils_get_integrated_address(MONERO_UTILS_NETWORK_STAGENET, STAGENET_SUBADDR_4, "03284e41c342f036", &json));
  if (json != NULL) {
    CHECK(extract_json_string(json, "standardAddress", value, sizeof(value), &value_len) && strcmp(value, STAGENET_SUBADDR_4) == 0);
    monero_utils_free(json);
    json = NULL;
  }

  // invalid payment id
  EXPECT_ERR_MSG(monero_utils_get_integrated_address(MONERO_UTILS_NETWORK_STAGENET, STAGENET_PRIMARY_4, "123", &json), "Invalid payment id");
  CHECK(json == NULL); // out param left untouched on error
}

// ------------------------------ PAYMENT URIS --------------------------------

static void test_get_payment_uri(void) {
  char* uri = NULL;
  char tx_config_json[512];
  snprintf(tx_config_json, sizeof(tx_config_json),
      "{\"destinations\":[{\"address\":\"%s\",\"amount\":250000000000}],\"recipientName\":\"John Doe\",\"note\":\"My transfer to wallet\"}",
      MAINNET_PRIMARY_1);

  EXPECT_OK(monero_utils_get_payment_uri(tx_config_json, MONERO_UTILS_NETWORK_MAINNET, &uri));
  if (uri != NULL) {
    char expected[512];
    snprintf(expected, sizeof(expected), "monero:%s?tx_amount=0.250000000000&recipient_name=John%%20Doe&tx_description=My%%20transfer%%20to%%20wallet", MAINNET_PRIMARY_1);
    CHECK(strcmp(uri, expected) == 0);

    // round trip through parse_payment_uri()
    char* parsed_json = NULL;
    EXPECT_OK(monero_utils_parse_payment_uri(uri, MONERO_UTILS_NETWORK_MAINNET, &parsed_json));
    if (parsed_json != NULL) {
      CHECK(strstr(parsed_json, MAINNET_PRIMARY_1) != NULL);
      CHECK(strstr(parsed_json, "250000000000") != NULL);
      monero_utils_free(parsed_json);
    }

    monero_utils_free(uri);
  }
}

// a JSON argument that nests too deeply fails before the parser, which would overflow the stack
static void test_json_depth_limit(void) {
  const char* message = "JSON is nested deeper than 64 levels";
  char* object = nested_json_object(100000);
  char* array = nested_json(65);
  char* at_limit = nested_json_object(64);
  char* uri = POISON_PTR;
  uint8_t* bin = POISON_PTR;
  size_t bin_len = 7;

  EXPECT_ERR_MSG(monero_utils_get_payment_uri(object, MONERO_UTILS_NETWORK_MAINNET, &uri), message);
  CHECK(uri == NULL);
  EXPECT_ERR_MSG(monero_utils_json_to_binary(array, &bin, &bin_len), message);
  CHECK(bin == NULL && bin_len == 0);
  EXPECT_ERR_MSG(monero_utils_json_to_binary(object, &bin, &bin_len), message);

  // 64 levels pass the check, and are then rejected as a transaction config
  EXPECT_ERR(monero_utils_get_payment_uri(at_limit, MONERO_UTILS_NETWORK_MAINNET, &uri));
  CHECK(strcmp(monero_last_error(), message) != 0);

  free(object);
  free(array);
  free(at_limit);
}

static void test_parse_payment_uri_wrong_scheme(void) {
  char* parsed_json = NULL;
  char uri[256];
  snprintf(uri, sizeof(uri), "bitcoin:%s", MAINNET_PRIMARY_1);
  EXPECT_ERR(monero_utils_parse_payment_uri(uri, MONERO_UTILS_NETWORK_MAINNET, &parsed_json));
  CHECK(parsed_json == NULL);
}

// ------------------------------ JSON / BINARY -------------------------------

static void test_json_binary_roundtrip(void) {
  uint8_t* bin = NULL;
  size_t bin_len = 0;
  char* json2 = NULL;

  EXPECT_OK(monero_utils_json_to_binary("{\"heights\":[111,222,333]}", &bin, &bin_len));
  CHECK(bin != NULL && bin_len > 0);
  if (bin != NULL) {
    EXPECT_OK(monero_utils_binary_to_json(bin, bin_len, &json2));
    if (json2 != NULL) {
      CHECK(strstr(json2, "heights") != NULL);
      CHECK(strstr(json2, "111") != NULL);
      CHECK(strstr(json2, "222") != NULL);
      CHECK(strstr(json2, "333") != NULL);
      monero_utils_free(json2);
      json2 = NULL;
    }
    monero_utils_free(bin);
    bin = NULL;
  }

  // uint64 values above INT64_MAX must not come back out as negative numbers
  EXPECT_OK(monero_utils_json_to_binary("{\"heights\":[18446744073709551615]}", &bin, &bin_len));
  if (bin != NULL) {
    EXPECT_OK(monero_utils_binary_to_json(bin, bin_len, &json2));
    if (json2 != NULL) {
      CHECK(strstr(json2, "18446744073709551615") != NULL);
      monero_utils_free(json2);
    }
    monero_utils_free(bin);
  }
}

// both converters read a get_blocks.bin response
static void test_binary_blocks_to_json(void) {
  uint8_t* bin = NULL;
  size_t bin_len = 0;
  char* json = NULL;
  const uint8_t garbage[] = {1, 2, 3};

  EXPECT_OK(monero_utils_json_to_binary("{\"blocks\":[],\"status\":\"OK\"}", &bin, &bin_len));
  if (bin != NULL) {
    EXPECT_OK(monero_utils_binary_blocks_to_json(bin, bin_len, &json));
    CHECK(json != NULL && strstr(json, "\"status\":\"OK\"") != NULL);
    monero_utils_free(json);
    json = NULL;
    EXPECT_OK(monero_utils_binary_blocks_fast_to_json(bin, bin_len, &json));
    CHECK(json != NULL && strstr(json, "\"current_height\"") != NULL);
    monero_utils_free(json);
    json = NULL;
    monero_utils_free(bin);
  }
  EXPECT_ERR_MSG(monero_utils_binary_blocks_fast_to_json(garbage, sizeof(garbage), &json), "failed to parse get_blocks.bin response");
  CHECK(json == NULL);
}

// the log setters return nothing, so check the log file
// a failed call resets its pointer and count outputs, and keeps its scalar outputs
static void test_failed_calls_reset_outputs(void) {
  char* text = POISON_PTR;
  uint8_t* data = POISON_PTR;
  size_t len = 7;
  uint64_t amount = 42;
  uint32_t minor = 7;
  uint32_t patch = 7;
  const uint8_t junk[] = {1, 2, 3};

  EXPECT_ERR_MSG(monero_utils_get_integrated_address(99, MAINNET_PRIMARY_1, "", &text), "unknown network type");
  CHECK(text == NULL);
  text = POISON_PTR;
  EXPECT_ERR_MSG(monero_utils_get_payment_uri("{}", 99, &text), "unknown network type");
  CHECK(text == NULL);
  text = POISON_PTR;
  EXPECT_ERR_MSG(monero_utils_parse_payment_uri("bitcoin:x", 99, &text), "unknown network type");
  CHECK(text == NULL);
  EXPECT_ERR_MSG(monero_utils_validate_address(MAINNET_PRIMARY_1, -1), "unknown network type");
  text = POISON_PTR;
  EXPECT_ERR(monero_utils_binary_blocks_to_json(junk, sizeof(junk), &text));
  CHECK(text == NULL);
  text = POISON_PTR;
  EXPECT_ERR(monero_utils_binary_blocks_fast_to_json(junk, sizeof(junk), &text));
  CHECK(text == NULL);

  // with one of the two outputs NULL, the other is still reset
  EXPECT_ERR(monero_utils_json_to_binary("{}", &data, NULL));
  CHECK(data == NULL);
  EXPECT_ERR(monero_utils_json_to_binary("{}", NULL, &len));
  CHECK(len == 0);

  EXPECT_ERR(monero_utils_xmr_to_atomic_units(-1.0, &amount));
  CHECK(amount == 42);
  EXPECT_ERR(monero_utils_get_abi_version(NULL, &minor, &patch));
  CHECK(minor == 7 && patch == 7);
}

#if !defined(_WIN32)

// the logging of monero-project is global, and two threads that configure it together used to crash
static void* configure_logging_worker(void* arg) {
  const char* path = (const char*) arg;
  for (int i = 0; i < 100; i++) {
    monero_utils_configure_logging(path, i % 3 == 0);
    monero_utils_set_log_level(i % 5);
    monero_utils_set_log_categories(i % 2 ? "*:INFO" : "*:WARNING");
  }
  return NULL;
}

static void test_logging_from_several_threads(void) {
  const char* paths[4] = {"monero_c_utils_unit_tests_a.log", "monero_c_utils_unit_tests_b.log", "monero_c_utils_unit_tests_a.log", "monero_c_utils_unit_tests_b.log"};
  pthread_t threads[4];
  int started = 0;
  for (int i = 0; i < 4; i++) {
    if (pthread_create(&threads[i], NULL, configure_logging_worker, (void*) paths[i]) == 0) started++;
  }
  CHECK(started == 4);
  for (int i = 0; i < started; i++) pthread_join(threads[i], NULL);
  monero_utils_configure_logging(NULL, false);
  monero_utils_set_log_level(0);
  remove(paths[0]);
  remove(paths[1]);
}

#endif

static void test_logging(void) {
  const char* path = "monero_c_utils_unit_tests.log";
  FILE* file = NULL;
  remove(path);
  monero_utils_set_log_level(1);
  monero_utils_set_log_categories("");
  monero_utils_set_log_categories(NULL);
  monero_utils_configure_logging(path, false);
  monero_utils_set_log_level(0);
  file = fopen(path, "r");
  CHECK(file != NULL);
  if (file != NULL) fclose(file);
  monero_utils_configure_logging(NULL, false);
  remove(path);
}

// ---------------------------------- MAIN ------------------------------------

int main(void) {
  test_address_validation();
  test_key_validation();
  test_mnemonic_validation();
  test_mnemonic_length_limit();
  test_seed_language_validation();
  test_payment_id_validation();
  test_payment_id_long_short_validation();
  test_atomic_unit_conversion();
  test_xmr_to_atomic_units_zero();
  test_xmr_to_atomic_units_rounds_down_to_zero();
  test_xmr_to_atomic_units_invalid_amount();
  test_xmr_to_atomic_units_overflow();
  test_get_ring_size();
  test_get_abi_version();
  test_get_integrated_address();
  test_get_payment_uri();
  test_parse_payment_uri_wrong_scheme();
  test_json_depth_limit();
  test_json_binary_roundtrip();
  test_binary_blocks_to_json();
  test_failed_calls_reset_outputs();
  test_logging();
#if !defined(_WIN32)
  test_logging_from_several_threads();
#endif

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

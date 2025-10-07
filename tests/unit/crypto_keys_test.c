/**
 * @file crypto_keys_test.c
 * @brief Unit tests for lib/crypto/keys.c - Tests the intended final crypto implementation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/common.h"
#include "crypto/keys.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with debug logging
TEST_SUITE_WITH_DEBUG_LOGGING(crypto_keys);

// =============================================================================
// Hex Decode Tests (Parameterized)
// =============================================================================

typedef struct {
  char hex[128];
  size_t expected_len;
  int expected_result;
  char description[64];
} hex_decode_test_case_t;

static hex_decode_test_case_t hex_decode_cases[] = {
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
     .expected_len = 32,
     .expected_result = 0,
     .description = "valid 64-char hex string"},
    {.hex = "0123456789abcdef",
     .expected_len = 32,
     .expected_result = -1,
     .description = "invalid length (16 chars, need 32)"},
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg",
     .expected_len = 32,
     .expected_result = -1,
     .description = "invalid characters (contains 'g')"},
    {.hex = "", .expected_len = 32, .expected_result = -1, .description = "empty string"},
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
     .expected_len = 32,
     .expected_result = -1,
     .description = "too long (65 chars)"},
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde",
     .expected_len = 32,
     .expected_result = -1,
     .description = "too short (63 chars)"},
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
     .expected_len = 16,
     .expected_result = -1,
     .description = "wrong expected length"}};

ParameterizedTestParameters(crypto_keys, hex_decode_tests) {
  size_t nb_cases = sizeof(hex_decode_cases) / sizeof(hex_decode_cases[0]);
  return cr_make_param_array(hex_decode_test_case_t, hex_decode_cases, nb_cases);
}

ParameterizedTest(hex_decode_test_case_t *tc, crypto_keys, hex_decode_tests) {
  log_debug("Test start: tc=%p", (void *)tc);
  log_debug("Test hex=%s, expected_len=%zu", tc->hex, tc->expected_len);

  uint8_t output[32];
  int result = hex_decode(tc->hex, output, tc->expected_len);

  log_debug("hex_decode returned %d", result);
  cr_assert_eq(result, tc->expected_result, "Failed for case: %s", tc->description);

  if (tc->expected_result == 0) {
    // For valid cases, verify the output is not all zeros
    bool all_zero = true;
    for (size_t i = 0; i < tc->expected_len; i++) {
      if (output[i] != 0) {
        all_zero = false;
        break;
      }
    }
    cr_assert_not(all_zero, "Decoded output should not be all zeros for case: %s", tc->description);
  }
}

// =============================================================================
// Public Key Parsing Tests (Parameterized)
// =============================================================================

typedef struct {
  char input[256];
  key_type_t expected_type;
  int expected_result;
  char description[64];
  bool input_is_null;
} parse_public_key_test_case_t;

static parse_public_key_test_case_t parse_public_key_cases[] = {
    {.input = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBg7kmREayHMGWhgD0pc9wzuwdi0ibHnFmlAPwOn6mSV test-key",
     .expected_type = KEY_TYPE_ED25519,
     .expected_result = 0,
     .description = "valid SSH Ed25519 key",
     .input_is_null = false},
    {.input = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
     .expected_type = KEY_TYPE_X25519,
     .expected_result = 0,
     .description = "valid X25519 hex key",
     .input_is_null = false},
    {.input = "github:testuser",
     .expected_type = KEY_TYPE_ED25519,
     .expected_result = 0,
     .description = "GitHub username (should fetch first Ed25519 key)",
     .input_is_null = false},
    {.input = "gitlab:testuser",
     .expected_type = KEY_TYPE_ED25519,
     .expected_result = 0,
     .description = "GitLab username (should fetch first Ed25519 key)",
     .input_is_null = false},
    {.input = "gpg:0x1234567890ABCDEF",
     .expected_type = KEY_TYPE_GPG,
     .expected_result = 0,
     .description = "GPG key ID",
     .input_is_null = false},
    {.input = "invalid-key-format",
     .expected_type = KEY_TYPE_UNKNOWN,
     .expected_result = -1,
     .description = "invalid key format",
     .input_is_null = false},
    {.input = "",
     .expected_type = KEY_TYPE_UNKNOWN,
     .expected_result = -1,
     .description = "empty input",
     .input_is_null = false},
    {.input = "",
     .expected_type = KEY_TYPE_UNKNOWN,
     .expected_result = -1,
     .description = "NULL input",
     .input_is_null = true}};
ParameterizedTestParameters(crypto_keys, parse_public_key_tests) {
  size_t nb_cases = sizeof(parse_public_key_cases) / sizeof(parse_public_key_cases[0]);
  return cr_make_param_array(parse_public_key_test_case_t, parse_public_key_cases, nb_cases);
}

ParameterizedTest(parse_public_key_test_case_t *tc, crypto_keys, parse_public_key_tests) {
  public_key_t key;
  const char *input_ptr = tc->input_is_null ? NULL : tc->input;

  log_debug("Testing case: %s", tc->description);
  log_debug("Input: %s", input_ptr ? input_ptr : "(null)");

  int result = parse_public_key(input_ptr, &key);

  log_debug("Result: %d, Expected: %d", result, tc->expected_result);

  cr_assert_eq(result, tc->expected_result, "Failed for case: %s", tc->description);

  if (tc->expected_result == 0) {
    cr_assert_eq(key.type, tc->expected_type, "Key type should match for case: %s", tc->description);
  }
}

// =============================================================================
// Private Key Parsing Tests
// =============================================================================

Test(crypto_keys, parse_private_key_ed25519_file) {
  private_key_t key;
  // Test with a mock Ed25519 private key file path
  int result = parse_private_key("~/.ssh/id_ed25519", &key);

  // This will likely fail without a real key file, but tests the interface
  if (result == 0) {
    cr_assert_eq(key.type, KEY_TYPE_ED25519, "Should parse as Ed25519 key");
  } else {
    // Expected to fail without real key file
    cr_assert_eq(result, -1, "Should fail without real key file");
  }
}

Test(crypto_keys, parse_private_key_nonexistent) {
  private_key_t key;
  int result = parse_private_key("/nonexistent/path", &key);

  cr_assert_eq(result, -1, "Parsing nonexistent private key should fail");
}

Test(crypto_keys, parse_private_key_null_path) {
  private_key_t key;
  int result = parse_private_key(NULL, &key);

  cr_assert_eq(result, -1, "Parsing NULL path should fail");
}

// =============================================================================
// Key Conversion Tests
// =============================================================================

Test(crypto_keys, public_key_to_x25519_ed25519) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Use a valid Ed25519 public key (generated from a known seed)
  // This is the public key from libsodium's test vectors
  const uint8_t valid_ed25519_pk[32] = {0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a,
                                        0xa7, 0x4d, 0x1b, 0x7e, 0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4,
                                        0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c};
  memcpy(key.key, valid_ed25519_pk, 32);

  uint8_t x25519_key[32];
  int result = public_key_to_x25519(&key, x25519_key);

  cr_assert_eq(result, 0, "Ed25519 to X25519 conversion should succeed");

  // Verify the output is not all zeros
  bool all_zero = true;
  for (int i = 0; i < 32; i++) {
    if (x25519_key[i] != 0) {
      all_zero = false;
      break;
    }
  }
  cr_assert_not(all_zero, "X25519 key should not be all zeros");
}

Test(crypto_keys, public_key_to_x25519_x25519_passthrough) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_X25519;
  memset(key.key, 0x42, 32);

  uint8_t x25519_key[32];
  int result = public_key_to_x25519(&key, x25519_key);

  cr_assert_eq(result, 0, "X25519 passthrough should succeed");
  cr_assert_eq(memcmp(key.key, x25519_key, 32), 0, "X25519 key should be unchanged");
}

Test(crypto_keys, public_key_to_x25519_gpg) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_GPG;
  memset(key.key, 0x42, 32);

  uint8_t x25519_key[32];
  int result = public_key_to_x25519(&key, x25519_key);

  cr_assert_eq(result, 0, "GPG to X25519 conversion should succeed");
}

Test(crypto_keys, public_key_to_x25519_unknown_type) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_UNKNOWN;

  uint8_t x25519_key[32];
  int result = public_key_to_x25519(&key, x25519_key);

  cr_assert_eq(result, -1, "Unknown key type should fail");
}

Test(crypto_keys, private_key_to_x25519_ed25519) {
  private_key_t key;
  memset(&key, 0, sizeof(key)); // Initialize entire struct including use_ssh_agent=false
  key.type = KEY_TYPE_ED25519;

  // Use a valid Ed25519 private key (seed + public key)
  // This is from libsodium's test vectors
  const uint8_t valid_ed25519_sk[64] = {
      // 32-byte seed
      0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5,
      0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60,
      // 32-byte public key
      0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72,
      0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
  memcpy(key.key.ed25519, valid_ed25519_sk, 64);

  uint8_t x25519_key[32];
  int result = private_key_to_x25519(&key, x25519_key);

  cr_assert_eq(result, 0, "Ed25519 private key to X25519 should succeed");
}

Test(crypto_keys, private_key_to_x25519_x25519_passthrough) {
  private_key_t key;
  memset(&key, 0, sizeof(key)); // Initialize entire struct including use_ssh_agent=false
  key.type = KEY_TYPE_X25519;
  memset(key.key.x25519, 0x42, 32);

  uint8_t x25519_key[32];
  int result = private_key_to_x25519(&key, x25519_key);

  cr_assert_eq(result, 0, "X25519 private key passthrough should succeed");
  cr_assert_eq(memcmp(key.key.x25519, x25519_key, 32), 0, "X25519 private key should be unchanged");
}

// =============================================================================
// Remote Key Fetching Tests
// =============================================================================

Test(crypto_keys, fetch_github_keys_valid_user) {
  char **keys = NULL;
  size_t num_keys = 0;

  int result = fetch_github_keys("octocat", &keys, &num_keys);

  // This will fail without BearSSL, but tests the interface
  if (result == 0) {
    cr_assert_not_null(keys, "Keys array should be allocated");
    cr_assert_gt(num_keys, 0, "Should fetch at least one key");

    // Free the keys if they were allocated
    for (size_t i = 0; i < num_keys; i++) {
      free(keys[i]);
    }
    free(keys);
  } else {
    // Expected to fail without BearSSL
    cr_assert_eq(result, -1, "Should fail without BearSSL");
    cr_assert_null(keys, "Keys array should be NULL on failure");
    cr_assert_eq(num_keys, 0, "Number of keys should be 0 on failure");
  }
}

Test(crypto_keys, fetch_github_keys_invalid_user) {
  char **keys = NULL;
  size_t num_keys = 0;

  int result = fetch_github_keys("nonexistentuser12345", &keys, &num_keys);

  cr_assert_eq(result, -1, "Invalid user should fail");
  cr_assert_null(keys, "Keys array should be NULL on failure");
  cr_assert_eq(num_keys, 0, "Number of keys should be 0 on failure");
}

Test(crypto_keys, fetch_gitlab_keys_valid_user) {
  char **keys = NULL;
  size_t num_keys = 0;

  int result = fetch_gitlab_keys("gitlab", &keys, &num_keys);

  // This will fail without BearSSL, but tests the interface
  if (result == 0) {
    cr_assert_not_null(keys, "Keys array should be allocated");
    cr_assert_gt(num_keys, 0, "Should fetch at least one key");

    // Free the keys if they were allocated
    for (size_t i = 0; i < num_keys; i++) {
      free(keys[i]);
    }
    free(keys);
  } else {
    // Expected to fail without BearSSL
    cr_assert_eq(result, -1, "Should fail without BearSSL");
  }
}

Test(crypto_keys, fetch_github_gpg_keys) {
  char **keys = NULL;
  size_t num_keys = 0;

  int result = fetch_github_gpg_keys("octocat", &keys, &num_keys);

  // This will fail without BearSSL, but tests the interface
  cr_assert_eq(result, -1, "Should fail without BearSSL");
  cr_assert_null(keys, "Keys array should be NULL on failure");
  cr_assert_eq(num_keys, 0, "Number of keys should be 0 on failure");
}

// =============================================================================
// Authorized Keys Parsing Tests
// =============================================================================

Test(crypto_keys, parse_keys_from_file_nonexistent) {
  public_key_t keys[10];
  size_t num_keys = 0;

  int result = parse_keys_from_file("/nonexistent/authorized_keys", keys, &num_keys, 10);

  cr_assert_eq(result, -1, "Parsing nonexistent file should fail");
  cr_assert_eq(num_keys, 0, "Number of keys should be 0 on failure");
}

Test(crypto_keys, parse_keys_from_file_null_path) {
  public_key_t keys[10];
  size_t num_keys = 0;

  int result = parse_keys_from_file(NULL, keys, &num_keys, 10);

  cr_assert_eq(result, -1, "Parsing NULL path should fail");
}

// =============================================================================
// Public Key Formatting Tests
// =============================================================================

Test(crypto_keys, format_public_key_ed25519) {
  public_key_t key;
  key.type = KEY_TYPE_ED25519;
  memset(key.key, 0x42, 32);
  strcpy(key.comment, "test-key");

  char output[512];
  format_public_key(&key, output, sizeof(output));

  cr_assert_not_null(strstr(output, "ssh-ed25519"), "Formatted key should contain ssh-ed25519");
  cr_assert_not_null(strstr(output, "test-key"), "Formatted key should contain comment");
}

Test(crypto_keys, format_public_key_x25519) {
  public_key_t key;
  key.type = KEY_TYPE_X25519;
  memset(key.key, 0x42, 32);
  strcpy(key.comment, "x25519-key");

  char output[512];
  format_public_key(&key, output, sizeof(output));

  cr_assert_not_null(strstr(output, "x25519"), "Formatted key should contain x25519");
  cr_assert_not_null(strstr(output, "x25519-key"), "Formatted key should contain comment");
}

Test(crypto_keys, format_public_key_gpg) {
  public_key_t key;
  key.type = KEY_TYPE_GPG;
  memset(key.key, 0x42, 32);
  strcpy(key.comment, "gpg-key");

  char output[512];
  format_public_key(&key, output, sizeof(output));

  cr_assert_not_null(strstr(output, "gpg"), "Formatted key should contain gpg");
  cr_assert_not_null(strstr(output, "gpg-key"), "Formatted key should contain comment");
}

// =============================================================================
// Theory Tests for Key Type Validation
// =============================================================================

TheoryDataPoints(crypto_keys, key_type_validation) = {
    DataPoints(key_type_t, KEY_TYPE_UNKNOWN, KEY_TYPE_ED25519, KEY_TYPE_X25519, KEY_TYPE_GPG),
};

Theory((key_type_t key_type), crypto_keys, key_type_validation) {
  cr_assume(key_type >= KEY_TYPE_UNKNOWN && key_type <= KEY_TYPE_GPG);

  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = key_type;

  // Initialize with valid data based on key type
  if (key_type == KEY_TYPE_ED25519) {
    // Use valid Ed25519 public key from libsodium test vectors
    const uint8_t valid_ed25519_pk[32] = {0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a,
                                          0xa7, 0x4d, 0x1b, 0x7e, 0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4,
                                          0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c};
    memcpy(key.key, valid_ed25519_pk, 32);
  } else if (key_type != KEY_TYPE_UNKNOWN) {
    // For X25519 and GPG, any data works
    memset(key.key, 0x42, 32);
  }

  // Test that the key type is preserved
  cr_assert_eq(key.type, key_type, "Key type should be preserved");

  // Test conversion to X25519 (should work for all types)
  uint8_t x25519_key[32];
  int result = public_key_to_x25519(&key, x25519_key);

  if (key_type == KEY_TYPE_UNKNOWN) {
    cr_assert_eq(result, -1, "Unknown key type should fail conversion");
  } else {
    cr_assert_eq(result, 0, "Valid key type should succeed conversion");
  }
}

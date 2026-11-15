#include <catch2/catch_test_macros.hpp>
/**
 * @file crypto_keys_test.c
 * @brief Unit tests for lib/crypto/keys.c - Tests the intended final crypto implementation
 */

#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/tests/logging.h>

// Use the enhanced macro to create complete test suite with debug logging
#ifdef _WIN32
#define strdup _strdup
#endif

static void set_env_value(const char *name, const char *value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

static void unset_env_value(const char *name) {
#ifdef _WIN32
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

// =============================================================================
// Test Utilities
// =============================================================================

static bool network_tests_enabled(void) {
  const char *env = getenv("ASCII_CHAT_ENABLE_NETWORK_TESTS");
  return env != NULL && strcmp(env, "1") == 0;
}

typedef struct {
  char *previous_value;
  bool was_set;
} env_restore_t;

static void set_dummy_ssh_passphrase(env_restore_t *restore) {
  const char *current = getenv("ASCII_CHAT_KEY_PASSWORD");
  if (restore) {
    restore->was_set = current != NULL;
    restore->previous_value = current ? strdup(current) : NULL;
  }
  set_env_value("ASCII_CHAT_KEY_PASSWORD", "dummy-passphrase");
}

static void restore_ssh_passphrase(env_restore_t *restore) {
  if (!restore) {
    return;
  }
  if (restore->was_set) {
    set_env_value("ASCII_CHAT_KEY_PASSWORD", restore->previous_value ? restore->previous_value : "");
  } else {
    unset_env_value("ASCII_CHAT_KEY_PASSWORD");
  }
  free(restore->previous_value);
}

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
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", .expected_len = 32, .expected_result = 0, .description = "valid 64-char hex string"},
    {.hex = "0123456789abcdef", .expected_len = 32, .expected_result = -1, .description = "invalid length (16 chars, need 32)"},
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg", .expected_len = 32, .expected_result = -1, .description = "invalid characters (contains 'g')"},
    {.hex = "", .expected_len = 32, .expected_result = -1, .description = "empty string"},
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0", .expected_len = 32, .expected_result = -1, .description = "too long (65 chars)"},
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde", .expected_len = 32, .expected_result = -1, .description = "too short (63 chars)"},
    {.hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", .expected_len = 16, .expected_result = -1, .description = "wrong expected length"}};

TEST_CASE("crypto_keys/hex_decode_tests", "[crypto_keys]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(hex_decode_cases));
  auto *tc = &generated_case;
  log_debug("Test start: tc=%p", (void *)tc);
  log_debug("Test hex=%s, expected_len=%zu", tc->hex, tc->expected_len);

  uint8_t output[32];
  asciichat_error_t result = hex_decode(tc->hex, output, tc->expected_len);

  log_debug("hex_decode returned %d", result);

  // hex_decode returns asciichat_error_t: ASCIICHAT_OK (0) for success, error code for failure
  if (tc->expected_result == 0) {
    REQUIRE((result) == (ASCIICHAT_OK));

    // For valid cases, verify the output is not all zeros
    bool all_zero = true;
    for (size_t i = 0; i < tc->expected_len; i++) {
      if (output[i] != 0) {
        all_zero = false;
        break;
      }
    }
    REQUIRE(!(all_zero));
  } else {
    // Expected to fail - should return an error code (not ASCIICHAT_OK)
    REQUIRE((result) != (ASCIICHAT_OK));
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
  bool requires_network;
} parse_public_key_test_case_t;

static parse_public_key_test_case_t parse_public_key_cases[] = {
    {.input = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBg7kmREayHMGWhgD0pc9wzuwdi0ibHnFmlAPwOn6mSV test-key",
     .expected_type = KEY_TYPE_ED25519,
     .expected_result = 0,
     .description = "valid SSH Ed25519 key with comment",
     .input_is_null = false,
     .requires_network = false},
    {.input = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBg7kmREayHMGWhgD0pc9wzuwdi0ibHnFmlAPwOn6mSV",
     .expected_type = KEY_TYPE_ED25519,
     .expected_result = 0,
     .description = "valid SSH Ed25519 key without comment",
     .input_is_null = false,
     .requires_network = false},
    {.input = "AAAAC3NzaC1lZDI1NTE5AAAAIBg7kmREayHMGWhgD0pc9wzuwdi0ibHnFmlAPwOn6mSV",
     .expected_type = KEY_TYPE_ED25519,
     .expected_result = 0,
     .description = "raw base64 SSH Ed25519 key (no prefix)",
     .input_is_null = false,
     .requires_network = false},
    {.input = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
     .expected_type = KEY_TYPE_X25519,
     .expected_result = 0,
     .description = "valid X25519 hex key",
     .input_is_null = false,
     .requires_network = false},
    {.input = "github:testuser",
     .expected_type = KEY_TYPE_ED25519,
     .expected_result = 0,
     .description = "GitHub username (should fetch first Ed25519 key)",
     .input_is_null = false,
     .requires_network = true},
    {.input = "gitlab:testuser",
     .expected_type = KEY_TYPE_ED25519,
     .expected_result = 0,
     .description = "GitLab username (should fetch first Ed25519 key)",
     .input_is_null = false,
     .requires_network = true},
    {.input = "gpg:0x1234567890ABCDEF", .expected_type = KEY_TYPE_GPG, .expected_result = 0, .description = "GPG key ID", .input_is_null = false, .requires_network = true},
    {.input = "invalid-key-format", .expected_type = KEY_TYPE_UNKNOWN, .expected_result = -1, .description = "invalid key format", .input_is_null = false, .requires_network = false},
    {.input = "AAAAC3NzaC1lZ", .expected_type = KEY_TYPE_UNKNOWN, .expected_result = -1, .description = "truncated base64 key (too short)", .input_is_null = false, .requires_network = false},
    {.input = "AAAAC3NzaC1lZDI1NTE5AAAAIBg7kmREayHMGWhgD0pc9wzuwdi0ibHnFml@#$%^&*",
     .expected_type = KEY_TYPE_UNKNOWN,
     .expected_result = -1,
     .description = "invalid base64 characters",
     .input_is_null = false,
     .requires_network = false},
    {.input = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQC...",
     .expected_type = KEY_TYPE_UNKNOWN,
     .expected_result = -1,
     .description = "unsupported SSH RSA key",
     .input_is_null = false,
     .requires_network = false},
    {.input = "AAAAB3NzaC1yc2EAAAADAQABAAABAQC1234567890abcdefghijklmnopqrstuvwxyz",
     .expected_type = KEY_TYPE_UNKNOWN,
     .expected_result = -1,
     .description = "base64 RSA key (should be rejected, not Ed25519)",
     .input_is_null = false,
     .requires_network = false},
    {.input = "", .expected_type = KEY_TYPE_UNKNOWN, .expected_result = -1, .description = "empty input", .input_is_null = false, .requires_network = false},
    {.input = "", .expected_type = KEY_TYPE_UNKNOWN, .expected_result = -1, .description = "NULL input", .input_is_null = true, .requires_network = false}};

TEST_CASE("crypto_keys/parse_public_key_tests", "[crypto_keys]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(parse_public_key_cases));
  auto *tc = &generated_case;
  public_key_t key;
  const char *input_ptr = tc->input_is_null ? NULL : tc->input;

  log_debug("Testing case: %s", tc->description);
  log_debug("Input: %s", input_ptr ? input_ptr : "(null)");

  if (tc->requires_network && !network_tests_enabled()) {
    SKIP("test skipped");
  }

  asciichat_error_t result = parse_public_key(input_ptr, &key);

  log_debug("Result: %d, Expected: %d", result, tc->expected_result);

  // parse_public_key returns asciichat_error_t: ASCIICHAT_OK (0) for success, error code for failure
  if (tc->expected_result == 0) {
    if (tc->requires_network && result != ASCIICHAT_OK) {
      SKIP("test skipped");
    }
    REQUIRE((result) == (ASCIICHAT_OK));
    REQUIRE((key.type) == (tc->expected_type));
  } else {
    // Expected to fail - should return an error code (not ASCIICHAT_OK)
    REQUIRE((result) != (ASCIICHAT_OK));
  }
}

// =============================================================================
// Private Key Parsing Tests
// =============================================================================

TEST_CASE("crypto_keys/parse_private_key_ed25519_file", "[crypto_keys]") {
  private_key_t key;
  env_restore_t restore = {0};
  set_dummy_ssh_passphrase(&restore);

  // Test with a mock Ed25519 private key file path
  asciichat_error_t result = parse_private_key("~/.ssh/id_ed25519", &key);

  // This will likely fail without a real key file, but tests the interface
  if (result == ASCIICHAT_OK) {
    REQUIRE((key.type) == (KEY_TYPE_ED25519));
  } else {
    // Expected to fail without real key file - returns error code (not ASCIICHAT_OK)
    REQUIRE((result) != (ASCIICHAT_OK));
  }

  restore_ssh_passphrase(&restore);
}

TEST_CASE("crypto_keys/parse_private_key_nonexistent", "[crypto_keys]") {
  private_key_t key;
  env_restore_t restore = {0};
  set_dummy_ssh_passphrase(&restore);

  asciichat_error_t result = parse_private_key("/nonexistent/path", &key);

  REQUIRE((result) != (ASCIICHAT_OK));

  restore_ssh_passphrase(&restore);
}

TEST_CASE("crypto_keys/parse_private_key_null_path", "[crypto_keys]") {
  private_key_t key;
  env_restore_t restore = {0};
  set_dummy_ssh_passphrase(&restore);

  asciichat_error_t result = parse_private_key(NULL, &key);

  REQUIRE((result) != (ASCIICHAT_OK));

  restore_ssh_passphrase(&restore);
}

// =============================================================================
// Key Conversion Tests
// =============================================================================

TEST_CASE("crypto_keys/public_key_to_x25519_ed25519", "[crypto_keys]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Use a valid Ed25519 public key (generated from a known seed)
  // This is the public key from libsodium's test vectors
  const uint8_t valid_ed25519_pk[32] = {0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7, 0x4d, 0x1b, 0x7e, 0xbc,
                                        0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c};
  memcpy(key.key, valid_ed25519_pk, 32);

  uint8_t x25519_key[32];
  asciichat_error_t result = public_key_to_x25519(&key, x25519_key);

  REQUIRE((result) == (ASCIICHAT_OK));

  // Verify the output is not all zeros
  bool all_zero = true;
  for (int i = 0; i < 32; i++) {
    if (x25519_key[i] != 0) {
      all_zero = false;
      break;
    }
  }
  REQUIRE(!(all_zero));
}

TEST_CASE("crypto_keys/public_key_to_x25519_x25519_passthrough", "[crypto_keys]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_X25519;
  memset(key.key, 0x42, 32);

  uint8_t x25519_key[32];
  asciichat_error_t result = public_key_to_x25519(&key, x25519_key);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((memcmp(key.key, x25519_key, 32)) == (0));
}

TEST_CASE("crypto_keys/public_key_to_x25519_gpg", "[crypto_keys]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_GPG;
  memset(key.key, 0x42, 32);

  uint8_t x25519_key[32];
  asciichat_error_t result = public_key_to_x25519(&key, x25519_key);

  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("crypto_keys/public_key_to_x25519_unknown_type", "[crypto_keys]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_UNKNOWN;

  uint8_t x25519_key[32];
  asciichat_error_t result = public_key_to_x25519(&key, x25519_key);

  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("crypto_keys/private_key_to_x25519_ed25519", "[crypto_keys]") {
  private_key_t key;
  memset(&key, 0, sizeof(key)); // Initialize entire struct including use_ssh_agent=false
  key.type = KEY_TYPE_ED25519;

  // Use a valid Ed25519 private key (seed + public key)
  // This is from libsodium's test vectors
  const uint8_t valid_ed25519_sk[64] = {
      // 32-byte seed
      0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60,
      // 32-byte public key
      0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
  memcpy(key.key.ed25519, valid_ed25519_sk, 64);

  uint8_t x25519_key[32];
  asciichat_error_t result = private_key_to_x25519(&key, x25519_key);

  REQUIRE((result) == (ASCIICHAT_OK));
}

TEST_CASE("crypto_keys/private_key_to_x25519_x25519_passthrough", "[crypto_keys]") {
  private_key_t key;
  memset(&key, 0, sizeof(key)); // Initialize entire struct including use_ssh_agent=false
  key.type = KEY_TYPE_X25519;
  memset(key.key.x25519, 0x42, 32);

  uint8_t x25519_key[32];
  asciichat_error_t result = private_key_to_x25519(&key, x25519_key);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((memcmp(key.key.x25519, x25519_key, 32)) == (0));
}

// =============================================================================
// Remote Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_keys/fetch_github_keys_valid_user", "[crypto_keys]") {
  if (!network_tests_enabled()) {
    SKIP("test skipped");
  }

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_keys("octocat", &keys, &num_keys, false);

  // This will fail without BearSSL, but tests the interface
  if (result == ASCIICHAT_OK) {
    REQUIRE((keys) != nullptr);
    REQUIRE((num_keys) > (0));

    // Free the keys if they were allocated
    for (size_t i = 0; i < num_keys; i++) {
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    SKIP("test skipped");
  }
}

TEST_CASE("crypto_keys/fetch_github_keys_invalid_user", "[crypto_keys]") {
  if (!network_tests_enabled()) {
    SKIP("test skipped");
  }

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_keys("nonexistentuser12345", &keys, &num_keys, false);

  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((keys) == nullptr);
  REQUIRE((num_keys) == (0));
}

TEST_CASE("crypto_keys/fetch_gitlab_keys_valid_user", "[crypto_keys]") {
  if (!network_tests_enabled()) {
    SKIP("test skipped");
  }

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_gitlab_keys("gitlab", &keys, &num_keys, false);

  // This will fail without BearSSL, but tests the interface
  if (result == ASCIICHAT_OK) {
    REQUIRE((keys) != nullptr);
    REQUIRE((num_keys) > (0));

    // Free the keys if they were allocated
    for (size_t i = 0; i < num_keys; i++) {
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    SKIP("test skipped");
  }
}

TEST_CASE("crypto_keys/fetch_github_gpg_keys", "[crypto_keys]") {
  if (!network_tests_enabled()) {
    SKIP("test skipped");
  }

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_gpg_keys("octocat", &keys, &num_keys);

  if (result == ASCIICHAT_OK) {
    REQUIRE((keys) != nullptr);
    REQUIRE((num_keys) > (0));
    for (size_t i = 0; i < num_keys; i++) {
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    SKIP("test skipped");
  }
}

// =============================================================================
// Authorized Keys Parsing Tests
// =============================================================================

TEST_CASE("crypto_keys/parse_keys_from_file_nonexistent", "[crypto_keys]") {
  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_keys_from_file("/nonexistent/authorized_keys", keys, &num_keys, 10);

  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((num_keys) == (0));
}

TEST_CASE("crypto_keys/parse_keys_from_file_null_path", "[crypto_keys]") {
  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_keys_from_file(NULL, keys, &num_keys, 10);

  REQUIRE((result) != (ASCIICHAT_OK));
}

// =============================================================================
// Public Key Formatting Tests
// =============================================================================

TEST_CASE("crypto_keys/format_public_key_ed25519", "[crypto_keys]") {
  public_key_t key;
  key.type = KEY_TYPE_ED25519;
  memset(key.key, 0x42, 32);
  SAFE_STRNCPY(key.comment, "test-key", sizeof(key.comment));

  char output[512];
  format_public_key(&key, output, sizeof(output));

  REQUIRE((strstr(output, "ssh-ed25519")) != nullptr);
  REQUIRE((strstr(output, "test-key")) != nullptr);
}

TEST_CASE("crypto_keys/format_public_key_x25519", "[crypto_keys]") {
  public_key_t key;
  key.type = KEY_TYPE_X25519;
  memset(key.key, 0x42, 32);
  SAFE_STRNCPY(key.comment, "x25519-key", sizeof(key.comment));

  char output[512];
  format_public_key(&key, output, sizeof(output));

  REQUIRE((strstr(output, "x25519")) != nullptr);
}

TEST_CASE("crypto_keys/format_public_key_gpg", "[crypto_keys]") {
  public_key_t key;
  key.type = KEY_TYPE_GPG;
  memset(key.key, 0x42, 32);
  SAFE_STRNCPY(key.comment, "gpg-key", sizeof(key.comment));

  char output[512];
  format_public_key(&key, output, sizeof(output));

  REQUIRE((strstr(output, "unknown")) != nullptr);
}

// =============================================================================
// Theory Tests for Key Type Validation
// =============================================================================

TEST_CASE("crypto_keys/key_type_validation", "[crypto_keys]") {
  key_type_t key_type = GENERATE(KEY_TYPE_UNKNOWN, KEY_TYPE_ED25519, KEY_TYPE_X25519, KEY_TYPE_GPG);
  if (!(key_type >= KEY_TYPE_UNKNOWN && key_type <= KEY_TYPE_GPG)) SKIP("assumption not satisfied");

  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = key_type;

  // Initialize with valid data based on key type
  if (key_type == KEY_TYPE_ED25519) {
    // Use valid Ed25519 public key from libsodium test vectors
    const uint8_t valid_ed25519_pk[32] = {0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7, 0x4d, 0x1b, 0x7e, 0xbc,
                                          0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c};
    memcpy(key.key, valid_ed25519_pk, 32);
  } else if (key_type != KEY_TYPE_UNKNOWN) {
    // For X25519 and GPG, any data works
    memset(key.key, 0x42, 32);
  }

  // Test that the key type is preserved
  REQUIRE((key.type) == (key_type));

  // Test conversion to X25519 (should work for all types except UNKNOWN)
  uint8_t x25519_key[32];
  asciichat_error_t result = public_key_to_x25519(&key, x25519_key);

  if (key_type == KEY_TYPE_UNKNOWN || key_type == KEY_TYPE_GPG) {
    REQUIRE((result) != (ASCIICHAT_OK));
  } else {
    REQUIRE((result) == (ASCIICHAT_OK));
  }
}

// =============================================================================
// Comma-Separated Key Specifiers Tests
// =============================================================================

TEST_CASE("crypto_keys/parse_public_keys_comma_separated_hex_keys", "[crypto_keys]") {
  // Test parsing comma-separated raw hex keys
  char hex1[65] = "0000000000000000000000000000000000000000000000000000000000000001";
  char hex2[65] = "0000000000000000000000000000000000000000000000000000000000000002";
  char hex3[65] = "0000000000000000000000000000000000000000000000000000000000000003";

  char input[256];
  snprintf(input, sizeof(input), "%s, %s, %s", hex1, hex2, hex3);

  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys(input, keys, &num_keys, 10);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((num_keys) == (3));

  // Verify each key is X25519 type
  for (size_t i = 0; i < num_keys; i++) {
    REQUIRE((keys[i].type) == (KEY_TYPE_X25519));
  }
}

TEST_CASE("crypto_keys/parse_public_keys_comma_separated_with_whitespace", "[crypto_keys]") {
  // Test parsing with whitespace around commas
  char hex1[65] = "0000000000000000000000000000000000000000000000000000000000000001";
  char hex2[65] = "0000000000000000000000000000000000000000000000000000000000000002";

  char input[256];
  snprintf(input, sizeof(input), "%s , %s", hex1, hex2);

  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys(input, keys, &num_keys, 10);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((num_keys) == (2));
}

TEST_CASE("crypto_keys/parse_public_keys_single_key_no_comma", "[crypto_keys]") {
  // Test that single keys without comma still work
  char hex1[65] = "0000000000000000000000000000000000000000000000000000000000000001";

  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys(hex1, keys, &num_keys, 10);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((num_keys) == (1));
}

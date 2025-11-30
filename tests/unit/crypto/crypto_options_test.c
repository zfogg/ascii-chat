/**
 * @file crypto_options_test.c
 * @brief Unit tests for crypto CLI options parsing - Tests the intended final crypto implementation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "tests/common.h"
#include "options.h"
#include "tests/logging.h"

// External getopt variable that needs resetting between tests
extern int optind;

// Reset global options between tests to prevent pollution
static void reset_crypto_options(void) {
  // Reset crypto-related globals
  opt_no_encrypt = 0;
  opt_encrypt_key[0] = '\0';
  opt_password[0] = '\0';
  opt_encrypt_keyfile[0] = '\0';
  opt_server_key[0] = '\0';
  opt_client_keys[0] = '\0';

  // Reset optind for getopt_long (critical for multiple test runs)
  optind = 1;
}

// Use the enhanced macro to create complete test suite with basic quiet logging
TestSuite(crypto_options, .init = reset_crypto_options);

// =============================================================================
// Crypto Options Parsing Tests (Parameterized)
// =============================================================================

// clang-format off
typedef struct {
  char description[128];
  int argc;
  char argv[10][256];             // Use static char arrays instead of pointers for Criterion compatibility
  bool is_client;
  bool expect_no_encrypt;
  bool expect_key_set;
  bool expect_server_key_set;
  bool expect_client_keys_set;
  char expected_key[256];         // Use static char array for Criterion fork compatibility
  char expected_server_key[256];  // Use static char array for Criterion fork compatibility
  char expected_client_keys[256]; // Use static char array for Criterion fork compatibility
  int expected_result;
} crypto_options_test_case_t;
// clang-format on

// clang-format off
static crypto_options_test_case_t crypto_options_cases[] = {
    // Note: --help and --version tests are separate (they call _exit(0))
    // Use "" instead of NULL for static char arrays (Criterion fork compatibility)
    {"Disable encryption", 2, {"program", "--no-encrypt"}, true, true, false, false, false, "", "", "", 0},
    {"Set password key",
     3,
     {"program", "--key", "mypassword"},
     true,
     false,
     true,
     false,
     false,
     "mypassword",
     "",
     "",
     0},
    {"Set server key file (client only)",
     3,
     {"program", "--server-key", "/etc/ascii-chat/server_key"},
     true,                         // --server-key is CLIENT ONLY (client verifies server's public key)
     false,                        // expect_no_encrypt
     false,                        // expect_key_set
     true,                         // expect_server_key_set
     false,                        // expect_client_keys_set
     "",                           // expected_key
     "/etc/ascii-chat/server_key", // expected_server_key
     "",                           // expected_client_keys
     0},                           // expected_result
    {"Set client keys file (server only)",
     3,
     {"program", "--client-keys", "/etc/ascii-chat/authorized_keys"},
     false,
     false,
     false,
     false,
     true,
     "",
     "",
     "/etc/ascii-chat/authorized_keys",
     0},
    {"Multiple crypto options",
     4,
     {"program", "--no-encrypt", "--key", "password"},
     true,
     true,
     true,
     false,
     false,
     "password",
     "",
     "",
     0},
    {"GitHub key reference",
     3,
     {"program", "--key", "github:username"},
     true,
     false,
     true,
     false,
     false,
     "github:username",
     "",
     "",
     0},
    {"GitLab key reference",
     3,
     {"program", "--key", "gitlab:username"},
     true,
     false,
     true,
     false,
     false,
     "gitlab:username",
     "",
     "",
     0},
    {"GPG key reference",
     3,
     {"program", "--key", "gpg:0x1234567890ABCDEF"},
     true,
     false,
     true,
     false,
     false,
     "gpg:0x1234567890ABCDEF",
     "",
     "",
     0},
    {"Raw X25519 key",
     3,
     {"program", "--key", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
     true,
     false,
     true,
     false,
     false,
     "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
     "",
     "",
     0},
    {"SSH Ed25519 key",
     3,
     {"program", "--key", "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5"},
     true,
     false,
     true,
     false,
     false,
     "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5",
     "",
     "",
     0},
    {"Long password key",
     3,
     {"program", "--key", "very-long-password-with-special-chars!@#$%^&*()"},
     true,
     false,
     true,
     false,
     false,
     "very-long-password-with-special-chars!@#$%^&*()",
     "",
     "",
     0},
    {"Empty key (should fail)", 3, {"program", "--key", ""}, true, false, false, false, false, "", "", "", -1},
    {"Missing key value (should fail)", 2, {"program", "--key"}, true, false, false, false, false, "", "", "", -1}};
// clang-format on

ParameterizedTestParameters(crypto_options, crypto_options_parsing_tests) {
  size_t nb_cases = sizeof(crypto_options_cases) / sizeof(crypto_options_cases[0]);
  return cr_make_param_array(crypto_options_test_case_t, crypto_options_cases, nb_cases);
}

ParameterizedTest(crypto_options_test_case_t *tc, crypto_options, crypto_options_parsing_tests) {
  // Reset globals before each parameterized test case
  reset_crypto_options();

  // Build a proper char* array from the 2D char array
  // tc->argv is char[10][256], not char**, so we need to create an array of pointers
  char *argv_ptrs[10];
  for (int i = 0; i < tc->argc && i < 10; i++) {
    argv_ptrs[i] = tc->argv[i];
  }

  // Initialize options and check return value
  asciichat_error_t result = options_init(tc->argc, argv_ptrs, tc->is_client);

  // Check return value matches expectation
  if (tc->expected_result == 0) {
    cr_assert_eq(result, ASCIICHAT_OK, "options_init should succeed for case: %s", tc->description);
  } else {
    cr_assert_neq(result, ASCIICHAT_OK, "options_init should fail for case: %s", tc->description);
    return; // Don't check other assertions if we expected failure
  }

  // Test the results (only if we expected success)
  cr_assert_eq(opt_no_encrypt, tc->expect_no_encrypt, "No encrypt flag should match for case: %s", tc->description);
  cr_assert_eq(opt_encrypt_key[0] != '\0', tc->expect_key_set, "Key should be set for case: %s", tc->description);
  cr_assert_eq(opt_server_key[0] != '\0', tc->expect_server_key_set, "Server key should be set for case: %s",
               tc->description);
  cr_assert_eq(opt_client_keys[0] != '\0', tc->expect_client_keys_set, "Client keys should be set for case: %s",
               tc->description);

  // Check expected values (use [0] != '\0' to check if string is non-empty)
  if (tc->expected_key[0] != '\0') {
    cr_assert(opt_encrypt_key[0] != '\0', "Key should not be empty for case: %s", tc->description);
    cr_assert_str_eq(opt_encrypt_key, tc->expected_key, "Key should match for case: %s", tc->description);
  }

  if (tc->expected_server_key[0] != '\0') {
    cr_assert(opt_server_key[0] != '\0', "Server key should not be empty for case: %s", tc->description);
    cr_assert_str_eq(opt_server_key, tc->expected_server_key, "Server key should match for case: %s", tc->description);
  }

  if (tc->expected_client_keys[0] != '\0') {
    cr_assert(opt_client_keys[0] != '\0', "Client keys should not be empty for case: %s", tc->description);
    cr_assert_str_eq(opt_client_keys, tc->expected_client_keys, "Client keys should match for case: %s",
                     tc->description);
  }
}

// =============================================================================
// Validation Tests
// =============================================================================

Test(crypto_options, client_only_options) {
  const char *argv[] = {"program", "--server-key", "/path/to/server/key"};

  // --server-key is client-only, should work for client
  asciichat_error_t result = options_init(3, (char **)argv, true);

  cr_assert_eq(result, ASCIICHAT_OK, "Client-only option should work for client");
  cr_assert(opt_server_key[0] != '\0', "Server key should be set for client");
  cr_assert_str_eq(opt_server_key, "/path/to/server/key", "Server key should match");
}

Test(crypto_options, server_only_options) {
  const char *argv[] = {"program", "--client-keys", "/path/to/authorized_keys"};

  // This should work for server
  options_init(3, (char **)argv, false);

  cr_assert(opt_client_keys[0] != '\0', "Client keys should be set for server");
  cr_assert_str_eq(opt_client_keys, "/path/to/authorized_keys", "Client keys path should match");
}

Test(crypto_options, mutually_exclusive_options) {
  const char *argv[] = {"program", "--no-encrypt", "--key", "password"};

  options_init(4, (char **)argv, true);

  // Both should be set, but --no-encrypt takes precedence
  cr_assert(opt_no_encrypt, "No encrypt should be set");
  cr_assert(opt_encrypt_key[0] != '\0', "Key should still be set");
}

Test(crypto_options, invalid_key_formats) {
  const char *invalid_keys[] = {
      "invalid-key-format",
      "too-short",
      "github:", // Empty username
      "gitlab:", // Empty username
      "gpg:",    // Empty key ID
      "ssh-rsa", // RSA not supported
      "ssh-dss"  // DSA not supported
  };

  for (int i = 0; i < 7; i++) {
    // Reset globals between iterations since we're calling options_init() multiple times
    reset_crypto_options();

    const char *argv[] = {"program", "--key", invalid_keys[i]};
    options_init(3, (char **)argv, true);

    // These should still be accepted by the parser (validation happens later)
    cr_assert(opt_encrypt_key[0] != '\0', "Key should be set even for invalid format: %s", invalid_keys[i]);
    cr_assert_str_eq(opt_encrypt_key, invalid_keys[i], "Key should match input: %s", invalid_keys[i]);
  }
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

Test(crypto_options, very_long_key_value) {
  char long_key[251]; // Less than OPTIONS_BUFF_SIZE (256)
  memset(long_key, 'A', 250);
  long_key[250] = '\0';

  const char *argv[] = {"program", "--key", long_key};

  options_init(3, (char **)argv, true);

  cr_assert(opt_encrypt_key[0] != '\0', "Long key should be accepted");
  cr_assert_str_eq(opt_encrypt_key, long_key, "Long key should match input");
}

Test(crypto_options, special_characters_in_key) {
  const char *special_key = "key!@#$%^&*()_+-=[]{}|;':\",./<>?`~";
  const char *argv[] = {"program", "--key", special_key};

  options_init(3, (char **)argv, true);

  cr_assert(opt_encrypt_key[0] != '\0', "Special characters should be accepted");
  cr_assert_str_eq(opt_encrypt_key, special_key, "Special characters should be preserved");
}

Test(crypto_options, unicode_characters_in_key) {
  const char *unicode_key = "key_with_unicode_测试_🔑";
  const char *argv[] = {"program", "--key", unicode_key};

  options_init(3, (char **)argv, true);

  cr_assert(opt_encrypt_key[0] != '\0', "Unicode characters should be accepted");
  cr_assert_str_eq(opt_encrypt_key, unicode_key, "Unicode characters should be preserved");
}

Test(crypto_options, empty_arguments) {
  const char *argv[] = {"program"};

  options_init(1, (char **)argv, true);

  // Should have default values
  cr_assert_not(opt_no_encrypt, "No encrypt should be false by default");
  cr_assert(opt_encrypt_key[0] == '\0', "Key should be NULL by default");
}

Test(crypto_options, null_arguments) {
  // This should not crash
  options_init(0, NULL, true);

  // Should have default values
  cr_assert_not(opt_no_encrypt, "No encrypt should be false by default");
  cr_assert(opt_encrypt_key[0] == '\0', "Key should be NULL by default");
}

// =============================================================================
// Theory Tests for Option Combinations
// =============================================================================

TheoryDataPoints(crypto_options, option_combinations) = {
    DataPoints(bool, true, false), // is_client
    DataPoints(bool, true, false), // no_encrypt
    DataPoints(bool, true, false), // has_key
};

Theory((bool is_client, bool no_encrypt, bool has_key), crypto_options, option_combinations) {
  // Reset globals and getopt state before each theory iteration
  reset_crypto_options();

  // Build argv based on theory parameters - use static strings
  static const char *program = "program";
  static const char *no_encrypt_opt = "--no-encrypt";
  static const char *key_opt = "--key";
  static const char *key_val = "test-key";

  const char *argv[10];
  int argc = 1;
  argv[0] = program;

  if (no_encrypt) {
    argv[argc++] = no_encrypt_opt;
  }

  if (has_key) {
    argv[argc++] = key_opt;
    argv[argc++] = key_val;
  }

  options_init(argc, (char **)argv, is_client);

  // Verify the options were parsed correctly
  cr_assert_eq(opt_no_encrypt, no_encrypt, "No encrypt flag should match");
  cr_assert_eq(opt_encrypt_key[0] != '\0', has_key, "Key should be set if specified");
}

// =============================================================================
// File Path Tests
// =============================================================================

Test(crypto_options, absolute_file_paths) {
  const char *argv[] = {"program", "--server-key", "/etc/ascii-chat/server_key"};

  // --server-key is CLIENT ONLY (client verifies server's public key)
  options_init(3, (char **)argv, true);

  cr_assert(opt_server_key[0] != '\0', "Server key should be set");
  cr_assert_str_eq(opt_server_key, "/etc/ascii-chat/server_key", "Server key path should match");
}

Test(crypto_options, relative_file_paths) {
  const char *argv[] = {"program", "--client-keys", "./authorized_keys"};

  options_init(3, (char **)argv, false);

  cr_assert(opt_client_keys[0] != '\0', "Client keys should be set");
  cr_assert_str_eq(opt_client_keys, "./authorized_keys", "Client keys path should match");
}

// =============================================================================
// Help and Usage Tests
// =============================================================================

// Test that --help calls _exit(0) - runs in separate process
Test(crypto_options, help_display, .exit_code = 0) {
  const char *argv[] = {"program", "--help"};

  // This will call _exit(0) after printing help
  options_init(2, (char **)argv, true);

  // Should never reach here due to _exit(0)
  cr_fatal("Should have exited before reaching this line");
}

// Test that --version calls _exit(0) - runs in separate process
Test(crypto_options, version_display, .exit_code = 0) {
  const char *argv[] = {"program", "--version"};

  // This will call _exit(0) after printing version
  options_init(2, (char **)argv, true);

  // Should never reach here due to _exit(0)
  cr_fatal("Should have exited before reaching this line");
}

// =============================================================================
// Stress Tests
// =============================================================================

Test(crypto_options, many_options) {
  // Note: --server-key is CLIENT-only, --client-keys is SERVER-only
  // So we test with client mode and skip --client-keys
  const char *argv[] = {"program", "--no-encrypt", "--key", "password", "--server-key", "/etc/server_key"};

  options_init(6, (char **)argv, true); // true = client mode for --server-key

  // Options should be set
  cr_assert(opt_no_encrypt, "No encrypt should be set");
  cr_assert(opt_encrypt_key[0] != '\0', "Key should be set");
  cr_assert(opt_server_key[0] != '\0', "Server key should be set");
}

Test(crypto_options, repeated_options) {
  const char *argv[] = {"program", "--key", "first-key", "--key", "second-key"};

  options_init(5, (char **)argv, true);

  // Should use the last values
  cr_assert(opt_encrypt_key[0] != '\0', "Key should be set");
  cr_assert_str_eq(opt_encrypt_key, "second-key", "Should use last key");
}

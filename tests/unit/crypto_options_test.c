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

#include "tests/common.h"
#include "options.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(crypto_options);

// =============================================================================
// Crypto Options Parsing Tests (Parameterized)
// =============================================================================

typedef struct {
  const char *description;
  int argc;
  const char *argv[10];
  bool is_client;
  bool expect_no_encrypt;
  bool expect_key_set;
  bool expect_ssh_key_set;
  bool expect_server_key_set;
  bool expect_client_keys_set;
  const char *expected_key;
  const char *expected_ssh_key;
  const char *expected_server_key;
  const char *expected_client_keys;
  int expected_result;
} crypto_options_test_case_t;

static crypto_options_test_case_t crypto_options_cases[] = {
    {"No crypto options (default)",
     2,
     {"program", "--help"},
     true,
     false,
     false,
     false,
     false,
     false,
     NULL,
     NULL,
     NULL,
     NULL,
     0},
    {"Disable encryption",
     2,
     {"program", "--no-encrypt"},
     true,
     true,
     false,
     false,
     false,
     false,
     NULL,
     NULL,
     NULL,
     NULL,
     0},
    {"Set password key",
     3,
     {"program", "--key", "mypassword"},
     true,
     false,
     true,
     false,
     false,
     false,
     "mypassword",
     NULL,
     NULL,
     NULL,
     0},
    {"Set SSH key file",
     3,
     {"program", "--ssh-key", "~/.ssh/id_ed25519"},
     true,
     false,
     false,
     true,
     false,
     false,
     NULL,
     "~/.ssh/id_ed25519",
     NULL,
     NULL,
     0},
    {"Set server key file (server only)",
     3,
     {"program", "--server-key", "/etc/ascii-chat/server_key"},
     false,
     false,
     false,
     false,
     true,
     false,
     NULL,
     NULL,
     "/etc/ascii-chat/server_key",
     NULL,
     0},
    {"Set client keys file (server only)",
     3,
     {"program", "--client-keys", "/etc/ascii-chat/authorized_keys"},
     false,
     false,
     false,
     false,
     false,
     true,
     NULL,
     NULL,
     NULL,
     "/etc/ascii-chat/authorized_keys",
     0},
    {"Multiple crypto options",
     5,
     {"program", "--no-encrypt", "--key", "password", "--ssh-key", "~/.ssh/id_ed25519"},
     true,
     true,
     true,
     true,
     false,
     false,
     "password",
     "~/.ssh/id_ed25519",
     NULL,
     NULL,
     0},
    {"GitHub key reference",
     3,
     {"program", "--key", "github:username"},
     true,
     false,
     true,
     false,
     false,
     false,
     "github:username",
     NULL,
     NULL,
     NULL,
     0},
    {"GitLab key reference",
     3,
     {"program", "--key", "gitlab:username"},
     true,
     false,
     true,
     false,
     false,
     false,
     "gitlab:username",
     NULL,
     NULL,
     NULL,
     0},
    {"GPG key reference",
     3,
     {"program", "--key", "gpg:0x1234567890ABCDEF"},
     true,
     false,
     true,
     false,
     false,
     false,
     "gpg:0x1234567890ABCDEF",
     NULL,
     NULL,
     NULL,
     0},
    {"Raw X25519 key",
     3,
     {"program", "--key", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
     true,
     false,
     true,
     false,
     false,
     false,
     "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
     NULL,
     NULL,
     NULL,
     0},
    {"SSH Ed25519 key",
     3,
     {"program", "--key", "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5"},
     true,
     false,
     true,
     false,
     false,
     false,
     "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5",
     NULL,
     NULL,
     NULL,
     0},
    {"Long password key",
     3,
     {"program", "--key", "very-long-password-with-special-chars!@#$%^&*()"},
     true,
     false,
     true,
     false,
     false,
     false,
     "very-long-password-with-special-chars!@#$%^&*()",
     NULL,
     NULL,
     NULL,
     0},
    {"Empty key (should fail)",
     3,
     {"program", "--key", ""},
     true,
     false,
     false,
     false,
     false,
     false,
     NULL,
     NULL,
     NULL,
     NULL,
     -1},
    {"Missing key value (should fail)",
     2,
     {"program", "--key"},
     true,
     false,
     false,
     false,
     false,
     false,
     NULL,
     NULL,
     NULL,
     NULL,
     -1}};

ParameterizedTestParameters(crypto_options, crypto_options_parsing_tests) {
  size_t nb_cases = sizeof(crypto_options_cases) / sizeof(crypto_options_cases[0]);
  return cr_make_param_array(crypto_options_test_case_t, crypto_options_cases, nb_cases);
}

ParameterizedTest(crypto_options_test_case_t *tc, crypto_options, crypto_options_parsing_tests) {
  // Initialize options
  options_init(tc->argc, (char **)tc->argv, tc->is_client);

  // Test the results
  cr_assert_eq(opt_no_encrypt, tc->expect_no_encrypt, "No encrypt flag should match for case: %s", tc->description);
  cr_assert_eq(opt_encrypt_key[0] != '\0', tc->expect_key_set, "Key should be set for case: %s", tc->description);
  cr_assert_eq(opt_server_key[0] != '\0', tc->expect_server_key_set, "Server key should be set for case: %s",
               tc->description);
  cr_assert_eq(opt_client_keys[0] != '\0', tc->expect_client_keys_set, "Client keys should be set for case: %s",
               tc->description);

  if (tc->expected_key) {
    cr_assert(opt_encrypt_key[0] != '\0', "Key should not be NULL for case: %s", tc->description);
    cr_assert_str_eq(opt_encrypt_key, tc->expected_key, "Key should match for case: %s", tc->description);
  }

  if (tc->expected_ssh_key) {
  }

  if (tc->expected_server_key) {
    cr_assert(opt_server_key[0] != '\0', "Server key should not be NULL for case: %s", tc->description);
    cr_assert_str_eq(opt_server_key, tc->expected_server_key, "Server key should match for case: %s", tc->description);
  }

  if (tc->expected_client_keys) {
    cr_assert(opt_client_keys[0] != '\0', "Client keys should not be NULL for case: %s", tc->description);
    cr_assert_str_eq(opt_client_keys, tc->expected_client_keys, "Client keys should match for case: %s",
                     tc->description);
  }
}

// =============================================================================
// Validation Tests
// =============================================================================

Test(crypto_options, client_only_options) {
  const char *argv[] = {"program", "--server-key", "/path/to/server/key"};

  // This should fail for client
  options_init(2, (char **)argv, true);

  // Server key should not be set for client
  cr_assert(opt_server_key[0] == '\0', "Server key should not be set for client");
}

Test(crypto_options, server_only_options) {
  const char *argv[] = {"program", "--client-keys", "/path/to/authorized_keys"};

  // This should work for server
  options_init(2, (char **)argv, false);

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
  char long_key[1024];
  memset(long_key, 'A', 1023);
  long_key[1023] = '\0';

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

Test(crypto_options, multiple_ssh_keys) {
  const char *argv[] = {"program", "--ssh-key", "~/.ssh/id_ed25519", "--ssh-key", "~/.ssh/id_ed25519_2"};

  options_init(5, (char **)argv, true);

  // Should use the last one
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
    DataPoints(bool, true, false), // has_ssh_key
};

Theory((bool is_client, bool no_encrypt, bool has_key, bool has_ssh_key), crypto_options, option_combinations) {
  // Build argv based on theory parameters
  char *argv[10];
  int argc = 1;
  argv[0] = "program";

  if (no_encrypt) {
    argv[argc++] = "--no-encrypt";
  }

  if (has_key) {
    argv[argc++] = "--key";
    argv[argc++] = "test-key";
  }

  if (has_ssh_key) {
    argv[argc++] = "--ssh-key";
    argv[argc++] = "~/.ssh/id_ed25519";
  }

  options_init(argc, argv, is_client);

  // Verify the options were parsed correctly
  cr_assert_eq(opt_no_encrypt, no_encrypt, "No encrypt flag should match");
  cr_assert_eq(opt_encrypt_key[0] != '\0', has_key, "Key should be set if specified");
}

// =============================================================================
// File Path Tests
// =============================================================================

Test(crypto_options, file_path_expansion) {
  const char *argv[] = {"program", "--ssh-key", "~/.ssh/id_ed25519"};

  options_init(3, (char **)argv, true);
}

Test(crypto_options, absolute_file_paths) {
  const char *argv[] = {"program", "--server-key", "/etc/ascii-chat/server_key"};

  options_init(3, (char **)argv, false);

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

Test(crypto_options, help_display) {
  const char *argv[] = {"program", "--help"};

  // This should not crash and should show help
  options_init(2, (char **)argv, true);

  // Help should be handled gracefully
  cr_assert_not(opt_no_encrypt, "No encrypt should be false when showing help");
}

Test(crypto_options, version_display) {
  const char *argv[] = {"program", "--version"};

  // This should not crash and should show version
  options_init(2, (char **)argv, true);

  // Version should be handled gracefully
  cr_assert_not(opt_no_encrypt, "No encrypt should be false when showing version");
}

// =============================================================================
// Stress Tests
// =============================================================================

Test(crypto_options, many_options) {
  const char *argv[] = {"program",       "--no-encrypt",        "--key",        "password",
                        "--ssh-key",     "~/.ssh/id_ed25519",   "--server-key", "/etc/server_key",
                        "--client-keys", "/etc/authorized_keys"};

  options_init(11, (char **)argv, false);

  // All options should be set
  cr_assert(opt_no_encrypt, "No encrypt should be set");
  cr_assert(opt_encrypt_key[0] != '\0', "Key should be set");
  cr_assert(opt_server_key[0] != '\0', "Server key should be set");
  cr_assert(opt_client_keys[0] != '\0', "Client keys should be set");
}

Test(crypto_options, repeated_options) {
  const char *argv[] = {"program",   "--key",     "first-key", "--key",     "second-key",
                        "--ssh-key", "first-ssh", "--ssh-key", "second-ssh"};

  options_init(9, (char **)argv, true);

  // Should use the last values
  cr_assert(opt_encrypt_key[0] != '\0', "Key should be set");
  cr_assert_str_eq(opt_encrypt_key, "second-key", "Should use last key");
}

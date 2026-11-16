#include <catch2/catch_test_macros.hpp>
/**
 * @file crypto_options_test.c
 * @brief Unit tests for crypto CLI options parsing - Tests the intended final crypto implementation
 */

#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <libplatform/windows/getopt.h>
#else
#include <getopt.h>
#endif
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/logging.h>

// Reset global options between tests to prevent pollution
static void reset_crypto_options(void) {
  // Shutdown and reinitialize RCU state to clear all previous state
  options_state_destroy();
  options_state_init();
}

static int options_init_exit_code(int argc, char **argv) {
#ifdef _WIN32
  return options_init(argc, argv) == ASCIICHAT_OK ? 0 : 1;
#else
  pid_t pid = fork();
  if (pid < 0) {
    return 1;
  }
  if (pid == 0) {
    _exit(options_init(argc, argv) == ASCIICHAT_OK ? 0 : 1);
  }
  int status = 0;
  return waitpid(pid, &status, 0) >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#endif
}

// Use the enhanced macro to create complete test suite with basic quiet logging
// =============================================================================
// Crypto Options Parsing Tests (Parameterized)
// =============================================================================

// clang-format off
typedef struct {
  char description[128];
  int argc;
  char argv[10][256];             // Use static char arrays instead of pointers for Catch2 compatibility
  bool is_client;
  bool expect_no_encrypt;
  bool expect_key_set;
  bool expect_server_key_set;
  bool expect_client_keys_set;
  char expected_key[256];         // Use static char array for Catch2 fork compatibility
  char expected_server_key[256];  // Use static char array for Catch2 fork compatibility
  char expected_client_keys[256]; // Use static char array for Catch2 fork compatibility
  int expected_result;
} crypto_options_test_case_t;
// clang-format on

// clang-format off
static crypto_options_test_case_t crypto_options_cases[] = {
    // Note: --help and --version tests are separate (they call exit(0))
    // Use "" instead of NULL for static char arrays (Catch2 fork compatibility)
    {"Disable encryption", 3, {"program", "client", "--no-encrypt"}, true, true, false, false, false, "", "", "", 0},
    {"Set password key",
     4,
     {"program", "client", "--key", "mypassword"},
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
     4,
     {"program", "client", "--server-key", "/etc/ascii-chat/server_key"},
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
     4,
     {"program", "server", "--client-keys", "/etc/ascii-chat/authorized_keys"},
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
     5,
     {"program", "client", "--no-encrypt", "--key", "password"},
     true,
     true,
     true,
     false,
     false,
     "password",
     "",
     "",
     0},  // Authentication remains available without payload encryption
    {"GitHub key reference",
     4,
     {"program", "client", "--key", "github:username"},
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
     4,
     {"program", "client", "--key", "gitlab:username"},
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
     4,
     {"program", "client", "--key", "gpg:0x1234567890ABCDEF"},
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
     4,
     {"program", "client", "--key", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"},
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
     4,
     {"program", "client", "--key", "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5"},
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
     4,
     {"program", "client", "--key", "very-long-password-with-special-chars!@#$%^&*()"},
     true,
     false,
     true,
     false,
     false,
     "very-long-password-with-special-chars!@#$%^&*()",
     "",
     "",
     0},
    {"Empty key (should fail)", 4, {"program", "client", "--key", ""}, true, false, false, false, false, "", "", "", -1},
    {"Missing key value (should fail)", 3, {"program", "client", "--key"}, true, false, false, false, false, "", "", "", -1}};
// clang-format on

TEST_CASE("crypto_options/crypto_options_parsing_tests", "[crypto_options]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(crypto_options_cases));
  crypto_options_test_case_t *tc = &generated_case;
  // Reset globals before each parameterized test case
  reset_crypto_options();

  // Build a proper char* array from the 2D char array
  // tc->argv is char[10][256], not char**, so we need to create an array of pointers
  // IMPORTANT: Initialize to NULL to prevent getopt_long from reading garbage beyond argc
  char *argv_ptrs[10] = {NULL};
  for (int i = 0; i < tc->argc && i < 10; i++) {
    argv_ptrs[i] = tc->argv[i];
  }

  // Initialize options and check return value
  asciichat_error_t result = options_init(tc->argc, argv_ptrs);

  // Check return value matches expectation
  if (tc->expected_result == 0) {
    REQUIRE((result) == (ASCIICHAT_OK));
  } else {
    REQUIRE((result) != (ASCIICHAT_OK));
    return; // Don't check other assertions if we expected failure
  }

  // Get options from RCU state
  const options_t *opts = options_get();

  // Test the results (only if we expected success)
  REQUIRE((opts->no_encrypt) == (tc->expect_no_encrypt));
  REQUIRE((opts->encrypt_key[0] != '\0') == (tc->expect_key_set));
  REQUIRE((opts->server_key[0] != '\0') == (tc->expect_server_key_set));
  REQUIRE((opts->client_keys[0] != '\0') == (tc->expect_client_keys_set));

  // Check expected values (use [0] != '\0' to check if string is non-empty)
  if (tc->expected_key[0] != '\0') {
    REQUIRE(opts->encrypt_key[0] != '\0');
    REQUIRE(std::strcmp((opts->encrypt_key), (tc->expected_key)) == 0);
  }

  if (tc->expected_server_key[0] != '\0') {
    REQUIRE(opts->server_key[0] != '\0');
    REQUIRE(std::strcmp((opts->server_key), (tc->expected_server_key)) == 0);
  }

  if (tc->expected_client_keys[0] != '\0') {
    REQUIRE(opts->client_keys[0] != '\0');
    REQUIRE(std::strcmp((opts->client_keys), (tc->expected_client_keys)) == 0);
  }
}

// =============================================================================
// Validation Tests
// =============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("crypto_options/client_only_options", "[crypto_options]") {
  const char *argv[] = {"program", "client", "--server-key", "/path/to/server/key"};

  // --server-key is client-only, should work for client
  asciichat_error_t result = options_init(4, (char **)argv);

  REQUIRE((result) == (ASCIICHAT_OK));

  // Get options from RCU state
  const options_t *opts = options_get();

  REQUIRE(opts->server_key[0] != '\0');
  REQUIRE(std::strcmp((opts->server_key), ("/path/to/server/key")) == 0);
}

TEST_CASE("crypto_options/server_only_options", "[crypto_options]") {
  const char *argv[] = {"program", "server", "--client-keys", "/path/to/authorized_keys"};

  // This should work for server
  options_init(4, (char **)argv);

  // Get options from RCU state
  const options_t *opts = options_get();

  REQUIRE(opts->client_keys[0] != '\0');
  REQUIRE(std::strcmp((opts->client_keys), ("/path/to/authorized_keys")) == 0);
}

TEST_CASE("crypto_options/mutually_exclusive_options", "[crypto_options]") {
  char program[] = "program";
  char client[] = "client";
  char encrypt[] = "--encrypt";
  char no_encrypt[] = "--no-encrypt";
  char *argv[] = {program, client, encrypt, no_encrypt};

  REQUIRE((options_init_exit_code(4, argv)) != (0));
}

TEST_CASE("crypto_options/no_auth_rejects_authentication", "[crypto_options]") {
  char program[] = "program";
  char client[] = "client";
  char no_auth[] = "--no-auth";
  char key[] = "--key";
  char identity[] = "test-key";
  char *argv[] = {program, client, no_auth, key, identity};

  REQUIRE((options_init_exit_code(5, argv)) != (0));
}

TEST_CASE("crypto_options/invalid_key_formats", "[crypto_options]") {
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

    const char *argv[] = {"program", "client", "--key", invalid_keys[i]};
    options_init(4, (char **)argv);

    // Get options from RCU state
    const options_t *opts = options_get();

    // These should still be accepted by the parser (validation happens later)
    REQUIRE(opts->encrypt_key[0] != '\0');
    REQUIRE(std::strcmp((opts->encrypt_key), (invalid_keys[i])) == 0);
  }
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

TEST_CASE("crypto_options/very_long_key_value", "[crypto_options]") {
  char long_key[251]; // Less than OPTIONS_BUFF_SIZE (256)
  memset(long_key, 'A', 250);
  long_key[250] = '\0';

  const char *argv[] = {"program", "client", "--key", long_key};

  options_init(4, (char **)argv);

  // Get options from RCU state
  const options_t *opts = options_get();

  REQUIRE(opts->encrypt_key[0] != '\0');
  REQUIRE(std::strcmp((opts->encrypt_key), (long_key)) == 0);
}

TEST_CASE("crypto_options/special_characters_in_key", "[crypto_options]") {
  const char *special_key = "key!@#$%^&*()_+-=[]{}|;':\",./<>?`~";
  const char *argv[] = {"program", "client", "--key", special_key};

  options_init(4, (char **)argv);

  // Get options from RCU state
  const options_t *opts = options_get();

  REQUIRE(opts->encrypt_key[0] != '\0');
  REQUIRE(std::strcmp((opts->encrypt_key), (special_key)) == 0);
}

TEST_CASE("crypto_options/unicode_characters_in_key", "[crypto_options]") {
  const char *unicode_key = "key_with_unicode_测试_🔑";
  const char *argv[] = {"program", "client", "--key", unicode_key};

  options_init(4, (char **)argv);

  // Get options from RCU state
  const options_t *opts = options_get();

  REQUIRE(opts->encrypt_key[0] != '\0');
  REQUIRE(std::strcmp((opts->encrypt_key), (unicode_key)) == 0);
}

TEST_CASE("crypto_options/empty_arguments", "[crypto_options]") {
  const char *argv[] = {"program", "client"};

  options_init(2, (char **)argv);

  // Get options from RCU state
  const options_t *opts = options_get();

  // Should have default values
  REQUIRE(!(opts->no_encrypt));
  REQUIRE(opts->encrypt_key[0] == '\0');
}

TEST_CASE("crypto_options/null_arguments", "[crypto_options]") {
  // This should not crash
  options_init(0, NULL);

  // Get options from RCU state
  const options_t *opts = options_get();

  // Should have default values
  REQUIRE(!(opts->no_encrypt));
  REQUIRE(opts->encrypt_key[0] == '\0');
}

// =============================================================================
// Theory Tests for Option Combinations
// =============================================================================

TEST_CASE("crypto_options/option_combinations", "[crypto_options]") {
  struct option_combination_t {
    bool is_client;
    bool no_encrypt;
    bool has_key;
  };
  static constexpr option_combination_t combinations[] = {
      {true, true, true}, {true, true, false}, {true, false, true}, {true, false, false}, {false, true, true}, {false, true, false}, {false, false, true}, {false, false, false},
  };
  auto &&generated_case = GENERATE(Catch::Generators::from_range(combinations));
  const auto &combination = generated_case;
  const bool is_client = combination.is_client;
  const bool no_encrypt = combination.no_encrypt;
  const bool has_key = combination.has_key;
  // Reset globals and getopt state before each theory iteration
  reset_crypto_options();

  // Build argv based on theory parameters - use static strings
  static const char *program = "program";
  static const char *client_mode = "client";
  static const char *server_mode = "server";
  static const char *no_encrypt_opt = "--no-encrypt";
  static const char *key_opt = "--key";
  static const char *key_val = "gpg:0x1234567890ABCDEF";

  const char *argv[10] = {NULL};
  int argc = 0;
  argv[argc++] = program;
  argv[argc++] = is_client ? client_mode : server_mode;

  if (no_encrypt) {
    argv[argc++] = no_encrypt_opt;
  }

  if (has_key) {
    argv[argc++] = key_opt;
    argv[argc++] = key_val;
  }

  REQUIRE((options_init(argc, (char **)argv)) == (ASCIICHAT_OK));

  // Get options from RCU state
  const options_t *opts = options_get();

  // Verify the options were parsed correctly
  REQUIRE((opts->no_encrypt) == (no_encrypt));
  REQUIRE((opts->encrypt_key[0] != '\0') == (has_key));
}

// =============================================================================
// File Path Tests
// =============================================================================

TEST_CASE("crypto_options/absolute_file_paths", "[crypto_options]") {
  const char *argv[] = {"program", "client", "--server-key", "/etc/ascii-chat/server_key"};

  // --server-key is CLIENT ONLY (client verifies server's public key)
  options_init(4, (char **)argv);

  // Get options from RCU state
  const options_t *opts = options_get();

  REQUIRE(opts->server_key[0] != '\0');
  REQUIRE(std::strcmp((opts->server_key), ("/etc/ascii-chat/server_key")) == 0);
}

TEST_CASE("crypto_options/relative_file_paths", "[crypto_options]") {
  const char *argv[] = {"program", "server", "--client-keys", "./authorized_keys"};

  options_init(4, (char **)argv);

  // Get options from RCU state
  const options_t *opts = options_get();

  REQUIRE(opts->client_keys[0] != '\0');
  REQUIRE(std::strcmp((opts->client_keys), ("./authorized_keys")) == 0);
}

// =============================================================================
// Help and Usage Tests
// =============================================================================

// Test that --help returns success (previously called exit(0))
TEST_CASE("crypto_options/help_display", "[crypto_options]") {
  const char *argv[] = {"program", "--help"};

  // options_init now returns a value instead of calling exit()
  asciichat_error_t result = options_init(2, (char **)argv);

  // Help display should return success (sets help flag in options)
  REQUIRE((result) == (ASCIICHAT_OK));
}

// Test that --version returns success (previously called exit(0))
TEST_CASE("crypto_options/version_display", "[crypto_options]") {
  const char *argv[] = {"program", "--version"};

  // options_init now returns a value instead of calling exit()
  asciichat_error_t result = options_init(2, (char **)argv);

  // Version display should return success (sets version flag in options)
  REQUIRE((result) == (ASCIICHAT_OK));
}

// =============================================================================
// Stress Tests
// =============================================================================

TEST_CASE("crypto_options/many_options", "[crypto_options]") {
  // Note: --server-key is CLIENT-only, --client-keys is SERVER-only
  // So we test with client mode and skip --client-keys
  char program[] = "program";
  char client[] = "client";
  char no_encrypt[] = "--no-encrypt";
  char key[] = "--key";
  char password[] = "password";
  char server_key[] = "--server-key";
  char server_key_value[] = "/etc/server_key";
  char *argv[] = {program, client, no_encrypt, key, password, server_key, server_key_value};

  REQUIRE((options_init(7, argv)) == (ASCIICHAT_OK));
  const options_t *opts = options_get();
  REQUIRE(opts->no_encrypt);
  REQUIRE(!(opts->encrypt_enabled));
  REQUIRE(std::strcmp((opts->encrypt_key), (password)) == 0);
  REQUIRE(std::strcmp((opts->server_key), (server_key_value)) == 0);
}

TEST_CASE("crypto_options/repeated_options", "[crypto_options]") {
  const char *argv[] = {"program", "--key", "first-key", "--key", "second-key"};

  options_init(5, (char **)argv);

  // Get options from RCU state
  const options_t *opts = options_get();

  // Should use the last values
  REQUIRE(opts->encrypt_key[0] != '\0');
  REQUIRE(std::strcmp((opts->encrypt_key), ("second-key")) == 0);
}

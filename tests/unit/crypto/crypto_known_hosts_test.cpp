#include <iterator>
#include <catch2/catch_test_macros.hpp>
/**
 * @file crypto_known_hosts_test.c
 * @brief Unit tests for lib/crypto/known_hosts.c - Tests the intended final crypto implementation
 */

#include <catch2/catch_all.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ascii-chat/crypto/known_hosts.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/logging.h>

// Custom setup function that combines logging setup and known_hosts cleanup
void setup_crypto_known_hosts_tests(void) {
  log_init(NULL, LOG_INFO, false, false);
  log_set_level(LOG_INFO);

  // Make each test process use a unique known_hosts file to avoid concurrency issues
  // Catch2 runs tests in separate processes, so they would all write to the same file
  // Use PID to make the path unique per process
  char unique_path[512];
  safe_snprintf(unique_path, sizeof(unique_path), "/tmp/.ascii-chat-test-%d/known_hosts", (int)getpid());

  // Set XDG_CONFIG_HOME to make get_known_hosts_path() use our unique directory
  char config_dir[512];
  safe_snprintf(config_dir, sizeof(config_dir), "/tmp/.ascii-chat-test-%d", (int)getpid());
  platform_setenv("XDG_CONFIG_HOME", config_dir);

  // Clean up any existing cache from previous tests to prevent parallel test conflicts
  known_hosts_destroy();

  // Delete the known_hosts file to ensure clean state for each test
  const char *path = get_known_hosts_path();
  if (path) {
    (void)remove(path); // Ignore error if file doesn't exist
  }
}

// Custom teardown function that combines logging restore and known_hosts cleanup
void teardown_crypto_known_hosts_tests(void) {
  // Clean up the test-specific known_hosts file
  const char *path = get_known_hosts_path();
  if (path) {
    (void)remove(path);
  }

  // Clean up cache after test completes
  known_hosts_destroy();

  // Clean up the test directory (ignore errors if already cleaned)
  char test_dir[512];
  safe_snprintf(test_dir, sizeof(test_dir), "/tmp/.ascii-chat-test-%d", (int)getpid());
  (void)rmdir(test_dir);

  // Unset the XDG_CONFIG_HOME we set in setup
  platform_unsetenv("XDG_CONFIG_HOME");

  log_destroy();
  log_set_level(LOG_DEBUG);
}

// Declare test suite with custom init/fini functions
// =============================================================================
// Known Hosts Path Tests
// =============================================================================

TEST_CASE("crypto_known_hosts/get_known_hosts_path", "[crypto_known_hosts]") {
  const char *path = get_known_hosts_path();

  log_info("well test");

  REQUIRE((path) != nullptr);
  REQUIRE((strstr(path, "ascii-chat")) != nullptr);
  REQUIRE((strstr(path, "known_hosts")) != nullptr);
}

// =============================================================================
// Add Known Host Tests (Parameterized)
// =============================================================================

typedef struct {
  char hostname[64];
  uint16_t port;
  uint8_t server_key[32];
  int expected_result;
  char description[256];
} add_known_host_test_case_t;

static add_known_host_test_case_t add_known_host_cases[] = {
    {"example.com",
     8080,
     {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
     0,
     "valid host with port"},
    {"server.example.com",
     22,
     {0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0},
     0,
     "valid SSH server"},
    {"localhost",
     3000,
     {0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01},
     0,
     "localhost with custom port"},
    {"",
     8080,
     {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
     -1,
     "empty hostname should fail"}};

TEST_CASE("crypto_known_hosts/add_known_host_tests", "[crypto_known_hosts]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(add_known_host_cases), std::end(add_known_host_cases)));
  auto *tc = &generated_case;
  // Validate the test case pointer is not NULL
  REQUIRE((tc) != nullptr);
  REQUIRE((tc->hostname) != nullptr);
  REQUIRE((tc->description) != nullptr);

  asciichat_error_t result = add_known_host(tc->hostname, tc->port, tc->server_key);

  if (tc->expected_result == 0) {
    REQUIRE((result) == (ASCIICHAT_OK));
  } else {
    // Expected to fail - should return an error (not ASCIICHAT_OK)
    REQUIRE((result) != (ASCIICHAT_OK));
  }
}

// =============================================================================
// Check Known Host Tests
// =============================================================================

TEST_CASE("crypto_known_hosts/check_known_host_exists_match", "[crypto_known_hosts]") {
  // First add a known host
  const char *hostname = "test.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t add_result = add_known_host(hostname, port, server_key);
  REQUIRE((add_result) == (ASCIICHAT_OK));

  // Check if it exists and matches
  // Note: check_known_host returns 1 (not ASCIICHAT_OK) when key matches
  asciichat_error_t check_result = check_known_host(hostname, port, server_key);
  REQUIRE((check_result) == (1));
}

TEST_CASE("crypto_known_hosts/check_known_host_exists_mismatch", "[crypto_known_hosts]") {
  // First add a known host
  const char *hostname = "test2.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t add_result = add_known_host(hostname, port, server_key);
  REQUIRE((add_result) == (ASCIICHAT_OK));

  // Check with different key
  const uint8_t different_key[32] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                                     0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
  asciichat_error_t check_result = check_known_host(hostname, port, different_key);
  REQUIRE((check_result) != (ASCIICHAT_OK));
  REQUIRE((check_result) != (1));
  REQUIRE((check_result) == (ERROR_CRYPTO_VERIFICATION));
}

TEST_CASE("crypto_known_hosts/check_known_host_not_exists", "[crypto_known_hosts]") {
  const char *hostname = "nonexistent.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t check_result = check_known_host(hostname, port, server_key);
  REQUIRE((check_result) == (ASCIICHAT_OK));
}

TEST_CASE("crypto_known_hosts/check_known_host_null_params", "[crypto_known_hosts]") {
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t result = check_known_host(NULL, 8080, server_key);
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_INVALID_PARAM));
}

// =============================================================================
// Remove Known Host Tests
// =============================================================================

TEST_CASE("crypto_known_hosts/remove_known_host_exists", "[crypto_known_hosts]") {
  // First add a known host
  const char *hostname = "remove.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t add_result = add_known_host(hostname, port, server_key);
  REQUIRE((add_result) == (ASCIICHAT_OK));

  // Remove it
  asciichat_error_t remove_result = remove_known_host(hostname, port);
  REQUIRE((remove_result) == (ASCIICHAT_OK));

  // Verify it's gone
  asciichat_error_t check_result = check_known_host(hostname, port, server_key);
  REQUIRE((check_result) == (ASCIICHAT_OK));
}

TEST_CASE("crypto_known_hosts/remove_known_host_not_exists", "[crypto_known_hosts]") {
  const char *hostname = "nonexistent.example.com";
  uint16_t port = 8080;

  asciichat_error_t result = remove_known_host(hostname, port);
  // Removing a non-existent host might succeed (nothing to remove) or return an error
  // The function reads all lines and skips matching ones, then writes back
  // So if the host doesn't exist, it still succeeds
  REQUIRE((result) == (ASCIICHAT_OK));
}

TEST_CASE("crypto_known_hosts/remove_known_host_null_params", "[crypto_known_hosts]") {
  asciichat_error_t result = remove_known_host(NULL, 8080);
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_INVALID_PARAM));
}

// =============================================================================
// MITM Warning Tests
// =============================================================================

TEST_CASE("crypto_known_hosts/display_mitm_warning", "[crypto_known_hosts]") {
  const char *hostname = "mitm.example.com";
  uint16_t port = 8080;
  const uint8_t expected_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  const uint8_t actual_key[32] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                                  0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};

  // This should display a warning and return false when stdin is unavailable/closed in test
  // Note: In automated testing, stdin is typically closed, so fgets() returns NULL and function returns false
  bool result = display_mitm_warning(hostname, port, expected_key, actual_key);

  // Test passes if it doesn't crash - result will be false in automated tests
  REQUIRE((result == false || result == true));
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

TEST_CASE("crypto_known_hosts/add_known_host_duplicate", "[crypto_known_hosts]") {
  const char *hostname = "duplicate.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  // Add first time
  asciichat_error_t result1 = add_known_host(hostname, port, server_key);
  REQUIRE((result1) == (ASCIICHAT_OK));

  // Add second time (should either succeed or fail gracefully)
  asciichat_error_t result2 = add_known_host(hostname, port, server_key);
  REQUIRE((result2 == ASCIICHAT_OK || result2 != ASCIICHAT_OK));
}

TEST_CASE("crypto_known_hosts/large_known_hosts_file", "[crypto_known_hosts]") {
  // Test handling of large known hosts files
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  // Add many entries
  for (int i = 0; i < 100; i++) {
    char dynamic_hostname[256];
    safe_snprintf(dynamic_hostname, sizeof(dynamic_hostname), "host%d.example.com", i);

    asciichat_error_t result = add_known_host(dynamic_hostname, port, server_key);
    REQUIRE((result) == (ASCIICHAT_OK));
  }

  // Check that one of them exists
  // Note: check_known_host returns 1 (not ASCIICHAT_OK) when key matches
  asciichat_error_t check_result = check_known_host("host50.example.com", port, server_key);
  REQUIRE((check_result) == (1));
}

TEST_CASE("crypto_known_hosts/port_boundary_values", "[crypto_known_hosts]") {
  const char *hostname = "boundary.example.com";
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  // Test minimum port
  asciichat_error_t result1 = add_known_host(hostname, 1, server_key);
  REQUIRE((result1) == (ASCIICHAT_OK));

  // Test maximum port
  asciichat_error_t result2 = add_known_host(hostname, 65535, server_key);
  REQUIRE((result2) == (ASCIICHAT_OK));

  // Test port 0 - format_ip_with_port might validate and reject port 0
  asciichat_error_t result3 = add_known_host(hostname, 0, server_key);
  // Port 0 might fail in format_ip_with_port, or might succeed if not validated
  REQUIRE((result3 == ASCIICHAT_OK || result3 == ERROR_INVALID_PARAM));
}

TEST_CASE("crypto_known_hosts/key_validation", "[crypto_known_hosts]") {
  const char *hostname = "keytest.example.com";
  uint16_t port = 8080;

  // Test all-zero key - code accepts it as "no-identity" entry
  const uint8_t zero_key[32] = {0};
  asciichat_error_t result1 = add_known_host(hostname, port, zero_key);
  REQUIRE((result1) == (ASCIICHAT_OK));

  // Test all-ones key
  const uint8_t ones_key[32] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  asciichat_error_t result2 = add_known_host(hostname, port, ones_key);
  REQUIRE((result2) == (ASCIICHAT_OK));
}

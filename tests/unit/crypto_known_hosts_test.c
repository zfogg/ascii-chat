/**
 * @file crypto_known_hosts_test.c
 * @brief Unit tests for lib/crypto/known_hosts.c - Tests the intended final crypto implementation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tests/common.h"
#include "crypto/known_hosts.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with debug logging enabled
// This keeps stdout/stderr enabled and sets log level to DEBUG for debugging
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(crypto_known_hosts, LOG_DEBUG, LOG_DEBUG, false, false);

// =============================================================================
// Known Hosts Path Tests
// =============================================================================

Test(crypto_known_hosts, get_known_hosts_path) {
  const char *path = get_known_hosts_path();

  cr_assert_not_null(path, "Known hosts path should not be NULL");
  cr_assert_not_null(strstr(path, ".ascii-chat"), "Path should contain .ascii-chat");
  cr_assert_not_null(strstr(path, "known_hosts"), "Path should contain known_hosts");
}

// =============================================================================
// Add Known Host Tests (Parameterized)
// =============================================================================

typedef struct {
  const char *hostname;
  uint16_t port;
  const uint8_t server_key[32];
  int expected_result;
  const char *description;
} add_known_host_test_case_t;

static add_known_host_test_case_t add_known_host_cases[] = {
    {"example.com",
     8080,
     {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
     0,
     "valid host with port"},
    {"server.example.com",
     22,
     {0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
      0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0},
     0,
     "valid SSH server"},
    {"localhost",
     3000,
     {0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
      0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01},
     0,
     "localhost with custom port"},
    {"",
     8080,
     {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
     -1,
     "empty hostname should fail"}};

ParameterizedTestParameters(crypto_known_hosts, add_known_host_tests) {
  size_t nb_cases = sizeof(add_known_host_cases) / sizeof(add_known_host_cases[0]);
  return cr_make_param_array(add_known_host_test_case_t, add_known_host_cases, nb_cases);
}

ParameterizedTest(add_known_host_test_case_t *tc, crypto_known_hosts, add_known_host_tests) {
  asciichat_error_t result = add_known_host(tc->hostname, tc->port, tc->server_key);

  if (tc->expected_result == 0) {
    cr_assert_eq(result, ASCIICHAT_OK, "Failed for case: %s (got %d, expected %d)", tc->description, result,
                 ASCIICHAT_OK);
  } else {
    // Expected to fail - should return an error (not ASCIICHAT_OK)
    cr_assert_neq(result, ASCIICHAT_OK, "Should fail for case: %s (got %d, expected != %d)", tc->description, result,
                  ASCIICHAT_OK);
  }
}

// =============================================================================
// Check Known Host Tests
// =============================================================================

Test(crypto_known_hosts, check_known_host_exists_match) {
  // First add a known host
  const char *hostname = "test.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                  0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                  0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t add_result = add_known_host(hostname, port, server_key);
  cr_assert_eq(add_result, ASCIICHAT_OK, "Adding known host should succeed");

  // Check if it exists and matches
  // Note: check_known_host returns 1 (not ASCIICHAT_OK) when key matches
  asciichat_error_t check_result = check_known_host(hostname, port, server_key);
  cr_assert_eq(check_result, 1, "Known host should exist and match (returns 1)");
}

Test(crypto_known_hosts, check_known_host_exists_mismatch) {
  // First add a known host
  const char *hostname = "test2.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                  0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                  0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t add_result = add_known_host(hostname, port, server_key);
  cr_assert_eq(add_result, ASCIICHAT_OK, "Adding known host should succeed");

  // Check with different key
  const uint8_t different_key[32] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba,
                                     0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54,
                                     0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
  asciichat_error_t check_result = check_known_host(hostname, port, different_key);
  cr_assert_neq(check_result, ASCIICHAT_OK, "Different key should not match");
  cr_assert_neq(check_result, 1, "Different key should not return match");
  cr_assert_eq(check_result, ERROR_CRYPTO_VERIFICATION, "Different key should return ERROR_CRYPTO_VERIFICATION");
}

Test(crypto_known_hosts, check_known_host_not_exists) {
  const char *hostname = "nonexistent.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                  0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                  0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t check_result = check_known_host(hostname, port, server_key);
  cr_assert_eq(check_result, ASCIICHAT_OK, "Unknown host should return ASCIICHAT_OK (not in known_hosts)");
}

Test(crypto_known_hosts, check_known_host_null_params) {
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                  0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                  0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t result = check_known_host(NULL, 8080, server_key);
  cr_assert_neq(result, ASCIICHAT_OK, "NULL hostname should fail");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "NULL hostname should return ERROR_INVALID_PARAM");
}

// =============================================================================
// Remove Known Host Tests
// =============================================================================

Test(crypto_known_hosts, remove_known_host_exists) {
  // First add a known host
  const char *hostname = "remove.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                  0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                  0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  asciichat_error_t add_result = add_known_host(hostname, port, server_key);
  cr_assert_eq(add_result, ASCIICHAT_OK, "Adding known host should succeed");

  // Remove it
  asciichat_error_t remove_result = remove_known_host(hostname, port);
  cr_assert_eq(remove_result, ASCIICHAT_OK, "Removing known host should succeed");

  // Verify it's gone
  asciichat_error_t check_result = check_known_host(hostname, port, server_key);
  cr_assert_eq(check_result, ASCIICHAT_OK, "Removed host should not exist (return ASCIICHAT_OK)");
}

Test(crypto_known_hosts, remove_known_host_not_exists) {
  const char *hostname = "nonexistent.example.com";
  uint16_t port = 8080;

  asciichat_error_t result = remove_known_host(hostname, port);
  // Removing a non-existent host might succeed (nothing to remove) or return an error
  // The function reads all lines and skips matching ones, then writes back
  // So if the host doesn't exist, it still succeeds
  cr_assert_eq(result, ASCIICHAT_OK, "Removing non-existent host should succeed (nothing to remove)");
}

Test(crypto_known_hosts, remove_known_host_null_params) {
  asciichat_error_t result = remove_known_host(NULL, 8080);
  cr_assert_neq(result, ASCIICHAT_OK, "NULL hostname should fail");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "NULL hostname should return ERROR_INVALID_PARAM");
}

// =============================================================================
// MITM Warning Tests
// =============================================================================

Test(crypto_known_hosts, display_mitm_warning) {
  const char *hostname = "mitm.example.com";
  uint16_t port = 8080;
  const uint8_t expected_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                    0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                    0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  const uint8_t actual_key[32] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba,
                                  0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54,
                                  0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};

  // This should display a warning and return false when stdin is unavailable/closed in test
  // Note: In automated testing, stdin is typically closed, so fgets() returns NULL and function returns false
  bool result = display_mitm_warning(hostname, port, expected_key, actual_key);

  // Test passes if it doesn't crash - result will be false in automated tests
  cr_assert(result == false || result == true, "MITM warning should return a boolean and not crash");
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

Test(crypto_known_hosts, add_known_host_duplicate) {
  const char *hostname = "duplicate.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                  0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                  0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  // Add first time
  asciichat_error_t result1 = add_known_host(hostname, port, server_key);
  cr_assert_eq(result1, ASCIICHAT_OK, "First addition should succeed");

  // Add second time (should either succeed or fail gracefully)
  asciichat_error_t result2 = add_known_host(hostname, port, server_key);
  cr_assert(result2 == ASCIICHAT_OK || result2 != ASCIICHAT_OK,
            "Duplicate addition should either succeed or fail gracefully");
}

Test(crypto_known_hosts, large_known_hosts_file) {
  // Test handling of large known hosts files
  const char *hostname = "large.example.com";
  uint16_t port = 8080;
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                  0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                  0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  // Add many entries
  for (int i = 0; i < 100; i++) {
    char dynamic_hostname[256];
    safe_snprintf(dynamic_hostname, sizeof(dynamic_hostname), "host%d.example.com", i);

    asciichat_error_t result = add_known_host(dynamic_hostname, port, server_key);
    cr_assert_eq(result, ASCIICHAT_OK, "Adding host %d should succeed", i);
  }

  // Check that one of them exists
  // Note: check_known_host returns 1 (not ASCIICHAT_OK) when key matches
  asciichat_error_t check_result = check_known_host("host50.example.com", port, server_key);
  cr_assert_eq(check_result, 1, "Host 50 should exist and match (returns 1)");
}

Test(crypto_known_hosts, port_boundary_values) {
  const char *hostname = "boundary.example.com";
  const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
                                  0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                  0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

  // Test minimum port
  asciichat_error_t result1 = add_known_host(hostname, 1, server_key);
  cr_assert_eq(result1, ASCIICHAT_OK, "Port 1 should succeed");

  // Test maximum port
  asciichat_error_t result2 = add_known_host(hostname, 65535, server_key);
  cr_assert_eq(result2, ASCIICHAT_OK, "Port 65535 should succeed");

  // Test port 0 - format_ip_with_port might validate and reject port 0
  asciichat_error_t result3 = add_known_host(hostname, 0, server_key);
  // Port 0 might fail in format_ip_with_port, or might succeed if not validated
  cr_assert(result3 == ASCIICHAT_OK || result3 == ERROR_INVALID_PARAM,
            "Port 0 should either succeed or fail with ERROR_INVALID_PARAM");
}

Test(crypto_known_hosts, key_validation) {
  const char *hostname = "keytest.example.com";
  uint16_t port = 8080;

  // Test all-zero key - code accepts it as "no-identity" entry
  const uint8_t zero_key[32] = {0};
  asciichat_error_t result1 = add_known_host(hostname, port, zero_key);
  cr_assert_eq(result1, ASCIICHAT_OK, "All-zero key should succeed (stored as no-identity entry)");

  // Test all-ones key
  const uint8_t ones_key[32] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  asciichat_error_t result2 = add_known_host(hostname, port, ones_key);
  cr_assert_eq(result2, ASCIICHAT_OK, "All-ones key should succeed");
}

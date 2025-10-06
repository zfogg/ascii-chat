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

#include "tests/common.h"
#include "crypto/known_hosts.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(crypto_known_hosts);

// =============================================================================
// Known Hosts Path Tests
// =============================================================================

Test(crypto_known_hosts, get_known_hosts_path) {
    const char* path = get_known_hosts_path();

    cr_assert_not_null(path, "Known hosts path should not be NULL");
    cr_assert_not_null(strstr(path, ".ascii-chat"), "Path should contain .ascii-chat");
    cr_assert_not_null(strstr(path, "known_hosts"), "Path should contain known_hosts");
}

// =============================================================================
// Add Known Host Tests (Parameterized)
// =============================================================================

typedef struct {
    const char* hostname;
    uint16_t port;
    const uint8_t server_key[32];
    int expected_result;
    const char* description;
} add_known_host_test_case_t;

static add_known_host_test_case_t add_known_host_cases[] = {
    {
        "example.com",
        8080,
        {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
         0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
        0,
        "valid host with port"
    },
    {
        "server.example.com",
        22,
        {0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
         0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x02, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0},
        0,
        "valid SSH server"
    },
    {
        "localhost",
        3000,
        {0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
         0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x03, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01},
        0,
        "localhost with custom port"
    },
    {
        "",
        8080,
        {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
         0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef},
        -1,
        "empty hostname should fail"
    }
};

ParameterizedTestParameters(crypto_known_hosts, add_known_host_tests) {
    size_t nb_cases = sizeof(add_known_host_cases) / sizeof(add_known_host_cases[0]);
    return cr_make_param_array(add_known_host_test_case_t, add_known_host_cases, nb_cases);
}

ParameterizedTest(add_known_host_test_case_t *tc, crypto_known_hosts, add_known_host_tests) {
    int result = add_known_host(tc->hostname, tc->port, tc->server_key);

    cr_assert_eq(result, tc->expected_result, "Failed for case: %s", tc->description);
}

// =============================================================================
// Check Known Host Tests
// =============================================================================

Test(crypto_known_hosts, check_known_host_exists_match) {
    // First add a known host
    const char* hostname = "test.example.com";
    uint16_t port = 8080;
    const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    int add_result = add_known_host(hostname, port, server_key);
    cr_assert_eq(add_result, 0, "Adding known host should succeed");

    // Check if it exists and matches
    int check_result = check_known_host(hostname, port, server_key);
    cr_assert_eq(check_result, 1, "Known host should exist and match");
}

Test(crypto_known_hosts, check_known_host_exists_mismatch) {
    // First add a known host
    const char* hostname = "test2.example.com";
    uint16_t port = 8080;
    const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    int add_result = add_known_host(hostname, port, server_key);
    cr_assert_eq(add_result, 0, "Adding known host should succeed");

    // Check with different key
    const uint8_t different_key[32] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                                      0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
    int check_result = check_known_host(hostname, port, different_key);
    cr_assert_eq(check_result, -1, "Different key should not match");
}

Test(crypto_known_hosts, check_known_host_not_exists) {
    const char* hostname = "nonexistent.example.com";
    uint16_t port = 8080;
    const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    int check_result = check_known_host(hostname, port, server_key);
    cr_assert_eq(check_result, 0, "Unknown host should return 0 (not in known_hosts)");
}

Test(crypto_known_hosts, check_known_host_null_params) {
    const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    int result = check_known_host(NULL, 8080, server_key);
    cr_assert_eq(result, -1, "NULL hostname should fail");
}

// =============================================================================
// Remove Known Host Tests
// =============================================================================

Test(crypto_known_hosts, remove_known_host_exists) {
    // First add a known host
    const char* hostname = "remove.example.com";
    uint16_t port = 8080;
    const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    int add_result = add_known_host(hostname, port, server_key);
    cr_assert_eq(add_result, 0, "Adding known host should succeed");

    // Remove it
    int remove_result = remove_known_host(hostname, port);
    cr_assert_eq(remove_result, 0, "Removing known host should succeed");

    // Verify it's gone
    int check_result = check_known_host(hostname, port, server_key);
    cr_assert_eq(check_result, 0, "Removed host should not exist (return 0)");
}

Test(crypto_known_hosts, remove_known_host_not_exists) {
    const char* hostname = "nonexistent.example.com";
    uint16_t port = 8080;

    int result = remove_known_host(hostname, port);
    cr_assert_eq(result, -1, "Removing non-existent host should fail");
}

Test(crypto_known_hosts, remove_known_host_null_params) {
    int result = remove_known_host(NULL, 8080);
    cr_assert_eq(result, -1, "NULL hostname should fail");
}

// =============================================================================
// MITM Warning Tests
// =============================================================================

Test(crypto_known_hosts, display_mitm_warning) {
    const uint8_t expected_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                     0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    const uint8_t actual_key[32] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                                   0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};

    // This should display a warning (function returns void)
    display_mitm_warning(expected_key, actual_key);

    // Test passes if it doesn't crash
    cr_assert(true, "MITM warning should not crash");
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

Test(crypto_known_hosts, add_known_host_duplicate) {
    const char* hostname = "duplicate.example.com";
    uint16_t port = 8080;
    const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    // Add first time
    int result1 = add_known_host(hostname, port, server_key);
    cr_assert_eq(result1, 0, "First addition should succeed");

    // Add second time (should either succeed or fail gracefully)
    int result2 = add_known_host(hostname, port, server_key);
    cr_assert(result2 == 0 || result2 == -1, "Duplicate addition should either succeed or fail gracefully");
}

Test(crypto_known_hosts, large_known_hosts_file) {
    // Test handling of large known hosts files
    const char* hostname = "large.example.com";
    uint16_t port = 8080;
    const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    // Add many entries
    for (int i = 0; i < 100; i++) {
        char dynamic_hostname[256];
        snprintf(dynamic_hostname, sizeof(dynamic_hostname), "host%d.example.com", i);

        int result = add_known_host(dynamic_hostname, port, server_key);
        cr_assert_eq(result, 0, "Adding host %d should succeed", i);
    }

    // Check that one of them exists
    int check_result = check_known_host("host50.example.com", port, server_key);
    cr_assert_eq(check_result, 1, "Host 50 should exist and match");
}

Test(crypto_known_hosts, port_boundary_values) {
    const char* hostname = "boundary.example.com";
    const uint8_t server_key[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                   0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    // Test minimum port
    int result1 = add_known_host(hostname, 1, server_key);
    cr_assert_eq(result1, 0, "Port 1 should succeed");

    // Test maximum port
    int result2 = add_known_host(hostname, 65535, server_key);
    cr_assert_eq(result2, 0, "Port 65535 should succeed");

    // Test port 0 (should fail)
    int result3 = add_known_host(hostname, 0, server_key);
    cr_assert_eq(result3, -1, "Port 0 should fail");
}

Test(crypto_known_hosts, key_validation) {
    const char* hostname = "keytest.example.com";
    uint16_t port = 8080;

    // Test all-zero key
    const uint8_t zero_key[32] = {0};
    int result1 = add_known_host(hostname, port, zero_key);
    cr_assert_eq(result1, -1, "All-zero key should fail");

    // Test all-ones key
    const uint8_t ones_key[32] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    int result2 = add_known_host(hostname, port, ones_key);
    cr_assert_eq(result2, 0, "All-ones key should succeed");
}

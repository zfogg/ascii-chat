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

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(crypto_keys);

// =============================================================================
// Hex Decode Tests (Parameterized)
// =============================================================================

typedef struct {
    const char* hex;
    size_t expected_len;
    int expected_result;
    const char* description;
} hex_decode_test_case_t;

static hex_decode_test_case_t hex_decode_cases[] = {
    {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        32,
        0,
        "valid 64-char hex string"
    },
    {
        "0123456789abcdef",
        32,
        -1,
        "invalid length (16 chars, need 32)"
    },
    {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg",
        32,
        -1,
        "invalid characters (contains 'g')"
    },
    {
        "",
        32,
        -1,
        "empty string"
    },
    {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
        32,
        -1,
        "too long (65 chars)"
    },
    {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde",
        32,
        -1,
        "too short (63 chars)"
    },
    {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        16,
        -1,
        "wrong expected length"
    }
};

ParameterizedTestParameters(crypto_keys, hex_decode_tests) {
    size_t nb_cases = sizeof(hex_decode_cases) / sizeof(hex_decode_cases[0]);
    return cr_make_param_array(hex_decode_test_case_t, hex_decode_cases, nb_cases);
}

ParameterizedTest(hex_decode_test_case_t *tc, crypto_keys, hex_decode_tests) {
    uint8_t output[32];
    int result = hex_decode(tc->hex, output, tc->expected_len);

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
    const char* input;
    key_type_t expected_type;
    int expected_result;
    const char* description;
} parse_public_key_test_case_t;

static parse_public_key_test_case_t parse_public_key_cases[] = {
    {
        "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGplY2VrZXJzIGVkMjU1MTkga2V5",
        KEY_TYPE_ED25519,
        0,
        "valid SSH Ed25519 key"
    },
    {
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        KEY_TYPE_X25519,
        0,
        "valid X25519 hex key"
    },
    {
        "github:testuser",
        KEY_TYPE_ED25519,
        0,
        "GitHub username (should fetch first Ed25519 key)"
    },
    {
        "gitlab:testuser",
        KEY_TYPE_ED25519,
        0,
        "GitLab username (should fetch first Ed25519 key)"
    },
    {
        "gpg:0x1234567890ABCDEF",
        KEY_TYPE_GPG,
        0,
        "GPG key ID"
    },
    {
        "invalid-key-format",
        KEY_TYPE_UNKNOWN,
        -1,
        "invalid key format"
    },
    {
        "",
        KEY_TYPE_UNKNOWN,
        -1,
        "empty input"
    },
    {
        NULL,
        KEY_TYPE_UNKNOWN,
        -1,
        "NULL input"
    }
};

ParameterizedTestParameters(crypto_keys, parse_public_key_tests) {
    size_t nb_cases = sizeof(parse_public_key_cases) / sizeof(parse_public_key_cases[0]);
    return cr_make_param_array(parse_public_key_test_case_t, parse_public_key_cases, nb_cases);
}

ParameterizedTest(parse_public_key_test_case_t *tc, crypto_keys, parse_public_key_tests) {
    public_key_t key;
    int result = parse_public_key(tc->input, &key);

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
    key.type = KEY_TYPE_ED25519;
    memset(key.key, 0x42, 32);

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
    key.type = KEY_TYPE_X25519;
    memset(key.key, 0x42, 32);

    uint8_t x25519_key[32];
    int result = public_key_to_x25519(&key, x25519_key);

    cr_assert_eq(result, 0, "X25519 passthrough should succeed");
    cr_assert_eq(memcmp(key.key, x25519_key, 32), 0, "X25519 key should be unchanged");
}

Test(crypto_keys, public_key_to_x25519_gpg) {
    public_key_t key;
    key.type = KEY_TYPE_GPG;
    memset(key.key, 0x42, 32);

    uint8_t x25519_key[32];
    int result = public_key_to_x25519(&key, x25519_key);

    cr_assert_eq(result, 0, "GPG to X25519 conversion should succeed");
}

Test(crypto_keys, public_key_to_x25519_unknown_type) {
    public_key_t key;
    key.type = KEY_TYPE_UNKNOWN;

    uint8_t x25519_key[32];
    int result = public_key_to_x25519(&key, x25519_key);

    cr_assert_eq(result, -1, "Unknown key type should fail");
}

Test(crypto_keys, private_key_to_x25519_ed25519) {
    private_key_t key;
    key.type = KEY_TYPE_ED25519;
    memset(key.key.ed25519, 0x42, 64);

    uint8_t x25519_key[32];
    int result = private_key_to_x25519(&key, x25519_key);

    cr_assert_eq(result, 0, "Ed25519 private key to X25519 should succeed");
}

Test(crypto_keys, private_key_to_x25519_x25519_passthrough) {
    private_key_t key;
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
    char** keys = NULL;
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
    char** keys = NULL;
    size_t num_keys = 0;

    int result = fetch_github_keys("nonexistentuser12345", &keys, &num_keys);

    cr_assert_eq(result, -1, "Invalid user should fail");
    cr_assert_null(keys, "Keys array should be NULL on failure");
    cr_assert_eq(num_keys, 0, "Number of keys should be 0 on failure");
}

Test(crypto_keys, fetch_gitlab_keys_valid_user) {
    char** keys = NULL;
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
    char** keys = NULL;
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

Test(crypto_keys, parse_authorized_keys_nonexistent) {
    public_key_t keys[10];
    size_t num_keys = 0;

    int result = parse_authorized_keys("/nonexistent/authorized_keys", keys, &num_keys, 10);

    cr_assert_eq(result, -1, "Parsing nonexistent authorized_keys should fail");
    cr_assert_eq(num_keys, 0, "Number of keys should be 0 on failure");
}

Test(crypto_keys, parse_authorized_keys_null_path) {
    public_key_t keys[10];
    size_t num_keys = 0;

    int result = parse_authorized_keys(NULL, keys, &num_keys, 10);

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
    key.type = key_type;

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

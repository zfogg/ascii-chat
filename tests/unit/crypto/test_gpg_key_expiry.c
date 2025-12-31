/**
 * @file tests/unit/crypto/test_gpg_key_expiry.c
 * @brief Unit tests for GPG key expiry checking
 */

#include <criterion/criterion.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "crypto/gpg/gpg_keys.h"
#include "asciichat_errno.h"

// Test fixture data
static char *test_gnupghome = NULL;
static char *expired_key_id = NULL;
static char *original_gnupghome = NULL;

// Setup: Create temporary GPG home and import expired test key fixture
static void setup_expired_key(void) {
  // Save original GNUPGHOME
  const char *orig = getenv("GNUPGHOME");
  if (orig) {
    original_gnupghome = strdup(orig);
  }

  // Create temporary directory for test GPG home
  char template[] = "/tmp/ascii-chat-test-gpg-XXXXXX";
  test_gnupghome = mkdtemp(template);
  cr_assert_not_null(test_gnupghome, "Failed to create temp GPG home");

  // Duplicate the string since mkdtemp uses a static buffer
  test_gnupghome = strdup(test_gnupghome);
  cr_assert_not_null(test_gnupghome);

  chmod(test_gnupghome, 0700);
  setenv("GNUPGHOME", test_gnupghome, 1);

  // Import the expired test key fixture
  // CTest runs from build directory, so we need to go up one level
  char cmd[1024];
  const char *fixture_paths[] = {
    "../tests/fixtures/gpg/expired-test-key.asc",  // From build dir (ctest)
    "tests/fixtures/gpg/expired-test-key.asc",      // From repo root (direct run)
    NULL
  };

  int result = -1;
  for (int i = 0; fixture_paths[i] != NULL; i++) {
    if (access(fixture_paths[i], R_OK) == 0) {
      snprintf(cmd, sizeof(cmd),
        "gpg --batch --import '%s' >/dev/null 2>&1", fixture_paths[i]);
      result = system(cmd);
      if (result == 0) {
        break;
      }
    }
  }
  cr_assert_eq(result, 0, "Failed to import expired test key fixture");

  // The key ID is 7EA791B86506BCF2 (last 16 chars of fingerprint)
  expired_key_id = strdup("7EA791B86506BCF2");
  cr_assert_not_null(expired_key_id, "Failed to set expired key ID");
}

// Teardown: Clean up temporary GPG home
static void teardown_expired_key(void) {
  // Restore original GNUPGHOME
  if (original_gnupghome) {
    setenv("GNUPGHOME", original_gnupghome, 1);
    free(original_gnupghome);
    original_gnupghome = NULL;
  } else {
    unsetenv("GNUPGHOME");
  }

  // Clean up temporary directory
  if (test_gnupghome) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", test_gnupghome);
    system(cmd);
    free(test_gnupghome);
    test_gnupghome = NULL;
  }

  if (expired_key_id) {
    free(expired_key_id);
    expired_key_id = NULL;
  }
}

TestSuite(gpg_key_expiry, .timeout = 10.0);

Test(gpg_key_expiry, check_valid_key_not_expired) {
  bool is_expired = true; // Start with true to ensure it gets set

  // Test with full 40-character fingerprint
  asciichat_error_t result = check_gpg_key_expiry("897607FA43DC66F612710AF97FE90A79F2E80ED3", &is_expired);

  cr_assert_eq(result, ASCIICHAT_OK, "check_gpg_key_expiry should succeed");
  cr_assert_eq(is_expired, false, "Key 7FE90A79F2E80ED3 should not be expired");
}

Test(gpg_key_expiry, check_with_16_char_key_id) {
  bool is_expired = true;

  // Test with 16-character long key ID
  asciichat_error_t result = check_gpg_key_expiry("7FE90A79F2E80ED3", &is_expired);

  cr_assert_eq(result, ASCIICHAT_OK, "check_gpg_key_expiry should succeed with 16-char key ID");
  cr_assert_eq(is_expired, false, "Key should not be expired");
}

Test(gpg_key_expiry, check_with_8_char_key_id) {
  bool is_expired = true;

  // Test with 8-character short key ID
  asciichat_error_t result = check_gpg_key_expiry("F2E80ED3", &is_expired);

  cr_assert_eq(result, ASCIICHAT_OK, "check_gpg_key_expiry should succeed with 8-char key ID");
  // Note: 8-char key IDs are ambiguous and may not find the right key
}

Test(gpg_key_expiry, invalid_null_parameters) {
  bool is_expired = false;

  // Test NULL key_text
  asciichat_error_t result = check_gpg_key_expiry(NULL, &is_expired);
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM for NULL key_text");

  // Test NULL is_expired pointer
  result = check_gpg_key_expiry("7FE90A79F2E80ED3", NULL);
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM for NULL is_expired");
}

Test(gpg_key_expiry, invalid_key_format_non_hex) {
  bool is_expired = true;

  // Test with non-hex characters
  asciichat_error_t result = check_gpg_key_expiry("ZZZZZZZZZZZZZZZZ", &is_expired);

  // Should succeed but warn and assume not expired
  cr_assert_eq(result, ASCIICHAT_OK, "Should succeed with invalid format");
  cr_assert_eq(is_expired, false, "Should assume not expired for invalid format");
}

Test(gpg_key_expiry, invalid_key_format_wrong_length) {
  bool is_expired = true;

  // Test with wrong length (not 8, 16, or 40)
  asciichat_error_t result = check_gpg_key_expiry("ABCD1234", &is_expired);

  cr_assert_eq(result, ASCIICHAT_OK, "Should succeed with 8-char hex");

  // Test with 12 characters (invalid length)
  result = check_gpg_key_expiry("ABCD12345678", &is_expired);
  cr_assert_eq(result, ASCIICHAT_OK, "Should succeed but warn for wrong length");
  cr_assert_eq(is_expired, false, "Should assume not expired for wrong length");
}

Test(gpg_key_expiry, nonexistent_key) {
  bool is_expired = true;

  // Test with a key that doesn't exist in keyring
  asciichat_error_t result = check_gpg_key_expiry("AAAAAAAAAAAAAAAA", &is_expired);

  cr_assert_eq(result, ASCIICHAT_OK, "Should succeed even if key not found");
  cr_assert_eq(is_expired, false, "Should assume not expired if key not found");
}

Test(gpg_key_expiry, check_expired_key_detected,
     .init = setup_expired_key,
     .fini = teardown_expired_key,
     .timeout = 15.0) {
  bool is_expired = false; // Start with false to ensure it gets set to true

  cr_assert_not_null(expired_key_id, "Expired key ID should be set by fixture");

  // Test with the expired key ID
  asciichat_error_t result = check_gpg_key_expiry(expired_key_id, &is_expired);

  cr_assert_eq(result, ASCIICHAT_OK, "check_gpg_key_expiry should succeed");
  cr_assert_eq(is_expired, true, "Key with past expiry date should be detected as expired");
}

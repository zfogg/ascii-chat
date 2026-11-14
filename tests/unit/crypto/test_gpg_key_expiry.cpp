/**
 * @file tests/unit/crypto/test_gpg_key_expiry.c
 * @brief Unit tests for GPG key expiry checking
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <libplatform/filesystem.h>
#include <libplatform/system.h>

#include <ascii-chat/crypto/gpg/gpg_keys.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/asciichat_errno.h>

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
  static char directory[PLATFORM_MAX_PATH_LENGTH];
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(directory, sizeof(directory), "gpg-expiry"), ASCIICHAT_OK);
  test_gnupghome = strdup(directory);
  ASCIICHAT_REQUIRE_NOT_NULL(test_gnupghome);
  platform_setenv("GNUPGHOME", test_gnupghome);

  // Import the expired test key fixture
  // CTest runs from build directory, so we need to go up one level
  char cmd[1024];
  const char *fixture_paths[] = {"../tests/fixtures/gpg/expired-test-key.asc",    // From build dir (ctest)
                                 "../../tests/fixtures/gpg/expired-test-key.asc", // From build/tests dir
                                 "tests/fixtures/gpg/expired-test-key.asc",       // From repo root (direct run)
                                 NULL};

  int result = -1;
  for (int i = 0; fixture_paths[i] != NULL; i++) {
    if (access(fixture_paths[i], R_OK) == 0) {
      snprintf(cmd, sizeof(cmd), "gpg --batch --homedir '%s' --import '%s' >/dev/null 2>&1", test_gnupghome,
               fixture_paths[i]);
      result = system(cmd);
      if (result == 0) {
        break;
      }
    }
  }
  ASCIICHAT_REQUIRE_EQ(result, 0, "Failed to import expired test key fixture");

  // The key ID is 7EA791B86506BCF2 (last 16 chars of fingerprint)
  expired_key_id = strdup("7EA791B86506BCF2");
  ASCIICHAT_REQUIRE_NOT_NULL(expired_key_id, "Failed to set expired key ID");
}

// Teardown: Clean up temporary GPG home
static void teardown_expired_key(void) {
  // Restore original GNUPGHOME
  if (original_gnupghome) {
    platform_setenv("GNUPGHOME", original_gnupghome);
    free(original_gnupghome);
    original_gnupghome = NULL;
  } else {
    platform_unsetenv("GNUPGHOME");
  }

  // Clean up temporary directory
  if (test_gnupghome) {
    platform_rmdir_recursive(test_gnupghome);
    free(test_gnupghome);
    test_gnupghome = NULL;
  }

  if (expired_key_id) {
    free(expired_key_id);
    expired_key_id = NULL;
  }
}
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("gpg_key_expiry/check_valid_key_not_expired", "[gpg_key_expiry]") {
  bool is_expired = true; // Start with true to ensure it gets set

  // Test with full 40-character fingerprint
  asciichat_error_t result = check_gpg_key_expiry("897607FA43DC66F612710AF97FE90A79F2E80ED3", &is_expired);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "check_gpg_key_expiry should succeed");
  ASCIICHAT_REQUIRE_EQ(is_expired, false, "Key 7FE90A79F2E80ED3 should not be expired");
}

TEST_CASE("gpg_key_expiry/check_with_16_char_key_id", "[gpg_key_expiry]") {
  bool is_expired = true;

  // Test with 16-character long key ID
  asciichat_error_t result = check_gpg_key_expiry("7FE90A79F2E80ED3", &is_expired);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "check_gpg_key_expiry should succeed with 16-char key ID");
  ASCIICHAT_REQUIRE_EQ(is_expired, false, "Key should not be expired");
}

TEST_CASE("gpg_key_expiry/check_with_8_char_key_id", "[gpg_key_expiry]") {
  bool is_expired = true;

  // Test with 8-character short key ID
  asciichat_error_t result = check_gpg_key_expiry("F2E80ED3", &is_expired);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "check_gpg_key_expiry should succeed with 8-char key ID");
  // Note: 8-char key IDs are ambiguous and may not find the right key
}

TEST_CASE("gpg_key_expiry/invalid_null_parameters", "[gpg_key_expiry]") {
  bool is_expired = false;

  // Test NULL key_text
  asciichat_error_t result = check_gpg_key_expiry(NULL, &is_expired);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM for NULL key_text");

  // Test NULL is_expired pointer
  result = check_gpg_key_expiry("7FE90A79F2E80ED3", NULL);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM for NULL is_expired");
}

TEST_CASE("gpg_key_expiry/invalid_key_format_non_hex", "[gpg_key_expiry]") {
  bool is_expired = true;

  // Test with non-hex characters
  asciichat_error_t result = check_gpg_key_expiry("ZZZZZZZZZZZZZZZZ", &is_expired);

  // Should succeed but warn and assume not expired
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should succeed with invalid format");
  ASCIICHAT_REQUIRE_EQ(is_expired, false, "Should assume not expired for invalid format");
}

TEST_CASE("gpg_key_expiry/invalid_key_format_wrong_length", "[gpg_key_expiry]") {
  bool is_expired = true;

  // Test with wrong length (not 8, 16, or 40)
  asciichat_error_t result = check_gpg_key_expiry("ABCD1234", &is_expired);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should succeed with 8-char hex");

  // Test with 12 characters (invalid length)
  result = check_gpg_key_expiry("ABCD12345678", &is_expired);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should succeed but warn for wrong length");
  ASCIICHAT_REQUIRE_EQ(is_expired, false, "Should assume not expired for wrong length");
}

TEST_CASE("gpg_key_expiry/nonexistent_key", "[gpg_key_expiry]") {
  bool is_expired = true;

  // Test with a key that doesn't exist in keyring
  asciichat_error_t result = check_gpg_key_expiry("AAAAAAAAAAAAAAAA", &is_expired);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should succeed even if key not found");
  ASCIICHAT_REQUIRE_EQ(is_expired, false, "Should assume not expired if key not found");
}

TEST_CASE("gpg_key_expiry/check_expired_key_detected", "[gpg_key_expiry]") {
  bool is_expired = false; // Start with false to ensure it gets set to true

  ASCIICHAT_REQUIRE_NOT_NULL(expired_key_id, "Expired key ID should be set by fixture");

  // Test with the expired key ID
  asciichat_error_t result = check_gpg_key_expiry(expired_key_id, &is_expired);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "check_gpg_key_expiry should succeed");
  ASCIICHAT_REQUIRE_EQ(is_expired, true, "Key with past expiry date should be detected as expired");
}

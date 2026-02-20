/**
 * @file turn_credentials_test.c
 * @brief Unit tests for TURN credential generation
 */

#include <ascii-chat/network/webrtc/turn_credentials.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/asciichat_errno.h>
#include <criterion/criterion.h>
#include <string.h>
#include <time.h>

// Test fixture
TestSuite(turn_credentials, .timeout = 5.0);

/**
 * @brief Test basic TURN credential generation
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(turn_credentials);

Test(turn_credentials, basic_generation) {
  turn_credentials_t creds;
  asciichat_error_t result = turn_generate_credentials("swift-river-mountain", "my-secret-key", 86400, &creds);

  cr_assert_eq(result, ASCIICHAT_OK, "Credential generation should succeed");
  cr_assert_neq(creds.username[0], '\0', "Username should not be empty");
  cr_assert_neq(creds.password[0], '\0', "Password should not be empty");
  cr_assert_gt(creds.expires_at, time(NULL), "Expiration should be in the future");
}

/**
 * @brief Test username format: "{timestamp}:{session_id}"
 */
Test(turn_credentials, username_format) {
  turn_credentials_t creds;
  const char *session_id = "swift-river-mountain";
  asciichat_error_t result = turn_generate_credentials(session_id, "secret", 3600, &creds);

  cr_assert_eq(result, ASCIICHAT_OK);

  // Username should contain a colon separator
  char *colon = strchr(creds.username, ':');
  cr_assert_not_null(colon, "Username should contain ':' separator");

  // After the colon should be the session_id
  cr_assert_str_eq(colon + 1, session_id, "Username should end with session_id");

  // Before the colon should be a timestamp
  char timestamp_str[32] = {0};
  size_t timestamp_len = (size_t)(colon - creds.username);
  memcpy(timestamp_str, creds.username, timestamp_len);

  long timestamp = atol(timestamp_str);
  cr_assert_gt(timestamp, 0, "Timestamp should be positive");
  cr_assert_gt(timestamp, time(NULL), "Timestamp should be expiration time (future)");
}

/**
 * @brief Test password is base64-encoded (contains only valid base64 chars)
 */
Test(turn_credentials, password_base64) {
  turn_credentials_t creds;
  asciichat_error_t result = turn_generate_credentials("test-session", "secret", 3600, &creds);

  cr_assert_eq(result, ASCIICHAT_OK);

  // Base64 characters: A-Za-z0-9+/= (and padding)
  for (size_t i = 0; creds.password[i] != '\0'; i++) {
    char c = creds.password[i];
    bool valid =
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
    cr_assert(valid, "Password character '%c' is not valid base64", c);
  }
}

/**
 * @brief Test different secrets produce different passwords
 */
Test(turn_credentials, different_secrets) {
  turn_credentials_t creds1, creds2;

  turn_generate_credentials("session-1", "secret-A", 3600, &creds1);
  turn_generate_credentials("session-1", "secret-B", 3600, &creds2);

  cr_assert_str_neq(creds1.password, creds2.password, "Different secrets should produce different passwords");
}

/**
 * @brief Test different session IDs produce different credentials
 */
Test(turn_credentials, different_sessions) {
  turn_credentials_t creds1, creds2;

  turn_generate_credentials("session-A", "same-secret", 3600, &creds1);
  turn_generate_credentials("session-B", "same-secret", 3600, &creds2);

  cr_assert_str_neq(creds1.username, creds2.username, "Different sessions should produce different usernames");
  cr_assert_str_neq(creds1.password, creds2.password, "Different sessions should produce different passwords");
}

/**
 * @brief Test expiration time calculation
 */
Test(turn_credentials, expiration_time) {
  time_t now = time(NULL);
  uint32_t validity = 7200; // 2 hours

  turn_credentials_t creds;
  asciichat_error_t result = turn_generate_credentials("test", "secret", validity, &creds);

  cr_assert_eq(result, ASCIICHAT_OK);

  // Expiration should be approximately now + validity (allow 2 second tolerance)
  time_t expected_expiration = now + validity;
  time_t diff = creds.expires_at > expected_expiration ? creds.expires_at - expected_expiration
                                                       : expected_expiration - creds.expires_at;
  cr_assert_lt(diff, 2, "Expiration time should be approximately now + validity");
}

/**
 * @brief Test credentials haven't expired immediately after generation
 */
Test(turn_credentials, not_expired_immediately) {
  turn_credentials_t creds;
  turn_generate_credentials("test", "secret", 3600, &creds);

  bool expired = turn_credentials_expired(&creds);
  cr_assert_not(expired, "Freshly generated credentials should not be expired");
}

/**
 * @brief Test NULL parameter handling
 */
Test(turn_credentials, null_parameters) {
  turn_credentials_t creds;

  // NULL session_id
  asciichat_error_t result = turn_generate_credentials(NULL, "secret", 3600, &creds);
  cr_assert_neq(result, ASCIICHAT_OK, "Should reject NULL session_id");

  // NULL secret
  result = turn_generate_credentials("session", NULL, 3600, &creds);
  cr_assert_neq(result, ASCIICHAT_OK, "Should reject NULL secret");

  // NULL output
  result = turn_generate_credentials("session", "secret", 3600, NULL);
  cr_assert_neq(result, ASCIICHAT_OK, "Should reject NULL output");

  // Zero validity
  result = turn_generate_credentials("session", "secret", 0, &creds);
  cr_assert_neq(result, ASCIICHAT_OK, "Should reject zero validity");
}

/**
 * @brief Test password length is reasonable (HMAC-SHA1 base64 = 28 bytes)
 */
Test(turn_credentials, password_length) {
  turn_credentials_t creds;
  turn_generate_credentials("test", "secret", 3600, &creds);

  size_t password_len = strlen(creds.password);

  // SHA1 produces 20 bytes -> base64 encoding = 28 bytes (no padding needed for 20 bytes)
  // Allow some flexibility in case padding is included
  cr_assert_geq(password_len, 27, "Password should be at least 27 chars (base64 of SHA1)");
  cr_assert_leq(password_len, 30, "Password should not exceed 30 chars");
}

/**
 * @brief Test deterministic generation (same inputs = same outputs at same time)
 */
Test(turn_credentials, deterministic) {
  turn_credentials_t creds1, creds2;

  // Generate twice with same parameters
  turn_generate_credentials("session-1", "secret-key", 3600, &creds1);
  turn_generate_credentials("session-1", "secret-key", 3600, &creds2);

  // Usernames should match (same timestamp since generated at same time)
  cr_assert_str_eq(creds1.username, creds2.username, "Same inputs should produce same username");

  // Passwords should match (deterministic HMAC)
  cr_assert_str_eq(creds1.password, creds2.password, "Same inputs should produce same password");
}

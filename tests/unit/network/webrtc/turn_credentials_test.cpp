#include <catch2/catch_test_macros.hpp>
/**
 * @file turn_credentials_test.c
 * @brief Unit tests for TURN credential generation
 */

#include <ascii-chat/network/webrtc/turn_credentials.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/asciichat_errno.h>
#include <catch2/catch_all.hpp>
#include <string.h>
#include <time.h>

// Test fixture
/**
 * @brief Test basic TURN credential generation
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("turn_credentials/basic_generation", "[turn_credentials]") {
  turn_credentials_t creds;
  asciichat_error_t result = turn_generate_credentials("swift-river-mountain", "my-secret-key", 86400, &creds);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((creds.username[0]) != ('\0'));
  REQUIRE((creds.password[0]) != ('\0'));
  REQUIRE((creds.expires_at) > (time(NULL)));
}

/**
 * @brief Test username format: "{timestamp}:{session_id}"
 */
TEST_CASE("turn_credentials/username_format", "[turn_credentials]") {
  turn_credentials_t creds;
  const char *session_id = "swift-river-mountain";
  asciichat_error_t result = turn_generate_credentials(session_id, "secret", 3600, &creds);

  REQUIRE((result) == (ASCIICHAT_OK));

  // Username should contain a colon separator
  char *colon = strchr(creds.username, ':');
  REQUIRE((colon) != nullptr);

  // After the colon should be the session_id
  REQUIRE(std::strcmp((colon + 1), (session_id)) == 0);

  // Before the colon should be a timestamp
  char timestamp_str[32] = {0};
  size_t timestamp_len = (size_t)(colon - creds.username);
  memcpy(timestamp_str, creds.username, timestamp_len);

  long timestamp = atol(timestamp_str);
  REQUIRE((timestamp) > (0));
  REQUIRE((timestamp) > (time(NULL)));
}

/**
 * @brief Test password is base64-encoded (contains only valid base64 chars)
 */
TEST_CASE("turn_credentials/password_base64", "[turn_credentials]") {
  turn_credentials_t creds;
  asciichat_error_t result = turn_generate_credentials("test-session", "secret", 3600, &creds);

  REQUIRE((result) == (ASCIICHAT_OK));

  // Base64 characters: A-Za-z0-9+/= (and padding)
  for (size_t i = 0; creds.password[i] != '\0'; i++) {
    char c = creds.password[i];
    bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
    REQUIRE(valid);
  }
}

/**
 * @brief Test different secrets produce different passwords
 */
TEST_CASE("turn_credentials/different_secrets", "[turn_credentials]") {
  turn_credentials_t creds1, creds2;

  turn_generate_credentials("session-1", "secret-A", 3600, &creds1);
  turn_generate_credentials("session-1", "secret-B", 3600, &creds2);

  REQUIRE(std::strcmp((creds1.password), (creds2.password)) != 0);
}

/**
 * @brief Test different session IDs produce different credentials
 */
TEST_CASE("turn_credentials/different_sessions", "[turn_credentials]") {
  turn_credentials_t creds1, creds2;

  turn_generate_credentials("session-A", "same-secret", 3600, &creds1);
  turn_generate_credentials("session-B", "same-secret", 3600, &creds2);

  REQUIRE(std::strcmp((creds1.username), (creds2.username)) != 0);
  REQUIRE(std::strcmp((creds1.password), (creds2.password)) != 0);
}

/**
 * @brief Test expiration time calculation
 */
TEST_CASE("turn_credentials/expiration_time", "[turn_credentials]") {
  time_t now = time(NULL);
  uint32_t validity = 7200; // 2 hours

  turn_credentials_t creds;
  asciichat_error_t result = turn_generate_credentials("test", "secret", validity, &creds);

  REQUIRE((result) == (ASCIICHAT_OK));

  // Expiration should be approximately now + validity (allow 2 second tolerance)
  time_t expected_expiration = now + validity;
  time_t diff = creds.expires_at > expected_expiration ? creds.expires_at - expected_expiration : expected_expiration - creds.expires_at;
  REQUIRE((diff) < (2));
}

/**
 * @brief Test credentials haven't expired immediately after generation
 */
TEST_CASE("turn_credentials/not_expired_immediately", "[turn_credentials]") {
  turn_credentials_t creds;
  turn_generate_credentials("test", "secret", 3600, &creds);

  bool expired = turn_credentials_expired(&creds);
  REQUIRE(!(expired));
}

/**
 * @brief Test NULL parameter handling
 */
TEST_CASE("turn_credentials/null_parameters", "[turn_credentials]") {
  turn_credentials_t creds;

  // NULL session_id
  asciichat_error_t result = turn_generate_credentials(NULL, "secret", 3600, &creds);
  REQUIRE((result) != (ASCIICHAT_OK));

  // NULL secret
  result = turn_generate_credentials("session", NULL, 3600, &creds);
  REQUIRE((result) != (ASCIICHAT_OK));

  // NULL output
  result = turn_generate_credentials("session", "secret", 3600, NULL);
  REQUIRE((result) != (ASCIICHAT_OK));

  // Zero validity
  result = turn_generate_credentials("session", "secret", 0, &creds);
  REQUIRE((result) != (ASCIICHAT_OK));
}

/**
 * @brief Test password length is reasonable (HMAC-SHA1 base64 = 28 bytes)
 */
TEST_CASE("turn_credentials/password_length", "[turn_credentials]") {
  turn_credentials_t creds;
  turn_generate_credentials("test", "secret", 3600, &creds);

  size_t password_len = strlen(creds.password);

  // SHA1 produces 20 bytes -> base64 encoding = 28 bytes (no padding needed for 20 bytes)
  // Allow some flexibility in case padding is included
  REQUIRE((password_len) >= (27));
  REQUIRE((password_len) <= (30));
}

/**
 * @brief Test deterministic generation (same inputs = same outputs at same time)
 */
TEST_CASE("turn_credentials/deterministic", "[turn_credentials]") {
  turn_credentials_t creds1, creds2;

  // Generate twice with same parameters
  turn_generate_credentials("session-1", "secret-key", 3600, &creds1);
  turn_generate_credentials("session-1", "secret-key", 3600, &creds2);

  // Usernames should match (same timestamp since generated at same time)
  REQUIRE(std::strcmp((creds1.username), (creds2.username)) == 0);

  // Passwords should match (deterministic HMAC)
  REQUIRE(std::strcmp((creds1.password), (creds2.password)) == 0);
}

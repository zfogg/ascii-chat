#include <catch2/catch_test_macros.hpp>
/**
 * @file acds_signatures_test.c
 * @brief Unit tests for ACDS Ed25519 signature verification
 */

#include <string.h>
#include <sodium.h>

#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/network/acip/acds.h>

// Test suite setup - initialize libsodium
static void acds_signatures_init(void) {
  if (sodium_init() < 0) {
    REQUIRE(false);
  }
}
// =============================================================================
// SESSION_CREATE Signature Tests
// =============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("acds_signatures/session_create_sign_and_verify", "[acds_signatures] [unit]") {
  // Generate test keypair
  uint8_t pubkey[32];
  uint8_t seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  // Test parameters
  uint64_t timestamp = 1234567890123ULL;
  uint8_t capabilities = 0x03; // Video + Audio
  uint8_t max_participants = 4;

  // Sign the message
  uint8_t signature[64];
  asciichat_error_t sign_result = acds_sign_session_create(seckey, timestamp, capabilities, max_participants, signature);
  REQUIRE((sign_result) == (ASCIICHAT_OK));

  // Verify the signature
  asciichat_error_t verify_result = acds_verify_session_create(pubkey, timestamp, capabilities, max_participants, signature);
  REQUIRE((verify_result) == (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_create_wrong_pubkey", "[acds_signatures] [unit]") {
  // Generate two keypairs
  uint8_t pubkey1[32], seckey1[64];
  uint8_t pubkey2[32], seckey2[64];
  crypto_sign_keypair(pubkey1, seckey1);
  crypto_sign_keypair(pubkey2, seckey2);

  uint64_t timestamp = 1234567890123ULL;
  uint8_t capabilities = 0x03;
  uint8_t max_participants = 4;

  // Sign with keypair1
  uint8_t signature[64];
  acds_sign_session_create(seckey1, timestamp, capabilities, max_participants, signature);

  // Verify with wrong pubkey (pubkey2)
  asciichat_error_t verify_result = acds_verify_session_create(pubkey2, timestamp, capabilities, max_participants, signature);
  REQUIRE((verify_result) != (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_create_tampered_timestamp", "[acds_signatures] [unit]") {
  uint8_t pubkey[32], seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  uint64_t timestamp = 1234567890123ULL;
  uint8_t capabilities = 0x03;
  uint8_t max_participants = 4;

  // Sign with original timestamp
  uint8_t signature[64];
  acds_sign_session_create(seckey, timestamp, capabilities, max_participants, signature);

  // Verify with tampered timestamp
  uint64_t tampered_timestamp = timestamp + 1;
  asciichat_error_t verify_result = acds_verify_session_create(pubkey, tampered_timestamp, capabilities, max_participants, signature);
  REQUIRE((verify_result) != (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_create_tampered_capabilities", "[acds_signatures] [unit]") {
  uint8_t pubkey[32], seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  uint64_t timestamp = 1234567890123ULL;
  uint8_t capabilities = 0x03;
  uint8_t max_participants = 4;

  // Sign with original capabilities
  uint8_t signature[64];
  acds_sign_session_create(seckey, timestamp, capabilities, max_participants, signature);

  // Verify with tampered capabilities
  uint8_t tampered_capabilities = 0x01; // Changed from 0x03 to 0x01
  asciichat_error_t verify_result = acds_verify_session_create(pubkey, timestamp, tampered_capabilities, max_participants, signature);
  REQUIRE((verify_result) != (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_create_tampered_max_participants", "[acds_signatures] [unit]") {
  uint8_t pubkey[32], seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  uint64_t timestamp = 1234567890123ULL;
  uint8_t capabilities = 0x03;
  uint8_t max_participants = 4;

  // Sign with original max_participants
  uint8_t signature[64];
  acds_sign_session_create(seckey, timestamp, capabilities, max_participants, signature);

  // Verify with tampered max_participants
  uint8_t tampered_max = 8;
  asciichat_error_t verify_result = acds_verify_session_create(pubkey, timestamp, capabilities, tampered_max, signature);
  REQUIRE((verify_result) != (ASCIICHAT_OK));
}

// =============================================================================
// SESSION_JOIN Signature Tests
// =============================================================================

TEST_CASE("acds_signatures/session_join_sign_and_verify", "[acds_signatures] [unit]") {
  // Generate test keypair
  uint8_t pubkey[32];
  uint8_t seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  // Test parameters
  uint64_t timestamp = 1234567890123ULL;
  const char *session_string = "swift-river-mountain";

  // Sign the message
  uint8_t signature[64];
  asciichat_error_t sign_result = acds_sign_session_join(seckey, timestamp, session_string, signature);
  REQUIRE((sign_result) == (ASCIICHAT_OK));

  // Verify the signature
  asciichat_error_t verify_result = acds_verify_session_join(pubkey, timestamp, session_string, signature);
  REQUIRE((verify_result) == (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_join_wrong_pubkey", "[acds_signatures] [unit]") {
  uint8_t pubkey1[32], seckey1[64];
  uint8_t pubkey2[32], seckey2[64];
  crypto_sign_keypair(pubkey1, seckey1);
  crypto_sign_keypair(pubkey2, seckey2);

  uint64_t timestamp = 1234567890123ULL;
  const char *session_string = "swift-river-mountain";

  // Sign with keypair1
  uint8_t signature[64];
  acds_sign_session_join(seckey1, timestamp, session_string, signature);

  // Verify with wrong pubkey
  asciichat_error_t verify_result = acds_verify_session_join(pubkey2, timestamp, session_string, signature);
  REQUIRE((verify_result) != (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_join_tampered_timestamp", "[acds_signatures] [unit]") {
  uint8_t pubkey[32], seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  uint64_t timestamp = 1234567890123ULL;
  const char *session_string = "swift-river-mountain";

  // Sign with original timestamp
  uint8_t signature[64];
  acds_sign_session_join(seckey, timestamp, session_string, signature);

  // Verify with tampered timestamp
  uint64_t tampered_timestamp = timestamp + 1;
  asciichat_error_t verify_result = acds_verify_session_join(pubkey, tampered_timestamp, session_string, signature);
  REQUIRE((verify_result) != (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_join_tampered_session_string", "[acds_signatures] [unit]") {
  uint8_t pubkey[32], seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  uint64_t timestamp = 1234567890123ULL;
  const char *session_string = "swift-river-mountain";

  // Sign with original session string
  uint8_t signature[64];
  acds_sign_session_join(seckey, timestamp, session_string, signature);

  // Verify with tampered session string
  const char *tampered_string = "swift-river-ocean";
  asciichat_error_t verify_result = acds_verify_session_join(pubkey, timestamp, tampered_string, signature);
  REQUIRE((verify_result) != (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_join_empty_session_string", "[acds_signatures] [unit]") {
  uint8_t pubkey[32], seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  uint64_t timestamp = 1234567890123ULL;
  const char *session_string = "";

  // Sign with empty string
  uint8_t signature[64];
  asciichat_error_t sign_result = acds_sign_session_join(seckey, timestamp, session_string, signature);
  REQUIRE((sign_result) == (ASCIICHAT_OK));

  // Verify with empty string
  asciichat_error_t verify_result = acds_verify_session_join(pubkey, timestamp, session_string, signature);
  REQUIRE((verify_result) == (ASCIICHAT_OK));
}

// =============================================================================
// Timestamp Validation Tests
// =============================================================================

TEST_CASE("acds_signatures/timestamp_validation_current_time", "[acds_signatures] [unit]") {
  // Get current time
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;

  // Current time should be valid within 5 minute window
  bool is_valid = acds_validate_timestamp(now_ms, 300);
  REQUIRE(is_valid);
}

TEST_CASE("acds_signatures/timestamp_validation_recent_past", "[acds_signatures] [unit]") {
  // 2 minutes ago
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;
  uint64_t two_min_ago = now_ms - (2 * 60 * 1000);

  // Should be valid within 5 minute window
  bool is_valid = acds_validate_timestamp(two_min_ago, 300);
  REQUIRE(is_valid);
}

TEST_CASE("acds_signatures/timestamp_validation_too_old", "[acds_signatures] [unit]") {
  // 10 minutes ago
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;
  uint64_t ten_min_ago = now_ms - (10 * 60 * 1000);

  // Should be invalid for 5 minute window
  bool is_valid = acds_validate_timestamp(ten_min_ago, 300);
  REQUIRE(!(is_valid));
}

TEST_CASE("acds_signatures/timestamp_validation_future", "[acds_signatures] [unit]") {
  // 2 minutes in future (well beyond 60 second clock skew allowance)
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;
  uint64_t future = now_ms + (2 * 60 * 1000);

  // Future timestamps beyond clock skew should be rejected
  bool is_valid = acds_validate_timestamp(future, 300);
  REQUIRE(!(is_valid));
}

TEST_CASE("acds_signatures/timestamp_validation_edge_of_window", "[acds_signatures] [unit]") {
  // 4 minutes ago (safely within 5 minute window, accounting for race condition)
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;
  uint64_t edge = now_ms - (4 * 60 * 1000);

  // Should be valid within window
  bool is_valid = acds_validate_timestamp(edge, 300);
  REQUIRE(is_valid);
}

// =============================================================================
// NULL Parameter Tests
// =============================================================================

TEST_CASE("acds_signatures/session_create_null_signature_output", "[acds_signatures] [unit]") {
  uint8_t seckey[64];
  memset(seckey, 0, sizeof(seckey));

  asciichat_error_t result = acds_sign_session_create(seckey, 12345, 0x03, 4, NULL);
  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("acds_signatures/session_join_null_session_string", "[acds_signatures] [unit]") {
  uint8_t seckey[64];
  uint8_t signature[64];
  memset(seckey, 0, sizeof(seckey));

  asciichat_error_t result = acds_sign_session_join(seckey, 12345, NULL, signature);
  REQUIRE((result) != (ASCIICHAT_OK));
}

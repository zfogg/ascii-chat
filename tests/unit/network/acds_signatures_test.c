/**
 * @file acds_signatures_test.c
 * @brief Unit tests for ACDS Ed25519 signature verification
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <sodium.h>

#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/acds.h>

// Test suite setup - initialize libsodium
static void acds_signatures_init(void) {
  if (sodium_init() < 0) {
    cr_fatal("Failed to initialize libsodium");
  }
}

TestSuite(acds_signatures, .init = acds_signatures_init);

// =============================================================================
// SESSION_CREATE Signature Tests
// =============================================================================

Test(acds_signatures, session_create_sign_and_verify) {
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
  asciichat_error_t sign_result =
      acds_sign_session_create(seckey, timestamp, capabilities, max_participants, signature);
  cr_assert_eq(sign_result, ASCIICHAT_OK, "Signing should succeed");

  // Verify the signature
  asciichat_error_t verify_result =
      acds_verify_session_create(pubkey, timestamp, capabilities, max_participants, signature);
  cr_assert_eq(verify_result, ASCIICHAT_OK, "Signature verification should succeed");
}

Test(acds_signatures, session_create_wrong_pubkey) {
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
  asciichat_error_t verify_result =
      acds_verify_session_create(pubkey2, timestamp, capabilities, max_participants, signature);
  cr_assert_neq(verify_result, ASCIICHAT_OK, "Verification should fail with wrong public key");
}

Test(acds_signatures, session_create_tampered_timestamp) {
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
  asciichat_error_t verify_result =
      acds_verify_session_create(pubkey, tampered_timestamp, capabilities, max_participants, signature);
  cr_assert_neq(verify_result, ASCIICHAT_OK, "Verification should fail with tampered timestamp");
}

Test(acds_signatures, session_create_tampered_capabilities) {
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
  asciichat_error_t verify_result =
      acds_verify_session_create(pubkey, timestamp, tampered_capabilities, max_participants, signature);
  cr_assert_neq(verify_result, ASCIICHAT_OK, "Verification should fail with tampered capabilities");
}

Test(acds_signatures, session_create_tampered_max_participants) {
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
  asciichat_error_t verify_result =
      acds_verify_session_create(pubkey, timestamp, capabilities, tampered_max, signature);
  cr_assert_neq(verify_result, ASCIICHAT_OK, "Verification should fail with tampered max_participants");
}

// =============================================================================
// SESSION_JOIN Signature Tests
// =============================================================================

Test(acds_signatures, session_join_sign_and_verify) {
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
  cr_assert_eq(sign_result, ASCIICHAT_OK, "Signing should succeed");

  // Verify the signature
  asciichat_error_t verify_result = acds_verify_session_join(pubkey, timestamp, session_string, signature);
  cr_assert_eq(verify_result, ASCIICHAT_OK, "Signature verification should succeed");
}

Test(acds_signatures, session_join_wrong_pubkey) {
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
  cr_assert_neq(verify_result, ASCIICHAT_OK, "Verification should fail with wrong public key");
}

Test(acds_signatures, session_join_tampered_timestamp) {
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
  cr_assert_neq(verify_result, ASCIICHAT_OK, "Verification should fail with tampered timestamp");
}

Test(acds_signatures, session_join_tampered_session_string) {
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
  cr_assert_neq(verify_result, ASCIICHAT_OK, "Verification should fail with tampered session string");
}

Test(acds_signatures, session_join_empty_session_string) {
  uint8_t pubkey[32], seckey[64];
  crypto_sign_keypair(pubkey, seckey);

  uint64_t timestamp = 1234567890123ULL;
  const char *session_string = "";

  // Sign with empty string
  uint8_t signature[64];
  asciichat_error_t sign_result = acds_sign_session_join(seckey, timestamp, session_string, signature);
  cr_assert_eq(sign_result, ASCIICHAT_OK, "Signing empty session string should succeed");

  // Verify with empty string
  asciichat_error_t verify_result = acds_verify_session_join(pubkey, timestamp, session_string, signature);
  cr_assert_eq(verify_result, ASCIICHAT_OK, "Verification should succeed with empty session string");
}

// =============================================================================
// Timestamp Validation Tests
// =============================================================================

Test(acds_signatures, timestamp_validation_current_time) {
  // Get current time
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;

  // Current time should be valid within 5 minute window
  bool is_valid = acds_validate_timestamp(now_ms, 300);
  cr_assert(is_valid, "Current timestamp should be valid");
}

Test(acds_signatures, timestamp_validation_recent_past) {
  // 2 minutes ago
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;
  uint64_t two_min_ago = now_ms - (2 * 60 * 1000);

  // Should be valid within 5 minute window
  bool is_valid = acds_validate_timestamp(two_min_ago, 300);
  cr_assert(is_valid, "Recent past timestamp should be valid");
}

Test(acds_signatures, timestamp_validation_too_old) {
  // 10 minutes ago
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;
  uint64_t ten_min_ago = now_ms - (10 * 60 * 1000);

  // Should be invalid for 5 minute window
  bool is_valid = acds_validate_timestamp(ten_min_ago, 300);
  cr_assert_not(is_valid, "Old timestamp should be invalid");
}

Test(acds_signatures, timestamp_validation_future) {
  // 2 minutes in future (well beyond 60 second clock skew allowance)
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;
  uint64_t future = now_ms + (2 * 60 * 1000);

  // Future timestamps beyond clock skew should be rejected
  bool is_valid = acds_validate_timestamp(future, 300);
  cr_assert_not(is_valid, "Future timestamp should be invalid");
}

Test(acds_signatures, timestamp_validation_edge_of_window) {
  // 4 minutes ago (safely within 5 minute window, accounting for race condition)
  uint64_t now_ms = (uint64_t)time(NULL) * 1000;
  uint64_t edge = now_ms - (4 * 60 * 1000);

  // Should be valid within window
  bool is_valid = acds_validate_timestamp(edge, 300);
  cr_assert(is_valid, "Timestamp within window should be valid");
}

// =============================================================================
// NULL Parameter Tests
// =============================================================================

Test(acds_signatures, session_create_null_signature_output) {
  uint8_t seckey[64];
  memset(seckey, 0, sizeof(seckey));

  asciichat_error_t result = acds_sign_session_create(seckey, 12345, 0x03, 4, NULL);
  cr_assert_neq(result, ASCIICHAT_OK, "Signing should fail with NULL signature output");
}

Test(acds_signatures, session_join_null_session_string) {
  uint8_t seckey[64];
  uint8_t signature[64];
  memset(seckey, 0, sizeof(seckey));

  asciichat_error_t result = acds_sign_session_join(seckey, 12345, NULL, signature);
  cr_assert_neq(result, ASCIICHAT_OK, "Signing should fail with NULL session string");
}

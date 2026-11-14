/**
 * @file test_gpg_authentication.c
 * @brief Integration tests for GPG authentication
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <stdlib.h>
#include <ascii-chat/crypto/gpg/gpg.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/crypto/gpg/gpg_keys.h>

// Test GPG key ID - obtained from environment variable set by setup script
// If not set, tests will be skipped
static const char *get_test_gpg_key_id(void) {
  const char *key_id = getenv("TEST_GPG_KEY_ID");
  if (!key_id || strlen(key_id) != 16) {
    return NULL;
  }
  return key_id;
}
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("gpg_authentication/sign_message_with_gpg_key", "[gpg_authentication]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    ASCIICHAT_SKIP("TEST_GPG_KEY_ID environment variable not set");
  }

  const char *message = "Authentication challenge nonce 123456";
  uint8_t signature[512];
  size_t signature_len = 0;

  int result = gpg_sign_with_key(test_key_id, (const uint8_t *)message, strlen(message), signature, &signature_len);

  ASCIICHAT_REQUIRE_EQ(result, 0, "gpg_sign_with_key should succeed");
  ASCIICHAT_REQUIRE_GT(signature_len, 0, "Signature length should be > 0");
  ASCIICHAT_REQUIRE_LT(signature_len, 512, "Signature length should be < 512");

  // Ed25519 OpenPGP signatures are typically ~119 bytes
  ASCIICHAT_INFO("Signature created: %zu bytes", signature_len);
}

TEST_CASE("gpg_authentication/verify_valid_signature", "[gpg_authentication]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    ASCIICHAT_SKIP("TEST_GPG_KEY_ID environment variable not set");
  }

  const char *message = "Authentication challenge nonce 123456";
  uint8_t signature[512];
  size_t signature_len = 0;

  // First create a signature
  int sign_result =
      gpg_sign_with_key(test_key_id, (const uint8_t *)message, strlen(message), signature, &signature_len);
  ASCIICHAT_REQUIRE_EQ(sign_result, 0, "Signing should succeed");

  // Now verify it
  int verify_result = gpg_verify_signature_with_binary(signature, signature_len, (const uint8_t *)message,
                                                       strlen(message), test_key_id);

  ASCIICHAT_REQUIRE_EQ(verify_result, 0, "Verification should succeed for valid signature");
}

TEST_CASE("gpg_authentication/reject_tampered_message", "[gpg_authentication]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    ASCIICHAT_SKIP("TEST_GPG_KEY_ID environment variable not set");
  }

  const char *message = "Authentication challenge nonce 123456";
  const char *tampered = "TAMPERED challenge nonce 123456";
  uint8_t signature[512];
  size_t signature_len = 0;

  // Create signature for original message
  int sign_result =
      gpg_sign_with_key(test_key_id, (const uint8_t *)message, strlen(message), signature, &signature_len);
  ASCIICHAT_REQUIRE_EQ(sign_result, 0, "Signing should succeed");

  // Try to verify with tampered message
  int verify_result = gpg_verify_signature_with_binary(signature, signature_len, (const uint8_t *)tampered,
                                                       strlen(tampered), test_key_id);

  ASCIICHAT_REQUIRE_NE(verify_result, 0, "Verification should fail for tampered message");
}

TEST_CASE("gpg_authentication/parse_gpg_public_key", "[gpg_authentication]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    ASCIICHAT_SKIP("TEST_GPG_KEY_ID environment variable not set");
  }

  char key_input[64];
  snprintf(key_input, sizeof(key_input), "gpg:%s", test_key_id);

  public_key_t public_key;
  memset(&public_key, 0, sizeof(public_key));

  asciichat_error_t result = parse_public_key(key_input, &public_key);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "parse_public_key should succeed for gpg:KEYID format");
  ASCIICHAT_REQUIRE_EQ(public_key.type, KEY_TYPE_GPG, "Key type should be KEY_TYPE_GPG");

  // Verify we got a valid 32-byte public key
  bool all_zeros = true;
  for (int i = 0; i < 32; i++) {
    if (public_key.key[i] != 0) {
      all_zeros = false;
      break;
    }
  }
  ASCIICHAT_REQUIRE_EQ(all_zeros, false, "Public key should not be all zeros");
}

TEST_CASE("gpg_authentication/parse_gpg_private_key", "[gpg_authentication]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    ASCIICHAT_SKIP("TEST_GPG_KEY_ID environment variable not set");
  }

  char key_input[64];
  snprintf(key_input, sizeof(key_input), "gpg:%s", test_key_id);

  private_key_t private_key;
  memset(&private_key, 0, sizeof(private_key));

  asciichat_error_t result = parse_private_key(key_input, &private_key);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "parse_private_key should succeed for gpg:KEYID format");
  ASCIICHAT_REQUIRE_EQ(private_key.use_gpg_agent, true, "use_gpg_agent flag should be set");
  ASCIICHAT_REQUIRE_EQ(strlen(private_key.gpg_keygrip), 40, "Keygrip should be 40 characters");

  ASCIICHAT_INFO("Keygrip: %s", private_key.gpg_keygrip);
}

TEST_CASE("gpg_authentication/get_public_key_from_keyring", "[gpg_authentication]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    ASCIICHAT_SKIP("TEST_GPG_KEY_ID environment variable not set");
  }

  uint8_t public_key[32];
  char keygrip[64];

  int result = gpg_get_public_key(test_key_id, public_key, keygrip);

  ASCIICHAT_REQUIRE_EQ(result, 0, "gpg_get_public_key should succeed");
  ASCIICHAT_REQUIRE_EQ(strlen(keygrip), 40, "Keygrip should be 40 characters");

  // Verify we got a valid public key (not all zeros)
  bool all_zeros = true;
  for (int i = 0; i < 32; i++) {
    if (public_key[i] != 0) {
      all_zeros = false;
      break;
    }
  }
  ASCIICHAT_REQUIRE_EQ(all_zeros, false, "Public key should not be all zeros");

  ASCIICHAT_INFO("Public key (first 16 bytes): ");
  for (int i = 0; i < 16; i++) {
    ASCIICHAT_INFO("%02x", public_key[i]);
  }
}

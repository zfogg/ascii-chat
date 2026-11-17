#include <catch2/catch_test_macros.hpp>
/**
 * @file test_gpg_authentication.c
 * @brief Integration tests for GPG authentication
 */

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
TEST_CASE("gpg_authentication/sign_message_with_gpg_key", "[gpg_authentication] [integration]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    SKIP("test skipped");
  }

  const char *message = "Authentication challenge nonce 123456";
  uint8_t signature[512];
  size_t signature_len = 0;

  int result = gpg_sign_with_key(test_key_id, (const uint8_t *)message, strlen(message), signature, &signature_len);

  REQUIRE((result) == (0));
  REQUIRE((signature_len) > (0));
  REQUIRE((signature_len) < (512));

  // Ed25519 OpenPGP signatures are typically ~119 bytes
  INFO("Signature created: %zu bytes");
}

TEST_CASE("gpg_authentication/verify_valid_signature", "[gpg_authentication] [integration]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    SKIP("test skipped");
  }

  const char *message = "Authentication challenge nonce 123456";
  uint8_t signature[512];
  size_t signature_len = 0;

  // First create a signature
  int sign_result = gpg_sign_with_key(test_key_id, (const uint8_t *)message, strlen(message), signature, &signature_len);
  REQUIRE((sign_result) == (0));

  // Now verify it
  int verify_result = gpg_verify_signature_with_binary(signature, signature_len, (const uint8_t *)message, strlen(message), test_key_id);

  REQUIRE((verify_result) == (0));
}

TEST_CASE("gpg_authentication/reject_tampered_message", "[gpg_authentication] [integration]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    SKIP("test skipped");
  }

  const char *message = "Authentication challenge nonce 123456";
  const char *tampered = "TAMPERED challenge nonce 123456";
  uint8_t signature[512];
  size_t signature_len = 0;

  // Create signature for original message
  int sign_result = gpg_sign_with_key(test_key_id, (const uint8_t *)message, strlen(message), signature, &signature_len);
  REQUIRE((sign_result) == (0));

  // Try to verify with tampered message
  int verify_result = gpg_verify_signature_with_binary(signature, signature_len, (const uint8_t *)tampered, strlen(tampered), test_key_id);

  REQUIRE((verify_result) != (0));
}

TEST_CASE("gpg_authentication/parse_gpg_public_key", "[gpg_authentication] [integration]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    SKIP("test skipped");
  }

  char key_input[64];
  snprintf(key_input, sizeof(key_input), "gpg:%s", test_key_id);

  public_key_t public_key;
  memset(&public_key, 0, sizeof(public_key));

  asciichat_error_t result = parse_public_key(key_input, &public_key);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((public_key.type) == (KEY_TYPE_GPG));

  // Verify we got a valid 32-byte public key
  bool all_zeros = true;
  for (int i = 0; i < 32; i++) {
    if (public_key.key[i] != 0) {
      all_zeros = false;
      break;
    }
  }
  REQUIRE((all_zeros) == (false));
}

TEST_CASE("gpg_authentication/parse_gpg_private_key", "[gpg_authentication] [integration]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    SKIP("test skipped");
  }

  char key_input[64];
  snprintf(key_input, sizeof(key_input), "gpg:%s", test_key_id);

  private_key_t private_key;
  memset(&private_key, 0, sizeof(private_key));

  asciichat_error_t result = parse_private_key(key_input, &private_key);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((private_key.use_gpg_agent) == (true));
  REQUIRE((strlen(private_key.gpg_keygrip)) == (40));

  INFO("Keygrip: %s");
}

TEST_CASE("gpg_authentication/get_public_key_from_keyring", "[gpg_authentication] [integration]") {
  const char *test_key_id = get_test_gpg_key_id();
  if (!test_key_id) {
    SKIP("test skipped");
  }

  uint8_t public_key[32];
  char keygrip[64];

  int result = gpg_get_public_key(test_key_id, public_key, keygrip);

  REQUIRE((result) == (0));
  REQUIRE((strlen(keygrip)) == (40));

  // Verify we got a valid public key (not all zeros)
  bool all_zeros = true;
  for (int i = 0; i < 32; i++) {
    if (public_key[i] != 0) {
      all_zeros = false;
      break;
    }
  }
  REQUIRE((all_zeros) == (false));

  INFO("Public key (first 16 bytes): ");
  for (int i = 0; i < 16; i++) {
    INFO("%02x");
  }
}

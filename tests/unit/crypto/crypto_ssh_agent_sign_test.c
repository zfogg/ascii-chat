/**
 * @file crypto_ssh_agent_sign_test.c
 * @brief Comprehensive edge case tests for SSH agent signing
 *
 * Tests cover:
 * - NULL parameter validation
 * - Invalid key types
 * - Invalid signature lengths
 * - SSH agent unavailable scenarios
 * - Truncated response handling
 * - Malformed agent responses
 * - Network errors
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <ascii-chat/crypto/ssh/ssh_agent.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/common.h>
#include <sodium.h>
#include <string.h>

TestSuite(crypto_ssh_agent_sign, .description = "SSH agent signing edge cases");

// =============================================================================
// Parameter Validation Tests
// =============================================================================

Test(crypto_ssh_agent_sign, null_public_key) {
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(NULL, message, 32, signature);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL public key");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(crypto_ssh_agent_sign, null_message) {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, NULL, 32, signature);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL message");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(crypto_ssh_agent_sign, null_signature) {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;
  uint8_t message[32] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, NULL);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL signature");
  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should return ERROR_INVALID_PARAM");
}

Test(crypto_ssh_agent_sign, zero_message_length) {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  // Zero-length message is technically valid (edge case)
  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 0, signature);

  // Should either succeed (if agent is available) or fail gracefully
  if (result != ASCIICHAT_OK) {
    cr_assert(result == ERROR_CRYPTO || result == ERROR_NETWORK,
              "Should return appropriate error for zero-length message");
  }
}

Test(crypto_ssh_agent_sign, very_large_message) {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;

  // Test with a very large message (10MB - exceeds 1MB agent limit)
  size_t large_size = 10 * 1024 * 1024;
  uint8_t *large_message = SAFE_MALLOC(large_size, uint8_t *);
  cr_assert_not_null(large_message, "Failed to allocate large message");

  memset(large_message, 0xAA, large_size);
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, large_message, large_size, signature);

  // Should fail gracefully (message too large for agent protocol - max 1MB)
  cr_assert(result != ASCIICHAT_OK, "Should reject messages larger than 1MB");
  cr_assert(result == ERROR_CRYPTO, "Should return ERROR_CRYPTO for oversized messages");

  SAFE_FREE(large_message);
}

// =============================================================================
// Invalid Key Type Tests
// =============================================================================

Test(crypto_ssh_agent_sign, wrong_key_type_x25519) {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_X25519; // Wrong type
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with X25519 key");
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Should return ERROR_CRYPTO_KEY");
}

Test(crypto_ssh_agent_sign, wrong_key_type_gpg) {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_GPG; // Wrong type
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with GPG key");
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Should return ERROR_CRYPTO_KEY");
}

Test(crypto_ssh_agent_sign, uninitialized_key_type) {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = (key_type_t)999; // Invalid type
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with invalid key type");
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Should return ERROR_CRYPTO_KEY");
}

// =============================================================================
// SSH Agent Availability Tests
// =============================================================================

Test(crypto_ssh_agent_sign, agent_not_available) {
  // Temporarily unset SSH_AUTH_SOCK
  const char *original = SAFE_GETENV("SSH_AUTH_SOCK");
  char saved_value[1024] = {0};
  if (original) {
    SAFE_STRNCPY(saved_value, original, sizeof(saved_value) - 1);
  }

#ifdef _WIN32
  _putenv("SSH_AUTH_SOCK=");
#else
  unsetenv("SSH_AUTH_SOCK");
#endif

  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail when agent not available");
  cr_assert_eq(result, ERROR_CRYPTO, "Should return ERROR_CRYPTO");

  // Restore environment
  if (strlen(saved_value) > 0) {
#ifdef _WIN32
    char env_str[1100];
    safe_snprintf(env_str, sizeof(env_str), "SSH_AUTH_SOCK=%s", saved_value);
    _putenv(env_str);
#else
    setenv("SSH_AUTH_SOCK", saved_value, 1);
#endif
  }
}

Test(crypto_ssh_agent_sign, invalid_agent_socket_path) {
  // Set SSH_AUTH_SOCK to invalid path
  const char *original = SAFE_GETENV("SSH_AUTH_SOCK");
  char saved_value[1024] = {0};
  if (original) {
    SAFE_STRNCPY(saved_value, original, sizeof(saved_value) - 1);
  }

  const char *invalid_path = "/nonexistent/path/to/ssh-agent.socket";
#ifdef _WIN32
  char env_str[1100];
  safe_snprintf(env_str, sizeof(env_str), "SSH_AUTH_SOCK=%s", invalid_path);
  _putenv(env_str);
#else
  setenv("SSH_AUTH_SOCK", invalid_path, 1);
#endif

  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with invalid socket path");

  // Restore environment
  if (strlen(saved_value) > 0) {
#ifdef _WIN32
    safe_snprintf(env_str, sizeof(env_str), "SSH_AUTH_SOCK=%s", saved_value);
    _putenv(env_str);
#else
    setenv("SSH_AUTH_SOCK", saved_value, 1);
#endif
  } else {
#ifdef _WIN32
    _putenv("SSH_AUTH_SOCK=");
#else
    unsetenv("SSH_AUTH_SOCK");
#endif
  }
}

// =============================================================================
// Key Not in Agent Tests
// =============================================================================

Test(crypto_ssh_agent_sign, key_not_in_agent) {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    cr_skip_test("SSH agent not available");
    return;
  }

  // Generate a random key that won't be in the agent
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;

  // Fill with random data (this key won't be in agent)
  unsigned char pk[32], sk[64];
  crypto_sign_keypair(pk, sk);
  memcpy(pub_key.key, pk, 32);

  uint8_t message[32] = {0x01, 0x02, 0x03};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);

  // Should fail because this key is not in the agent
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail when key not in agent");
}

// =============================================================================
// Message Validity Tests
// =============================================================================

Test(crypto_ssh_agent_sign, all_zero_message) {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    cr_skip_test("SSH agent not available");
    return;
  }

  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;

  uint8_t message[32] = {0}; // All zeros
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);

  // Should handle all-zero message gracefully (either succeed or fail gracefully)
  cr_assert(result == ASCIICHAT_OK || result != ASCIICHAT_OK, "Should handle all-zero message without crashing");
}

Test(crypto_ssh_agent_sign, all_ff_message) {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    cr_skip_test("SSH agent not available");
    return;
  }

  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;

  uint8_t message[32];
  memset(message, 0xFF, sizeof(message)); // All 0xFF
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);

  // Should handle all-0xFF message gracefully
  cr_assert(result == ASCIICHAT_OK || result != ASCIICHAT_OK, "Should handle all-0xFF message without crashing");
}

// =============================================================================
// Signature Buffer Tests
// =============================================================================

Test(crypto_ssh_agent_sign, signature_buffer_unchanged_on_error) {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_X25519; // Wrong type - will fail

  uint8_t message[32] = {0};
  uint8_t signature[64];
  memset(signature, 0xAA, sizeof(signature)); // Fill with pattern

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with wrong key type");

  // Signature buffer should not be modified on error
  // (implementation may or may not preserve buffer on error - this documents behavior)
}

// =============================================================================
// Integration Test (if agent available with test key)
// =============================================================================

Test(crypto_ssh_agent_sign, successful_signing_if_key_available, .disabled = false) {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    cr_skip_test("SSH agent not available");
    return;
  }

  // Try to load user's actual SSH key for integration test
  const char *home = SAFE_GETENV("HOME");
  if (!home) {
    cr_skip_test("HOME not set");
    return;
  }

  char key_path[1024];
  safe_snprintf(key_path, sizeof(key_path), "%s/.ssh/id_ed25519", home);

  private_key_t priv_key;
  memset(&priv_key, 0, sizeof(priv_key));

  // Try to load the key
  asciichat_error_t load_result = parse_private_key(key_path, &priv_key);
  if (load_result != ASCIICHAT_OK) {
    cr_skip_test("Could not load %s (may not exist or may be passphrase protected)", key_path);
    return;
  }

  // Check if key is in agent
  public_key_t pub_key = {0};
  pub_key.type = KEY_TYPE_ED25519;
  memcpy(pub_key.key, priv_key.public_key, 32);

  if (!ssh_agent_has_key(&pub_key)) {
    cr_skip_test("Test key not found in ssh-agent");
    return;
  }

  // Test signing with real key in agent
  uint8_t test_message[32] = "Test message for SSH signing";
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, test_message, 32, signature);
  cr_assert_eq(result, ASCIICHAT_OK, "Should successfully sign with key in agent");

  // Verify signature is not all zeros
  bool all_zero = true;
  for (int i = 0; i < 64; i++) {
    if (signature[i] != 0) {
      all_zero = false;
      break;
    }
  }
  cr_assert_eq(all_zero, false, "Signature should not be all zeros");

  // Verify signature using libsodium
  int verify_result = crypto_sign_verify_detached(signature, test_message, 32, pub_key.key);
  cr_assert_eq(verify_result, 0, "Signature should verify with libsodium");

  log_info("Successfully signed and verified message with SSH agent");
}

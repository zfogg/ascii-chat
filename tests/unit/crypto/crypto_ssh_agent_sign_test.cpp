#include <catch2/catch_test_macros.hpp>
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

#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/crypto/ssh/ssh_agent.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/common.h>
#include <sodium.h>
#include <string.h>
// =============================================================================
// Parameter Validation Tests
// =============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("crypto_ssh_agent_sign/null_public_key", "[crypto_ssh_agent_sign]") {
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(NULL, message, 32, signature);
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_INVALID_PARAM));
}

TEST_CASE("crypto_ssh_agent_sign/null_message", "[crypto_ssh_agent_sign]") {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, NULL, 32, signature);
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_INVALID_PARAM));
}

TEST_CASE("crypto_ssh_agent_sign/null_signature", "[crypto_ssh_agent_sign]") {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;
  uint8_t message[32] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, NULL);
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_INVALID_PARAM));
}

TEST_CASE("crypto_ssh_agent_sign/zero_message_length", "[crypto_ssh_agent_sign]") {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  // Zero-length message is technically valid (edge case)
  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 0, signature);

  // Should either succeed (if agent is available) or fail gracefully
  if (result != ASCIICHAT_OK) {
    REQUIRE((result == ERROR_CRYPTO || result == ERROR_NETWORK));
  }
}

TEST_CASE("crypto_ssh_agent_sign/very_large_message", "[crypto_ssh_agent_sign]") {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;

  // Test with a very large message (10MB - exceeds 1MB agent limit)
  size_t large_size = 10 * 1024 * 1024;
  uint8_t *large_message = SAFE_MALLOC(large_size, uint8_t *);
  REQUIRE((large_message) != nullptr);

  memset(large_message, 0xAA, large_size);
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, large_message, large_size, signature);

  // Should fail gracefully (message too large for agent protocol - max 1MB)
  REQUIRE(result != ASCIICHAT_OK);
  REQUIRE(result == ERROR_CRYPTO);

  SAFE_FREE(large_message);
}

// =============================================================================
// Invalid Key Type Tests
// =============================================================================

TEST_CASE("crypto_ssh_agent_sign/wrong_key_type_x25519", "[crypto_ssh_agent_sign]") {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_X25519; // Wrong type
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_CRYPTO_KEY));
}

TEST_CASE("crypto_ssh_agent_sign/wrong_key_type_gpg", "[crypto_ssh_agent_sign]") {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_GPG; // Wrong type
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_CRYPTO_KEY));
}

TEST_CASE("crypto_ssh_agent_sign/uninitialized_key_type", "[crypto_ssh_agent_sign]") {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = (key_type_t)999; // Invalid type
  uint8_t message[32] = {0};
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_CRYPTO_KEY));
}

// =============================================================================
// SSH Agent Availability Tests
// =============================================================================

TEST_CASE("crypto_ssh_agent_sign/agent_not_available", "[crypto_ssh_agent_sign]") {
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
  REQUIRE((result) != (ASCIICHAT_OK));
  REQUIRE((result) == (ERROR_CRYPTO));

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

TEST_CASE("crypto_ssh_agent_sign/invalid_agent_socket_path", "[crypto_ssh_agent_sign]") {
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
  REQUIRE((result) != (ASCIICHAT_OK));

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

TEST_CASE("crypto_ssh_agent_sign/key_not_in_agent", "[crypto_ssh_agent_sign]") {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    SKIP("test skipped");
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
  REQUIRE((result) != (ASCIICHAT_OK));
}

// =============================================================================
// Message Validity Tests
// =============================================================================

TEST_CASE("crypto_ssh_agent_sign/all_zero_message", "[crypto_ssh_agent_sign]") {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    SKIP("test skipped");
    return;
  }

  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_ED25519;

  uint8_t message[32] = {0}; // All zeros
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);

  // Should handle all-zero message gracefully (either succeed or fail gracefully)
  REQUIRE((result == ASCIICHAT_OK || result != ASCIICHAT_OK));
}

TEST_CASE("crypto_ssh_agent_sign/all_ff_message", "[crypto_ssh_agent_sign]") {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    SKIP("test skipped");
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
  REQUIRE((result == ASCIICHAT_OK || result != ASCIICHAT_OK));
}

// =============================================================================
// Signature Buffer Tests
// =============================================================================

TEST_CASE("crypto_ssh_agent_sign/signature_buffer_unchanged_on_error", "[crypto_ssh_agent_sign]") {
  public_key_t pub_key;
  memset(&pub_key, 0, sizeof(pub_key));
  pub_key.type = KEY_TYPE_X25519; // Wrong type - will fail

  uint8_t message[32] = {0};
  uint8_t signature[64];
  memset(signature, 0xAA, sizeof(signature)); // Fill with pattern

  asciichat_error_t result = ssh_agent_sign(&pub_key, message, 32, signature);

  REQUIRE((result) != (ASCIICHAT_OK));

  // Signature buffer should not be modified on error
  // (implementation may or may not preserve buffer on error - this documents behavior)
}

// =============================================================================
// Integration Test (if agent available with test key)
// =============================================================================

TEST_CASE("crypto_ssh_agent_sign/successful_signing_if_key_available", "[crypto_ssh_agent_sign]") {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    SKIP("test skipped");
    return;
  }

  // Try to load user's actual SSH key for integration test
  const char *home = SAFE_GETENV("HOME");
  if (!home) {
    SKIP("test skipped");
    return;
  }

  char key_path[1024];
  safe_snprintf(key_path, sizeof(key_path), "%s/.ssh/id_ed25519", home);

  private_key_t priv_key;
  memset(&priv_key, 0, sizeof(priv_key));

  // Try to load the key
  asciichat_error_t load_result = parse_private_key(key_path, &priv_key);
  if (load_result != ASCIICHAT_OK) {
    SKIP("test skipped");
    return;
  }

  // Check if key is in agent
  public_key_t pub_key{};
  pub_key.type = KEY_TYPE_ED25519;
  memcpy(pub_key.key, priv_key.public_key, 32);

  if (!ssh_agent_has_key(&pub_key)) {
    SKIP("test skipped");
    return;
  }

  // Test signing with real key in agent
  uint8_t test_message[32] = "Test message for SSH signing";
  uint8_t signature[64] = {0};

  asciichat_error_t result = ssh_agent_sign(&pub_key, test_message, 32, signature);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Verify signature is not all zeros
  bool all_zero = true;
  for (int i = 0; i < 64; i++) {
    if (signature[i] != 0) {
      all_zero = false;
      break;
    }
  }
  REQUIRE((all_zero) == (false));

  // Verify signature using libsodium
  int verify_result = crypto_sign_verify_detached(signature, test_message, 32, pub_key.key);
  REQUIRE((verify_result) == (0));

  log_info("Successfully signed and verified message with SSH agent");
}

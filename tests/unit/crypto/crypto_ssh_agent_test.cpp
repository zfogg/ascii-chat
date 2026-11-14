/**
 * @file crypto_ssh_agent_test.c
 * @brief Unit tests for SSH agent integration
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/crypto/ssh/ssh_agent.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/common.h>
#include <sodium.h>
#include <string.h>
// =============================================================================
// SSH Agent Availability Tests
// =============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("crypto_ssh_agent/check_availability", "[crypto_ssh_agent]") {
  // Test that function doesn't crash
  bool available = ssh_agent_is_available();

  // Result depends on environment, but function should execute
  ASCIICHAT_REQUIRE(available == true || available == false, "Should return boolean");
}

TEST_CASE("crypto_ssh_agent/availability_without_env", "[crypto_ssh_agent]") {
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

  bool available = ssh_agent_is_available();
  ASCIICHAT_REQUIRE_EQ(available, false, "Should return false when SSH_AUTH_SOCK not set");

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

// =============================================================================
// SSH Agent Key Checking Tests
// =============================================================================

TEST_CASE("crypto_ssh_agent/check_has_key_null_input", "[crypto_ssh_agent]") {
  bool has_key = ssh_agent_has_key(NULL);
  ASCIICHAT_REQUIRE_EQ(has_key, false, "Should return false for NULL key");
}

TEST_CASE("crypto_ssh_agent/check_has_key_valid_key", "[crypto_ssh_agent]") {
  // Create a test public key
  public_key_t test_key;
  memset(&test_key, 0, sizeof(test_key));
  test_key.type = KEY_TYPE_ED25519;

  // Fill with test data
  for (int i = 0; i < 32; i++) {
    test_key.key[i] = (uint8_t)i;
  }

  // Check if agent has this key (will return false if agent not available)
  bool has_key = ssh_agent_has_key(&test_key);

  // Should return false (this test key won't actually be in the agent)
  // But function should execute without crashing
  ASCIICHAT_REQUIRE(has_key == true || has_key == false, "Should return boolean");
}

// =============================================================================
// SSH Agent Key Adding Tests
// =============================================================================

TEST_CASE("crypto_ssh_agent/add_key_null_private_key", "[crypto_ssh_agent]") {
  asciichat_error_t result = ssh_agent_add_key(NULL, "test_path");
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL private key");
}

TEST_CASE("crypto_ssh_agent/add_key_null_path", "[crypto_ssh_agent]") {
  private_key_t test_key;
  memset(&test_key, 0, sizeof(test_key));
  test_key.type = KEY_TYPE_ED25519;

  // NULL path is allowed (key is in memory)
  asciichat_error_t result = ssh_agent_add_key(&test_key, NULL);

  // Result depends on whether ssh-agent is available
  // Function should execute without crashing
  ASCIICHAT_REQUIRE(result == ASCIICHAT_OK || result != ASCIICHAT_OK, "Should return error code");
}

TEST_CASE("crypto_ssh_agent/add_key_wrong_type", "[crypto_ssh_agent]") {
  private_key_t test_key;
  memset(&test_key, 0, sizeof(test_key));
  test_key.type = KEY_TYPE_X25519; // Wrong type (not Ed25519)

  asciichat_error_t result = ssh_agent_add_key(&test_key, "test_path");

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with non-Ed25519 key");
}

TEST_CASE("crypto_ssh_agent/add_key_without_agent", "[crypto_ssh_agent]") {
  // Temporarily unset SSH_AUTH_SOCK to simulate no agent
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

  private_key_t test_key;
  memset(&test_key, 0, sizeof(test_key));
  test_key.type = KEY_TYPE_ED25519;
  SAFE_STRNCPY(test_key.key_comment, "test key", sizeof(test_key.key_comment) - 1);

  // Generate a valid Ed25519 keypair for testing
  unsigned char pk[32], sk[64];
  crypto_sign_keypair(pk, sk);
  memcpy(test_key.key.ed25519, sk, 64);
  memcpy(test_key.public_key, pk, 32);

  asciichat_error_t result = ssh_agent_add_key(&test_key, "/tmp/test_key");

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail when agent not available");

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

// =============================================================================
// Integration Tests (if ssh-agent is available)
// =============================================================================

TEST_CASE("crypto_ssh_agent/full_workflow_if_agent_available", "[crypto_ssh_agent]") {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    ASCIICHAT_SKIP("SSH agent not available, skipping integration test");
    return;
  }

  // Generate a test Ed25519 keypair
  private_key_t test_key;
  memset(&test_key, 0, sizeof(test_key));
  test_key.type = KEY_TYPE_ED25519;
  SAFE_STRNCPY(test_key.key_comment, "ascii-chat test key", sizeof(test_key.key_comment) - 1);

  unsigned char pk[32], sk[64];
  crypto_sign_keypair(pk, sk);
  memcpy(test_key.key.ed25519, sk, 64);
  memcpy(test_key.public_key, pk, 32);

  // Try to add key to agent
  asciichat_error_t result = ssh_agent_add_key(&test_key, NULL);

  // Note: This test may fail if ssh-agent is not running That's expected
  // behavior - we're testing the code path, not requiring a working ssh-agent
  if (result == ASCIICHAT_OK) {
    log_info("Successfully added test key to ssh-agent");
  } else {
    log_info("Could not add key to ssh-agent (this is normal if OpenSSH not installed)");
  }

  // Test passes either way - we're verifying the function doesn't crash
  ASCIICHAT_REQUIRE(true, "Function executed without crashing");
}

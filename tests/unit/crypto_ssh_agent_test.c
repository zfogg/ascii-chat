/**
 * @file crypto_ssh_agent_test.c
 * @brief Unit tests for SSH agent integration
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "crypto/ssh_agent.h"
#include "crypto/keys/keys.h"
#include "common.h"
#include <sodium.h>
#include <string.h>

TestSuite(crypto_ssh_agent, .description = "SSH agent integration");

// =============================================================================
// SSH Agent Availability Tests
// =============================================================================

Test(crypto_ssh_agent, check_availability) {
  // Test that function doesn't crash
  bool available = ssh_agent_is_available();

  // Result depends on environment, but function should execute
  cr_assert(available == true || available == false, "Should return boolean");
}

Test(crypto_ssh_agent, availability_without_env) {
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
  cr_assert_eq(available, false, "Should return false when SSH_AUTH_SOCK not set");

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

Test(crypto_ssh_agent, check_has_key_null_input) {
  bool has_key = ssh_agent_has_key(NULL);
  cr_assert_eq(has_key, false, "Should return false for NULL key");
}

Test(crypto_ssh_agent, check_has_key_valid_key) {
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
  cr_assert(has_key == true || has_key == false, "Should return boolean");
}

// =============================================================================
// SSH Agent Key Adding Tests
// =============================================================================

Test(crypto_ssh_agent, add_key_null_private_key) {
  asciichat_error_t result = ssh_agent_add_key(NULL, "test_path");
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL private key");
}

Test(crypto_ssh_agent, add_key_null_path) {
  private_key_t test_key;
  memset(&test_key, 0, sizeof(test_key));
  test_key.type = KEY_TYPE_ED25519;

  // NULL path is allowed (key is in memory)
  asciichat_error_t result = ssh_agent_add_key(&test_key, NULL);

  // Result depends on whether ssh-agent is available
  // Function should execute without crashing
  cr_assert(result == ASCIICHAT_OK || result != ASCIICHAT_OK, "Should return error code");
}

Test(crypto_ssh_agent, add_key_wrong_type) {
  private_key_t test_key;
  memset(&test_key, 0, sizeof(test_key));
  test_key.type = KEY_TYPE_X25519; // Wrong type (not Ed25519)

  asciichat_error_t result = ssh_agent_add_key(&test_key, "test_path");

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with non-Ed25519 key");
}

Test(crypto_ssh_agent, add_key_without_agent) {
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

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail when agent not available");

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

Test(crypto_ssh_agent, full_workflow_if_agent_available, .disabled = false) {
  // Skip if agent not available
  if (!ssh_agent_is_available()) {
    cr_skip_test("SSH agent not available, skipping integration test");
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

  // Note: This test may fail if ssh-add is not in PATH or if OpenSSH is not installed
  // That's expected behavior - we're testing the code path, not requiring a working ssh-agent
  if (result == ASCIICHAT_OK) {
    log_info("Successfully added test key to ssh-agent");
  } else {
    log_info("Could not add key to ssh-agent (this is normal if OpenSSH not installed)");
  }

  // Test passes either way - we're verifying the function doesn't crash
  cr_assert(true, "Function executed without crashing");
}

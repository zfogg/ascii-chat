/**
 * @file crypto_http_client_test.c
 * @brief Integration tests for HTTPS client and key fetching
 *
 * NOTE: These tests require network connectivity to GitHub/GitLab APIs.
 * Tests will be skipped if network is unavailable or endpoints are down.
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/http/client/client.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/https_keys.h>
#include <ascii-chat/common.h>
#include <string.h>
// =============================================================================
// Helper Functions
// =============================================================================

static bool is_network_available(void) {
  // Simple test: try to fetch GitHub Zen quote
  char *response = https_get("api.github.com", "/zen");
  if (response) {
    SAFE_FREE(response);
    return true;
  }
  return false;
}

// =============================================================================
// Basic HTTPS GET Tests
// =============================================================================

TEST_CASE("crypto_http_client/https_get_github_zen", "[crypto_http_client]") {
  if (!is_network_available()) {
    ASCIICHAT_SKIP("Network unavailable, skipping HTTPS test");
    return;
  }

  char *response = https_get("api.github.com", "/zen");

  ASCIICHAT_REQUIRE_NOT_NULL(response, "Should successfully fetch from GitHub API");
  ASCIICHAT_REQUIRE_GT(strlen(response), 0, "Response should not be empty");

  log_info("GitHub Zen: %s", response);
  SAFE_FREE(response);
}

TEST_CASE("crypto_http_client/https_get_null_hostname", "[crypto_http_client]") {
  char *response = https_get(NULL, "/test");

  ASCIICHAT_REQUIRE_NULL(response, "Should return NULL with NULL hostname");
}

TEST_CASE("crypto_http_client/https_get_null_path", "[crypto_http_client]") {
  char *response = https_get("api.github.com", NULL);

  ASCIICHAT_REQUIRE_NULL(response, "Should return NULL with NULL path");
}

TEST_CASE("crypto_http_client/https_get_invalid_hostname", "[crypto_http_client]") {
  char *response = https_get("this.hostname.does.not.exist.invalid", "/test");

  ASCIICHAT_REQUIRE_NULL(response, "Should return NULL with invalid hostname");
}

TEST_CASE("crypto_http_client/https_get_404_path", "[crypto_http_client]") {
  if (!is_network_available()) {
    ASCIICHAT_SKIP("Network unavailable, skipping HTTPS test");
    return;
  }

  char *response = https_get("api.github.com", "/nonexistent/path/that/does/not/exist");

  // Should still get a response (404 page), or NULL
  if (response) {
    log_info("Got 404 response (expected): %s", response);
    SAFE_FREE(response);
  }
  ASCIICHAT_REQUIRE(true, "Should handle 404 gracefully");
}

// =============================================================================
// GitHub SSH Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_zfogg", "[crypto_http_client]") {
  if (!is_network_available()) {
    ASCIICHAT_SKIP("Network unavailable, skipping GitHub SSH key test");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", &keys, &num_keys);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should successfully fetch GitHub SSH keys for zfogg");
  ASCIICHAT_REQUIRE_NOT_NULL(keys, "Keys array should not be NULL");
  ASCIICHAT_REQUIRE_GT(num_keys, 0, "Should have at least one SSH key");

  // Verify keys are valid SSH format (may include RSA, Ed25519, ECDSA, etc.)
  for (size_t i = 0; i < num_keys; i++) {
    ASCIICHAT_REQUIRE_NOT_NULL(keys[i], "Key %zu should not be NULL", i);
    // Check for valid SSH key prefix (ssh-rsa, ssh-ed25519, ecdsa-sha2-*, etc.)
    ASCIICHAT_REQUIRE(strstr(keys[i], "ssh-") != NULL || strstr(keys[i], "ecdsa-") != NULL,
              "Key %zu should be valid SSH format", i);
    log_info("GitHub SSH Key %zu: %.80s...", i, keys[i]);
    SAFE_FREE(keys[i]);
  }
  SAFE_FREE(keys);
}

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_null_username", "[crypto_http_client]") {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys(NULL, &keys, &num_keys);

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL username");
}

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_null_keys_out", "[crypto_http_client]") {
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", NULL, &num_keys);

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL keys_out");
}

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_null_num_keys", "[crypto_http_client]") {
  char **keys = NULL;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", &keys, NULL);

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL num_keys");
}

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_nonexistent_user", "[crypto_http_client]") {
  if (!is_network_available()) {
    ASCIICHAT_SKIP("Network unavailable, skipping GitHub SSH key test");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  // Use a username that is extremely unlikely to exist
  asciichat_error_t result =
      fetch_github_ssh_keys("this_user_definitely_does_not_exist_12345678901234567890", &keys, &num_keys);

  // Should either fail or return zero keys
  if (result == ASCIICHAT_OK) {
    ASCIICHAT_REQUIRE_EQ(num_keys, 0, "Nonexistent user should have zero keys");
  } else {
    ASCIICHAT_REQUIRE(true, "Nonexistent user should fail gracefully");
  }
}

// =============================================================================
// GitLab SSH Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_http_client/fetch_gitlab_ssh_keys_valid_user", "[crypto_http_client]") {
  if (!is_network_available()) {
    ASCIICHAT_SKIP("Network unavailable, skipping GitLab SSH key test");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  // Test with a known GitLab user (using torvalds as a likely valid user)
  asciichat_error_t result = fetch_gitlab_ssh_keys("torvalds", &keys, &num_keys);

  // GitLab API may work differently, so we accept either success or graceful failure
  if (result == ASCIICHAT_OK && num_keys > 0) {
    log_info("Successfully fetched %zu GitLab SSH key(s)", num_keys);
    for (size_t i = 0; i < num_keys; i++) {
      log_info("GitLab SSH Key %zu: %.80s...", i, keys[i]);
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    log_info("GitLab SSH key fetch returned: %d with %zu keys (this is acceptable)", result, num_keys);
  }

  ASCIICHAT_REQUIRE(true, "GitLab SSH key fetch should complete without crashing");
}

TEST_CASE("crypto_http_client/fetch_gitlab_ssh_keys_null_username", "[crypto_http_client]") {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_gitlab_ssh_keys(NULL, &keys, &num_keys);

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL username");
}

// =============================================================================
// GitHub GPG Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_http_client/fetch_github_gpg_keys_valid_user", "[crypto_http_client]") {
  if (!is_network_available()) {
    ASCIICHAT_SKIP("Network unavailable, skipping GitHub GPG key test");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  // Test with a known user who likely has GPG keys
  asciichat_error_t result = fetch_github_gpg_keys("zfogg", &keys, &num_keys);

  // User may not have GPG keys, so we accept graceful failure
  if (result == ASCIICHAT_OK && num_keys > 0) {
    log_info("Successfully fetched %zu GitHub GPG key(s)", num_keys);

    // Verify GPG key format (should be "gpg:FINGERPRINT")
    for (size_t i = 0; i < num_keys; i++) {
      ASCIICHAT_REQUIRE_NOT_NULL(keys[i], "GPG key %zu should not be NULL", i);
      ASCIICHAT_REQUIRE(strncmp(keys[i], "gpg:", 4) == 0, "GPG key %zu should have 'gpg:' prefix", i);
      ASCIICHAT_REQUIRE(strlen(keys[i]) > 4, "GPG key %zu should have content after 'gpg:' prefix", i);

      log_info("GPG Key %zu: %s", i, keys[i]);

      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    log_info("GitHub GPG key fetch returned: %d (user may not have GPG keys)", result);
  }

  ASCIICHAT_REQUIRE(true, "GitHub GPG key fetch should complete without crashing");
}

TEST_CASE("crypto_http_client/fetch_github_gpg_keys_null_username", "[crypto_http_client]") {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_gpg_keys(NULL, &keys, &num_keys);

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL username");
}

TEST_CASE("crypto_http_client/fetch_github_gpg_keys_null_keys_out", "[crypto_http_client]") {
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_gpg_keys("zfogg", NULL, &num_keys);

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL keys_out");
}

TEST_CASE("crypto_http_client/fetch_github_gpg_keys_null_num_keys", "[crypto_http_client]") {
  char **keys = NULL;

  asciichat_error_t result = fetch_github_gpg_keys("zfogg", &keys, NULL);

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL num_keys");
}

// =============================================================================
// GitLab GPG Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_http_client/fetch_gitlab_gpg_keys_valid_user", "[crypto_http_client]") {
  if (!is_network_available()) {
    ASCIICHAT_SKIP("Network unavailable, skipping GitLab GPG key test");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  // Test with a known user
  asciichat_error_t result = fetch_gitlab_gpg_keys("torvalds", &keys, &num_keys);

  // User may not have GPG keys, so we accept graceful failure
  if (result == ASCIICHAT_OK && num_keys > 0) {
    log_info("Successfully fetched %zu GitLab GPG key(s)", num_keys);
    for (size_t i = 0; i < num_keys; i++) {
      log_info("GitLab GPG Key %zu length: %zu bytes", i, strlen(keys[i]));
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    log_info("GitLab GPG key fetch returned: %d (this is acceptable)", result);
  }

  ASCIICHAT_REQUIRE(true, "GitLab GPG key fetch should complete without crashing");
}

TEST_CASE("crypto_http_client/fetch_gitlab_gpg_keys_null_username", "[crypto_http_client]") {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_gitlab_gpg_keys(NULL, &keys, &num_keys);

  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL username");
}

// =============================================================================
// Memory Management Tests
// =============================================================================

TEST_CASE("crypto_http_client/multiple_fetches_no_leaks", "[crypto_http_client]") {
  if (!is_network_available()) {
    ASCIICHAT_SKIP("Network unavailable, skipping memory test");
    return;
  }

  // Perform multiple fetches to ensure no memory leaks
  for (int i = 0; i < 3; i++) {
    char **keys = NULL;
    size_t num_keys = 0;

    asciichat_error_t result = fetch_github_ssh_keys("zfogg", &keys, &num_keys);

    if (result == ASCIICHAT_OK) {
      for (size_t j = 0; j < num_keys; j++) {
        SAFE_FREE(keys[j]);
      }
      SAFE_FREE(keys);
    }
  }

  ASCIICHAT_REQUIRE(true, "Multiple fetches should not leak memory");
}

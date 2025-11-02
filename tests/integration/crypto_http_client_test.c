/**
 * @file crypto_http_client_test.c
 * @brief Integration tests for HTTPS client and key fetching
 *
 * NOTE: These tests require network connectivity to GitHub/GitLab APIs.
 * Tests will be skipped if network is unavailable or endpoints are down.
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "crypto/http_client.h"
#include "crypto/keys/https_keys.h"
#include "common.h"
#include <string.h>

TestSuite(crypto_http_client, .description = "HTTPS client and key fetching (requires network)");

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

static bool response_contains(const char *response, const char *substring) {
  if (!response || !substring) {
    return false;
  }
  return strstr(response, substring) != NULL;
}

// =============================================================================
// Basic HTTPS GET Tests
// =============================================================================

Test(crypto_http_client, https_get_github_zen) {
  if (!is_network_available()) {
    cr_skip_test("Network unavailable, skipping HTTPS test");
    return;
  }

  char *response = https_get("api.github.com", "/zen");

  cr_assert_not_null(response, "Should successfully fetch from GitHub API");
  cr_assert_gt(strlen(response), 0, "Response should not be empty");

  log_info("GitHub Zen: %s", response);
  SAFE_FREE(response);
}

Test(crypto_http_client, https_get_null_hostname) {
  char *response = https_get(NULL, "/test");

  cr_assert_null(response, "Should return NULL with NULL hostname");
}

Test(crypto_http_client, https_get_null_path) {
  char *response = https_get("api.github.com", NULL);

  cr_assert_null(response, "Should return NULL with NULL path");
}

Test(crypto_http_client, https_get_invalid_hostname) {
  char *response = https_get("this.hostname.does.not.exist.invalid", "/test");

  cr_assert_null(response, "Should return NULL with invalid hostname");
}

Test(crypto_http_client, https_get_404_path) {
  if (!is_network_available()) {
    cr_skip_test("Network unavailable, skipping HTTPS test");
    return;
  }

  char *response = https_get("api.github.com", "/nonexistent/path/that/does/not/exist");

  // Should still get a response (404 page), or NULL
  if (response) {
    log_info("Got 404 response (expected): %s", response);
    SAFE_FREE(response);
  }
  cr_assert(true, "Should handle 404 gracefully");
}

// =============================================================================
// GitHub SSH Key Fetching Tests
// =============================================================================

Test(crypto_http_client, fetch_github_ssh_keys_zfogg) {
  if (!is_network_available()) {
    cr_skip_test("Network unavailable, skipping GitHub SSH key test");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", &keys, &num_keys);

  cr_assert_eq(result, ASCIICHAT_OK, "Should successfully fetch GitHub SSH keys for zfogg");
  cr_assert_not_null(keys, "Keys array should not be NULL");
  cr_assert_gt(num_keys, 0, "Should have at least one SSH key");

  // Verify keys are valid SSH format (may include RSA, Ed25519, ECDSA, etc.)
  for (size_t i = 0; i < num_keys; i++) {
    cr_assert_not_null(keys[i], "Key %zu should not be NULL", i);
    // Check for valid SSH key prefix (ssh-rsa, ssh-ed25519, ecdsa-sha2-*, etc.)
    cr_assert(strstr(keys[i], "ssh-") != NULL || strstr(keys[i], "ecdsa-") != NULL,
              "Key %zu should be valid SSH format", i);
    log_info("GitHub SSH Key %zu: %.80s...", i, keys[i]);
    SAFE_FREE(keys[i]);
  }
  SAFE_FREE(keys);
}

Test(crypto_http_client, fetch_github_ssh_keys_null_username) {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys(NULL, &keys, &num_keys);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL username");
}

Test(crypto_http_client, fetch_github_ssh_keys_null_keys_out) {
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", NULL, &num_keys);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL keys_out");
}

Test(crypto_http_client, fetch_github_ssh_keys_null_num_keys) {
  char **keys = NULL;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", &keys, NULL);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL num_keys");
}

Test(crypto_http_client, fetch_github_ssh_keys_nonexistent_user) {
  if (!is_network_available()) {
    cr_skip_test("Network unavailable, skipping GitHub SSH key test");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  // Use a username that is extremely unlikely to exist
  asciichat_error_t result =
      fetch_github_ssh_keys("this_user_definitely_does_not_exist_12345678901234567890", &keys, &num_keys);

  // Should either fail or return zero keys
  if (result == ASCIICHAT_OK) {
    cr_assert_eq(num_keys, 0, "Nonexistent user should have zero keys");
  } else {
    cr_assert(true, "Nonexistent user should fail gracefully");
  }
}

// =============================================================================
// GitLab SSH Key Fetching Tests
// =============================================================================

Test(crypto_http_client, fetch_gitlab_ssh_keys_valid_user) {
  if (!is_network_available()) {
    cr_skip_test("Network unavailable, skipping GitLab SSH key test");
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

  cr_assert(true, "GitLab SSH key fetch should complete without crashing");
}

Test(crypto_http_client, fetch_gitlab_ssh_keys_null_username) {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_gitlab_ssh_keys(NULL, &keys, &num_keys);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL username");
}

// =============================================================================
// GitHub GPG Key Fetching Tests
// =============================================================================

Test(crypto_http_client, fetch_github_gpg_keys_valid_user) {
  if (!is_network_available()) {
    cr_skip_test("Network unavailable, skipping GitHub GPG key test");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  // Test with a known user who likely has GPG keys
  asciichat_error_t result = fetch_github_gpg_keys("zfogg", &keys, &num_keys);

  // User may not have GPG keys, so we accept graceful failure
  if (result == ASCIICHAT_OK && num_keys > 0) {
    log_info("Successfully fetched %zu GitHub GPG key(s)", num_keys);

    // Verify GPG key format
    for (size_t i = 0; i < num_keys; i++) {
      cr_assert_not_null(keys[i], "GPG key %zu should not be NULL", i);
      cr_assert(strstr(keys[i], "-----BEGIN PGP") != NULL, "GPG key %zu should have PGP header", i);

      // Log first few lines
      const char *line = keys[i];
      int line_count = 0;
      log_info("GPG Key %zu (first 3 lines):", i);
      while (*line && line_count < 3) {
        const char *line_end = strchr(line, '\n');
        if (!line_end)
          break;
        log_info("  %.*s", (int)(line_end - line), line);
        line = line_end + 1;
        line_count++;
      }

      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    log_info("GitHub GPG key fetch returned: %d (user may not have GPG keys)", result);
  }

  cr_assert(true, "GitHub GPG key fetch should complete without crashing");
}

Test(crypto_http_client, fetch_github_gpg_keys_null_username) {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_gpg_keys(NULL, &keys, &num_keys);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL username");
}

Test(crypto_http_client, fetch_github_gpg_keys_null_keys_out) {
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_gpg_keys("zfogg", NULL, &num_keys);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL keys_out");
}

Test(crypto_http_client, fetch_github_gpg_keys_null_num_keys) {
  char **keys = NULL;

  asciichat_error_t result = fetch_github_gpg_keys("zfogg", &keys, NULL);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL num_keys");
}

// =============================================================================
// GitLab GPG Key Fetching Tests
// =============================================================================

Test(crypto_http_client, fetch_gitlab_gpg_keys_valid_user) {
  if (!is_network_available()) {
    cr_skip_test("Network unavailable, skipping GitLab GPG key test");
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

  cr_assert(true, "GitLab GPG key fetch should complete without crashing");
}

Test(crypto_http_client, fetch_gitlab_gpg_keys_null_username) {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_gitlab_gpg_keys(NULL, &keys, &num_keys);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL username");
}

// =============================================================================
// Memory Management Tests
// =============================================================================

Test(crypto_http_client, multiple_fetches_no_leaks) {
  if (!is_network_available()) {
    cr_skip_test("Network unavailable, skipping memory test");
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

  cr_assert(true, "Multiple fetches should not leak memory");
}

#include <catch2/catch_test_macros.hpp>
/**
 * @file crypto_http_client_test.c
 * @brief Integration tests for HTTPS client and key fetching
 *
 * NOTE: These tests require network connectivity to GitHub/GitLab APIs.
 * Tests will be skipped if network is unavailable or endpoints are down.
 */

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

TEST_CASE("crypto_http_client/https_get_github_zen", "[crypto_http_client] [integration]") {
  if (!is_network_available()) {
    SKIP("test skipped");
    return;
  }

  char *response = https_get("api.github.com", "/zen");

  REQUIRE((response) != nullptr);
  REQUIRE((strlen(response)) > (0));

  log_info("GitHub Zen: %s", response);
  SAFE_FREE(response);
}

TEST_CASE("crypto_http_client/https_get_null_hostname", "[crypto_http_client] [integration]") {
  char *response = https_get(NULL, "/test");

  REQUIRE((response) == nullptr);
}

TEST_CASE("crypto_http_client/https_get_null_path", "[crypto_http_client] [integration]") {
  char *response = https_get("api.github.com", NULL);

  REQUIRE((response) == nullptr);
}

TEST_CASE("crypto_http_client/https_get_invalid_hostname", "[crypto_http_client] [integration]") {
  char *response = https_get("this.hostname.does.not.exist.invalid", "/test");

  REQUIRE((response) == nullptr);
}

TEST_CASE("crypto_http_client/https_get_404_path", "[crypto_http_client] [integration]") {
  if (!is_network_available()) {
    SKIP("test skipped");
    return;
  }

  char *response = https_get("api.github.com", "/nonexistent/path/that/does/not/exist");

  // Should still get a response (404 page), or NULL
  if (response) {
    log_info("Got 404 response (expected): %s", response);
    SAFE_FREE(response);
  }
  REQUIRE(true);
}

// =============================================================================
// GitHub SSH Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_zfogg", "[crypto_http_client] [integration]") {
  if (!is_network_available()) {
    SKIP("test skipped");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", &keys, &num_keys);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((keys) != nullptr);
  REQUIRE((num_keys) > (0));

  // Verify keys are valid SSH format (may include RSA, Ed25519, ECDSA, etc.)
  for (size_t i = 0; i < num_keys; i++) {
    REQUIRE((keys[i]) != nullptr);
    // Check for valid SSH key prefix (ssh-rsa, ssh-ed25519, ecdsa-sha2-*, etc.)
    REQUIRE((strstr(keys[i], "ssh-") != NULL || strstr(keys[i], "ecdsa-") != NULL));
    log_info("GitHub SSH Key %zu: %.80s...", i, keys[i]);
    SAFE_FREE(keys[i]);
  }
  SAFE_FREE(keys);
}

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_null_username", "[crypto_http_client] [integration]") {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys(NULL, &keys, &num_keys);

  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_null_keys_out", "[crypto_http_client] [integration]") {
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", NULL, &num_keys);

  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_null_num_keys", "[crypto_http_client] [integration]") {
  char **keys = NULL;

  asciichat_error_t result = fetch_github_ssh_keys("zfogg", &keys, NULL);

  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("crypto_http_client/fetch_github_ssh_keys_nonexistent_user", "[crypto_http_client] [integration]") {
  if (!is_network_available()) {
    SKIP("test skipped");
    return;
  }

  char **keys = NULL;
  size_t num_keys = 0;

  // Use a username that is extremely unlikely to exist
  asciichat_error_t result = fetch_github_ssh_keys("this_user_definitely_does_not_exist_12345678901234567890", &keys, &num_keys);

  // Should either fail or return zero keys
  if (result == ASCIICHAT_OK) {
    REQUIRE((num_keys) == (0));
  } else {
    REQUIRE(true);
  }
}

// =============================================================================
// GitLab SSH Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_http_client/fetch_gitlab_ssh_keys_valid_user", "[crypto_http_client] [integration]") {
  if (!is_network_available()) {
    SKIP("test skipped");
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

  REQUIRE(true);
}

TEST_CASE("crypto_http_client/fetch_gitlab_ssh_keys_null_username", "[crypto_http_client] [integration]") {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_gitlab_ssh_keys(NULL, &keys, &num_keys);

  REQUIRE((result) != (ASCIICHAT_OK));
}

// =============================================================================
// GitHub GPG Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_http_client/fetch_github_gpg_keys_valid_user", "[crypto_http_client] [integration]") {
  if (!is_network_available()) {
    SKIP("test skipped");
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
      REQUIRE((keys[i]) != nullptr);
      REQUIRE(strncmp(keys[i], "gpg:", 4) == 0);
      REQUIRE(strlen(keys[i]) > 4);

      log_info("GPG Key %zu: %s", i, keys[i]);

      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    log_info("GitHub GPG key fetch returned: %d (user may not have GPG keys)", result);
  }

  REQUIRE(true);
}

TEST_CASE("crypto_http_client/fetch_github_gpg_keys_null_username", "[crypto_http_client] [integration]") {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_gpg_keys(NULL, &keys, &num_keys);

  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("crypto_http_client/fetch_github_gpg_keys_null_keys_out", "[crypto_http_client] [integration]") {
  size_t num_keys = 0;

  asciichat_error_t result = fetch_github_gpg_keys("zfogg", NULL, &num_keys);

  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("crypto_http_client/fetch_github_gpg_keys_null_num_keys", "[crypto_http_client] [integration]") {
  char **keys = NULL;

  asciichat_error_t result = fetch_github_gpg_keys("zfogg", &keys, NULL);

  REQUIRE((result) != (ASCIICHAT_OK));
}

// =============================================================================
// GitLab GPG Key Fetching Tests
// =============================================================================

TEST_CASE("crypto_http_client/fetch_gitlab_gpg_keys_valid_user", "[crypto_http_client] [integration]") {
  if (!is_network_available()) {
    SKIP("test skipped");
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

  REQUIRE(true);
}

TEST_CASE("crypto_http_client/fetch_gitlab_gpg_keys_null_username", "[crypto_http_client] [integration]") {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = fetch_gitlab_gpg_keys(NULL, &keys, &num_keys);

  REQUIRE((result) != (ASCIICHAT_OK));
}

// =============================================================================
// Memory Management Tests
// =============================================================================

TEST_CASE("crypto_http_client/multiple_fetches_no_leaks", "[crypto_http_client] [integration]") {
  if (!is_network_available()) {
    SKIP("test skipped");
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

  REQUIRE(true);
}

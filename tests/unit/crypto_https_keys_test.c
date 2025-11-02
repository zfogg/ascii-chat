/**
 * @file crypto_https_keys_test.c
 * @brief Unit tests for HTTPS key fetching and parsing
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "crypto/keys/https_keys.h"
#include "common.h"
#include <string.h>

TestSuite(crypto_https_keys, .description = "HTTPS key fetching and URL construction");

// =============================================================================
// URL Construction Tests - GitHub SSH
// =============================================================================

Test(crypto_https_keys, build_github_ssh_url_valid) {
  char url[256];
  asciichat_error_t result = build_github_ssh_url("testuser", url, sizeof(url));

  cr_assert_eq(result, ASCIICHAT_OK, "Should build GitHub SSH URL successfully");
  cr_assert_str_eq(url, "https://github.com/testuser.keys", "URL should match expected format");
}

Test(crypto_https_keys, build_github_ssh_url_null_username) {
  char url[256];
  asciichat_error_t result = build_github_ssh_url(NULL, url, sizeof(url));

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL username");
}

Test(crypto_https_keys, build_github_ssh_url_null_output) {
  asciichat_error_t result = build_github_ssh_url("testuser", NULL, 256);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL output buffer");
}

Test(crypto_https_keys, build_github_ssh_url_buffer_too_small) {
  char url[10];
  asciichat_error_t result = build_github_ssh_url("testuser", url, sizeof(url));

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with buffer too small");
}

Test(crypto_https_keys, build_github_ssh_url_long_username) {
  char url[256];
  char long_username[200];
  memset(long_username, 'a', sizeof(long_username) - 1);
  long_username[sizeof(long_username) - 1] = '\0';

  asciichat_error_t result = build_github_ssh_url(long_username, url, sizeof(url));

  // Should succeed (buffer is large enough)
  cr_assert_eq(result, ASCIICHAT_OK, "Should handle long username within buffer limits");
}

// =============================================================================
// URL Construction Tests - GitLab SSH
// =============================================================================

Test(crypto_https_keys, build_gitlab_ssh_url_valid) {
  char url[256];
  asciichat_error_t result = build_gitlab_ssh_url("testuser", url, sizeof(url));

  cr_assert_eq(result, ASCIICHAT_OK, "Should build GitLab SSH URL successfully");
  cr_assert_str_eq(url, "https://gitlab.com/testuser.keys", "URL should match expected format");
}

Test(crypto_https_keys, build_gitlab_ssh_url_null_username) {
  char url[256];
  asciichat_error_t result = build_gitlab_ssh_url(NULL, url, sizeof(url));

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL username");
}

Test(crypto_https_keys, build_gitlab_ssh_url_null_output) {
  asciichat_error_t result = build_gitlab_ssh_url("testuser", NULL, 256);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL output buffer");
}

Test(crypto_https_keys, build_gitlab_ssh_url_buffer_too_small) {
  char url[10];
  asciichat_error_t result = build_gitlab_ssh_url("testuser", url, sizeof(url));

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with buffer too small");
}

// =============================================================================
// URL Construction Tests - GitHub GPG
// =============================================================================

Test(crypto_https_keys, build_github_gpg_url_valid) {
  char url[256];
  asciichat_error_t result = build_github_gpg_url("testuser", url, sizeof(url));

  cr_assert_eq(result, ASCIICHAT_OK, "Should build GitHub GPG URL successfully");
  cr_assert_str_eq(url, "https://github.com/testuser.gpg", "URL should match expected format");
}

Test(crypto_https_keys, build_github_gpg_url_null_username) {
  char url[256];
  asciichat_error_t result = build_github_gpg_url(NULL, url, sizeof(url));

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL username");
}

Test(crypto_https_keys, build_github_gpg_url_null_output) {
  asciichat_error_t result = build_github_gpg_url("testuser", NULL, 256);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL output buffer");
}

Test(crypto_https_keys, build_github_gpg_url_buffer_too_small) {
  char url[10];
  asciichat_error_t result = build_github_gpg_url("testuser", url, sizeof(url));

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with buffer too small");
}

// =============================================================================
// URL Construction Tests - GitLab GPG
// =============================================================================

Test(crypto_https_keys, build_gitlab_gpg_url_valid) {
  char url[256];
  asciichat_error_t result = build_gitlab_gpg_url("testuser", url, sizeof(url));

  cr_assert_eq(result, ASCIICHAT_OK, "Should build GitLab GPG URL successfully");
  cr_assert_str_eq(url, "https://gitlab.com/testuser.gpg", "URL should match expected format");
}

Test(crypto_https_keys, build_gitlab_gpg_url_null_username) {
  char url[256];
  asciichat_error_t result = build_gitlab_gpg_url(NULL, url, sizeof(url));

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL username");
}

Test(crypto_https_keys, build_gitlab_gpg_url_null_output) {
  asciichat_error_t result = build_gitlab_gpg_url("testuser", NULL, 256);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL output buffer");
}

Test(crypto_https_keys, build_gitlab_gpg_url_buffer_too_small) {
  char url[10];
  asciichat_error_t result = build_gitlab_gpg_url("testuser", url, sizeof(url));

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with buffer too small");
}

// =============================================================================
// SSH Key Parsing Tests
// =============================================================================

Test(crypto_https_keys, parse_ssh_keys_single_key) {
  const char *response =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT test@example.com";
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_ssh_keys_from_response(response, strlen(response), &keys, &num_keys, 10);

  cr_assert_eq(result, ASCIICHAT_OK, "Should parse single SSH key successfully");
  cr_assert_eq(num_keys, 1, "Should parse exactly one key");
  cr_assert_not_null(keys, "Keys array should not be NULL");
  cr_assert_str_eq(keys[0], response, "Parsed key should match input");

  // Cleanup
  for (size_t i = 0; i < num_keys; i++) {
    SAFE_FREE(keys[i]);
  }
  SAFE_FREE(keys);
}

Test(crypto_https_keys, parse_ssh_keys_multiple_keys) {
  const char *response =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT key1@example.com\n"
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnop key2@example.com\n"
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456 key3@example.com";

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_ssh_keys_from_response(response, strlen(response), &keys, &num_keys, 10);

  cr_assert_eq(result, ASCIICHAT_OK, "Should parse multiple SSH keys successfully");
  cr_assert_eq(num_keys, 3, "Should parse exactly three keys");
  cr_assert_not_null(keys, "Keys array should not be NULL");

  // Verify each key
  cr_assert(strstr(keys[0], "key1@example.com") != NULL, "First key should contain key1");
  cr_assert(strstr(keys[1], "key2@example.com") != NULL, "Second key should contain key2");
  cr_assert(strstr(keys[2], "key3@example.com") != NULL, "Third key should contain key3");

  // Cleanup
  for (size_t i = 0; i < num_keys; i++) {
    SAFE_FREE(keys[i]);
  }
  SAFE_FREE(keys);
}

Test(crypto_https_keys, parse_ssh_keys_with_empty_lines) {
  const char *response = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT "
                         "key1@example.com\n\nssh-ed25519 "
                         "AAAAC3NzaC1lZDI1NTE5AAAAIABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnop key2@example.com\n\n";

  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_ssh_keys_from_response(response, strlen(response), &keys, &num_keys, 10);

  cr_assert_eq(result, ASCIICHAT_OK, "Should parse SSH keys while skipping empty lines");
  cr_assert_eq(num_keys, 2, "Should parse exactly two keys");

  // Cleanup
  for (size_t i = 0; i < num_keys; i++) {
    SAFE_FREE(keys[i]);
  }
  SAFE_FREE(keys);
}

Test(crypto_https_keys, parse_ssh_keys_max_keys_limit) {
  const char *response = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT "
                         "key1@example.com\nssh-ed25519 "
                         "AAAAC3NzaC1lZDI1NTE5AAAAIABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnop "
                         "key2@example.com\nssh-ed25519 "
                         "AAAAC3NzaC1lZDI1NTE5AAAAIQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456 key3@example.com";

  char **keys = NULL;
  size_t num_keys = 0;

  // Limit to 2 keys even though there are 3
  asciichat_error_t result = parse_ssh_keys_from_response(response, strlen(response), &keys, &num_keys, 2);

  cr_assert_eq(result, ASCIICHAT_OK, "Should respect max_keys limit");
  cr_assert_eq(num_keys, 2, "Should only parse 2 keys when max_keys is 2");

  // Cleanup
  for (size_t i = 0; i < num_keys; i++) {
    SAFE_FREE(keys[i]);
  }
  SAFE_FREE(keys);
}

Test(crypto_https_keys, parse_ssh_keys_null_response) {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_ssh_keys_from_response(NULL, 100, &keys, &num_keys, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL response");
}

Test(crypto_https_keys, parse_ssh_keys_null_keys_out) {
  const char *response =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT test@example.com";
  size_t num_keys = 0;

  asciichat_error_t result = parse_ssh_keys_from_response(response, strlen(response), NULL, &num_keys, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL keys_out");
}

Test(crypto_https_keys, parse_ssh_keys_null_num_keys) {
  const char *response =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT test@example.com";
  char **keys = NULL;

  asciichat_error_t result = parse_ssh_keys_from_response(response, strlen(response), &keys, NULL, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL num_keys");
}

Test(crypto_https_keys, parse_ssh_keys_empty_response) {
  const char *response = "";
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_ssh_keys_from_response(response, 0, &keys, &num_keys, 10);

  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Should fail with empty response");
}

Test(crypto_https_keys, parse_ssh_keys_only_newlines) {
  const char *response = "\n\n\n";
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_ssh_keys_from_response(response, strlen(response), &keys, &num_keys, 10);

  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Should fail with only newlines");
}

// =============================================================================
// GPG Key Parsing Tests
// =============================================================================

Test(crypto_https_keys, parse_gpg_keys_valid) {
  const char *response =
      "-----BEGIN PGP PUBLIC KEY BLOCK-----\nVersion: GnuPG v2\n\nmQENBF...\n-----END PGP PUBLIC KEY "
      "BLOCK-----\n";
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_gpg_keys_from_response(response, strlen(response), &keys, &num_keys, 10);

  cr_assert_eq(result, ASCIICHAT_OK, "Should parse GPG key successfully");
  cr_assert_eq(num_keys, 1, "Should parse exactly one GPG key");
  cr_assert_not_null(keys, "Keys array should not be NULL");
  cr_assert_str_eq(keys[0], response, "Parsed GPG key should match input");

  // Cleanup
  for (size_t i = 0; i < num_keys; i++) {
    SAFE_FREE(keys[i]);
  }
  SAFE_FREE(keys);
}

Test(crypto_https_keys, parse_gpg_keys_null_response) {
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_gpg_keys_from_response(NULL, 100, &keys, &num_keys, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL response");
}

Test(crypto_https_keys, parse_gpg_keys_null_keys_out) {
  const char *response = "-----BEGIN PGP PUBLIC KEY BLOCK-----\nmQENBF...\n-----END PGP PUBLIC KEY BLOCK-----\n";
  size_t num_keys = 0;

  asciichat_error_t result = parse_gpg_keys_from_response(response, strlen(response), NULL, &num_keys, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL keys_out");
}

Test(crypto_https_keys, parse_gpg_keys_null_num_keys) {
  const char *response = "-----BEGIN PGP PUBLIC KEY BLOCK-----\nmQENBF...\n-----END PGP PUBLIC KEY BLOCK-----\n";
  char **keys = NULL;

  asciichat_error_t result = parse_gpg_keys_from_response(response, strlen(response), &keys, NULL, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL num_keys");
}

Test(crypto_https_keys, parse_gpg_keys_invalid_format) {
  const char *response = "This is not a GPG key";
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_gpg_keys_from_response(response, strlen(response), &keys, &num_keys, 10);

  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Should fail with invalid GPG format");
}

Test(crypto_https_keys, parse_gpg_keys_ssh_key_input) {
  const char *response =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT test@example.com";
  char **keys = NULL;
  size_t num_keys = 0;

  asciichat_error_t result = parse_gpg_keys_from_response(response, strlen(response), &keys, &num_keys, 10);

  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Should fail when given SSH key instead of GPG key");
}

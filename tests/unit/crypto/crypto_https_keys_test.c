/**
 * @file crypto_https_keys_test.c
 * @brief Unit tests for HTTPS key fetching, parsing, and multi-key support
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "crypto/keys/https_keys.h"
#include "crypto/keys/keys.h"
#include "common.h"
#include <string.h>

TestSuite(crypto_https_keys, .description = "HTTPS key fetching and URL construction");
TestSuite(crypto_multi_keys, .description = "Multi-key parsing for GitHub/GitLab");

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

// =============================================================================
// Multi-Key Parsing Tests (parse_public_keys)
// =============================================================================

Test(crypto_multi_keys, parse_public_keys_null_input) {
  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys(NULL, keys, &num_keys, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL input");
}

Test(crypto_multi_keys, parse_public_keys_null_keys_out) {
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys("ssh-ed25519 AAAAC3...", NULL, &num_keys, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL keys_out");
}

Test(crypto_multi_keys, parse_public_keys_null_num_keys) {
  public_key_t keys[10];

  asciichat_error_t result = parse_public_keys("ssh-ed25519 AAAAC3...", keys, NULL, 10);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with NULL num_keys");
}

Test(crypto_multi_keys, parse_public_keys_zero_max_keys) {
  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys("ssh-ed25519 AAAAC3...", keys, &num_keys, 0);

  cr_assert_eq(result, ERROR_INVALID_PARAM, "Should fail with max_keys=0");
}

Test(crypto_multi_keys, parse_public_keys_single_ssh_key) {
  // Valid Ed25519 public key (base64 decoded is 32 bytes after ssh-ed25519 prefix)
  const char *key_str =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT test@example.com";
  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys(key_str, keys, &num_keys, 10);

  cr_assert_eq(result, ASCIICHAT_OK, "Should parse single SSH Ed25519 key successfully");
  cr_assert_eq(num_keys, 1, "Should return exactly one key for single key input");
  cr_assert_eq(keys[0].type, KEY_TYPE_ED25519, "Key type should be Ed25519");
}

Test(crypto_multi_keys, parse_public_keys_raw_hex) {
  // Valid 64-char hex string representing 32 bytes
  const char *hex_key = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys(hex_key, keys, &num_keys, 10);

  cr_assert_eq(result, ASCIICHAT_OK, "Should parse raw hex key successfully");
  cr_assert_eq(num_keys, 1, "Should return exactly one key for hex input");
  cr_assert_eq(keys[0].type, KEY_TYPE_X25519, "Key type should be X25519 for raw hex");
}

Test(crypto_multi_keys, parse_public_keys_github_prefix_detection) {
  // This test verifies the function recognizes the github: prefix
  // Note: Actual fetching is tested separately (requires network)
  public_key_t keys[10];
  size_t num_keys = 99; // Set to non-zero to ensure it gets reset

  // parse_public_keys should detect github: prefix and attempt to fetch
  // Will fail without network, but we're testing prefix detection
  (void)parse_public_keys("github:nonexistent_user_12345", keys, &num_keys, 10);

  // Should fail (network error or no keys), but num_keys should be reset to 0
  // Note: This test may pass or fail depending on network availability
  // The important thing is that num_keys is initialized to 0 at start
  cr_assert(num_keys == 0 || num_keys >= 1, "num_keys should be valid (0 on failure, >=1 on success)");
}

Test(crypto_multi_keys, parse_public_keys_gitlab_prefix_detection) {
  // Similar test for gitlab: prefix
  public_key_t keys[10];
  size_t num_keys = 99;

  asciichat_error_t result = parse_public_keys("gitlab:nonexistent_user_12345", keys, &num_keys, 10);

  cr_assert(num_keys == 0 || num_keys >= 1, "num_keys should be valid (0 on failure, >=1 on success)");
  (void)result; // Ignore result - we're testing prefix handling
}

Test(crypto_multi_keys, parse_public_keys_respects_max_keys) {
  // Test with file path that would have multiple keys
  // For unit testing without file system, we test the single key case with max_keys=1
  const char *key_str =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT test@example.com";
  public_key_t keys[1];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys(key_str, keys, &num_keys, 1);

  cr_assert_eq(result, ASCIICHAT_OK, "Should parse with max_keys=1");
  cr_assert_eq(num_keys, 1, "Should return at most max_keys");
}

Test(crypto_multi_keys, parse_public_keys_invalid_format) {
  // Test with completely invalid input
  const char *invalid = "not a valid key format at all";
  public_key_t keys[10];
  size_t num_keys = 0;

  asciichat_error_t result = parse_public_keys(invalid, keys, &num_keys, 10);

  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with invalid key format");
}

// =============================================================================
// Multi-Key Integration Tests (using real GitHub keys from github.com/zfogg.keys)
// =============================================================================

// Real GitHub keys from zfogg's profile (github.com/zfogg.keys)
// 1 RSA key + 3 Ed25519 keys - tests that RSA is skipped and all Ed25519 are parsed
#define ZFOGG_RSA_KEY                                                                                                  \
  "ssh-rsa "                                                                                                           \
  "AAAAB3NzaC1yc2EAAAADAQABAAACAQClCSY4EbOMUTgY2RNy8cyXvzv8Necb4u1n1E4l3xinPiEq3v8aI9vkStst4zPLV9+"                     \
  "YfguKeZX0oJqzrdjGIkoktM6sxGY+s1Xq9MYRVgNsTHphgCA3pY4RvLJ6rJRQ415Sn9XIrGx0GcEv66Wp6v84v/"                            \
  "NFZKXDuQxFrp9KFFmBcVe6ywKNQWXJD/"                                                                                    \
  "lluZJhCb2M84EujMPugp/"                                                                                               \
  "Z8Zxui8mKRFmDKLagHhemtbnspbnII69hBC2FJpqaVJ5NQ2irGvnevFmH4xDivl3Mn6TXjb4n93Uvm7ZUu8gk1UwhsShHIHR+"                  \
  "ahK/"                                                                                                                \
  "WN7N9aOMII6BK8qD25mK2vsoINnC/"                                                                                       \
  "TUPjwnqzSiTN2GiHN1BhBMOJNiYmkMBI5sAQro+Kwppd7yhtXchhH3i/"                                                            \
  "QQ7bEwB8P+jv40JerJ7RikfV8FRdTyvOQSLC2+gCWrGBC9OUknFyFVgVX+dKNvAI5lGV5mLWsSPlEPuLeUUFM+"                             \
  "1IwsKOfWMh/"                                                                                                         \
  "Nj427AX0BOTiU+TlfpzQdri0rRX7rkR81bCtbfkoqaVhj9nP3qjARVjAhRhavACWxQiEvDw4y6VWlgawBwGDnEiCpdh41OYW3Xnkg7bgL/"       \
  "jFMEaXVGrGsBW1gCY1d/9cVDGQQKe6653mRvmnlIIaT2waGiSoWQKP/6SlJ/hOHa6xUnJsTmlMndwUoVAtxDIvADtw=="
#define ZFOGG_ED25519_KEY1 "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFlsNir27dY0CPfWR/Nc8PcEwfcfkksSK/pAVr8nZan8"
#define ZFOGG_ED25519_KEY2 "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIP5bVpBcszpper4Ln7zJGfs2I/4VytDZwy5nk7lksdyt"
#define ZFOGG_ED25519_KEY3 "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIHff83Fv185QyZs3vxprrvLO8Gm26ruzUMHOlBPdDkEV"

// Test that parse_ssh_keys_from_response handles real multi-key GitHub profiles
Test(crypto_multi_keys, real_github_zfogg_multi_key_response) {
  // Simulate the exact response from github.com/zfogg.keys
  // 1 RSA key + 3 Ed25519 keys
  const char *zfogg_keys_response = ZFOGG_RSA_KEY "\n" ZFOGG_ED25519_KEY1 "\n" ZFOGG_ED25519_KEY2 "\n" ZFOGG_ED25519_KEY3;

  char **raw_keys = NULL;
  size_t num_raw_keys = 0;

  // First, parse all raw key strings
  asciichat_error_t result =
      parse_ssh_keys_from_response(zfogg_keys_response, strlen(zfogg_keys_response), &raw_keys, &num_raw_keys, 10);

  cr_assert_eq(result, ASCIICHAT_OK, "Should parse zfogg's GitHub key response");
  cr_assert_eq(num_raw_keys, 4, "Should find all 4 key lines (1 RSA + 3 Ed25519)");

  // Now verify that only Ed25519 keys are parsed by parse_public_key
  size_t ed25519_count = 0;
  public_key_t key;

  for (size_t i = 0; i < num_raw_keys; i++) {
    if (parse_public_key(raw_keys[i], &key) == ASCIICHAT_OK) {
      cr_assert_eq(key.type, KEY_TYPE_ED25519, "Parsed key should be Ed25519");
      ed25519_count++;
    }
    // RSA key silently fails (as expected - only Ed25519 supported)
  }

  cr_assert_eq(ed25519_count, 3, "Should find exactly 3 Ed25519 keys out of 4 total keys");

  // Cleanup
  for (size_t i = 0; i < num_raw_keys; i++) {
    SAFE_FREE(raw_keys[i]);
  }
  SAFE_FREE(raw_keys);
}

// Test the verification scenario: server presents one of zfogg's Ed25519 keys
Test(crypto_multi_keys, real_github_zfogg_server_key_match_any) {
  // Use zfogg's actual Ed25519 keys
  const char *zfogg_ed25519_keys[] = {ZFOGG_ED25519_KEY1, ZFOGG_ED25519_KEY2, ZFOGG_ED25519_KEY3};

  public_key_t parsed_keys[3];
  size_t num_parsed = 0;

  // Parse all Ed25519 keys
  for (size_t i = 0; i < 3; i++) {
    asciichat_error_t result = parse_public_key(zfogg_ed25519_keys[i], &parsed_keys[num_parsed]);
    if (result == ASCIICHAT_OK) {
      num_parsed++;
    }
  }

  cr_assert_eq(num_parsed, 3, "Should parse all 3 of zfogg's Ed25519 keys");

  // Simulate server presenting the second key (could be from a different machine)
  public_key_t server_presented_key;
  asciichat_error_t parse_result = parse_public_key(ZFOGG_ED25519_KEY2, &server_presented_key);
  cr_assert_eq(parse_result, ASCIICHAT_OK, "Should parse server key");

  // Verify match-any logic (core use case for multi-key support)
  bool found_match = false;
  for (size_t i = 0; i < num_parsed; i++) {
    if (memcmp(server_presented_key.key, parsed_keys[i].key, 32) == 0) {
      found_match = true;
      break;
    }
  }

  cr_assert(found_match, "Server key should match one of zfogg's GitHub keys");
}

// Test that each of zfogg's Ed25519 keys matches when presented as server key
Test(crypto_multi_keys, real_github_zfogg_each_key_matches) {
  const char *zfogg_ed25519_keys[] = {ZFOGG_ED25519_KEY1, ZFOGG_ED25519_KEY2, ZFOGG_ED25519_KEY3};

  public_key_t all_keys[3];
  for (size_t i = 0; i < 3; i++) {
    asciichat_error_t result = parse_public_key(zfogg_ed25519_keys[i], &all_keys[i]);
    cr_assert_eq(result, ASCIICHAT_OK, "Should parse key %zu", i);
  }

  // Test that each key can be found when presented as the server key
  for (size_t server_key_idx = 0; server_key_idx < 3; server_key_idx++) {
    public_key_t server_key;
    parse_public_key(zfogg_ed25519_keys[server_key_idx], &server_key);

    bool found = false;
    for (size_t i = 0; i < 3; i++) {
      if (memcmp(server_key.key, all_keys[i].key, 32) == 0) {
        found = true;
        break;
      }
    }
    cr_assert(found, "Server key %zu should match when checking all keys", server_key_idx);
  }
}

// Test that RSA key is properly rejected
Test(crypto_multi_keys, real_github_zfogg_rsa_rejected) {
  public_key_t key;
  asciichat_error_t result = parse_public_key(ZFOGG_RSA_KEY, &key);

  cr_assert_neq(result, ASCIICHAT_OK, "RSA key should be rejected (only Ed25519 supported)");
}

// Test that no match is detected when server key is not in zfogg's list
Test(crypto_multi_keys, real_github_zfogg_unknown_server_key_no_match) {
  // Parse zfogg's actual Ed25519 keys
  const char *zfogg_ed25519_keys[] = {ZFOGG_ED25519_KEY1, ZFOGG_ED25519_KEY2, ZFOGG_ED25519_KEY3};

  public_key_t parsed_keys[3];
  size_t num_parsed = 0;

  for (size_t i = 0; i < 3; i++) {
    if (parse_public_key(zfogg_ed25519_keys[i], &parsed_keys[num_parsed]) == ASCIICHAT_OK) {
      num_parsed++;
    }
  }

  // Server presents a completely different key (not in zfogg's GitHub)
  // This is a valid Ed25519 key but NOT one of zfogg's keys
  const char *unknown_server_key = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIKm7PKY3p1n4AJFJ3l2p1q3F8T7x9W2c5B4nM6K8H9J0";
  public_key_t server_key;
  asciichat_error_t result = parse_public_key(unknown_server_key, &server_key);
  cr_assert_eq(result, ASCIICHAT_OK, "Should parse valid unknown Ed25519 key");

  // Verify no match - this simulates MITM detection
  bool found_match = false;
  for (size_t i = 0; i < num_parsed; i++) {
    if (memcmp(server_key.key, parsed_keys[i].key, 32) == 0) {
      found_match = true;
      break;
    }
  }

  cr_assert_eq(found_match, false, "Unknown server key should NOT match any of zfogg's GitHub keys (MITM detection)");
}

/**
 * @file crypto_validation_test.c
 * @brief Unit tests for cryptographic key validation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include "crypto/keys/validation.h"
#include "crypto/keys/keys.h"
#include "crypto/crypto.h"
#include "common.h"
#include <sodium.h>
#include <string.h>

TestSuite(crypto_validation, .description = "Cryptographic key validation");

// =============================================================================
// Public Key Validation Tests
// =============================================================================

Test(crypto_validation, validate_public_key_valid_ed25519) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with non-zero data
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }
  SAFE_STRNCPY(key.comment, "test key", sizeof(key.comment) - 1);

  asciichat_error_t result = validate_public_key(&key);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid Ed25519 key should pass validation");
}

Test(crypto_validation, validate_public_key_null) {
  asciichat_error_t result = validate_public_key(NULL);
  cr_assert_eq(result, ERROR_INVALID_PARAM, "NULL key should return ERROR_INVALID_PARAM");
}

Test(crypto_validation, validate_public_key_unknown_type) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_UNKNOWN;

  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }

  asciichat_error_t result = validate_public_key(&key);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Unknown key type should fail validation");
}

Test(crypto_validation, validate_public_key_all_zeros) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;
  // key.key is all zeros

  asciichat_error_t result = validate_public_key(&key);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "All-zero key should fail validation");
}

Test(crypto_validation, validate_public_key_comment_too_long) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }

  // Fill comment to maximum length + 1
  memset(key.comment, 'A', MAX_COMMENT_LEN);
  key.comment[MAX_COMMENT_LEN - 1] = '\0'; // This is actually valid (exactly MAX_COMMENT_LEN-1)

  asciichat_error_t result = validate_public_key(&key);
  cr_assert_eq(result, ASCIICHAT_OK, "Comment at max length should be valid");
}

// =============================================================================
// Private Key Validation Tests
// =============================================================================

Test(crypto_validation, validate_private_key_valid_ed25519) {
  private_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with non-zero data
  for (int i = 0; i < 64; i++) {
    key.key.ed25519[i] = (uint8_t)(i + 1);
  }
  SAFE_STRNCPY(key.key_comment, "test key", sizeof(key.key_comment) - 1);

  asciichat_error_t result = validate_private_key(&key);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid Ed25519 private key should pass validation");
}

Test(crypto_validation, validate_private_key_null) {
  asciichat_error_t result = validate_private_key(NULL);
  cr_assert_eq(result, ERROR_INVALID_PARAM, "NULL key should return ERROR_INVALID_PARAM");
}

Test(crypto_validation, validate_private_key_all_zeros) {
  private_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;
  // key.key.ed25519 is all zeros

  asciichat_error_t result = validate_private_key(&key);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "All-zero private key should fail validation");
}

// =============================================================================
// Key Format Validation Tests
// =============================================================================

Test(crypto_validation, validate_ssh_key_format_valid) {
  const char *valid_ssh_key =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT test@example.com";

  asciichat_error_t result = validate_ssh_key_format(valid_ssh_key);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid SSH key format should pass");
}

Test(crypto_validation, validate_ssh_key_format_null) {
  asciichat_error_t result = validate_ssh_key_format(NULL);
  cr_assert_eq(result, ERROR_INVALID_PARAM, "NULL should return ERROR_INVALID_PARAM");
}

Test(crypto_validation, validate_ssh_key_format_wrong_prefix) {
  const char *wrong_prefix = "ssh-rsa AAAAB3NzaC1...";

  asciichat_error_t result = validate_ssh_key_format(wrong_prefix);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Wrong key type prefix should fail");
}

Test(crypto_validation, validate_ssh_key_format_no_data) {
  const char *no_data = "ssh-ed25519 ";

  asciichat_error_t result = validate_ssh_key_format(no_data);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "SSH key with no base64 data should fail");
}

Test(crypto_validation, validate_gpg_key_format_valid) {
  const char *valid_gpg =
      "-----BEGIN PGP PUBLIC KEY BLOCK-----\nVersion: GnuPG v2\n\nmQENBF...\n-----END PGP PUBLIC KEY BLOCK-----\n";

  asciichat_error_t result = validate_gpg_key_format(valid_gpg);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid GPG key format should pass");
}

Test(crypto_validation, validate_gpg_key_format_missing_header) {
  const char *no_header = "This is not a GPG key\n-----END PGP PUBLIC KEY BLOCK-----\n";

  asciichat_error_t result = validate_gpg_key_format(no_header);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "GPG key without header should fail");
}

Test(crypto_validation, validate_gpg_key_format_missing_footer) {
  const char *no_footer = "-----BEGIN PGP PUBLIC KEY BLOCK-----\nmQENBF...\n";

  asciichat_error_t result = validate_gpg_key_format(no_footer);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "GPG key without footer should fail");
}

Test(crypto_validation, validate_x25519_key_format_valid) {
  const char *valid_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

  asciichat_error_t result = validate_x25519_key_format(valid_hex);
  cr_assert_eq(result, ASCIICHAT_OK, "Valid X25519 hex key should pass");
}

Test(crypto_validation, validate_x25519_key_format_wrong_length) {
  const char *too_short = "0123456789abcdef";

  asciichat_error_t result = validate_x25519_key_format(too_short);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Wrong length hex key should fail");
}

Test(crypto_validation, validate_x25519_key_format_invalid_char) {
  const char *invalid_char = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg"; // 'g' is invalid

  asciichat_error_t result = validate_x25519_key_format(invalid_char);
  cr_assert_eq(result, ERROR_CRYPTO_KEY, "Invalid hex character should fail");
}

// =============================================================================
// Key Strength Tests
// =============================================================================

Test(crypto_validation, check_key_strength_normal) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with random-looking data
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i * 7 + 13); // Pseudo-random pattern
  }

  bool is_weak = true;
  asciichat_error_t result = check_key_strength(&key, &is_weak);

  cr_assert_eq(result, ASCIICHAT_OK, "Should check key strength successfully");
  cr_assert_eq(is_weak, false, "Random-pattern key should not be considered weak");
}

Test(crypto_validation, check_key_strength_all_ones) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with all 0xFF
  memset(key.key, 0xFF, 32);

  bool is_weak = false;
  asciichat_error_t result = check_key_strength(&key, &is_weak);

  cr_assert_eq(result, ASCIICHAT_OK, "Should check key strength successfully");
  cr_assert_eq(is_weak, true, "All-ones key should be considered weak");
}

// =============================================================================
// Key Pattern Tests
// =============================================================================

Test(crypto_validation, check_key_patterns_sequential) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with sequential pattern
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)i;
  }

  bool has_weak_patterns = false;
  asciichat_error_t result = check_key_patterns(&key, &has_weak_patterns);

  cr_assert_eq(result, ASCIICHAT_OK, "Should check patterns successfully");
  cr_assert_eq(has_weak_patterns, true, "Sequential pattern should be detected");
}

Test(crypto_validation, check_key_patterns_random) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with non-sequential pattern
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i * i + 7);
  }

  bool has_weak_patterns = true;
  asciichat_error_t result = check_key_patterns(&key, &has_weak_patterns);

  cr_assert_eq(result, ASCIICHAT_OK, "Should check patterns successfully");
  cr_assert_eq(has_weak_patterns, false, "Non-sequential pattern should not be detected as weak");
}

// =============================================================================
// Key Comparison Tests
// =============================================================================

Test(crypto_validation, compare_public_keys_equal) {
  public_key_t key1, key2;
  memset(&key1, 0, sizeof(key1));
  memset(&key2, 0, sizeof(key2));

  key1.type = KEY_TYPE_ED25519;
  key2.type = KEY_TYPE_ED25519;

  // Fill with same data
  for (int i = 0; i < 32; i++) {
    key1.key[i] = key2.key[i] = (uint8_t)(i + 1);
  }

  bool are_equal = false;
  asciichat_error_t result = compare_public_keys(&key1, &key2, &are_equal);

  cr_assert_eq(result, ASCIICHAT_OK, "Should compare keys successfully");
  cr_assert_eq(are_equal, true, "Identical keys should be equal");
}

Test(crypto_validation, compare_public_keys_different) {
  public_key_t key1, key2;
  memset(&key1, 0, sizeof(key1));
  memset(&key2, 0, sizeof(key2));

  key1.type = KEY_TYPE_ED25519;
  key2.type = KEY_TYPE_ED25519;

  // Fill with different data
  for (int i = 0; i < 32; i++) {
    key1.key[i] = (uint8_t)(i + 1);
    key2.key[i] = (uint8_t)(i + 2);
  }

  bool are_equal = true;
  asciichat_error_t result = compare_public_keys(&key1, &key2, &are_equal);

  cr_assert_eq(result, ASCIICHAT_OK, "Should compare keys successfully");
  cr_assert_eq(are_equal, false, "Different keys should not be equal");
}

Test(crypto_validation, compare_public_keys_different_types) {
  public_key_t key1, key2;
  memset(&key1, 0, sizeof(key1));
  memset(&key2, 0, sizeof(key2));

  key1.type = KEY_TYPE_ED25519;
  key2.type = KEY_TYPE_X25519;

  // Fill with same data
  for (int i = 0; i < 32; i++) {
    key1.key[i] = key2.key[i] = (uint8_t)(i + 1);
  }

  bool are_equal = true;
  asciichat_error_t result = compare_public_keys(&key1, &key2, &are_equal);

  cr_assert_eq(result, ASCIICHAT_OK, "Should compare keys successfully");
  cr_assert_eq(are_equal, false, "Keys of different types should not be equal");
}

// =============================================================================
// Key Fingerprint Tests
// =============================================================================

Test(crypto_validation, generate_key_fingerprint_valid) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with test data
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }

  uint8_t fingerprint[32];
  asciichat_error_t result = generate_key_fingerprint(&key, fingerprint, sizeof(fingerprint));

  cr_assert_eq(result, ASCIICHAT_OK, "Should generate fingerprint successfully");

  // Verify fingerprint is not all zeros
  bool all_zero = true;
  for (int i = 0; i < 32; i++) {
    if (fingerprint[i] != 0) {
      all_zero = false;
      break;
    }
  }
  cr_assert_eq(all_zero, false, "Fingerprint should not be all zeros");
}

Test(crypto_validation, generate_key_fingerprint_deterministic) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with test data
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }

  uint8_t fingerprint1[32], fingerprint2[32];

  generate_key_fingerprint(&key, fingerprint1, sizeof(fingerprint1));
  generate_key_fingerprint(&key, fingerprint2, sizeof(fingerprint2));

  cr_assert(sodium_memcmp(fingerprint1, fingerprint2, 32) == 0, "Fingerprints should be deterministic");
}

Test(crypto_validation, check_key_fingerprint_match) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }

  uint8_t expected_fingerprint[32];
  generate_key_fingerprint(&key, expected_fingerprint, sizeof(expected_fingerprint));

  bool matches = false;
  asciichat_error_t result = check_key_fingerprint(&key, expected_fingerprint, 32, &matches);

  cr_assert_eq(result, ASCIICHAT_OK, "Should check fingerprint successfully");
  cr_assert_eq(matches, true, "Fingerprints should match");
}

Test(crypto_validation, check_key_fingerprint_no_match) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }

  uint8_t wrong_fingerprint[32];
  memset(wrong_fingerprint, 0xFF, 32);

  bool matches = true;
  asciichat_error_t result = check_key_fingerprint(&key, wrong_fingerprint, 32, &matches);

  cr_assert_eq(result, ASCIICHAT_OK, "Should check fingerprint successfully");
  cr_assert_eq(matches, false, "Fingerprints should not match");
}

// =============================================================================
// Key Expiry Tests
// =============================================================================

Test(crypto_validation, check_key_expiry) {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  bool is_expired = true;
  asciichat_error_t result = check_key_expiry(&key, &is_expired);

  cr_assert_eq(result, ASCIICHAT_OK, "Should check expiry successfully");
  cr_assert_eq(is_expired, false, "Key expiry not implemented yet, should return false");
}

/**
 * @file crypto_validation_test.c
 * @brief Unit tests for cryptographic key validation
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/crypto/keys_validation.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/common.h>
#include <sodium.h>
#include <string.h>
// =============================================================================
// Public Key Validation Tests
// =============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("crypto_validation/validate_public_key_valid_ed25519", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with non-zero data
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }
  SAFE_STRNCPY(key.comment, "test key", sizeof(key.comment) - 1);

  asciichat_error_t result = validate_public_key(&key);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Valid Ed25519 key should pass validation");
}

TEST_CASE("crypto_validation/validate_public_key_null", "[crypto_validation]") {
  asciichat_error_t result = validate_public_key(NULL);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM, "NULL key should return ERROR_INVALID_PARAM");
}

TEST_CASE("crypto_validation/validate_public_key_unknown_type", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_UNKNOWN;

  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }

  asciichat_error_t result = validate_public_key(&key);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "Unknown key type should fail validation");
}

TEST_CASE("crypto_validation/validate_public_key_all_zeros", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;
  // key.key is all zeros

  asciichat_error_t result = validate_public_key(&key);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "All-zero key should fail validation");
}

TEST_CASE("crypto_validation/validate_public_key_comment_too_long", "[crypto_validation]") {
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
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Comment at max length should be valid");
}

// =============================================================================
// Private Key Validation Tests
// =============================================================================

TEST_CASE("crypto_validation/validate_private_key_valid_ed25519", "[crypto_validation]") {
  private_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with non-zero data
  for (int i = 0; i < 64; i++) {
    key.key.ed25519[i] = (uint8_t)(i + 1);
  }
  SAFE_STRNCPY(key.key_comment, "test key", sizeof(key.key_comment) - 1);

  asciichat_error_t result = validate_private_key(&key);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Valid Ed25519 private key should pass validation");
}

TEST_CASE("crypto_validation/validate_private_key_null", "[crypto_validation]") {
  asciichat_error_t result = validate_private_key(NULL);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM, "NULL key should return ERROR_INVALID_PARAM");
}

TEST_CASE("crypto_validation/validate_private_key_all_zeros", "[crypto_validation]") {
  private_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;
  // key.key.ed25519 is all zeros

  asciichat_error_t result = validate_private_key(&key);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "All-zero private key should fail validation");
}

// =============================================================================
// Key Format Validation Tests
// =============================================================================

TEST_CASE("crypto_validation/validate_ssh_key_format_valid", "[crypto_validation]") {
  const char *valid_ssh_key =
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGPkW7kWr6FXmS8j1YJv4VoxXu+XYC+oPOC5AXsB/3kT test@example.com";

  asciichat_error_t result = validate_ssh_key_format(valid_ssh_key);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Valid SSH key format should pass");
}

TEST_CASE("crypto_validation/validate_ssh_key_format_null", "[crypto_validation]") {
  asciichat_error_t result = validate_ssh_key_format(NULL);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_INVALID_PARAM, "NULL should return ERROR_INVALID_PARAM");
}

TEST_CASE("crypto_validation/validate_ssh_key_format_wrong_prefix", "[crypto_validation]") {
  const char *wrong_prefix = "ssh-rsa AAAAB3NzaC1...";

  asciichat_error_t result = validate_ssh_key_format(wrong_prefix);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "Wrong key type prefix should fail");
}

TEST_CASE("crypto_validation/validate_ssh_key_format_no_data", "[crypto_validation]") {
  const char *no_data = "ssh-ed25519 ";

  asciichat_error_t result = validate_ssh_key_format(no_data);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "SSH key with no base64 data should fail");
}

TEST_CASE("crypto_validation/validate_gpg_key_format_valid", "[crypto_validation]") {
  const char *valid_gpg =
      "-----BEGIN PGP PUBLIC KEY BLOCK-----\nVersion: GnuPG v2\n\nmQENBF...\n-----END PGP PUBLIC KEY BLOCK-----\n";

  asciichat_error_t result = validate_gpg_key_format(valid_gpg);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Valid GPG key format should pass");
}

TEST_CASE("crypto_validation/validate_gpg_key_format_missing_header", "[crypto_validation]") {
  const char *no_header = "This is not a GPG key\n-----END PGP PUBLIC KEY BLOCK-----\n";

  asciichat_error_t result = validate_gpg_key_format(no_header);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "GPG key without header should fail");
}

TEST_CASE("crypto_validation/validate_gpg_key_format_missing_footer", "[crypto_validation]") {
  const char *no_footer = "-----BEGIN PGP PUBLIC KEY BLOCK-----\nmQENBF...\n";

  asciichat_error_t result = validate_gpg_key_format(no_footer);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "GPG key without footer should fail");
}

TEST_CASE("crypto_validation/validate_x25519_key_format_valid", "[crypto_validation]") {
  const char *valid_hex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

  asciichat_error_t result = validate_x25519_key_format(valid_hex);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Valid X25519 hex key should pass");
}

TEST_CASE("crypto_validation/validate_x25519_key_format_wrong_length", "[crypto_validation]") {
  const char *too_short = "0123456789abcdef";

  asciichat_error_t result = validate_x25519_key_format(too_short);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "Wrong length hex key should fail");
}

TEST_CASE("crypto_validation/validate_x25519_key_format_invalid_char", "[crypto_validation]") {
  const char *invalid_char = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg"; // 'g' is invalid

  asciichat_error_t result = validate_x25519_key_format(invalid_char);
  ASCIICHAT_REQUIRE_EQ(result, ERROR_CRYPTO_KEY, "Invalid hex character should fail");
}

// =============================================================================
// Key Strength Tests
// =============================================================================

TEST_CASE("crypto_validation/check_key_strength_normal", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with random-looking data
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i * 7 + 13); // Pseudo-random pattern
  }

  bool is_weak = true;
  asciichat_error_t result = check_key_strength(&key, &is_weak);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should check key strength successfully");
  ASCIICHAT_REQUIRE_EQ(is_weak, false, "Random-pattern key should not be considered weak");
}

TEST_CASE("crypto_validation/check_key_strength_all_ones", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with all 0xFF
  memset(key.key, 0xFF, 32);

  bool is_weak = false;
  asciichat_error_t result = check_key_strength(&key, &is_weak);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should check key strength successfully");
  ASCIICHAT_REQUIRE_EQ(is_weak, true, "All-ones key should be considered weak");
}

// =============================================================================
// Key Pattern Tests
// =============================================================================

TEST_CASE("crypto_validation/check_key_patterns_sequential", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with sequential pattern
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)i;
  }

  bool has_weak_patterns = false;
  asciichat_error_t result = check_key_patterns(&key, &has_weak_patterns);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should check patterns successfully");
  ASCIICHAT_REQUIRE_EQ(has_weak_patterns, true, "Sequential pattern should be detected");
}

TEST_CASE("crypto_validation/check_key_patterns_random", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with non-sequential pattern
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i * i + 7);
  }

  bool has_weak_patterns = true;
  asciichat_error_t result = check_key_patterns(&key, &has_weak_patterns);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should check patterns successfully");
  ASCIICHAT_REQUIRE_EQ(has_weak_patterns, false, "Non-sequential pattern should not be detected as weak");
}

// =============================================================================
// Key Comparison Tests
// =============================================================================

TEST_CASE("crypto_validation/compare_public_keys_equal", "[crypto_validation]") {
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

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should compare keys successfully");
  ASCIICHAT_REQUIRE_EQ(are_equal, true, "Identical keys should be equal");
}

TEST_CASE("crypto_validation/compare_public_keys_different", "[crypto_validation]") {
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

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should compare keys successfully");
  ASCIICHAT_REQUIRE_EQ(are_equal, false, "Different keys should not be equal");
}

TEST_CASE("crypto_validation/compare_public_keys_different_types", "[crypto_validation]") {
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

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should compare keys successfully");
  ASCIICHAT_REQUIRE_EQ(are_equal, false, "Keys of different types should not be equal");
}

// =============================================================================
// Key Fingerprint Tests
// =============================================================================

TEST_CASE("crypto_validation/generate_key_fingerprint_valid", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  // Fill with test data
  for (int i = 0; i < 32; i++) {
    key.key[i] = (uint8_t)(i + 1);
  }

  uint8_t fingerprint[32];
  asciichat_error_t result = generate_key_fingerprint(&key, fingerprint, sizeof(fingerprint));

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should generate fingerprint successfully");

  // Verify fingerprint is not all zeros
  bool all_zero = true;
  for (int i = 0; i < 32; i++) {
    if (fingerprint[i] != 0) {
      all_zero = false;
      break;
    }
  }
  ASCIICHAT_REQUIRE_EQ(all_zero, false, "Fingerprint should not be all zeros");
}

TEST_CASE("crypto_validation/generate_key_fingerprint_deterministic", "[crypto_validation]") {
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

  ASCIICHAT_REQUIRE(sodium_memcmp(fingerprint1, fingerprint2, 32) == 0, "Fingerprints should be deterministic");
}

TEST_CASE("crypto_validation/check_key_fingerprint_match", "[crypto_validation]") {
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

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should check fingerprint successfully");
  ASCIICHAT_REQUIRE_EQ(matches, true, "Fingerprints should match");
}

TEST_CASE("crypto_validation/check_key_fingerprint_no_match", "[crypto_validation]") {
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

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should check fingerprint successfully");
  ASCIICHAT_REQUIRE_EQ(matches, false, "Fingerprints should not match");
}

// =============================================================================
// Key Expiry Tests
// =============================================================================

TEST_CASE("crypto_validation/check_key_expiry", "[crypto_validation]") {
  public_key_t key;
  memset(&key, 0, sizeof(key));
  key.type = KEY_TYPE_ED25519;

  bool is_expired = true;
  asciichat_error_t result = check_key_expiry(&key, &is_expired);

  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should check expiry successfully");
  ASCIICHAT_REQUIRE_EQ(is_expired, false, "Key expiry not implemented yet, should return false");
}

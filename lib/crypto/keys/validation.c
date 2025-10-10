#include "validation.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include <sodium.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifdef _WIN32
#include <sys/stat.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

// =============================================================================
// Key Validation Implementation
// =============================================================================

asciichat_error_t validate_public_key(const public_key_t *key) {
  if (!key) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key=%p", key);
    return ERROR_INVALID_PARAM;
  }

  // Check key type
  if (key->type == KEY_TYPE_UNKNOWN) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Key type is unknown");
    return ERROR_CRYPTO_KEY;
  }

  if (key->type != KEY_TYPE_ED25519 && key->type != KEY_TYPE_X25519 && key->type != KEY_TYPE_GPG) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported key type: %d", key->type);
    return ERROR_CRYPTO_KEY;
  }

  // Check key data is not all zeros
  bool is_zero = true;
  for (int i = 0; i < 32; i++) {
    if (key->key[i] != 0) {
      is_zero = false;
      break;
    }
  }

  if (is_zero) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Key data is all zeros");
    return ERROR_CRYPTO_KEY;
  }

  // Check comment length
  if (strlen(key->comment) >= MAX_COMMENT_LEN) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Key comment too long: %zu (maximum %d)",
                  strlen(key->comment), MAX_COMMENT_LEN - 1);
    return ERROR_CRYPTO_KEY;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t validate_private_key(const private_key_t *key) {
  if (!key) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key=%p", key);
    return ERROR_INVALID_PARAM;
  }

  // Check key type
  if (key->type == KEY_TYPE_UNKNOWN) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Private key type is unknown");
    return ERROR_CRYPTO_KEY;
  }

  if (key->type != KEY_TYPE_ED25519 && key->type != KEY_TYPE_X25519) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported private key type: %d", key->type);
    return ERROR_CRYPTO_KEY;
  }

  // Check key data is not all zeros
  bool is_zero = true;
  size_t key_size = (key->type == KEY_TYPE_ED25519) ? 64 : 32;

  for (size_t i = 0; i < key_size; i++) {
    if (key->key.ed25519[i] != 0) {
      is_zero = false;
      break;
    }
  }

  if (is_zero) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Private key data is all zeros");
    return ERROR_CRYPTO_KEY;
  }

  // Check comment length
  if (strlen(key->key_comment) >= MAX_COMMENT_LEN) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Private key comment too long: %zu (maximum %d)",
                  strlen(key->key_comment), MAX_COMMENT_LEN - 1);
    return ERROR_CRYPTO_KEY;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t check_key_expiry(const public_key_t *key, bool *is_expired) {
  if (!key || !is_expired) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key=%p, is_expired=%p", key, is_expired);
    return ERROR_INVALID_PARAM;
  }

  // For now, we don't implement key expiry checking
  // This would require parsing key creation dates and expiration times
  *is_expired = false;

  return ASCIICHAT_OK;
}

asciichat_error_t validate_key_security(const char *key_path) {
  if (!key_path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
    return ERROR_INVALID_PARAM;
  }

  // Check file permissions
  asciichat_error_t perm_result = validate_key_permissions(key_path);
  if (perm_result != ASCIICHAT_OK) {
    return perm_result;
  }

  // TODO: Add more security checks
  // - Check for weak key patterns
  // - Validate key strength
  // - Check for known weak keys

  return ASCIICHAT_OK;
}

// =============================================================================
// Key Format Validation
// =============================================================================

asciichat_error_t validate_ssh_key_format(const char *key_text) {
  if (!key_text) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_text=%p", key_text);
    return ERROR_INVALID_PARAM;
  }

  // Check for SSH key format
  if (strncmp(key_text, "ssh-ed25519 ", 12) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "SSH key does not start with 'ssh-ed25519 '");
    return ERROR_CRYPTO_KEY;
  }

  // Check for base64 data
  const char *base64_start = key_text + 12;
  while (*base64_start == ' ' || *base64_start == '\t') {
    base64_start++;
  }

  if (*base64_start == '\0' || *base64_start == '\n' || *base64_start == '\r') {
    SET_ERRNO(ERROR_CRYPTO_KEY, "SSH key has no base64 data");
    return ERROR_CRYPTO_KEY;
  }

  // TODO: Add more comprehensive SSH key validation
  // - Validate base64 encoding
  // - Check key blob structure
  // - Verify key length

  return ASCIICHAT_OK;
}

asciichat_error_t validate_gpg_key_format(const char *key_text) {
  if (!key_text) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_text=%p", key_text);
    return ERROR_INVALID_PARAM;
  }

  // Check for GPG armor header
  if (strncmp(key_text, "-----BEGIN PGP", 14) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "GPG key does not start with armor header");
    return ERROR_CRYPTO_KEY;
  }

  // Check for GPG armor footer
  if (strstr(key_text, "-----END PGP") == NULL) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "GPG key does not contain armor footer");
    return ERROR_CRYPTO_KEY;
  }

  // TODO: Add more comprehensive GPG key validation
  // - Validate armor format
  // - Check base64 encoding
  // - Verify packet structure

  return ASCIICHAT_OK;
}

asciichat_error_t validate_x25519_key_format(const char *key_hex) {
  if (!key_hex) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_hex=%p", key_hex);
    return ERROR_INVALID_PARAM;
  }

  // Check hex string length (32 bytes = 64 hex characters)
  size_t hex_len = strlen(key_hex);
  if (hex_len != 64) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "X25519 key has invalid length: %zu (expected 64)", hex_len);
    return ERROR_CRYPTO_KEY;
  }

  // Check that all characters are valid hex
  for (size_t i = 0; i < hex_len; i++) {
    if (!isxdigit(key_hex[i])) {
      SET_ERRNO(ERROR_CRYPTO_KEY, "X25519 key contains invalid hex character at position %zu: '%c'",
                    i, key_hex[i]);
      return ERROR_CRYPTO_KEY;
    }
  }

  return ASCIICHAT_OK;
}

// =============================================================================
// Key Security Checks
// =============================================================================

asciichat_error_t check_key_strength(const public_key_t *key, bool *is_weak) {
  if (!key || !is_weak) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key=%p, is_weak=%p", key, is_weak);
    return ERROR_INVALID_PARAM;
  }

  *is_weak = false;

  // Check for all zeros (already checked in validate_public_key)
  // Check for all ones
  bool is_ones = true;
  for (int i = 0; i < 32; i++) {
    if (key->key[i] != 0xFF) {
      is_ones = false;
      break;
    }
  }

  if (is_ones) {
    *is_weak = true;
    return ASCIICHAT_OK;
  }

  // TODO: Add more weak key pattern detection
  // - Check for sequential patterns
  // - Check for known weak keys
  // - Validate key entropy

  return ASCIICHAT_OK;
}

asciichat_error_t validate_key_permissions(const char *key_path) {
  if (!key_path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
    return ERROR_INVALID_PARAM;
  }

#ifndef _WIN32
  struct stat st;
  if (stat(key_path, &st) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot stat key file: %s", key_path);
    return ERROR_CRYPTO_KEY;
  }

  // Check for overly permissive permissions
  if ((st.st_mode & SSH_KEY_PERMISSIONS_MASK) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY,
                  "Key file has overly permissive permissions: %o (recommended: 600)",
                  st.st_mode & 0777);
    return ERROR_CRYPTO_KEY;
  }
#endif

  return ASCIICHAT_OK;
}

asciichat_error_t check_key_patterns(const public_key_t *key, bool *has_weak_patterns) {
  if (!key || !has_weak_patterns) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key=%p, has_weak_patterns=%p",
                  key, has_weak_patterns);
    return ERROR_INVALID_PARAM;
  }

  *has_weak_patterns = false;

  // Check for sequential patterns
  bool is_sequential = true;
  for (int i = 1; i < 32; i++) {
    if (key->key[i] != key->key[i-1] + 1) {
      is_sequential = false;
      break;
    }
  }

  if (is_sequential) {
    *has_weak_patterns = true;
    return ASCIICHAT_OK;
  }

  // TODO: Add more pattern detection
  // - Check for repeated patterns
  // - Check for known weak sequences
  // - Validate key randomness

  return ASCIICHAT_OK;
}

// =============================================================================
// Key Comparison and Matching
// =============================================================================

asciichat_error_t compare_public_keys(const public_key_t *key1, const public_key_t *key2, bool *are_equal) {
  if (!key1 || !key2 || !are_equal) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key1=%p, key2=%p, are_equal=%p",
                  key1, key2, are_equal);
    return ERROR_INVALID_PARAM;
  }

  *are_equal = false;

  // Compare key types
  if (key1->type != key2->type) {
    return ASCIICHAT_OK;
  }

  // Compare key data using constant-time comparison
  if (sodium_memcmp(key1->key, key2->key, 32) == 0) {
    *are_equal = true;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t check_key_fingerprint(const public_key_t *key, const uint8_t *fingerprint,
                                              size_t fingerprint_len, bool *matches) {
  if (!key || !fingerprint || !matches) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key=%p, fingerprint=%p, matches=%p",
                  key, fingerprint, matches);
    return ERROR_INVALID_PARAM;
  }

  *matches = false;

  // Generate fingerprint for the key
  uint8_t key_fingerprint[32];
  asciichat_error_t fingerprint_result = generate_key_fingerprint(key, key_fingerprint, 32);
  if (fingerprint_result != ASCIICHAT_OK) {
    return fingerprint_result;
  }

  // Compare fingerprints
  size_t compare_len = (fingerprint_len < 32) ? fingerprint_len : 32;
  if (sodium_memcmp(key_fingerprint, fingerprint, compare_len) == 0) {
    *matches = true;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t generate_key_fingerprint(const public_key_t *key, uint8_t *fingerprint_out,
                                                size_t fingerprint_size) {
  if (!key || !fingerprint_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key=%p, fingerprint_out=%p",
                  key, fingerprint_out);
    return ERROR_INVALID_PARAM;
  }

  if (fingerprint_size < 32) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Fingerprint buffer too small: %zu (minimum 32)", fingerprint_size);
    return ERROR_INVALID_PARAM;
  }

  // Generate SHA-256 fingerprint of the key
  if (crypto_hash_sha256(fingerprint_out, key->key, 32) != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to generate key fingerprint");
    return ERROR_CRYPTO;
  }

  return ASCIICHAT_OK;
}

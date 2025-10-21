#include "gpg_keys.h"
#include "validation.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include "../../platform/string.h"
#include <sodium.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// GPG Key Parsing Implementation
// =============================================================================

asciichat_error_t parse_gpg_key(const char *gpg_key_text, public_key_t *key_out) {
  if (!gpg_key_text || !key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_text=%p, key_out=%p", gpg_key_text, key_out);
    return ERROR_INVALID_PARAM;
  }

  // Validate GPG key format first
  asciichat_error_t validation_result = validate_gpg_key_format(gpg_key_text);
  if (validation_result != ASCIICHAT_OK) {
    return validation_result;
  }

  // Extract Ed25519 public key from GPG key
  uint8_t ed25519_pk[32];
  asciichat_error_t extract_result = extract_ed25519_from_gpg(gpg_key_text, ed25519_pk);
  if (extract_result != ASCIICHAT_OK) {
    return extract_result;
  }

  // Initialize the public key structure
  memset(key_out, 0, sizeof(public_key_t));
  key_out->type = KEY_TYPE_GPG;
  memcpy(key_out->key, ed25519_pk, 32);

  // Extract key comment/email for display
  char comment[256];
  if (extract_gpg_key_comment(gpg_key_text, comment, sizeof(comment)) == ASCIICHAT_OK) {
    SAFE_STRNCPY(key_out->comment, comment, sizeof(key_out->comment) - 1);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t parse_gpg_key_binary(const uint8_t *gpg_key_binary, size_t key_size, public_key_t *key_out) {
  if (!gpg_key_binary || !key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_binary=%p, key_out=%p", gpg_key_binary, key_out);
    return ERROR_INVALID_PARAM;
  }

  if (key_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid key size: %zu", key_size);
    return ERROR_INVALID_PARAM;
  }

  // TODO: Implement binary GPG key parsing
  // This requires parsing the OpenPGP packet format
  SET_ERRNO(ERROR_CRYPTO_KEY, "Binary GPG key parsing not yet implemented");
  return ERROR_CRYPTO_KEY;
}

asciichat_error_t extract_ed25519_from_gpg(const char *gpg_key_text, uint8_t ed25519_pk[32]) {
  if (!gpg_key_text || !ed25519_pk) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_text=%p, ed25519_pk=%p", gpg_key_text, ed25519_pk);
    return ERROR_INVALID_PARAM;
  }

  // TODO: Implement Ed25519 extraction from GPG key
  // This requires:
  // 1. Parse the OpenPGP packet structure
  // 2. Find the public key packet
  // 3. Extract the Ed25519 public key material
  // 4. Convert to our 32-byte format

  SET_ERRNO(ERROR_CRYPTO_KEY, "Ed25519 extraction from GPG key not yet implemented");
  return ERROR_CRYPTO_KEY;
}

asciichat_error_t gpg_to_x25519_public(const char *gpg_key_text, uint8_t x25519_pk[32]) {
  if (!gpg_key_text || !x25519_pk) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_text=%p, x25519_pk=%p", gpg_key_text, x25519_pk);
    return ERROR_INVALID_PARAM;
  }

  // First extract the Ed25519 public key from the GPG key
  uint8_t ed25519_pk[32];
  asciichat_error_t extract_result = extract_ed25519_from_gpg(gpg_key_text, ed25519_pk);
  if (extract_result != ASCIICHAT_OK) {
    return extract_result;
  }

  // Convert Ed25519 to X25519
  if (crypto_sign_ed25519_pk_to_curve25519(x25519_pk, ed25519_pk) != 0) {
    SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to convert Ed25519 to X25519");
    return ERROR_CRYPTO_KEY;
  }

  return ASCIICHAT_OK;
}

// =============================================================================
// GPG Key Operations
// =============================================================================

asciichat_error_t get_gpg_fingerprint(const char *gpg_key_text, uint8_t fingerprint_out[20]) {
  if (!gpg_key_text || !fingerprint_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_text=%p, fingerprint_out=%p", gpg_key_text,
              fingerprint_out);
    return ERROR_INVALID_PARAM;
  }

  // TODO: Implement GPG fingerprint extraction
  // This requires parsing the OpenPGP packet structure and computing SHA-1

  SET_ERRNO(ERROR_CRYPTO_KEY, "GPG fingerprint extraction not yet implemented");
  return ERROR_CRYPTO_KEY;
}

asciichat_error_t get_gpg_key_id(const char *gpg_key_text, uint8_t key_id_out[8]) {
  if (!gpg_key_text || !key_id_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_text=%p, key_id_out=%p", gpg_key_text, key_id_out);
    return ERROR_INVALID_PARAM;
  }

  // TODO: Implement GPG key ID extraction
  // This is typically the last 8 bytes of the fingerprint

  SET_ERRNO(ERROR_CRYPTO_KEY, "GPG key ID extraction not yet implemented");
  return ERROR_CRYPTO_KEY;
}

asciichat_error_t check_gpg_key_expiry(const char *gpg_key_text, bool *is_expired) {
  if (!gpg_key_text || !is_expired) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_text=%p, is_expired=%p", gpg_key_text, is_expired);
    return ERROR_INVALID_PARAM;
  }

  // TODO: Implement GPG key expiry checking
  // This requires parsing the OpenPGP packet structure and checking expiration dates

  *is_expired = false; // Default to not expired
  SET_ERRNO(ERROR_CRYPTO_KEY, "GPG key expiry checking not yet implemented");
  return ERROR_CRYPTO_KEY;
}

// =============================================================================
// GPG Key Formatting
// =============================================================================

asciichat_error_t format_gpg_key_display(const char *gpg_key_text, char *output, size_t output_size) {
  if (!gpg_key_text || !output) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_text=%p, output=%p", gpg_key_text, output);
    return ERROR_INVALID_PARAM;
  }

  if (output_size < 64) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Output buffer too small: %zu (minimum 64)", output_size);
    return ERROR_INVALID_PARAM;
  }

  // Extract key ID for display
  uint8_t key_id[8];
  asciichat_error_t key_id_result = get_gpg_key_id(gpg_key_text, key_id);
  if (key_id_result != ASCIICHAT_OK) {
    // Fallback to generic GPG key display
    SAFE_STRNCPY(output, "GPG key (key ID extraction failed)", output_size - 1);
    return ASCIICHAT_OK;
  }

  // Format as hex key ID
  char hex_key_id[17];
  for (int i = 0; i < 8; i++) {
    safe_snprintf(hex_key_id + i * 2, 3, "%02x", key_id[i]);
  }
  hex_key_id[16] = '\0';

  // Create display string
  int result = safe_snprintf(output, output_size, "GPG key ID: %s", hex_key_id);
  if (result < 0 || result >= (int)output_size) {
    SET_ERRNO(ERROR_STRING, "Failed to format GPG key display");
    return ERROR_STRING;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t extract_gpg_key_comment(const char *gpg_key_text, char *comment_out, size_t comment_size) {
  if (!gpg_key_text || !comment_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_text=%p, comment_out=%p", gpg_key_text, comment_out);
    return ERROR_INVALID_PARAM;
  }

  if (comment_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid comment size: %zu", comment_size);
    return ERROR_INVALID_PARAM;
  }

  // TODO: Implement GPG key comment extraction
  // This requires parsing the OpenPGP packet structure and extracting user ID packets

  // For now, use a generic comment
  SAFE_STRNCPY(comment_out, "GPG key", comment_size - 1);
  return ASCIICHAT_OK;
}

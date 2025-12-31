/**
 * @file crypto/keys/gpg_keys.c
 * @ingroup keys
 * @brief 🔑 GPG public key extraction and validation from GPG keyrings
 */

#include "gpg_keys.h"
#include "validation.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include "../../platform/string.h"
#include "../gpg.h" // For gpg_get_public_key()
#include <sodium.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// Platform-specific popen/pclose
#ifdef _WIN32
#define SAFE_POPEN _popen
#define SAFE_PCLOSE _pclose
#else
#define SAFE_POPEN popen
#define SAFE_PCLOSE pclose
#endif

// =============================================================================
// GPG Key Parsing Implementation
// =============================================================================

asciichat_error_t parse_gpg_key(const char *gpg_key_id, public_key_t *key_out) {
  if (!gpg_key_id || !key_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_id=%p, key_out=%p", gpg_key_id, key_out);
    return ERROR_INVALID_PARAM;
  }

  // Validate key ID format (must be 8, 16, or 40 hex characters, optionally with 0x prefix)
  // - 8 chars: short key ID (last 8 chars of fingerprint)
  // - 16 chars: long key ID (last 16 chars of fingerprint)
  // - 40 chars: full fingerprint
  const char *key_id = gpg_key_id;
  if (strncmp(key_id, "0x", 2) == 0 || strncmp(key_id, "0X", 2) == 0) {
    key_id += 2; // Skip 0x prefix
  }

  size_t key_id_len = strlen(key_id);
  if (key_id_len != 8 && key_id_len != 16 && key_id_len != 40) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid GPG key ID length: %zu (expected 8, 16, or 40 hex chars)", key_id_len);
  }

  // Validate hex characters
  for (size_t i = 0; i < key_id_len; i++) {
    char c = key_id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid GPG key ID: contains non-hex character '%c'", c);
    }
  }

  // Extract Ed25519 public key from GPG keyring using gpg --list-keys
  uint8_t ed25519_pk[32];
  asciichat_error_t extract_result = extract_ed25519_from_gpg(key_id, ed25519_pk);
  if (extract_result != ASCIICHAT_OK) {
    return extract_result;
  }

  // Initialize the public key structure
  memset(key_out, 0, sizeof(public_key_t));
  key_out->type = KEY_TYPE_GPG;
  memcpy(key_out->key, ed25519_pk, 32);

  // Set comment for display
  safe_snprintf(key_out->comment, sizeof(key_out->comment), "GPG key %s", key_id);

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

asciichat_error_t extract_ed25519_from_gpg(const char *gpg_key_id, uint8_t ed25519_pk[32]) {
  if (!gpg_key_id || !ed25519_pk) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: gpg_key_id=%p, ed25519_pk=%p", gpg_key_id, ed25519_pk);
    return ERROR_INVALID_PARAM;
  }

  // Use gpg_get_public_key() to extract Ed25519 public key from GPG keyring
  // Note: gpg_key_id should be the key ID (e.g., "7FE90A79F2E80ED3")
  char keygrip[64];
  if (gpg_get_public_key(gpg_key_id, ed25519_pk, keygrip) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to extract Ed25519 public key from GPG for key ID: %s", gpg_key_id);
  }

  return ASCIICHAT_OK;
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

  // For GPG key expiry checking, we need a key ID
  // If gpg_key_text is a key ID (8, 16, or 40 hex chars), use it directly
  // Otherwise, we'd need to parse the GPG armored text (complex)
  size_t key_len = strlen(gpg_key_text);
  const char *key_id = gpg_key_text;

  // Check if it looks like a key ID (hex characters only)
  bool is_hex = true;
  for (size_t i = 0; i < key_len; i++) {
    if (!((gpg_key_text[i] >= '0' && gpg_key_text[i] <= '9') || (gpg_key_text[i] >= 'A' && gpg_key_text[i] <= 'F') ||
          (gpg_key_text[i] >= 'a' && gpg_key_text[i] <= 'f'))) {
      is_hex = false;
      break;
    }
  }

  // Only support key ID format (8, 16, or 40 hex chars)
  if (!is_hex || (key_len != 8 && key_len != 16 && key_len != 40)) {
    log_warn("check_gpg_key_expiry: Input is not a key ID format (expected 8/16/40 hex chars)");
    *is_expired = false; // Assume not expired if we can't check
    return ASCIICHAT_OK;
  }

  // Use gpg --list-keys with colon-separated output to check expiry
  char cmd[512];
  safe_snprintf(cmd, sizeof(cmd), "gpg --list-keys --with-colons %s 2>/dev/null", key_id);

  FILE *fp = SAFE_POPEN(cmd, "r");
  if (!fp) {
    log_error("Failed to run gpg --list-keys for key %s", key_id);
    *is_expired = false; // Assume not expired if we can't check
    return ASCIICHAT_OK;
  }

  char line[1024];
  bool found_pub = false;
  *is_expired = false;

  // Parse colon-separated output
  // Format: pub:trust:keylen:algo:keyid:creation:expiry:...
  // Field 6 is expiry timestamp (seconds since epoch), or empty if no expiry
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "pub:", 4) == 0) {
      found_pub = true;

      // Split line by colons
      char *fields[12];
      int field_count = 0;
      char *ptr = line;
      char *field_start = ptr;

      while (*ptr && field_count < 12) {
        if (*ptr == ':' || *ptr == '\n') {
          *ptr = '\0';
          fields[field_count++] = field_start;
          field_start = ptr + 1;
        }
        ptr++;
      }

      // Field 6 is expiry date (index 6)
      if (field_count >= 7 && strlen(fields[6]) > 0) {
        // Parse expiry timestamp
        long expiry_timestamp = atol(fields[6]);
        time_t now = time(NULL);

        if (expiry_timestamp > 0 && expiry_timestamp < now) {
          *is_expired = true;
          log_warn("GPG key %s has expired (expiry: %ld, now: %ld)", key_id, expiry_timestamp, (long)now);
        } else if (expiry_timestamp > 0) {
          log_debug("GPG key %s expires at timestamp %ld (valid)", key_id, expiry_timestamp);
        } else {
          log_debug("GPG key %s has no expiration date", key_id);
        }
      } else {
        log_debug("GPG key %s has no expiration date (field empty)", key_id);
      }

      break; // Found the pub line, stop parsing
    }
  }

  SAFE_PCLOSE(fp);

  if (!found_pub) {
    log_warn("Could not find GPG key %s in keyring", key_id);
    *is_expired = false; // Assume not expired if key not found
  }

  return ASCIICHAT_OK;
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

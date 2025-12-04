/**
 * @file crypto/keys/keys.c
 * @ingroup keys
 * @brief 🔑 High-level key management interface with SSH, GPG, and HTTPS key support
 */

#include "keys.h"
#include "types.h"
#include "ssh_keys.h"
#include "gpg_keys.h"
#include "https_keys.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include "../../util/path.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// High-Level Key Parsing Functions
// =============================================================================

asciichat_error_t parse_public_key(const char *input, public_key_t *key_out) {
  if (!input || !key_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for public key parsing");
  }

  // Clear output structure
  memset(key_out, 0, sizeof(public_key_t));

  // Try SSH key parsing first
  if (strncmp(input, "ssh-ed25519", 11) == 0) {
    key_out->type = KEY_TYPE_ED25519;
    asciichat_error_t result = parse_ssh_ed25519_line(input, key_out->key);
    if (result == ASCIICHAT_OK) {
      platform_strncpy(key_out->comment, sizeof(key_out->comment), "ssh-ed25519", sizeof(key_out->comment) - 1);
    }
    return result;
  }

  // Try GPG key parsing
  if (strncmp(input, "gpg:", 4) == 0) {
    return parse_gpg_key(input + 4, key_out);
  }

  // Try HTTPS key fetching (GitHub/GitLab) - delegate to parse_public_keys and return first
  if (strncmp(input, "github:", 7) == 0 || strncmp(input, "gitlab:", 7) == 0) {
    public_key_t keys[1];
    size_t num_keys = 0;
    asciichat_error_t result = parse_public_keys(input, keys, &num_keys, 1);
    if (result == ASCIICHAT_OK && num_keys > 0) {
      *key_out = keys[0];
    }
    return result;
  }

  // Try raw hex key (64 hex chars = 32 bytes)
  if (strlen(input) == 64) {
    // Check if it's valid hex
    bool is_valid_hex = true;
    for (int i = 0; i < 64; i++) {
      if (!((input[i] >= '0' && input[i] <= '9') || (input[i] >= 'a' && input[i] <= 'f') ||
            (input[i] >= 'A' && input[i] <= 'F'))) {
        is_valid_hex = false;
        break;
      }
    }

    if (is_valid_hex) {
      // Assume it's a raw X25519 public key in hex (default for raw hex)
      key_out->type = KEY_TYPE_X25519;
      for (int i = 0; i < 32; i++) {
        char hex_byte[3] = {input[i * 2], input[i * 2 + 1], 0};
        char *endptr;
        unsigned long val = strtoul(hex_byte, &endptr, 16);
        if (endptr != hex_byte + 2 || val > 255) {
          return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid hex character in key");
        }
        key_out->key[i] = (uint8_t)val;
      }
      platform_strncpy(key_out->comment, sizeof(key_out->comment), "raw-hex", sizeof(key_out->comment) - 1);
      return ASCIICHAT_OK;
    }
  }

  if (path_looks_like_path(input)) {
    char *normalized_path = NULL;
    asciichat_error_t path_result = path_validate_user_path(input, PATH_ROLE_KEY_PUBLIC, &normalized_path);
    if (path_result != ASCIICHAT_OK) {
      SAFE_FREE(normalized_path);
      return path_result;
    }

    FILE *f = platform_fopen(normalized_path, "r");
    if (f) {
      char line[BUFFER_SIZE_LARGE];
      if (fgets(line, sizeof(line), f)) {
        (void)fclose(f);
        SAFE_FREE(normalized_path);
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;
        return parse_public_key(line, key_out);
      }
      (void)fclose(f);
    }
    SAFE_FREE(normalized_path);
  }

  return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported key format: %s", input);
}

asciichat_error_t parse_private_key(const char *key_path, private_key_t *key_out) {
  if (!key_path || !key_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for private key parsing");
  }

  // Clear output structure
  memset(key_out, 0, sizeof(private_key_t));

  // Try SSH private key parsing
  char *normalized_path = NULL;
  asciichat_error_t path_result = path_validate_user_path(key_path, PATH_ROLE_KEY_PRIVATE, &normalized_path);
  if (path_result != ASCIICHAT_OK) {
    SAFE_FREE(normalized_path);
    return path_result;
  }
  asciichat_error_t result = parse_ssh_private_key(normalized_path, key_out);
  SAFE_FREE(normalized_path);
  return result;
}

// =============================================================================
// Multi-Key Public Key Parsing
// =============================================================================

asciichat_error_t parse_public_keys(const char *input, public_key_t *keys_out, size_t *num_keys, size_t max_keys) {
  if (!input || !keys_out || !num_keys || max_keys == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for multi-key parsing");
  }

  *num_keys = 0;

  // Check for direct SSH Ed25519 key format BEFORE checking for file paths
  // (SSH keys can contain '/' in base64, which would match path_looks_like_path)
  if (strncmp(input, "ssh-ed25519", 11) == 0) {
    asciichat_error_t result = parse_public_key(input, &keys_out[0]);
    if (result == ASCIICHAT_OK) {
      *num_keys = 1;
    }
    return result;
  }

  // Check if this is a GitHub/GitLab reference - these support multiple keys
  if (strncmp(input, "github:", 7) == 0 || strncmp(input, "gitlab:", 7) == 0) {
    const char *username = input + 7; // Skip "github:" or "gitlab:"
    bool is_github = (strncmp(input, "github:", 7) == 0);
    bool is_gpg = (strstr(username, ".gpg") != NULL);

    char **keys = NULL;
    size_t num_fetched_keys = 0;
    asciichat_error_t result;

    if (is_github) {
      if (is_gpg) {
        result = fetch_github_gpg_keys(username, &keys, &num_fetched_keys);
      } else {
        result = fetch_github_ssh_keys(username, &keys, &num_fetched_keys);
      }
    } else {
      if (is_gpg) {
        result = fetch_gitlab_gpg_keys(username, &keys, &num_fetched_keys);
      } else {
        result = fetch_gitlab_ssh_keys(username, &keys, &num_fetched_keys);
      }
    }

    if (result != ASCIICHAT_OK || num_fetched_keys == 0) {
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to fetch keys from %s for user: %s", is_github ? "GitHub" : "GitLab",
                       username);
    }

    // Parse each fetched key (only Ed25519 keys will succeed)
    for (size_t i = 0; i < num_fetched_keys && *num_keys < max_keys; i++) {
      if (parse_public_key(keys[i], &keys_out[*num_keys]) == ASCIICHAT_OK) {
        (*num_keys)++;
      }
      // Non-Ed25519 keys are silently skipped
    }

    // Free the keys array
    for (size_t i = 0; i < num_fetched_keys; i++) {
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);

    if (*num_keys == 0) {
      return SET_ERRNO(ERROR_CRYPTO_KEY, "No valid Ed25519 keys found for %s user: %s", is_github ? "GitHub" : "GitLab",
                       username);
    }

    log_info("Parsed %zu Ed25519 key(s) from %s user: %s", *num_keys, is_github ? "GitHub" : "GitLab", username);
    return ASCIICHAT_OK;
  }

  // For file paths, delegate to parse_keys_from_file
  if (path_looks_like_path(input)) {
    return parse_keys_from_file(input, keys_out, num_keys, max_keys);
  }

  // For all other formats, use single-key parsing
  asciichat_error_t result = parse_public_key(input, &keys_out[0]);
  if (result == ASCIICHAT_OK) {
    *num_keys = 1;
  }
  return result;
}

// =============================================================================
// Key Conversion Functions
// =============================================================================

asciichat_error_t public_key_to_x25519(const public_key_t *key, uint8_t x25519_pk[32]) {
  if (!key || !x25519_pk) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for public_key_to_x25519");
  }

  if (key->type == KEY_TYPE_X25519) {
    // Passthrough for X25519 keys
    memcpy(x25519_pk, key->key, 32);
    return ASCIICHAT_OK;
  }

  if (key->type == KEY_TYPE_ED25519) {
    // Convert Ed25519 to X25519
    return ed25519_to_x25519_public(key->key, x25519_pk);
  }

  return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported key type for X25519 conversion: %d", key->type);
}

asciichat_error_t private_key_to_x25519(const private_key_t *key, uint8_t x25519_sk[32]) {
  if (!key || !x25519_sk) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for private_key_to_x25519");
  }

  if (key->type == KEY_TYPE_X25519) {
    // Passthrough for X25519 keys
    memcpy(x25519_sk, key->key.x25519, 32);
    return ASCIICHAT_OK;
  }

  if (key->type == KEY_TYPE_ED25519) {
    // Convert Ed25519 to X25519 (Ed25519 private key is 64 bytes: seed + public)
    return ed25519_to_x25519_private(key->key.ed25519, x25519_sk);
  }

  return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported key type for X25519 conversion: %d", key->type);
}

// =============================================================================
// HTTPS Key Fetching Wrapper Functions
// =============================================================================

asciichat_error_t fetch_github_keys(const char *username, char ***keys_out, size_t *num_keys, bool use_gpg) {
  if (!username || !keys_out || !num_keys) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for fetch_github_keys");
  }

  if (use_gpg) {
    return fetch_github_gpg_keys(username, keys_out, num_keys);
  } else {
    return fetch_github_ssh_keys(username, keys_out, num_keys);
  }
}

asciichat_error_t fetch_gitlab_keys(const char *username, char ***keys_out, size_t *num_keys, bool use_gpg) {
  if (!username || !keys_out || !num_keys) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for fetch_gitlab_keys");
  }

  if (use_gpg) {
    return fetch_gitlab_gpg_keys(username, keys_out, num_keys);
  } else {
    return fetch_gitlab_ssh_keys(username, keys_out, num_keys);
  }
}

// =============================================================================
// Key Formatting Functions
// =============================================================================

asciichat_error_t parse_keys_from_file(const char *path, public_key_t *keys, size_t *num_keys, size_t max_keys) {
  if (!path || !keys || !num_keys) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for key file parsing");
  }

  *num_keys = 0;

  if (!path_looks_like_path(path)) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid keys file path: %s", path);
  }

  char *normalized_path = NULL;
  asciichat_error_t path_result = path_validate_user_path(path, PATH_ROLE_CLIENT_KEYS, &normalized_path);
  if (path_result != ASCIICHAT_OK) {
    SAFE_FREE(normalized_path);
    return path_result;
  }

  FILE *f = platform_fopen(normalized_path, "r");
  if (!f) {
    SAFE_FREE(normalized_path);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to open keys file: %s", path);
  }

  char line[BUFFER_SIZE_LARGE];
  while (fgets(line, sizeof(line), f) && *num_keys < max_keys) {
    // Remove newline
    line[strcspn(line, "\r\n")] = 0;

    // Skip empty lines and comments
    if (strlen(line) == 0 || line[0] == '#') {
      continue;
    }

    if (parse_public_key(line, &keys[*num_keys]) == ASCIICHAT_OK) {
      (*num_keys)++;
    }
  }

  (void)fclose(f);
  SAFE_FREE(normalized_path);
  return ASCIICHAT_OK;
}

void format_public_key(const public_key_t *key, char *output, size_t output_size) {
  if (!key || !output || output_size == 0) {
    return;
  }

  if (key->type == KEY_TYPE_ED25519) {
    // Format as SSH Ed25519 public key
    // Simple base64 encoding for 32 bytes = 43 chars + padding
    // For now, just use hex encoding
    char hex_key[65];
    for (size_t i = 0; i < 32; i++) {
      char hex_byte[3];
      safe_snprintf(hex_byte, sizeof(hex_byte), "%02x", key->key[i]);
      hex_key[i * 2] = hex_byte[0];
      hex_key[i * 2 + 1] = hex_byte[1];
    }
    hex_key[64] = '\0';
    safe_snprintf(output, output_size, "ssh-ed25519 %s %s", hex_key, key->comment);
  } else if (key->type == KEY_TYPE_X25519) {
    // Format as hex X25519 key
    char hex_key[65];
    for (size_t i = 0; i < 32; i++) {
      char hex_byte[3];
      safe_snprintf(hex_byte, sizeof(hex_byte), "%02x", key->key[i]);
      hex_key[i * 2] = hex_byte[0];
      hex_key[i * 2 + 1] = hex_byte[1];
    }
    hex_key[64] = '\0';
    safe_snprintf(output, output_size, "x25519 %s", hex_key);
  } else {
    safe_snprintf(output, output_size, "unknown key type: %d", key->type);
  }
}

// =============================================================================
// Hex Encoding/Decoding Utilities
// =============================================================================

asciichat_error_t hex_decode(const char *hex, uint8_t *output, size_t output_len) {
  if (!hex || !output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for hex_decode");
  }

  size_t hex_len = strlen(hex);
  size_t expected_hex_len = output_len * 2;

  if (hex_len != expected_hex_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM,
                     "Hex string length (%zu) doesn't match expected output length (%zu * 2 = %zu)", hex_len,
                     output_len, expected_hex_len);
  }

  // Decode hex string to binary
  for (size_t i = 0; i < output_len; i++) {
    char hex_byte[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    char *endptr;
    unsigned long byte = strtoul(hex_byte, &endptr, 16);

    // Validate hex character
    if (*endptr != '\0' || byte > 255) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid hex character at position %zu: '%c%c'", i * 2, hex[i * 2],
                       hex[i * 2 + 1]);
    }

    output[i] = (uint8_t)byte;
  }

  return ASCIICHAT_OK;
}

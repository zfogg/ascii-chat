/**
 * @file crypto/keys.c
 * @brief High-level key management interface
 *
 * This file provides the main interface functions for key management,
 * delegating to the specialized modules for specific key types.
 */

#include "keys.h"
#include "types.h"
#include "ssh_keys.h"
#include "gpg_keys.h"
#include "https_keys.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// High-Level Key Parsing Functions
// =============================================================================

asciichat_error_t parse_public_key(const char *input, public_key_t *key_out) {
  if (!input || !key_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_public_key");
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

  // Try HTTPS key fetching (GitHub/GitLab)
  if (strncmp(input, "github:", 7) == 0 || strncmp(input, "gitlab:", 7) == 0) {
    // Parse GitHub/GitLab key references
    const char *username = input + 7; // Skip "github:" or "gitlab:"
    bool is_github = (strncmp(input, "github:", 7) == 0);
    bool is_gpg = (strstr(username, ".gpg") != NULL);

    char **keys = NULL;
    size_t num_keys = 0;
    asciichat_error_t result;

    if (is_github) {
      if (is_gpg) {
        result = fetch_github_gpg_keys(username, &keys, &num_keys);
      } else {
        result = fetch_github_ssh_keys(username, &keys, &num_keys);
      }
    } else {
      if (is_gpg) {
        result = fetch_gitlab_gpg_keys(username, &keys, &num_keys);
      } else {
        result = fetch_gitlab_ssh_keys(username, &keys, &num_keys);
      }
    }

    if (result != ASCIICHAT_OK || num_keys == 0) {
      return SET_ERRNO(ERROR_CRYPTO, "Failed to fetch keys from %s", is_github ? "GitHub" : "GitLab");
    }

    // Parse the first key
    result = parse_public_key(keys[0], key_out);

    // Free the keys array
    for (size_t i = 0; i < num_keys; i++) {
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);

    return result;
  }

  // Try raw hex key
  if (strlen(input) == 64) {
    // Assume it's a raw Ed25519 public key in hex
    key_out->type = KEY_TYPE_ED25519;
    for (int i = 0; i < 32; i++) {
      char hex_byte[3] = {input[i * 2], input[i * 2 + 1], 0};
      key_out->key[i] = (uint8_t)strtol(hex_byte, NULL, 16);
    }
    platform_strncpy(key_out->comment, sizeof(key_out->comment), "raw-hex", sizeof(key_out->comment) - 1);
    return ASCIICHAT_OK;
  }

  // Try reading from file
  FILE *f = platform_fopen(input, "r");
  if (f) {
    char line[1024];
    if (fgets(line, sizeof(line), f)) {
      (void)fclose(f);
      // Remove newline
      line[strcspn(line, "\r\n")] = 0;
      return parse_public_key(line, key_out);
    }
    (void)fclose(f);
  }

  return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported key format: %s", input);
}

asciichat_error_t parse_private_key(const char *key_path, private_key_t *key_out) {
  if (!key_path || !key_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_private_key");
  }

  // Clear output structure
  memset(key_out, 0, sizeof(private_key_t));

  // Try SSH private key parsing
  return parse_ssh_private_key(key_path, key_out);
}

asciichat_error_t parse_client_keys(const char *keys_file, public_key_t *keys_out, size_t *num_keys, size_t max_keys) {
  if (!keys_file || !keys_out || !num_keys) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_client_keys");
  }

  *num_keys = 0;

  // Check if this is a GitHub/GitLab reference
  if (strncmp(keys_file, "github:", 7) == 0 || strncmp(keys_file, "gitlab:", 7) == 0) {
    const char *username = keys_file + 7; // Skip "github:" or "gitlab:"
    bool is_github = (strncmp(keys_file, "github:", 7) == 0);
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

    // Parse each fetched key
    for (size_t i = 0; i < num_fetched_keys && *num_keys < max_keys; i++) {
      if (parse_public_key(keys[i], &keys_out[*num_keys]) == ASCIICHAT_OK) {
        (*num_keys)++;
      } else {
        log_warn("Failed to parse fetched key from %s: %s", is_github ? "GitHub" : "GitLab", keys[i]);
      }
    }

    // Free the keys array
    for (size_t i = 0; i < num_fetched_keys; i++) {
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);

    if (*num_keys == 0) {
      return SET_ERRNO(ERROR_CRYPTO_KEY, "No valid keys found for %s user: %s", is_github ? "GitHub" : "GitLab", username);
    }

    return ASCIICHAT_OK;
  }

  // Otherwise, treat as a file path
  FILE *f = platform_fopen(keys_file, "r");
  if (!f) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to open client keys file: %s", keys_file);
  }

  char line[1024];

  while (fgets(line, sizeof(line), f) && *num_keys < max_keys) {
    // Remove newline
    line[strcspn(line, "\r\n")] = 0;

    // Skip empty lines and comments
    if (strlen(line) == 0 || line[0] == '#') {
      continue;
    }

    if (parse_public_key(line, &keys_out[*num_keys]) == ASCIICHAT_OK) {
      (*num_keys)++;
    } else {
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to parse client key: %s, keys: %d", line, num_keys);
    }
  }

  (void)fclose(f);
  return ASCIICHAT_OK;
}

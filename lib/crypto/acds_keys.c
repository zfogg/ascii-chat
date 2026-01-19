/**
 * @file crypto/acds_keys.c
 * @brief ACDS Server Public Key Trust Management Implementation
 * @ingroup crypto
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "crypto/acds_keys.h"

#include "asciichat_errno.h"
#include "common.h"
#include "crypto/keys.h"
#include "log/logging.h"
#include "platform/fs.h"
#include "platform/question.h"
#include "platform/system.h"
#include "util/path.h"

#include <ctype.h>
#include <errno.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h> // For _mkdir
#endif

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Recursively create directories (mkdir -p equivalent)
 */
static asciichat_error_t ensure_directory_exists(const char *path) {
  if (!path || path[0] == '\0') {
    return ASCIICHAT_OK;
  }

  char tmp[512];
  SAFE_STRNCPY(tmp, path, sizeof(tmp));

  // Determine directory separator based on platform
  const char sep =
#ifdef _WIN32
      '\\'
#else
      '/'
#endif
      ;

  // Create each directory in the path
  for (char *p = tmp + 1; *p; p++) {
    if (*p == sep || *p == '/' || *p == '\\') {
      char orig = *p;
      *p = '\0';

      // Skip empty components and root
      if (tmp[0] != '\0' && strcmp(tmp, ".") != 0) {
#ifdef _WIN32
        if (_mkdir(tmp) != 0 && errno != EEXIST) {
          if (errno != ENOENT) {
            log_debug("Failed to create directory: %s (errno=%d)", tmp, errno);
          }
        }
#else
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
          if (errno != ENOENT) {
            log_debug("Failed to create directory: %s (errno=%d)", tmp, errno);
          }
        }
#endif
      }

      *p = orig;
    }
  }

  // Create the final directory
#ifdef _WIN32
  if (_mkdir(tmp) != 0 && errno != EEXIST) {
    if (errno != ENOENT) {
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create directory: %s", tmp);
    }
  }
#else
  if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
    if (errno != ENOENT) {
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create directory: %s", tmp);
    }
  }
#endif

  return ASCIICHAT_OK;
}

/**
 * @brief Check if this is the official ACDS server
 */
static bool is_official_server(const char *acds_server) {
  if (!acds_server) {
    return false;
  }

  // Case-insensitive comparison
  char server_lower[256];
  SAFE_STRNCPY(server_lower, acds_server, sizeof(server_lower));
  for (char *p = server_lower; *p; p++) {
    *p = (char)tolower((unsigned char)*p);
  }

  return strcmp(server_lower, "discovery.ascii-chat.com") == 0;
}

/**
 * @brief Compute SHA256 fingerprint of Ed25519 public key
 */
static void compute_key_fingerprint(const uint8_t pubkey[32], char fingerprint_out[65]) {
  uint8_t hash[32];
  crypto_hash_sha256(hash, pubkey, 32);

  for (int i = 0; i < 32; i++) {
    snprintf(&fingerprint_out[i * 2], 3, "%02x", hash[i]);
  }
  fingerprint_out[64] = '\0';
}

// ============================================================================
// HTTPS Download and Key Loading
// ============================================================================

asciichat_error_t acds_keys_download_https(const char *url, uint8_t pubkey_out[32]) {
  if (!url || !pubkey_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_download_https");
  }

  log_debug("Downloading ACDS key from %s", url);

  // Use existing parse_public_key infrastructure which handles HTTPS URLs
  public_key_t key;
  asciichat_error_t result = parse_public_key(url, &key);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to download and parse ACDS key from %s", url);
  }

  memcpy(pubkey_out, key.key, 32);
  log_debug("Successfully downloaded and parsed ACDS key from %s", url);
  return ASCIICHAT_OK;
}

asciichat_error_t acds_keys_load_file(const char *file_path, uint8_t pubkey_out[32]) {
  if (!file_path || !pubkey_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_load_file");
  }

  log_debug("Loading ACDS key from file: %s", file_path);

  // Use existing parse_public_key infrastructure which handles file paths
  public_key_t key;
  asciichat_error_t result = parse_public_key(file_path, &key);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to load ACDS key from file: %s", file_path);
  }

  memcpy(pubkey_out, key.key, 32);
  log_debug("Successfully loaded ACDS key from file: %s", file_path);
  return ASCIICHAT_OK;
}

// ============================================================================
// GitHub/GitLab Fetching
// ============================================================================

asciichat_error_t acds_keys_fetch_github(const char *username, bool is_gpg, uint8_t pubkey_out[32]) {
  if (!username || !pubkey_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_fetch_github");
  }

  log_debug("Fetching ACDS key from GitHub for user: %s", username);

  // Use existing parse_public_key infrastructure
  char key_spec[512];
  if (is_gpg) {
    snprintf(key_spec, sizeof(key_spec), "github:%s.gpg", username);
  } else {
    snprintf(key_spec, sizeof(key_spec), "github:%s", username);
  }

  public_key_t key;
  asciichat_error_t result = parse_public_key(key_spec, &key);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to fetch ACDS key from GitHub: %s", username);
  }

  memcpy(pubkey_out, key.key, 32);
  return ASCIICHAT_OK;
}

asciichat_error_t acds_keys_fetch_gitlab(const char *username, uint8_t pubkey_out[32]) {
  if (!username || !pubkey_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_fetch_gitlab");
  }

  log_debug("Fetching ACDS key from GitLab for user: %s", username);

  // Use existing parse_public_key infrastructure
  char key_spec[512];
  snprintf(key_spec, sizeof(key_spec), "gitlab:%s.gpg", username);

  public_key_t key;
  asciichat_error_t result = parse_public_key(key_spec, &key);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to fetch ACDS key from GitLab: %s", username);
  }

  memcpy(pubkey_out, key.key, 32);
  return ASCIICHAT_OK;
}

// ============================================================================
// Key Caching
// ============================================================================

asciichat_error_t acds_keys_get_cache_path(const char *acds_server, char *path_out, size_t path_size) {
  if (!acds_server || !path_out || path_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_get_cache_path");
  }

  char *config_dir = get_config_dir();
  if (!config_dir) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory");
  }

  // Path: ~/.config/ascii-chat/acds_keys/<hostname>/key.pub
  snprintf(path_out, path_size, "%s" ACDS_KEYS_CACHE_DIR PATH_SEPARATOR_STR "%s" PATH_SEPARATOR_STR "key.pub",
           config_dir, acds_server);
  SAFE_FREE(config_dir);

  return ASCIICHAT_OK;
}

asciichat_error_t acds_keys_load_cached(const char *acds_server, uint8_t pubkey_out[32]) {
  if (!acds_server || !pubkey_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_load_cached");
  }

  char cache_path[1024];
  asciichat_error_t result = acds_keys_get_cache_path(acds_server, cache_path, sizeof(cache_path));
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Check if cached key exists
  if (!platform_is_regular_file(cache_path)) {
    return SET_ERRNO(ERROR_FILE_NOT_FOUND, "No cached key for ACDS server: %s", acds_server);
  }

  // Load cached key using existing infrastructure
  return acds_keys_load_file(cache_path, pubkey_out);
}

asciichat_error_t acds_keys_save_cached(const char *acds_server, const uint8_t pubkey[32]) {
  if (!acds_server || !pubkey) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_save_cached");
  }

  char cache_path[1024];
  asciichat_error_t result = acds_keys_get_cache_path(acds_server, cache_path, sizeof(cache_path));
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Create cache directory if it doesn't exist
  char dir_path[1024];
  snprintf(dir_path, sizeof(dir_path), "%s", cache_path);
#ifdef _WIN32
  char *last_sep = strrchr(dir_path, '\\');
  if (!last_sep) {
    last_sep = strrchr(dir_path, '/');
  }
#else
  char *last_sep = strrchr(dir_path, '/');
#endif
  if (last_sep) {
    *last_sep = '\0';
    if (!platform_is_directory(dir_path)) {
      asciichat_error_t result = ensure_directory_exists(dir_path);
      if (result != ASCIICHAT_OK) {
        return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to create ACDS key cache directory: %s", dir_path);
      }
    }
  }

  // Save key in OpenSSH public key format
  FILE *f = fopen(cache_path, "w");
  if (!f) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create cache file: %s", cache_path);
  }

  // Convert binary Ed25519 key to base64 for OpenSSH format
  char base64_key[128];
  sodium_bin2base64(base64_key, sizeof(base64_key), pubkey, 32, sodium_base64_VARIANT_ORIGINAL);

  fprintf(f, "ssh-ed25519 %s acds-cached-key\n", base64_key);
  fclose(f);

  log_debug("Cached ACDS key for server: %s", acds_server);
  return ASCIICHAT_OK;
}

asciichat_error_t acds_keys_clear_cache(const char *acds_server) {
  if (!acds_server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_clear_cache");
  }

  char cache_path[1024];
  asciichat_error_t result = acds_keys_get_cache_path(acds_server, cache_path, sizeof(cache_path));
  if (result != ASCIICHAT_OK) {
    return result;
  }

  if (platform_is_regular_file(cache_path)) {
    if (remove(cache_path) != 0) {
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to delete cached key: %s", cache_path);
    }
    log_debug("Cleared cached ACDS key for server: %s", acds_server);
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// User Verification for Key Changes
// ============================================================================

asciichat_error_t acds_keys_verify_change(const char *acds_server, const uint8_t old_pubkey[32],
                                          const uint8_t new_pubkey[32]) {
  if (!acds_server || !old_pubkey || !new_pubkey) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_verify_change");
  }

  char old_fingerprint[65], new_fingerprint[65];
  compute_key_fingerprint(old_pubkey, old_fingerprint);
  compute_key_fingerprint(new_pubkey, new_fingerprint);

  log_warn("ACDS server key changed for: %s", acds_server);

  log_plain_stderr("\n"
                   "⚠️  WARNING: ACDS SERVER KEY HAS CHANGED\n"
                   "═══════════════════════════════════════════════════════════════\n"
                   "Server: %s\n"
                   "\n"
                   "Old key (SHA256): %s\n"
                   "New key (SHA256): %s\n"
                   "\n"
                   "This could indicate:\n"
                   "  1. The server operator rotated their key\n"
                   "  2. A man-in-the-middle attack is in progress\n"
                   "\n"
                   "Verify the new key fingerprint with the server operator before accepting.\n"
                   "═══════════════════════════════════════════════════════════════\n",
                   acds_server, old_fingerprint, new_fingerprint);

  // Ask user to confirm
  bool accepted = platform_prompt_yes_no("Accept new ACDS server key", false);
  if (!accepted) {
    return SET_ERRNO(ERROR_CRYPTO_VERIFICATION, "User rejected ACDS key change for: %s", acds_server);
  }

  log_info("User accepted ACDS key change for: %s", acds_server);
  return ASCIICHAT_OK;
}

// ============================================================================
// Main Verification Function
// ============================================================================

asciichat_error_t acds_keys_verify(const char *acds_server, const char *key_spec, uint8_t pubkey_out[32]) {
  if (!acds_server || !pubkey_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL parameter in acds_keys_verify");
  }

  uint8_t new_pubkey[32];
  asciichat_error_t result;
  bool is_official = is_official_server(acds_server);

  // ===========================================================================
  // Step 1: Obtain the public key
  // ===========================================================================

  if (!key_spec && is_official) {
    // Automatic trust for official server: try SSH key first, then GPG
    log_info("Attempting automatic HTTPS key trust for official ACDS server");

    public_key_t key;
    result = parse_public_key(ACDS_OFFICIAL_KEY_SSH_URL, &key);
    if (result != ASCIICHAT_OK) {
      log_debug("SSH key download failed, trying GPG key");
      result = parse_public_key(ACDS_OFFICIAL_KEY_GPG_URL, &key);
    }

    if (result != ASCIICHAT_OK) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to download key from official ACDS server");
    }

    memcpy(new_pubkey, key.key, 32);

  } else if (!key_spec) {
    // No key_spec and not official server = error
    return SET_ERRNO(ERROR_INVALID_PARAM,
                     "Third-party ACDS servers require explicit --acds-key configuration. "
                     "Only %s has automatic trust.",
                     ACDS_OFFICIAL_SERVER);

  } else {
    // Use parse_public_key for all other cases (handles HTTPS, files, github:, gitlab:, etc.)
    public_key_t key;
    result = parse_public_key(key_spec, &key);
    if (result != ASCIICHAT_OK) {
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to load/download ACDS key from: %s", key_spec);
    }

    memcpy(new_pubkey, key.key, 32);
  }

  // ===========================================================================
  // Step 2: Check cache and handle key changes
  // ===========================================================================

  uint8_t cached_pubkey[32];
  result = acds_keys_load_cached(acds_server, cached_pubkey);

  if (result == ASCIICHAT_OK) {
    // Cached key exists - compare
    if (memcmp(cached_pubkey, new_pubkey, 32) != 0) {
      // Key changed - require user verification
      result = acds_keys_verify_change(acds_server, cached_pubkey, new_pubkey);
      if (result != ASCIICHAT_OK) {
        return result; // User rejected or error
      }

      // User accepted - update cache
      result = acds_keys_save_cached(acds_server, new_pubkey);
      if (result != ASCIICHAT_OK) {
        log_warn("Failed to update cached key, continuing anyway");
      }
    } else {
      log_debug("ACDS key matches cached key for: %s", acds_server);
    }

  } else {
    // No cached key - save it for next time
    log_info("First connection to ACDS server: %s, caching key", acds_server);
    result = acds_keys_save_cached(acds_server, new_pubkey);
    if (result != ASCIICHAT_OK) {
      log_warn("Failed to cache key, continuing anyway");
    }
  }

  // ===========================================================================
  // Step 3: Return verified key
  // ===========================================================================

  memcpy(pubkey_out, new_pubkey, 32);
  log_debug("ACDS key verification successful for: %s", acds_server);
  return ASCIICHAT_OK;
}

/**
 * @file acds/identity.c
 * @brief Identity key management implementation
 */

#include "discovery/identity.h"
#include "crypto/crypto.h"
#include "log/logging.h"
#include "platform/abstraction.h"
#include "platform/filesystem.h"
#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

asciichat_error_t acds_identity_generate(uint8_t public_key[32], uint8_t secret_key[64]) {
  if (!public_key || !secret_key) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "public_key and secret_key cannot be NULL");
  }

  // Generate Ed25519 keypair using libsodium
  if (crypto_sign_keypair(public_key, secret_key) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to generate Ed25519 keypair");
  }

  log_debug("Generated new Ed25519 identity keypair");
  return ASCIICHAT_OK;
}

asciichat_error_t acds_identity_load(const char *path, uint8_t public_key[32], uint8_t secret_key[64]) {
  if (!path || !public_key || !secret_key) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path, public_key, and secret_key cannot be NULL");
  }

  // Open file for reading
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    if (errno == ENOENT) {
      return SET_ERRNO(ERROR_CONFIG, "Identity file does not exist: %s", path);
    }
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open identity file: %s", path);
  }

  // Read secret key (64 bytes)
  size_t read = fread(secret_key, 1, 64, fp);
  if (read != 64) {
    fclose(fp);
    return SET_ERRNO(ERROR_CONFIG, "Identity file corrupted (expected 64 bytes, got %zu): %s", read, path);
  }

  // Extract public key from secret key (last 32 bytes of Ed25519 secret key)
  memcpy(public_key, secret_key + 32, 32);

  fclose(fp);
  log_info("Loaded identity from %s", path);
  return ASCIICHAT_OK;
}

asciichat_error_t acds_identity_save(const char *path, const uint8_t public_key[32], const uint8_t secret_key[64]) {
  if (!path || !public_key || !secret_key) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path, public_key, and secret_key cannot be NULL");
  }

  // Extract directory path and create all parent directories
  char dir_path[PLATFORM_MAX_PATH_LENGTH];
  SAFE_STRNCPY(dir_path, path, sizeof(dir_path));

  // Find last directory separator
  char *last_sep = strrchr(dir_path, '/');
  if (!last_sep) {
    last_sep = strrchr(dir_path, '\\');
  }

  if (last_sep) {
    *last_sep = '\0';

    // Create directory recursively (mkdir -p equivalent)
    asciichat_error_t result = platform_mkdir_recursive(dir_path, 0700);
    if (result != ASCIICHAT_OK) {
      return result;
    }
  }

  // Open file for writing (mode 0600 = owner read/write only)
  int fd = platform_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create identity file: %s", path);
  }

  // Write secret key (64 bytes)
  ssize_t written = write(fd, secret_key, 64);
  close(fd);

  if (written != 64) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to write identity file (wrote %zd/64 bytes): %s", written, path);
  }

  log_info("Saved identity to %s", path);
  return ASCIICHAT_OK;
}

void acds_identity_fingerprint(const uint8_t public_key[32], char fingerprint[65]) {
  if (!public_key || !fingerprint) {
    log_error("acds_identity_fingerprint: NULL parameters");
    return;
  }

  // Compute SHA256 hash of public key
  uint8_t hash[32];
  crypto_hash_sha256(hash, public_key, 32);

  // Convert to hex string
  for (int i = 0; i < 32; i++) {
    snprintf(&fingerprint[i * 2], 3, "%02x", hash[i]);
  }
  fingerprint[64] = '\0';
}

asciichat_error_t acds_identity_default_path(char *path_out, size_t path_size) {
  if (!path_out || path_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path_out cannot be NULL and path_size must be > 0");
  }

#ifdef _WIN32
  // Windows: %APPDATA%\ascii-chat\acds_identity
  const char *appdata = SAFE_GETENV("APPDATA");
  if (!appdata) {
    return SET_ERRNO(ERROR_CONFIG, "APPDATA environment variable not set");
  }

  int written = snprintf(path_out, path_size, "%s\\ascii-chat\\acds_identity", appdata);
  if (written < 0 || (size_t)written >= path_size) {
    return SET_ERRNO(ERROR_CONFIG, "Path buffer too small");
  }
#else
  // Unix: ~/.config/ascii-chat/acds_identity
  const char *home = SAFE_GETENV("HOME");
  if (!home) {
    return SET_ERRNO(ERROR_CONFIG, "HOME environment variable not set");
  }

  // Check for XDG_CONFIG_HOME
  const char *xdg_config = SAFE_GETENV("XDG_CONFIG_HOME");
  if (xdg_config && xdg_config[0] != '\0') {
    int written = snprintf(path_out, path_size, "%s/ascii-chat/acds_identity", xdg_config);
    if (written < 0 || (size_t)written >= path_size) {
      return SET_ERRNO(ERROR_CONFIG, "Path buffer too small");
    }
  } else {
    int written = snprintf(path_out, path_size, "%s/.config/ascii-chat/acds_identity", home);
    if (written < 0 || (size_t)written >= path_size) {
      return SET_ERRNO(ERROR_CONFIG, "Path buffer too small");
    }
  }
#endif

  return ASCIICHAT_OK;
}

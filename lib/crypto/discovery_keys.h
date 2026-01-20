/**
 * @file crypto/discovery_keys.h
 * @brief Discovery Server Public Key Trust Management
 * @ingroup crypto
 *
 * This module handles verification and trust management for ACDS server public keys.
 * It provides functionality to:
 * - Download keys from HTTPS URLs using lib/crypto/http_client.h
 * - Load keys from local files (SSH/GPG formats)
 * - Special-case automatic trust for discovery.ascii-chat.com
 * - Cache keys locally and detect changes (requires user verification)
 * - Parse github:user and gitlab:user.gpg key specifications
 *
 * ## Trust Model
 *
 * **Official ACDS Server (discovery.ascii-chat.com)**:
 * - Automatically trusts keys downloaded from https://discovery.ascii-chat.com/key.pub or /key.gpg
 * - First connection: downloads and caches key
 * - Subsequent connections: uses cached key
 * - Key changes: requires user verification (prevents MITM attacks)
 *
 * **Third-Party ACDS Servers**:
 * - Requires explicit --acds-key configuration
 * - Can be HTTPS URL, local file path, or github:user/gitlab:user specification
 * - Keys are cached after first successful download/verification
 * - Key changes require user verification
 *
 * ## Usage
 *
 * @code{.c}
 * // Download and verify key from HTTPS URL
 * uint8_t pubkey[32];
 * asciichat_error_t result = discovery_keys_verify("acds.example.com",
 *                                             "https://acds.example.com/key.pub",
 *                                             pubkey);
 *
 * // Automatic trust for official server
 * result = discovery_keys_verify("discovery.ascii-chat.com", NULL, pubkey);
 *
 * // Load from local file
 * result = discovery_keys_verify("acds.example.com", "/path/to/key.pub", pubkey);
 *
 * // GitHub key
 * result = discovery_keys_verify("acds.example.com", "github:zfogg", pubkey);
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "asciichat_errno.h"
#include "common.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Official ACDS server hostname (automatic HTTPS key trust)
 */
#define ACDS_OFFICIAL_SERVER "discovery.ascii-chat.com"

/**
 * @brief Default HTTPS URLs for official ACDS keys
 */
#define ACDS_OFFICIAL_KEY_SSH_URL "https://discovery.ascii-chat.com/key.pub"
#define ACDS_OFFICIAL_KEY_GPG_URL "https://discovery.ascii-chat.com/key.gpg"

/**
 * @brief Key cache directory (relative to config dir)
 */
#define ACDS_KEYS_CACHE_DIR "acds_keys"

/**
 * @brief Verify and load ACDS server public key
 *
 * This function handles all ACDS key verification scenarios:
 *
 * **Automatic Trust (discovery.ascii-chat.com only)**:
 * - If acds_server == "discovery.ascii-chat.com" and key_spec == NULL:
 *   - Downloads from ACDS_OFFICIAL_KEY_SSH_URL or ACDS_OFFICIAL_KEY_GPG_URL
 *   - Caches key locally
 *   - Trusts without user verification (first time)
 *   - Requires user verification if key changes
 *
 * **Explicit Key Specification**:
 * - HTTPS URL: Downloads key from URL, caches it
 * - Local file: Loads key from file path
 * - github:user: Fetches SSH or GPG key from GitHub
 * - gitlab:user.gpg: Fetches GPG key from GitLab
 *
 * **Caching Behavior**:
 * - Keys are cached in ~/.config/ascii-chat/acds_keys/<hostname>/
 * - Cache invalidation requires user confirmation (prevents MITM attacks)
 *
 * @param acds_server ACDS server hostname (e.g., "acds.example.com")
 * @param key_spec Key specification:
 *                 - NULL: automatic trust for discovery.ascii-chat.com only
 *                 - HTTPS URL: https://example.com/key.pub
 *                 - File path: /path/to/key.pub or ~/.ssh/server.pub
 *                 - GitHub: github:username
 *                 - GitLab: gitlab:username.gpg
 * @param pubkey_out Ed25519 public key output (32 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_verify(const char *acds_server, const char *key_spec, uint8_t pubkey_out[32]);

/**
 * @brief Download key from HTTPS URL
 *
 * Downloads a public key from an HTTPS URL using lib/crypto/http_client.h.
 * Supports both SSH (OpenSSH format) and GPG (ASCII-armored or binary) keys.
 *
 * @param url HTTPS URL to download from
 * @param pubkey_out Ed25519 public key output (32 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_download_https(const char *url, uint8_t pubkey_out[32]);

/**
 * @brief Load key from local file
 *
 * Loads a public key from a local file. Supports:
 * - OpenSSH format (.pub files)
 * - GPG ASCII-armored format (.asc, .gpg)
 * - GPG binary format
 *
 * @param file_path Path to key file
 * @param pubkey_out Ed25519 public key output (32 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_load_file(const char *file_path, uint8_t pubkey_out[32]);

/**
 * @brief Fetch key from GitHub
 *
 * Downloads public key from GitHub using:
 * - SSH keys: https://github.com/username.keys (tries first Ed25519)
 * - GPG keys: https://github.com/username.gpg (if key_spec ends with .gpg)
 *
 * @param username GitHub username
 * @param is_gpg true to fetch GPG key, false for SSH key
 * @param pubkey_out Ed25519 public key output (32 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_fetch_github(const char *username, bool is_gpg, uint8_t pubkey_out[32]);

/**
 * @brief Fetch GPG key from GitLab
 *
 * Downloads GPG public key from GitLab using:
 * https://gitlab.com/username.gpg
 *
 * @param username GitLab username
 * @param pubkey_out Ed25519 public key output (32 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_fetch_gitlab(const char *username, uint8_t pubkey_out[32]);

/**
 * @brief Get cached key path for ACDS server
 *
 * Returns the local cache path for a given ACDS server's key.
 * Path: ~/.config/ascii-chat/acds_keys/<hostname>/key.pub
 *
 * @param acds_server ACDS server hostname
 * @param path_out Output buffer for cache path
 * @param path_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_get_cache_path(const char *acds_server, char *path_out, size_t path_size);

/**
 * @brief Check if cached key exists and load it
 *
 * Attempts to load a cached key for the given ACDS server.
 *
 * @param acds_server ACDS server hostname
 * @param pubkey_out Ed25519 public key output (32 bytes)
 * @return ASCIICHAT_OK if cached key exists and loaded successfully, error otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_load_cached(const char *acds_server, uint8_t pubkey_out[32]);

/**
 * @brief Save key to cache
 *
 * Caches a public key for the given ACDS server.
 *
 * @param acds_server ACDS server hostname
 * @param pubkey Public key to cache (32 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_save_cached(const char *acds_server, const uint8_t pubkey[32]);

/**
 * @brief Verify key change with user
 *
 * When a cached key doesn't match the newly downloaded/loaded key,
 * prompts the user to verify the change.
 *
 * Displays:
 * - Old key fingerprint (SHA256)
 * - New key fingerprint (SHA256)
 * - Warning about potential MITM attack
 * - Prompt: "Accept new key? (y/N): "
 *
 * @param acds_server ACDS server hostname
 * @param old_pubkey Previous cached key (32 bytes)
 * @param new_pubkey New downloaded key (32 bytes)
 * @return ASCIICHAT_OK if user accepts, ERROR_USER_REJECTED if declined
 */
ASCIICHAT_API asciichat_error_t discovery_keys_verify_change(const char *acds_server, const uint8_t old_pubkey[32],
                                                        const uint8_t new_pubkey[32]);

/**
 * @brief Clear cached key for ACDS server
 *
 * Removes the cached key for the given ACDS server. Useful for testing
 * or forcing key re-download.
 *
 * @param acds_server ACDS server hostname
 * @return ASCIICHAT_OK on success, error code otherwise
 */
ASCIICHAT_API asciichat_error_t discovery_keys_clear_cache(const char *acds_server);

#ifdef __cplusplus
}
#endif

#pragma once

/**
 * @file acds/identity.h
 * @brief Identity key management for discovery server
 *
 * Simple Ed25519 key generation, storage, and fingerprint computation.
 * This is a minimal inline implementation - will be refactored to lib/identity/
 * in the future when we add randomart and other features.
 */

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

/**
 * @brief Generate new Ed25519 keypair
 * @param public_key Output buffer for 32-byte public key
 * @param secret_key Output buffer for 64-byte secret key
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t acds_identity_generate(uint8_t public_key[32], uint8_t secret_key[64]);

/**
 * @brief Load identity from file
 * @param path Path to identity file (e.g., ~/.config/ascii-chat/acds_identity)
 * @param public_key Output buffer for 32-byte public key
 * @param secret_key Output buffer for 64-byte secret key
 * @return ASCIICHAT_OK on success, ERROR_CONFIG if file doesn't exist
 */
asciichat_error_t acds_identity_load(const char *path, uint8_t public_key[32], uint8_t secret_key[64]);

/**
 * @brief Save identity to file
 * @param path Path to identity file
 * @param public_key 32-byte public key
 * @param secret_key 64-byte secret key
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t acds_identity_save(const char *path, const uint8_t public_key[32], const uint8_t secret_key[64]);

/**
 * @brief Compute SHA256 fingerprint of public key
 * @param public_key 32-byte Ed25519 public key
 * @param fingerprint Output buffer for 65 bytes (64 hex chars + null terminator)
 */
void acds_identity_fingerprint(const uint8_t public_key[32], char fingerprint[65]);

/**
 * @brief Get default identity file path for current platform
 * @param path_out Output buffer for path (should be at least 256 bytes)
 * @param path_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * Returns:
 * - Unix: ~/.config/ascii-chat/acds_identity
 * - Windows: %APPDATA%\ascii-chat\acds_identity
 */
asciichat_error_t acds_identity_default_path(char *path_out, size_t path_size);

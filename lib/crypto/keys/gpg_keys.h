#pragma once

/**
 * @file crypto/keys/gpg_keys.h
 * @brief GPG key parsing and validation functions
 *
 * This module handles GPG key parsing, validation, and conversion
 * to X25519 for key exchange operations.
 */

#include "../../common.h"
#include "types.h" // Include the key type definitions
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// GPG Key Parsing Functions
// =============================================================================

/**
 * @brief Parse GPG key from armored text format
 * @param gpg_key_text The GPG key in armored format
 * @param key_out Output structure for the parsed public key
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t parse_gpg_key(const char *gpg_key_text, public_key_t *key_out);

/**
 * @brief Parse GPG key from binary format
 * @param gpg_key_binary The GPG key in binary format
 * @param key_size Size of the binary key data
 * @param key_out Output structure for the parsed public key
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t parse_gpg_key_binary(const uint8_t *gpg_key_binary, size_t key_size, public_key_t *key_out);

/**
 * @brief Validate GPG key format and structure
 * @param gpg_key_text The GPG key text to validate
 * @return ASCIICHAT_OK if valid, error code on failure
 */
asciichat_error_t validate_gpg_key_format(const char *gpg_key_text);

/**
 * @brief Extract Ed25519 public key from GPG key
 * @param gpg_key_text The GPG key in armored format
 * @param ed25519_pk Output buffer for the Ed25519 public key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t extract_ed25519_from_gpg(const char *gpg_key_text, uint8_t ed25519_pk[32]);

/**
 * @brief Convert GPG key to X25519 for key exchange
 * @param gpg_key_text The GPG key in armored format
 * @param x25519_pk Output buffer for the X25519 public key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t gpg_to_x25519_public(const char *gpg_key_text, uint8_t x25519_pk[32]);

// =============================================================================
// GPG Key Operations
// =============================================================================

/**
 * @brief Get GPG key fingerprint
 * @param gpg_key_text The GPG key in armored format
 * @param fingerprint_out Output buffer for the fingerprint (20 bytes for SHA-1)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t get_gpg_fingerprint(const char *gpg_key_text, uint8_t fingerprint_out[20]);

/**
 * @brief Get GPG key ID (short fingerprint)
 * @param gpg_key_text The GPG key in armored format
 * @param key_id_out Output buffer for the key ID (8 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t get_gpg_key_id(const char *gpg_key_text, uint8_t key_id_out[8]);

/**
 * @brief Check if GPG key is expired
 * @param gpg_key_text The GPG key in armored format
 * @param is_expired Output: true if key is expired, false otherwise
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t check_gpg_key_expiry(const char *gpg_key_text, bool *is_expired);

// =============================================================================
// GPG Key Formatting
// =============================================================================

/**
 * @brief Format GPG key for display
 * @param gpg_key_text The GPG key in armored format
 * @param output Output buffer for formatted key
 * @param output_size Size of the output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t format_gpg_key_display(const char *gpg_key_text, char *output, size_t output_size);

/**
 * @brief Extract key comment/email from GPG key
 * @param gpg_key_text The GPG key in armored format
 * @param comment_out Output buffer for the comment
 * @param comment_size Size of the comment buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t extract_gpg_key_comment(const char *gpg_key_text, char *comment_out, size_t comment_size);

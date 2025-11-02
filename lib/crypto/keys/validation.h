#pragma once

/**
 * @file crypto/keys/validation.h
 * @brief Key validation and security functions
 *
 * This module provides comprehensive key validation, security checks,
 * and format verification for all supported key types.
 */

#include "../../common.h"
#include "types.h" // Include the key type definitions
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// Key Validation Functions
// =============================================================================

/**
 * @brief Validate a public key structure
 * @param key The public key to validate
 * @return ASCIICHAT_OK if valid, error code on failure
 */
asciichat_error_t validate_public_key(const public_key_t *key);

/**
 * @brief Validate a private key structure
 * @param key The private key to validate
 * @return ASCIICHAT_OK if valid, error code on failure
 */
asciichat_error_t validate_private_key(const private_key_t *key);

/**
 * @brief Check if a key is expired
 * @param key The key to check
 * @param is_expired Output: true if expired, false otherwise
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t check_key_expiry(const public_key_t *key, bool *is_expired);

/**
 * @brief Validate key permissions and security
 * @param key_path Path to the key file
 * @return ASCIICHAT_OK if secure, error code on failure
 */
asciichat_error_t validate_key_security(const char *key_path);

// =============================================================================
// Key Format Validation
// =============================================================================

/**
 * @brief Validate SSH key format
 * @param key_text The SSH key text to validate
 * @return ASCIICHAT_OK if valid, error code on failure
 */
asciichat_error_t validate_ssh_key_format(const char *key_text);

/**
 * @brief Validate GPG key format
 * @param key_text The GPG key text to validate
 * @return ASCIICHAT_OK if valid, error code on failure
 */
asciichat_error_t validate_gpg_key_format(const char *key_text);

/**
 * @brief Validate X25519 key format
 * @param key_hex The X25519 key in hex format
 * @return ASCIICHAT_OK if valid, error code on failure
 */
asciichat_error_t validate_x25519_key_format(const char *key_hex);

// =============================================================================
// Key Security Checks
// =============================================================================

/**
 * @brief Check if key has weak parameters
 * @param key The key to check
 * @param is_weak Output: true if weak, false otherwise
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t check_key_strength(const public_key_t *key, bool *is_weak);

/**
 * @brief Validate key file permissions
 * @param key_path Path to the key file
 * @return ASCIICHAT_OK if permissions are secure, error code on failure
 */
asciichat_error_t validate_key_permissions(const char *key_path);

/**
 * @brief Check for key reuse or weak patterns
 * @param key The key to check
 * @param has_weak_patterns Output: true if weak patterns found, false otherwise
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t check_key_patterns(const public_key_t *key, bool *has_weak_patterns);

// =============================================================================
// Key Comparison and Matching
// =============================================================================

/**
 * @brief Compare two public keys for equality
 * @param key1 First key to compare
 * @param key2 Second key to compare
 * @param are_equal Output: true if keys are equal, false otherwise
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t compare_public_keys(const public_key_t *key1, const public_key_t *key2, bool *are_equal);

/**
 * @brief Check if key matches a fingerprint
 * @param key The key to check
 * @param fingerprint The fingerprint to match against
 * @param fingerprint_len Length of the fingerprint
 * @param matches Output: true if fingerprint matches, false otherwise
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t check_key_fingerprint(const public_key_t *key, const uint8_t *fingerprint, size_t fingerprint_len,
                                        bool *matches);

/**
 * @brief Generate key fingerprint
 * @param key The key to fingerprint
 * @param fingerprint_out Output buffer for the fingerprint
 * @param fingerprint_size Size of the fingerprint buffer
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t generate_key_fingerprint(const public_key_t *key, uint8_t *fingerprint_out, size_t fingerprint_size);

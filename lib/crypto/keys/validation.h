#pragma once

/**
 * @file crypto/keys/validation.h
 * @ingroup keys
 * @brief Key validation and security functions
 *
 * This module provides comprehensive key validation, security checks,
 * and format verification for all supported key types.
 *
 * @note Key validation: Validates key structure, format, and security properties.
 *       Used before key operations to ensure keys are valid and secure.
 *
 * @note Security checks: Includes permission checking, weak key detection,
 *       and pattern analysis for security vulnerabilities.
 *
 * @note Key format validation: Validates SSH, GPG, and X25519 key formats.
 *       Returns error early if format is invalid.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include "../../common.h"
#include "types.h" // Include the key type definitions
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @name Key Validation
 * @{
 */

/**
 * @brief Validate a public key structure
 * @param key Public key to validate (must not be NULL)
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates public key structure by checking:
 * - Key type is valid (Ed25519, X25519, or GPG)
 * - Key data is not all zeros
 * - Comment length is within limits
 *
 * @note Key type validation: Checks that key type is one of:
 *       KEY_TYPE_ED25519, KEY_TYPE_X25519, or KEY_TYPE_GPG
 *
 * @note Zero key check: Rejects keys with all-zero key data.
 *       Zero keys are invalid and should not be used.
 *
 * @note Comment validation: Checks that comment length is less than MAX_COMMENT_LEN.
 *       Comments can be up to 255 characters (including null terminator).
 *
 * @warning Zero keys: Keys with all-zero data are rejected as invalid.
 *
 * @ingroup keys
 */
asciichat_error_t validate_public_key(const public_key_t *key);

/**
 * @brief Validate a private key structure
 * @param key Private key to validate (must not be NULL)
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates private key structure by checking:
 * - Key type is valid (Ed25519 or X25519)
 * - Key data is not all zeros
 * - Comment length is within limits
 *
 * @note Key type validation: Checks that key type is one of:
 *       KEY_TYPE_ED25519 or KEY_TYPE_X25519
 *       GPG keys are NOT supported for private keys (only public).
 *
 * @note Key size: Ed25519 private keys are 64 bytes (seed + public).
 *       X25519 private keys are 32 bytes.
 *       Function validates key size based on key type.
 *
 * @note Zero key check: Rejects keys with all-zero key data.
 *       Zero keys are invalid and should not be used.
 *
 * @note Comment validation: Checks that comment length is less than MAX_COMMENT_LEN.
 *
 * @warning Zero keys: Keys with all-zero data are rejected as invalid.
 *
 * @ingroup keys
 */
asciichat_error_t validate_private_key(const private_key_t *key);

/**
 * @brief Check if a key is expired
 * @param key Key to check (must not be NULL)
 * @param is_expired Output: true if expired, false otherwise (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Checks if key has expired by parsing expiration date from key structure.
 *
 * @note Expiration checking: Would parse key creation dates and expiration times
 *       from key structure. Currently not implemented.
 *
 * @note Current implementation: Always returns false (not expired).
 *       Key expiry checking is not yet implemented.
 *
 * @warning NOT YET IMPLEMENTED: This function always returns false (not expired).
 *          Key expiry checking is not yet implemented.
 *
 * @ingroup keys
 */
asciichat_error_t check_key_expiry(const public_key_t *key, bool *is_expired);

/**
 * @brief Validate key permissions and security
 * @param key_path Path to key file (must not be NULL)
 * @return ASCIICHAT_OK if secure, error code on failure
 *
 * Validates key file permissions and performs security checks.
 * Combines permission validation with additional security checks.
 *
 * @note Permission checking: Validates file permissions using validate_key_permissions().
 *       Checks that file has restrictive permissions (0600).
 *
 * @note Security checks: Additional security checks would include:
 *       - Weak key pattern detection
 *       - Key strength validation
 *       - Known weak key detection
 *       These are not yet fully implemented (TODOs in implementation).
 *
 * @warning Security checks incomplete: Additional security checks (weak keys, etc.)
 *          are not yet fully implemented. Function currently only checks permissions.
 *
 * @ingroup keys
 */
asciichat_error_t validate_key_security(const char *key_path);

/** @} */

/**
 * @name Key Format Validation
 * @{
 */

/**
 * @brief Validate SSH key format
 * @param key_text SSH key text to validate (must not be NULL)
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates SSH key format by checking for correct format markers.
 * Expects "ssh-ed25519 " prefix followed by base64-encoded key.
 *
 * @note Format validation: Checks that key text starts with "ssh-ed25519 ".
 *       Validates basic SSH key format structure.
 *
 * @note Key type: Only Ed25519 SSH keys are validated.
 *       Other key types (RSA, ECDSA) will fail validation.
 *
 * @warning Key format: Must start with "ssh-ed25519 ".
 *          Other SSH key types (RSA, ECDSA) are NOT supported.
 *
 * @ingroup keys
 */
asciichat_error_t validate_ssh_key_format(const char *key_text);

/**
 * @brief Validate GPG key format
 * @param key_text GPG key text to validate (must not be NULL)
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates GPG key format by checking for correct armored format markers.
 * Expects "-----BEGIN PGP PUBLIC KEY BLOCK-----" and "-----END PGP PUBLIC KEY BLOCK-----".
 *
 * @note Format validation: Checks for correct GPG armored format markers.
 *       Validates basic GPG key structure.
 *
 * @note GPG format: Expects PEM-armored GPG key format.
 *       Format: "-----BEGIN PGP PUBLIC KEY BLOCK-----" ... "-----END PGP PUBLIC KEY BLOCK-----"
 *
 * @warning GPG support: GPG key validation may not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t validate_gpg_key_format(const char *key_text);

/**
 * @brief Validate X25519 key format
 * @param key_hex X25519 key in hex format (must not be NULL)
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates X25519 key format by checking hex string format.
 * Expects 64 hex characters (32 bytes).
 *
 * @note Format validation: Checks that key is 64 hex characters (32 bytes).
 *       Validates hex character format (0-9, a-f, A-F).
 *
 * @note Key size: X25519 keys must be exactly 64 hex characters (32 bytes).
 *       Returns error if length doesn't match or contains invalid characters.
 *
 * @warning Key format: Must be exactly 64 hex characters.
 *          Invalid characters or wrong length will fail validation.
 *
 * @ingroup keys
 */
asciichat_error_t validate_x25519_key_format(const char *key_hex);

/** @} */

/**
 * @name Key Security Checks
 * @{
 */

/**
 * @brief Check if key has weak parameters
 * @param key Key to check (must not be NULL)
 * @param is_weak Output: true if weak, false otherwise (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Checks if key has weak cryptographic parameters.
 * Detects keys with weak entropy or predictable patterns.
 *
 * @note Weak key detection: Would analyze key material for weak patterns.
 *       Currently not fully implemented.
 *
 * @note Security analysis: Would check for:
 *       - Low entropy keys
 *       - Predictable patterns
 *       - Known weak keys
 *
 * @warning NOT YET FULLY IMPLEMENTED: Weak key detection is not yet fully implemented.
 *          Function may always return false (not weak).
 *
 * @ingroup keys
 */
asciichat_error_t check_key_strength(const public_key_t *key, bool *is_weak);

/**
 * @brief Validate key file permissions
 * @param key_path Path to key file (must not be NULL)
 * @return ASCIICHAT_OK if permissions are secure, error code on failure
 *
 * Validates key file permissions by checking Unix file permissions.
 * Ensures file has restrictive permissions (0600).
 *
 * @note Permission checking: Checks file permissions on Unix systems.
 *       Validates that file has owner read/write only (0600).
 *
 * @note Platform-specific: Permission checking is Unix-specific.
 *       Windows does not have Unix-style permissions.
 *
 * @note Recommended permissions: Private key files should have 0600 permissions.
 *       Public key files can have more permissive permissions (0644).
 *
 * @warning Platform-specific: Permission checking is Unix-specific.
 *          Windows does not have Unix-style permissions.
 *
 * @ingroup keys
 */
asciichat_error_t validate_key_permissions(const char *key_path);

/**
 * @brief Check for key reuse or weak patterns
 * @param key Key to check (must not be NULL)
 * @param has_weak_patterns Output: true if weak patterns found, false otherwise (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Checks key for weak patterns that could indicate security vulnerabilities.
 * Detects keys with predictable or weak patterns.
 *
 * @note Pattern analysis: Would analyze key material for weak patterns.
 *       Currently not fully implemented.
 *
 * @note Weak patterns: Would detect:
 *       - Keys with repeated patterns
 *       - Keys with low entropy
 *       - Keys matching known weak patterns
 *
 * @warning NOT YET FULLY IMPLEMENTED: Weak pattern detection is not yet fully implemented.
 *          Function may always return false (no weak patterns).
 *
 * @ingroup keys
 */
asciichat_error_t check_key_patterns(const public_key_t *key, bool *has_weak_patterns);

/** @} */

/**
 * @name Key Comparison and Matching
 * @{
 */

/**
 * @brief Compare two public keys for equality
 * @param key1 First key to compare (must not be NULL)
 * @param key2 Second key to compare (must not be NULL)
 * @param are_equal Output: true if keys are equal, false otherwise (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Compares two public keys for equality using constant-time comparison.
 * Compares key type and key data.
 *
 * @note Key comparison: Compares key type and 32-byte key data.
 *       Uses constant-time comparison to prevent timing attacks.
 *
 * @note Constant-time: Uses sodium_memcmp() for constant-time comparison.
 *       Prevents timing attacks that could leak key information.
 *
 * @note Comment comparison: Comments are NOT compared.
 *       Only key type and key data are compared.
 *
 * @warning Always uses constant-time comparison for keys.
 *          Do NOT use regular memcmp() for key comparison.
 *
 * @ingroup keys
 */
asciichat_error_t compare_public_keys(const public_key_t *key1, const public_key_t *key2, bool *are_equal);

/**
 * @brief Check if key matches a fingerprint
 * @param key Key to check (must not be NULL)
 * @param fingerprint Fingerprint to match against (must not be NULL)
 * @param fingerprint_len Length of fingerprint (must be > 0)
 * @param matches Output: true if fingerprint matches, false otherwise (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Checks if key matches a fingerprint by computing key fingerprint and comparing.
 *
 * @note Fingerprint comparison: Computes key fingerprint using generate_key_fingerprint()
 *       and compares with provided fingerprint using constant-time comparison.
 *
 * @note Constant-time: Uses constant-time comparison to prevent timing attacks.
 *
 * @note Fingerprint format: Fingerprint is typically SHA-256 hash of key material.
 *       Length depends on hash algorithm used (typically 32 bytes for SHA-256).
 *
 * @warning Always uses constant-time comparison for fingerprints.
 *          Do NOT use regular memcmp() for fingerprint comparison.
 *
 * @ingroup keys
 */
asciichat_error_t check_key_fingerprint(const public_key_t *key, const uint8_t *fingerprint, size_t fingerprint_len,
                                        bool *matches);

/**
 * @brief Generate key fingerprint
 * @param key Key to fingerprint (must not be NULL)
 * @param fingerprint_out Output buffer for fingerprint (must not be NULL)
 * @param fingerprint_size Size of fingerprint buffer (must be >= 32 for SHA-256)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Generates key fingerprint by computing hash of key material.
 * Typically uses SHA-256 for fingerprint generation.
 *
 * @note Fingerprint algorithm: Uses SHA-256 hash of key material.
 *       Fingerprint is 32 bytes (256 bits) for SHA-256.
 *
 * @note Key material: Hashes raw 32-byte key data (not key type or comment).
 *       Fingerprint uniquely identifies the key.
 *
 * @note Buffer requirements: Fingerprint buffer must be at least 32 bytes for SHA-256.
 *       Function may use different hash algorithms in the future.
 *
 * @warning Fingerprint buffer must be large enough for hash output.
 *          Function validates buffer size but may overflow if buffer is too small.
 *
 * @ingroup keys
 */
asciichat_error_t generate_key_fingerprint(const public_key_t *key, uint8_t *fingerprint_out, size_t fingerprint_size);

/** @} */

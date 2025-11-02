#pragma once

/**
 * @file crypto/keys/gpg_keys.h
 * @ingroup crypto
 * @brief GPG key parsing and validation functions
 *
 * This module handles GPG key parsing, validation, and conversion
 * to X25519 for key exchange operations.
 *
 * @warning GPG SUPPORT IS CURRENTLY DISABLED: This code exists but many
 *          functions are not fully implemented. GPG key parsing may not
 *          work until GPG support is re-enabled and functions are completed.
 *
 * @note Key format: Only Ed25519 GPG keys are supported.
 *       RSA/ECDSA GPG keys are NOT supported.
 *
 * @note Key conversion: GPG Ed25519 keys are extracted and converted to X25519
 *       for key exchange using libsodium's conversion functions.
 *
 * @note OpenPGP format: Functions parse OpenPGP packet structure to extract
 *       Ed25519 public keys from GPG keys.
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
 * @name GPG Key Parsing
 * @{
 */

/**
 * @brief Parse GPG key from armored text format
 * @param gpg_key_text GPG key in armored format (-----BEGIN PGP PUBLIC KEY BLOCK-----, must not be NULL)
 * @param key_out Output structure for parsed public key (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses GPG key from armored format and extracts Ed25519 public key.
 * Converts to X25519 for key exchange.
 *
 * @note GPG format: Expects PEM-armored GPG key format.
 *       Format: "-----BEGIN PGP PUBLIC KEY BLOCK-----" ... "-----END PGP PUBLIC KEY BLOCK-----"
 *
 * @note Ed25519 extraction: Extracts Ed25519 public key from GPG key structure.
 *       Parses OpenPGP packet structure to find Ed25519 subkey.
 *
 * @note Key conversion: Converts extracted Ed25519 key to X25519 for key exchange.
 *       Uses libsodium's conversion function.
 *
 * @note Key validation: Validates GPG key format before parsing.
 *       Returns error if format is invalid.
 *
 * @warning GPG support is currently disabled. Many underlying functions are not implemented.
 *          This function may not work until GPG support is re-enabled.
 *
 * @warning Key format: Only Ed25519 GPG keys are supported.
 *          RSA/ECDSA GPG keys will return error.
 *
 * @ingroup crypto
 */
asciichat_error_t parse_gpg_key(const char *gpg_key_text, public_key_t *key_out);

/**
 * @brief Parse GPG key from binary format
 * @param gpg_key_binary GPG key in binary format (must not be NULL)
 * @param key_size Size of binary key data (must be > 0)
 * @param key_out Output structure for parsed public key (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses GPG key from binary format (raw OpenPGP packets).
 * Extracts Ed25519 public key and converts to X25519.
 *
 * @note Binary format: Accepts raw OpenPGP packet structure.
 *       Must parse packet structure to find Ed25519 public key packet.
 *
 * @note OpenPGP packets: Parses OpenPGP packet format to extract public key.
 *       Requires understanding of OpenPGP packet structure.
 *
 * @warning NOT YET IMPLEMENTED: This function is not yet implemented.
 *          Returns ERROR_CRYPTO_KEY with "Binary GPG key parsing not yet implemented".
 *
 * @warning GPG support is currently disabled. Function will not work until implemented.
 *
 * @ingroup crypto
 */
asciichat_error_t parse_gpg_key_binary(const uint8_t *gpg_key_binary, size_t key_size, public_key_t *key_out);

/**
 * @brief Validate GPG key format and structure
 * @param gpg_key_text GPG key text to validate (must not be NULL)
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates GPG key format before parsing.
 * Checks for correct armored format and basic structure.
 *
 * @note Format validation: Checks for correct armored format markers.
 *       Validates basic GPG key structure.
 *
 * @note Use case: Called before parsing to ensure key format is valid.
 *       Returns error early if format is invalid.
 *
 * @warning GPG support is currently disabled. Function may not work until GPG support is re-enabled.
 *
 * @ingroup crypto
 */
asciichat_error_t validate_gpg_key_format(const char *gpg_key_text);

/**
 * @brief Extract Ed25519 public key from GPG key
 * @param gpg_key_text GPG key in armored format (must not be NULL)
 * @param ed25519_pk Output buffer for Ed25519 public key (32 bytes, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Extracts Ed25519 public key from GPG key by parsing OpenPGP packet structure.
 *
 * @note OpenPGP parsing: Parses OpenPGP packet structure to find Ed25519 public key packet.
 *       Requires parsing of packet headers and packet data.
 *
 * @note Key extraction: Finds Ed25519 subkey in GPG key structure.
 *       Extracts 32-byte Ed25519 public key material.
 *
 * @warning NOT YET IMPLEMENTED: This function is not yet implemented.
 *          Returns ERROR_CRYPTO_KEY with "Ed25519 extraction from GPG key not yet implemented".
 *
 * @warning GPG support is currently disabled. Function will not work until implemented.
 *
 * @ingroup crypto
 */
asciichat_error_t extract_ed25519_from_gpg(const char *gpg_key_text, uint8_t ed25519_pk[32]);

/**
 * @brief Convert GPG key to X25519 for key exchange
 * @param gpg_key_text GPG key in armored format (must not be NULL)
 * @param x25519_pk Output buffer for X25519 public key (32 bytes, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Converts GPG key to X25519 format for Diffie-Hellman key exchange.
 * Extracts Ed25519 key and converts to X25519.
 *
 * @note Conversion flow:
 *       1. Extract Ed25519 public key from GPG key
 *       2. Convert Ed25519 to X25519 using libsodium
 *
 * @note Key conversion: Uses crypto_sign_ed25519_pk_to_curve25519() from libsodium.
 *       Conversion is mathematically safe (same curve, different representation).
 *
 * @warning GPG support is currently disabled. Function depends on extract_ed25519_from_gpg()
 *          which is not yet implemented. Function will not work until GPG support is re-enabled.
 *
 * @ingroup crypto
 */
asciichat_error_t gpg_to_x25519_public(const char *gpg_key_text, uint8_t x25519_pk[32]);

/** @} */

/**
 * @name GPG Key Operations
 * @{
 */

/**
 * @brief Get GPG key fingerprint
 * @param gpg_key_text GPG key in armored format (must not be NULL)
 * @param fingerprint_out Output buffer for fingerprint (20 bytes for SHA-1, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Extracts GPG key fingerprint (SHA-1 hash of key material).
 * Used for key identification and verification.
 *
 * @note Fingerprint format: SHA-1 hash of key material (20 bytes).
 *       Standard GPG fingerprint format.
 *
 * @note OpenPGP parsing: Parses OpenPGP packet structure and computes SHA-1 hash.
 *       Requires parsing of packet structure.
 *
 * @warning NOT YET IMPLEMENTED: This function is not yet implemented.
 *          Returns ERROR_CRYPTO_KEY with "GPG fingerprint extraction not yet implemented".
 *
 * @warning GPG support is currently disabled. Function will not work until implemented.
 *
 * @ingroup crypto
 */
asciichat_error_t get_gpg_fingerprint(const char *gpg_key_text, uint8_t fingerprint_out[20]);

/**
 * @brief Get GPG key ID (short fingerprint)
 * @param gpg_key_text GPG key in armored format (must not be NULL)
 * @param key_id_out Output buffer for key ID (8 bytes, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Extracts GPG key ID (last 8 bytes of SHA-1 fingerprint).
 * Used for key identification in short form.
 *
 * @note Key ID format: Last 8 bytes of SHA-1 fingerprint (16 hex chars).
 *       Example: "EDDAE1DA7360D7F4"
 *
 * @note Key ID extraction: Computes full fingerprint and extracts last 8 bytes.
 *       Alternatively, can parse from GPG key structure directly.
 *
 * @warning NOT YET IMPLEMENTED: This function is not yet implemented.
 *          Returns ERROR_CRYPTO_KEY with "GPG key ID extraction not yet implemented".
 *
 * @warning GPG support is currently disabled. Function will not work until implemented.
 *
 * @ingroup crypto
 */
asciichat_error_t get_gpg_key_id(const char *gpg_key_text, uint8_t key_id_out[8]);

/**
 * @brief Check if GPG key is expired
 * @param gpg_key_text GPG key in armored format (must not be NULL)
 * @param is_expired Output: true if key is expired, false otherwise (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Checks if GPG key has expired by parsing expiration date from key structure.
 *
 * @note Expiration checking: Parses OpenPGP packet structure to find expiration date.
 *       Compares expiration date with current date.
 *
 * @note Default behavior: Currently defaults to not expired if parsing fails.
 *       Function returns error but sets is_expired to false.
 *
 * @warning NOT YET IMPLEMENTED: This function is not yet implemented.
 *          Returns ERROR_CRYPTO_KEY with "GPG key expiry checking not yet implemented".
 *          Defaults is_expired to false before returning error.
 *
 * @warning GPG support is currently disabled. Function will not work until implemented.
 *
 * @ingroup crypto
 */
asciichat_error_t check_gpg_key_expiry(const char *gpg_key_text, bool *is_expired);

/** @} */

/**
 * @name GPG Key Formatting
 * @{
 */

/**
 * @brief Format GPG key for display
 * @param gpg_key_text GPG key in armored format (must not be NULL)
 * @param output Output buffer for formatted key (must not be NULL)
 * @param output_size Size of output buffer (must be >= 64)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Formats GPG key for display by extracting key ID and creating display string.
 *
 * @note Display format: "GPG key ID: <hex_key_id>" (e.g., "GPG key ID: EDDAE1DA7360D7F4").
 *
 * @note Key ID extraction: Uses get_gpg_key_id() to extract key ID.
 *       Falls back to generic message if extraction fails.
 *
 * @note Buffer requirements: Output buffer must be at least 64 bytes.
 *       Format string is "GPG key ID: " (12 chars) + 16 hex chars + null = 29 bytes minimum.
 *
 * @warning GPG support is currently disabled. Function depends on get_gpg_key_id()
 *          which is not yet implemented. Falls back to generic message if extraction fails.
 *
 * @ingroup crypto
 */
asciichat_error_t format_gpg_key_display(const char *gpg_key_text, char *output, size_t output_size);

/**
 * @brief Extract key comment/email from GPG key
 * @param gpg_key_text GPG key in armored format (must not be NULL)
 * @param comment_out Output buffer for comment (must not be NULL)
 * @param comment_size Size of comment buffer (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Extracts comment/email from GPG key by parsing user ID packet.
 *
 * @note Comment extraction: Parses OpenPGP packet structure to find user ID packet.
 *       Extracts comment/email from user ID packet.
 *
 * @note User ID format: GPG user IDs typically contain name and email.
 *       Format: "Name <email@example.com>" or just "email@example.com"
 *
 * @warning GPG support is currently disabled. Function may not work until GPG support is re-enabled.
 *
 * @ingroup crypto
 */
asciichat_error_t extract_gpg_key_comment(const char *gpg_key_text, char *comment_out, size_t comment_size);

/** @} */

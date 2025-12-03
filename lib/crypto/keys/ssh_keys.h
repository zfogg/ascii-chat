#pragma once

/**
 * @file crypto/keys/ssh_keys.h
 * @brief SSH key parsing and validation functions
 * @ingroup keys
 * @addtogroup keys
 * @{
 *
 * This module handles SSH Ed25519 key parsing, validation, and conversion
 * to X25519 for key exchange operations.
 *
 * @note Key format: Only Ed25519 keys are supported.
 *       RSA/ECDSA keys are NOT supported.
 *
 * @note OpenSSH format: Supports OpenSSH private key format (openssh-key-v1).
 *       Supports encrypted and unencrypted keys.
 *
 * @note Key conversion: Ed25519 keys are converted to X25519 for key exchange
 *       using libsodium's conversion functions.
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
 * @name SSH Key Parsing
 * @{
 */

/**
 * @brief Parse SSH Ed25519 public key from "ssh-ed25519 AAAAC3..." format
 * @param line SSH key line to parse (must not be NULL)
 * @param ed25519_pk Output buffer for Ed25519 public key (32 bytes, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses SSH Ed25519 public key from standard SSH public key format.
 * Format: "ssh-ed25519 <base64_key> [comment]"
 *
 * @note Format: Expects "ssh-ed25519" prefix followed by base64-encoded public key.
 *       Optional comment after key is ignored.
 *
 * @note Base64 decoding: Decodes base64 key to extract 32-byte Ed25519 public key.
 *       Validates key format and length.
 *
 * @note Key size: Ed25519 public key is exactly 32 bytes.
 *       Returns error if decoded key is not 32 bytes.
 *
 * @warning Key format: Must start with "ssh-ed25519".
 *          Other key types (RSA, ECDSA) are NOT supported.
 *
 * @ingroup keys
 */
asciichat_error_t parse_ssh_ed25519_line(const char *line, uint8_t ed25519_pk[32]);

/**
 * @brief Parse SSH Ed25519 private key from file
 * @param key_path Path to SSH private key file (must not be NULL)
 * @param key_out Output structure for parsed private key (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses OpenSSH Ed25519 private key from file.
 * Supports both encrypted and unencrypted keys.
 *
 * @note Key format: Supports OpenSSH private key format (openssh-key-v1).
 *       Format: "-----BEGIN OPENSSH PRIVATE KEY-----" ... "-----END OPENSSH PRIVATE KEY-----"
 *
 * @note Encrypted keys: If key is encrypted, prompts for password and decrypts it natively.
 *       Uses bcrypt_pbkdf (from libsodium-bcrypt-pbkdf) + BearSSL AES (aes256-ctr/aes256-cbc) for OpenSSH format
 * decryption. No external tools (ssh-keygen) required.
 *
 * @note Key structure: Parses OpenSSH key structure:
 *       - Magic: "openssh-key-v1\0"
 *       - Ciphername: "none" for unencrypted, cipher name for encrypted
 *       - KDF name and options (for encrypted keys)
 *       - Number of keys (must be 1)
 *       - Public key (for verification)
 *       - Private key blob (decrypted if encrypted)
 *
 * @note Ed25519 extraction: Extracts Ed25519 seed (32 bytes) and public key (32 bytes)
 *       from private key blob. Stores as 64-byte Ed25519 key (seed + public).
 *
 * @note File validation: Validates file permissions before parsing.
 *       Warns if file has overly permissive permissions (world-readable).
 *
 * @warning Key format: Only Ed25519 keys are supported.
 *          RSA/ECDSA keys will return error.
 *
 * @warning File permissions: Private key files should have restrictive permissions (0600).
 *          Function warns but does not fail on overly permissive permissions.
 *
 * @warning Password: Encrypted keys require password for native decryption.
 *          Decryption is done using bcrypt_pbkdf + BearSSL AES (supports aes256-ctr and aes256-cbc).
 *          May fail if password is incorrect, KDF parameters are unsupported, or unsupported encryption cipher.
 *
 * @ingroup keys
 */
asciichat_error_t parse_ssh_private_key(const char *key_path, private_key_t *key_out);

/**
 * @brief Validate SSH key file permissions and format
 * @param key_path Path to SSH key file (must not be NULL)
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates SSH key file before parsing. Checks:
 * - File exists and is readable
 * - File has valid SSH private key header ("-----BEGIN OPENSSH PRIVATE KEY-----")
 * - File permissions are appropriate (warns if overly permissive)
 *
 * @note Permission checking: Checks file permissions on Unix systems.
 *       Warns if file has world-readable permissions but does not fail validation.
 *
 * @note File header: Validates that file starts with correct SSH private key header.
 *       Returns error if header is missing or invalid.
 *
 * @note Platform-specific: Permission checking is Unix-specific.
 *       Windows does not have Unix-style permissions.
 *
 * @warning File permissions: Private key files should have restrictive permissions (0600).
 *          Function warns but does not fail on overly permissive permissions.
 *
 * @ingroup keys
 */
asciichat_error_t validate_ssh_key_file(const char *key_path);

/** @} */

/**
 * @name Key Conversion
 * @{
 */

/**
 * @brief Convert Ed25519 public key to X25519 for key exchange
 * @param ed25519_pk Ed25519 public key (32 bytes, must not be NULL)
 * @param x25519_pk Output buffer for X25519 public key (32 bytes, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Converts Ed25519 public key to X25519 format for Diffie-Hellman key exchange.
 * Uses libsodium's conversion function.
 *
 * @note Conversion: Uses crypto_sign_ed25519_pk_to_curve25519() from libsodium.
 *       Conversion is mathematically safe (same curve, different representation).
 *
 * @note Key size: Both Ed25519 and X25519 keys are 32 bytes.
 *       No size change during conversion.
 *
 * @note Use case: Used for key exchange when Ed25519 key is used for signing
 *       but X25519 is needed for key exchange (DH).
 *
 * @warning All keys must be 32 bytes. Function validates key size before conversion.
 *
 * @ingroup keys
 */
asciichat_error_t ed25519_to_x25519_public(const uint8_t ed25519_pk[32], uint8_t x25519_pk[32]);

/**
 * @brief Convert Ed25519 private key to X25519 for key exchange
 * @param ed25519_sk Ed25519 private key (64 bytes: seed + public, must not be NULL)
 * @param x25519_sk Output buffer for X25519 private key (32 bytes, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Converts Ed25519 private key to X25519 format for Diffie-Hellman key exchange.
 * Uses libsodium's conversion function.
 *
 * @note Ed25519 key format: Ed25519 private key is 64 bytes (32-byte seed + 32-byte public key).
 *       Function extracts seed (first 32 bytes) and converts to X25519 scalar.
 *
 * @note Conversion: Uses crypto_sign_ed25519_sk_to_curve25519() from libsodium.
 *       Conversion is mathematically safe (same curve, different representation).
 *
 * @note Key size: Ed25519 private key is 64 bytes, X25519 private key is 32 bytes.
 *       Extracts 32-byte seed from Ed25519 key and converts to X25519 scalar.
 *
 * @note Use case: Used for key exchange when Ed25519 key is used for signing
 *       but X25519 is needed for key exchange (DH).
 *
 * @warning Ed25519 key must be exactly 64 bytes (32-byte seed + 32-byte public).
 *          Function validates key size before conversion.
 *
 * @ingroup keys
 */
asciichat_error_t ed25519_to_x25519_private(const uint8_t ed25519_sk[64], uint8_t x25519_sk[32]);

/** @} */

/**
 * @name SSH Key Operations
 * @{
 */

/**
 * @brief Sign a message with Ed25519 private key
 * @param key Ed25519 private key (must not be NULL)
 * @param message Message to sign (must not be NULL)
 * @param message_len Length of message to sign
 * @param signature Output buffer for Ed25519 signature (64 bytes, must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Signs a message using Ed25519. Uses SSH agent if available, otherwise in-memory key.
 *
 * @note Agent support: If use_ssh_agent is true, uses SSH agent for signing.
 *       Key stays in agent (not loaded into memory). Signing happens via agent protocol.
 *
 * @note In-memory signing: If use_ssh_agent is false, uses in-memory key for signing.
 *       Key must be loaded into memory (via parse_ssh_private_key()).
 *
 * @note Signature format: Ed25519 signature is always 64 bytes (R || S format).
 *
 * @warning Agent availability: If use_ssh_agent is true but SSH agent is not available,
 *          function returns error. Check SSH agent availability before using.
 *
 * @ingroup keys
 */
asciichat_error_t ed25519_sign_message(const private_key_t *key, const uint8_t *message, size_t message_len,
                                       uint8_t signature[64]);

/**
 * @brief Verify an Ed25519 signature
 * @param public_key Ed25519 public key (32 bytes, must not be NULL)
 * @param message Message that was signed (must not be NULL)
 * @param message_len Length of message that was signed
 * @param signature Ed25519 signature to verify (64 bytes, must not be NULL)
 * @return ASCIICHAT_OK if signature is valid, error code on failure
 *
 * Verifies an Ed25519 signature using libsodium's verification function.
 *
 * @note Signature format: Ed25519 signature is always 64 bytes (R || S format).
 *
 * @note Verification: Uses crypto_sign_ed25519_verify_detached() from libsodium.
 *       Returns error if signature is invalid or message was tampered with.
 *
 * @note Constant-time: Verification uses constant-time comparison to prevent timing attacks.
 *
 * @warning Always check return value. Error indicates invalid signature or tampering.
 *
 * @ingroup keys
 */
asciichat_error_t ed25519_verify_signature(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                                           const uint8_t signature[64]);

/** @} */

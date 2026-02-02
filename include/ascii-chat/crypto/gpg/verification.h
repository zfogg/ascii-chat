#pragma once

/**
 * @file crypto/gpg/verification.h
 * @brief GPG signature verification interface
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * This header provides GPG signature verification operations supporting both
 * GPG binary-based verification and direct cryptographic verification via libgcrypt.
 * Handles both raw Ed25519 signatures and OpenPGP-formatted signatures.
 *
 * @note Verification methods:
 *       - GPG binary: Uses `gpg --verify` for full OpenPGP packet verification
 *       - libgcrypt: Direct Ed25519 signature verification without GPG binary
 *
 * @note Key requirements: Only Ed25519 signatures are supported.
 *       RSA/ECDSA signatures will cause verification to fail.
 *
 * @note Signature formats:
 *       - Raw Ed25519: 64-byte signature (R||S format) from gpg_sign_detached_ed25519()
 *       - OpenPGP: Variable-length signature packet from gpg_sign_with_key()
 *
 * @note GPG binary dependency: Functions using GPG binary require `gpg` in PATH.
 *       libgcrypt-based verification works without GPG binary installed.
 *
 * @note Key trust: GPG binary verification checks key trust and validity.
 *       libgcrypt verification only checks cryptographic signature validity.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stddef.h>
#include <stdint.h>

/**
 * @name GPG Signature Verification
 * @{
 */

/**
 * @brief Verify Ed25519 signature using GPG binary
 * @param key_id GPG key ID to use for verification (8/16/40-char hex, must not be NULL)
 * @param message Message that was signed (must not be NULL)
 * @param message_len Message length in bytes (must be > 0)
 * @param signature 64-byte Ed25519 signature (must not be NULL)
 * @return 0 on success (valid signature), -1 on error (invalid signature or error)
 *
 * Verifies raw Ed25519 signature using `gpg --verify` command.
 * Converts raw signature to OpenPGP format, then uses GPG binary for verification.
 *
 * @note Key ID format: Accepts short (8-char), long (16-char), or full (40-char) hex key IDs.
 *       Prefix "0x" is optional and will be added automatically if missing.
 *       Examples: "7FE90A79F2E80ED3", "0x7FE90A79F2E80ED3", "EDDAE1DA7360D7F4"
 *
 * @note Verification method: Uses `gpg --verify` command on converted OpenPGP signature.
 *       Checks both cryptographic validity and key trust status.
 *
 * @note Signature format: Input must be raw 64-byte Ed25519 signature (R||S).
 *       Function internally converts to OpenPGP format for GPG binary.
 *       Not compatible with OpenPGP-wrapped signatures (use gpg_verify_signature_with_binary()).
 *
 * @note Key trust checking: GPG binary checks key trust and expiry.
 *       Verification fails if key is expired, revoked, or untrusted.
 *
 * @note Public key requirement: Key must be in GPG keyring for verification.
 *       Import public key first with `gpg --import <public_key_file>`.
 *
 * @note Return value interpretation:
 *       - 0: Signature is cryptographically valid and key is trusted
 *       - -1: Signature invalid, key not found, key untrusted, or other error
 *
 * @note Error conditions: Returns -1 if:
 *       - Signature cryptographically invalid
 *       - Key ID not found in keyring
 *       - Key is expired or revoked
 *       - Key is not trusted (not in web of trust)
 *       - `gpg` binary not found in PATH
 *       - OpenPGP conversion fails
 *
 * @warning GPG binary required: Requires `gpg` binary in PATH.
 *          Returns -1 if GPG is not installed or not accessible.
 *
 * @warning Ed25519 only: Only Ed25519 signatures are supported (OpenPGP algorithm 22).
 *          RSA/ECDSA signatures will return error.
 *
 * @warning Key must be imported: Public key must exist in GPG keyring.
 *          Returns -1 if key is not found (import with `gpg --import`).
 *
 * @warning Trust required: GPG checks key trust status.
 *          Verification may fail if key is not in web of trust.
 *
 * @ingroup crypto
 */
int gpg_verify_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                                const uint8_t signature[64]);

/**
 * @brief Verify Ed25519 signature using libgcrypt (no GPG binary required)
 * @param public_key 32-byte Ed25519 public key (must not be NULL)
 * @param message Message that was signed (must not be NULL)
 * @param message_len Message length in bytes (must be > 0)
 * @param signature 64-byte Ed25519 signature (must not be NULL)
 * @return 0 on success (valid signature), -1 on error (invalid signature or error)
 *
 * Verifies Ed25519 signature directly using libgcrypt cryptographic library.
 * Does not require GPG binary - performs pure cryptographic verification.
 *
 * @note Verification method: Uses libgcrypt's gcry_pk_verify() for Ed25519.
 *       Pure cryptographic verification - no key trust or expiry checking.
 *
 * @note No GPG required: Works without GPG binary installed.
 *       Only requires libgcrypt library (linked during build).
 *
 * @note Public key format: Accepts raw 32-byte Ed25519 public key.
 *       Can be extracted from GPG keyring using gpg_get_public_key().
 *
 * @note Signature format: Input must be raw 64-byte Ed25519 signature (R||S).
 *       Compatible with signatures from gpg_sign_detached_ed25519().
 *       Not compatible with OpenPGP-wrapped signatures.
 *
 * @note No trust checking: Only verifies cryptographic signature validity.
 *       Does not check key expiry, revocation, or trust status.
 *       Use gpg_verify_detached_ed25519() if trust checking is needed.
 *
 * @note Performance: Faster than GPG binary verification (no subprocess spawning).
 *       Suitable for high-frequency verification operations.
 *
 * @note Use case: Prefer this when:
 *       - You have raw public key (not just key ID)
 *       - You don't need key trust/expiry checking
 *       - You want faster verification (no subprocess)
 *       - GPG binary may not be installed
 *
 * @note Return value interpretation:
 *       - 0: Signature is cryptographically valid for given public key
 *       - -1: Signature invalid or verification error
 *
 * @note Buffer requirements:
 *       - public_key: Must be exactly 32 bytes (Ed25519 public key)
 *       - signature: Must be exactly 64 bytes (Ed25519 signature)
 *
 * @note Error conditions: Returns -1 if:
 *       - Signature cryptographically invalid
 *       - libgcrypt initialization fails
 *       - Public key format invalid
 *       - Signature format invalid
 *
 * @warning Ed25519 only: Only Ed25519 signatures are supported.
 *          Other key types will cause libgcrypt errors.
 *
 * @warning No trust checking: Does not verify key trust, expiry, or revocation.
 *          Only checks cryptographic signature validity.
 *
 * @warning Buffer sizes: public_key must be 32 bytes, signature must be 64 bytes.
 *          Other sizes will cause verification to fail.
 *
 * @ingroup crypto
 */
int gpg_verify_signature(const uint8_t *public_key, const uint8_t *message, size_t message_len,
                         const uint8_t *signature);

/**
 * @brief Verify OpenPGP signature using GPG binary
 * @param signature GPG signature in OpenPGP packet format (must not be NULL)
 * @param signature_len Signature length in bytes (must be > 0)
 * @param message Message that was signed (must not be NULL)
 * @param message_len Message length in bytes (must be > 0)
 * @param expected_key_id Expected GPG key ID for signature (optional, can be NULL)
 * @return 0 on success (valid signature), -1 on error (invalid signature or error)
 *
 * Verifies OpenPGP-formatted signature using `gpg --verify` command.
 * Accepts full OpenPGP signature packets from gpg_sign_with_key().
 *
 * @note Verification method: Uses `gpg --verify` on OpenPGP signature packet.
 *       Checks both cryptographic validity and key trust status.
 *
 * @note Signature format: Input must be OpenPGP signature packet (binary format).
 *       Compatible with output from gpg_sign_with_key().
 *       Not compatible with raw 64-byte Ed25519 signatures (use gpg_verify_detached_ed25519()).
 *
 * @note OpenPGP parsing: GPG binary parses signature packet to extract:
 *       - Signature algorithm (must be Ed25519/algorithm 22)
 *       - Signing key ID
 *       - Signature data
 *
 * @note Key ID checking: If expected_key_id is provided, verifies signature was made by that key.
 *       Returns -1 if signature is from different key (prevents key substitution attacks).
 *
 * @note Optional key verification: If expected_key_id is NULL, accepts signature from any key.
 *       Useful when you don't know signer's key ID in advance.
 *
 * @note Public key requirement: Signing key must be in GPG keyring for verification.
 *       Import public key first with `gpg --import <public_key_file>`.
 *
 * @note Key trust checking: GPG binary checks key trust and expiry.
 *       Verification fails if key is expired, revoked, or untrusted.
 *
 * @note Variable length: OpenPGP signatures are variable length (typically 150-200 bytes).
 *       Must pass actual signature length in signature_len parameter.
 *
 * @note Use case: Prefer this when:
 *       - You have OpenPGP-formatted signature (from gpg_sign_with_key())
 *       - You need key trust/expiry checking
 *       - You want to verify signer identity (via expected_key_id)
 *
 * @note Return value interpretation:
 *       - 0: Signature valid, from expected key (if specified), and key trusted
 *       - -1: Signature invalid, wrong key, key untrusted, or other error
 *
 * @note Error conditions: Returns -1 if:
 *       - Signature cryptographically invalid
 *       - Signature from unexpected key (if expected_key_id specified)
 *       - Signing key not found in keyring
 *       - Key is expired or revoked
 *       - Key is not trusted
 *       - `gpg` binary not found in PATH
 *       - OpenPGP packet parsing fails
 *
 * @warning GPG binary required: Requires `gpg` binary in PATH.
 *          Returns -1 if GPG is not installed or not accessible.
 *
 * @warning Ed25519 only: Only Ed25519 signatures are supported (OpenPGP algorithm 22).
 *          RSA/ECDSA signatures will return error.
 *
 * @warning Key must be imported: Signing public key must exist in GPG keyring.
 *          Returns -1 if key is not found (import with `gpg --import`).
 *
 * @warning Trust required: GPG checks key trust status.
 *          Verification may fail if key is not in web of trust.
 *
 * @warning Key ID mismatch: If expected_key_id is provided and signature is from different key,
 *          verification fails even if signature is cryptographically valid.
 *
 * @ingroup crypto
 */
int gpg_verify_signature_with_binary(const uint8_t *signature, size_t signature_len, const uint8_t *message,
                                     size_t message_len, const char *expected_key_id);

/** @} */

/** @} */ /* crypto */

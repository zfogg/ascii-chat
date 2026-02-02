#pragma once

/**
 * @file crypto/gpg/signing.h
 * @brief GPG message signing interface
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * This header provides GPG signing operations for creating detached signatures.
 * Supports both OpenPGP-formatted signatures and raw Ed25519 signatures extracted
 * from GPG output.
 *
 * @note Signing method: Uses `gpg --detach-sign` to create detached signatures.
 *       Signatures are separate from message data (not inline signatures).
 *
 * @note Key requirements: Only Ed25519 GPG keys are supported.
 *       RSA/ECDSA keys will cause signing operations to fail.
 *
 * @note Output formats:
 *       - gpg_sign_with_key(): Returns full OpenPGP signature packet (variable length)
 *       - gpg_sign_detached_ed25519(): Extracts raw 64-byte Ed25519 signature (R||S)
 *
 * @note GPG binary: Requires `gpg` binary in PATH for all operations.
 *       Returns error if GPG is not installed or not accessible.
 *
 * @note Key passphrase: If key is encrypted, GPG may prompt for passphrase.
 *       Use ssh-agent or gpg-agent for password-free signing, or set
 *       $ASCII_CHAT_KEY_PASSWORD environment variable.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stddef.h>
#include <stdint.h>

/**
 * @name GPG Signing Operations
 * @{
 */

/**
 * @brief Sign a message using GPG key and return OpenPGP signature
 * @param key_id GPG key ID (8/16/40-char hex string, must not be NULL)
 * @param message Message to sign (must not be NULL)
 * @param message_len Message length in bytes (must be > 0)
 * @param signature_out Output buffer for OpenPGP signature (must be >= 512 bytes)
 * @param signature_len_out Output parameter for actual signature length (must not be NULL)
 * @return 0 on success, -1 on error
 *
 * Signs message using `gpg --detach-sign` and returns full OpenPGP signature packet.
 * Signature is in binary OpenPGP format and includes packet headers.
 *
 * @note Key ID format: Accepts short (8-char), long (16-char), or full (40-char) hex key IDs.
 *       Prefix "0x" is optional and will be added automatically if missing.
 *       Examples: "7FE90A79F2E80ED3", "0x7FE90A79F2E80ED3", "EDDAE1DA7360D7F4"
 *
 * @note Signing method: Uses `gpg --detach-sign --local-user 0x<KEY_ID>` command.
 *       Creates detached signature (signature separate from message).
 *
 * @note Output format: Returns binary OpenPGP signature packet (RFC 4880).
 *       Signature includes packet headers and metadata beyond raw signature data.
 *       Typical size: 150-200 bytes for Ed25519 signatures.
 *
 * @note Variable length: OpenPGP format means signature length varies.
 *       Always check signature_len_out to know actual signature length.
 *       Buffer must be at least 512 bytes to accommodate various key types.
 *
 * @note GPG interaction: Executes `gpg` as subprocess and reads output.
 *       Signature is read from GPG's stdout in binary format.
 *
 * @note Key passphrase handling:
 *       - If key is encrypted, GPG may prompt for passphrase interactively
 *       - Use gpg-agent for password-free signing (recommended)
 *       - Or set $ASCII_CHAT_KEY_PASSWORD environment variable
 *
 * @note Buffer requirements:
 *       - signature_out: Must be at least 512 bytes
 *       - signature_len_out: Will be set to actual signature length (typically 150-200 bytes)
 *
 * @note Error conditions: Returns -1 if:
 *       - Key ID not found in keyring
 *       - Key is not Ed25519 (RSA/ECDSA not supported)
 *       - `gpg` binary not found in PATH
 *       - Signing operation fails (wrong passphrase, key expired, etc.)
 *       - Output buffer too small (< 512 bytes)
 *
 * @warning GPG binary required: Requires `gpg` binary in PATH.
 *          Returns -1 if GPG is not installed or not accessible.
 *
 * @warning Ed25519 only: Only Ed25519 keys are supported (OpenPGP algorithm 22).
 *          RSA/ECDSA keys will return error.
 *
 * @warning Buffer size: signature_out must be at least 512 bytes.
 *          Smaller buffers may cause buffer overflow or signature truncation.
 *
 * @warning Key must exist: Key must exist in local GPG keyring.
 *          Returns -1 if key is not found (check with `gpg --list-keys <KEY_ID>`).
 *
 * @ingroup crypto
 */
int gpg_sign_with_key(const char *key_id, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                      size_t *signature_len_out);

/**
 * @brief Sign message with GPG and extract raw Ed25519 signature
 * @param key_id GPG key ID (8/16/40-char hex string, must not be NULL)
 * @param message Message to sign (must not be NULL)
 * @param message_len Message length in bytes (must be > 0)
 * @param signature_out Output buffer for 64-byte Ed25519 signature (must not be NULL)
 * @return 0 on success, -1 on error
 *
 * Signs message using `gpg --detach-sign`, then extracts raw 64-byte Ed25519 signature
 * from OpenPGP packet format. Returns signature in libsodium-compatible format (R||S).
 *
 * @note Key ID format: Accepts short (8-char), long (16-char), or full (40-char) hex key IDs.
 *       Prefix "0x" is optional and will be added automatically if missing.
 *
 * @note Signing method: Uses `gpg --detach-sign --local-user 0x<KEY_ID>` command.
 *       Then parses OpenPGP output to extract raw signature bytes.
 *
 * @note OpenPGP parsing: Parses binary OpenPGP signature packet to locate signature data.
 *       Extracts raw Ed25519 signature (R||S format, 64 bytes total).
 *       Skips packet headers and metadata to get pure signature bytes.
 *
 * @note Output format: Returns raw Ed25519 signature in libsodium format.
 *       Format: R || S (32 bytes R + 32 bytes S = 64 bytes total).
 *       Compatible with crypto_sign_verify_detached() from libsodium.
 *
 * @note Fixed length: Ed25519 signatures are always exactly 64 bytes.
 *       No length parameter needed - signature_out is always fully written.
 *
 * @note Use case: Prefer this over gpg_sign_with_key() when you need raw signature.
 *       Useful for protocol implementations that expect raw Ed25519 signatures
 *       rather than OpenPGP-wrapped signatures.
 *
 * @note Comparison with gpg_sign_with_key():
 *       - gpg_sign_with_key(): Returns full OpenPGP packet (~150-200 bytes)
 *       - gpg_sign_detached_ed25519(): Returns raw Ed25519 signature (64 bytes)
 *
 * @note Key passphrase handling:
 *       - If key is encrypted, GPG may prompt for passphrase interactively
 *       - Use gpg-agent for password-free signing (recommended)
 *       - Or set $ASCII_CHAT_KEY_PASSWORD environment variable
 *
 * @note Buffer requirements:
 *       - signature_out: Must be exactly 64 bytes (Ed25519 signature size)
 *
 * @note Error conditions: Returns -1 if:
 *       - Key ID not found in keyring
 *       - Key is not Ed25519 (RSA/ECDSA not supported)
 *       - `gpg` binary not found in PATH
 *       - Signing operation fails (wrong passphrase, key expired, etc.)
 *       - OpenPGP packet parsing fails
 *       - Signature extraction fails (unexpected packet format)
 *
 * @warning GPG binary required: Requires `gpg` binary in PATH.
 *          Returns -1 if GPG is not installed or not accessible.
 *
 * @warning Ed25519 only: Only Ed25519 keys are supported (OpenPGP algorithm 22).
 *          RSA/ECDSA keys will return error.
 *
 * @warning Buffer size: signature_out must be exactly 64 bytes.
 *          Function always writes exactly 64 bytes on success.
 *
 * @warning Key must exist: Key must exist in local GPG keyring.
 *          Returns -1 if key is not found (check with `gpg --list-keys <KEY_ID>`).
 *
 * @ingroup crypto
 */
int gpg_sign_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                              uint8_t signature_out[64]);

/** @} */

/** @} */ /* crypto */

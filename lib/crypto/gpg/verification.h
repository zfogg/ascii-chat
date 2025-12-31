#pragma once

/**
 * @file crypto/gpg/verification.h
 * @brief GPG signature verification interface
 * @ingroup crypto
 */

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Verify GPG signature using gpg --verify
 * @param key_id GPG key ID to use for verification
 * @param message Message that was signed
 * @param message_len Message length
 * @param signature 64-byte Ed25519 signature
 * @return 0 on success, -1 on error
 */
int gpg_verify_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                                const uint8_t signature[64]);

/**
 * @brief Verify a GPG Ed25519 signature using libgcrypt
 * @param public_key 32-byte Ed25519 public key
 * @param message Message that was signed
 * @param message_len Message length
 * @param signature 64-byte Ed25519 signature
 * @return 0 on success, -1 on error
 */
int gpg_verify_signature(const uint8_t *public_key, const uint8_t *message, size_t message_len,
                         const uint8_t *signature);

/**
 * @brief Verify a GPG signature using gpg --verify binary
 * @param signature GPG signature in OpenPGP packet format
 * @param signature_len Signature length
 * @param message Message that was signed
 * @param message_len Message length
 * @param expected_key_id Expected GPG key ID (optional, can be NULL)
 * @return 0 on success, -1 on error
 */
int gpg_verify_signature_with_binary(const uint8_t *signature, size_t signature_len, const uint8_t *message,
                                     size_t message_len, const char *expected_key_id);

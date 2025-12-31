#pragma once

/**
 * @file crypto/gpg/signing.h
 * @brief GPG signing operations interface
 * @ingroup crypto
 */

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Sign a message using GPG key (via gpg --detach-sign)
 * @param key_id GPG key ID
 * @param message Message to sign
 * @param message_len Message length
 * @param signature_out Output buffer for signature (must provide at least 512 bytes)
 * @param signature_len_out Actual signature length written
 * @return 0 on success, -1 on error
 */
int gpg_sign_with_key(const char *key_id, const uint8_t *message, size_t message_len, uint8_t *signature_out,
                      size_t *signature_len_out);

/**
 * @brief Sign message using gpg --detach-sign and extract raw Ed25519 signature
 * @param key_id GPG key ID (16-char hex string)
 * @param message Message to sign
 * @param message_len Message length
 * @param signature_out Output buffer for 64-byte Ed25519 signature
 * @return 0 on success, -1 on error
 */
int gpg_sign_detached_ed25519(const char *key_id, const uint8_t *message, size_t message_len,
                              uint8_t signature_out[64]);

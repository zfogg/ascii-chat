#pragma once

/**
 * @file crypto/gpg/export.h
 * @brief GPG public key export interface
 * @ingroup crypto
 */

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Get public key from GPG keyring by key ID
 * @param key_id GPG key ID (16-char hex string)
 * @param public_key_out Output buffer for 32-byte Ed25519 public key
 * @param keygrip_out Output buffer for 40-char keygrip (optional, can be NULL)
 * @return 0 on success, -1 on error
 */
int gpg_get_public_key(const char *key_id, uint8_t *public_key_out, char *keygrip_out);

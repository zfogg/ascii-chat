#pragma once

/**
 * @file crypto/gpg/export.h
 * @brief GPG public key export interface
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * This header provides functions for exporting public keys from GPG keyring.
 * Supports retrieving Ed25519 public keys and keygrips for use in authentication
 * and signing operations.
 *
 * @note Key export: Uses `gpg --export` to extract public key from local keyring.
 *       Parses OpenPGP packet format to extract Ed25519 public key material.
 *
 * @note Keygrip extraction: Optionally extracts keygrip for use with GPG agent.
 *       Keygrip is a stable 40-char hex identifier computed from public key.
 *
 * @note Key ID formats: Supports short (8-char), long (16-char), and full (40-char) key IDs.
 *       Accepts key IDs with or without "0x" prefix.
 *
 * @note Ed25519 only: Only Ed25519 GPG keys are supported.
 *       RSA/ECDSA keys will cause export to fail.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stddef.h>
#include <stdint.h>

/**
 * @name GPG Key Export
 * @{
 */

/**
 * @brief Get public key from GPG keyring by key ID
 * @param key_id GPG key ID (8/16/40-char hex string, must not be NULL)
 * @param public_key_out Output buffer for 32-byte Ed25519 public key (must not be NULL)
 * @param keygrip_out Output buffer for 40-char keygrip + null terminator (can be NULL if not needed)
 * @return 0 on success, -1 on error
 *
 * Retrieves Ed25519 public key from GPG keyring using `gpg --export` command.
 * Parses OpenPGP packet format to extract raw 32-byte Ed25519 public key.
 *
 * @note Key ID format: Accepts short (8-char), long (16-char), or full (40-char) hex key IDs.
 *       Prefix "0x" is optional and will be added automatically if missing.
 *       Examples: "7FE90A79F2E80ED3", "0x7FE90A79F2E80ED3", "EDDAE1DA7360D7F4"
 *
 * @note Export method: Uses `gpg --export 0x<KEY_ID>` to export public key.
 *       Output is in binary OpenPGP packet format, which is then parsed.
 *
 * @note OpenPGP parsing: Parses OpenPGP packet structure to locate Ed25519 public key packet.
 *       Extracts 32-byte Ed25519 public key material from packet (algorithm ID 22).
 *       Skips 0x40 prefix byte if present (standard OpenPGP MPI format).
 *
 * @note Keygrip extraction: If keygrip_out is not NULL, extracts keygrip using `gpg --with-keygrip --list-keys`.
 *       Keygrip is 40-char hex string that identifies key in GPG agent.
 *       Useful for subsequent gpg_agent_sign() operations.
 *
 * @note Key validation: Validates that key is Ed25519 (algorithm 22 in OpenPGP).
 *       Returns error if key is RSA, ECDSA, or other unsupported algorithm.
 *
 * @note Buffer requirements:
 *       - public_key_out: Must be at least 32 bytes
 *       - keygrip_out: Must be at least 41 bytes (40 hex chars + null terminator) if provided
 *
 * @note Error conditions: Returns -1 if:
 *       - Key ID not found in keyring
 *       - Key is not Ed25519 (RSA/ECDSA not supported)
 *       - `gpg` binary not found in PATH
 *       - OpenPGP packet parsing fails
 *       - Key export produces empty output
 *
 * @warning GPG binary required: Requires `gpg` binary in PATH.
 *          Returns -1 if GPG is not installed or not accessible.
 *
 * @warning Ed25519 only: Only Ed25519 keys are supported (OpenPGP algorithm 22).
 *          RSA/ECDSA keys will return error.
 *
 * @warning Key ID must exist: Key must exist in local GPG keyring.
 *          Returns -1 if key is not found (check with `gpg --list-keys <KEY_ID>`).
 *
 * @ingroup crypto
 */
int gpg_get_public_key(const char *key_id, uint8_t *public_key_out, char *keygrip_out);

/** @} */

/** @} */ /* crypto */

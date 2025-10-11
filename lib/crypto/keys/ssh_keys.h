#pragma once

/**
 * @file crypto/keys/ssh_keys.h
 * @brief SSH key parsing and validation functions
 *
 * This module handles SSH Ed25519 key parsing, validation, and conversion
 * to X25519 for key exchange operations.
 */

#include "../constants.h"
#include "../../common.h"
#include "types.h" // Include the key type definitions
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// SSH Key Parsing Functions
// =============================================================================

/**
 * @brief Parse SSH Ed25519 public key from "ssh-ed25519 AAAAC3..." format
 * @param line The SSH key line to parse
 * @param ed25519_pk Output buffer for the Ed25519 public key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t parse_ssh_ed25519_line(const char *line, uint8_t ed25519_pk[32]);

/**
 * @brief Parse SSH Ed25519 private key from file
 * @param key_path Path to the SSH private key file
 * @param key_out Output structure for the parsed private key
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t parse_ssh_private_key(const char *key_path, private_key_t *key_out);

/**
 * @brief Validate SSH key file permissions and format
 * @param key_path Path to the SSH key file
 * @return ASCIICHAT_OK if valid, error code on failure
 */
asciichat_error_t validate_ssh_key_file(const char *key_path);

/**
 * @brief Convert Ed25519 public key to X25519 for key exchange
 * @param ed25519_pk The Ed25519 public key (32 bytes)
 * @param x25519_pk Output buffer for the X25519 public key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t ed25519_to_x25519_public(const uint8_t ed25519_pk[32], uint8_t x25519_pk[32]);

/**
 * @brief Convert Ed25519 private key to X25519 for key exchange
 * @param ed25519_sk The Ed25519 private key (64 bytes: seed + public)
 * @param x25519_sk Output buffer for the X25519 private key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t ed25519_to_x25519_private(const uint8_t ed25519_sk[64], uint8_t x25519_sk[32]);

// =============================================================================
// SSH Key Operations
// =============================================================================

/**
 * @brief Sign a message with Ed25519 private key
 * @param key The Ed25519 private key
 * @param message The message to sign
 * @param message_len Length of the message
 * @param signature Output buffer for the signature (64 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t ed25519_sign_message(const private_key_t *key, const uint8_t *message, size_t message_len,
                                       uint8_t signature[64]);

/**
 * @brief Verify an Ed25519 signature
 * @param public_key The Ed25519 public key (32 bytes)
 * @param message The message that was signed
 * @param message_len Length of the message
 * @param signature The signature to verify (64 bytes)
 * @return ASCIICHAT_OK if signature is valid, error code on failure
 */
asciichat_error_t ed25519_verify_signature(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                                           const uint8_t signature[64]);

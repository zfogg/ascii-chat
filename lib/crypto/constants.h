#pragma once

/**
 * @file crypto/constants.h
 * @brief Cryptographic constants and magic numbers
 *
 * This header defines cryptographic constants used throughout the crypto module
 * to eliminate magic numbers and improve maintainability.
 */

// =============================================================================
// SSH Key Structure Constants
// =============================================================================

// SSH key blob structure sizes
#define SSH_KEY_TYPE_LENGTH_SIZE 4       // Length of "ssh-ed25519" (4 bytes)
#define SSH_KEY_TYPE_STRING_SIZE 11      // "ssh-ed25519" string length
#define SSH_KEY_PUBLIC_KEY_LENGTH_SIZE 4 // Length of public key (4 bytes)
#define SSH_KEY_PUBLIC_KEY_SIZE 32       // Ed25519 public key size
#define SSH_KEY_HEADER_SIZE                                                                                            \
  (SSH_KEY_TYPE_LENGTH_SIZE + SSH_KEY_TYPE_STRING_SIZE + SSH_KEY_PUBLIC_KEY_LENGTH_SIZE + SSH_KEY_PUBLIC_KEY_SIZE)

// =============================================================================
// Cryptographic Constants
// =============================================================================

// Password requirements
#define MIN_PASSWORD_LENGTH 8   // Minimum password length
#define MAX_PASSWORD_LENGTH 256 // Maximum password length

// Key sizes
#define ED25519_PUBLIC_KEY_SIZE 32  // Ed25519 public key size
#define ED25519_PRIVATE_KEY_SIZE 64 // Ed25519 private key (seed + public)
#define ED25519_SIGNATURE_SIZE 64   // Ed25519 signature size
#define X25519_KEY_SIZE 32          // X25519 key size
#define HMAC_SHA256_SIZE 32         // HMAC-SHA256 output size

// Authentication packet sizes
#define AUTH_HMAC_SIZE 32                                                        // HMAC size in auth packets
#define AUTH_CHALLENGE_SIZE 32                                                   // Challenge nonce size
#define AUTH_COMBINED_SIZE (AUTH_HMAC_SIZE + AUTH_CHALLENGE_SIZE)                // Combined auth data size
#define AUTH_SIGNATURE_SIZE 64                                                   // Ed25519 signature size
#define AUTH_SIGNATURE_COMBINED_SIZE (AUTH_SIGNATURE_SIZE + AUTH_CHALLENGE_SIZE) // Signature + challenge

// =============================================================================
// File Permission Constants
// =============================================================================

#ifndef _WIN32
#define SSH_KEY_PERMISSIONS_MASK (S_IRWXG | S_IRWXO) // Group and other permissions mask
#define SSH_KEY_RECOMMENDED_PERMISSIONS 0600         // Recommended SSH key permissions
#endif

// =============================================================================
// String and Display Constants
// =============================================================================

#define MAX_COMMENT_LEN 256    // Maximum key comment length
#define MAX_GPG_KEYGRIP_LEN 64 // Maximum GPG keygrip length

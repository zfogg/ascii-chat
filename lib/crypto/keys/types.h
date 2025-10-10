#pragma once

/**
 * @file crypto/keys/types.h
 * @brief Key type definitions for modular key management
 *
 * This header contains the core key type definitions that can be shared
 * between the main keys.h and the specialized key modules without circular dependencies.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// Key Type Definitions
// =============================================================================

// Key type enumeration (Ed25519 and X25519 only - no RSA/ECDSA!)
//
// NOTE: RSA and ECDSA are NOT supported because:
//   - libsodium (our crypto library) only supports Ed25519/X25519
//   - RSA/ECDSA require variable-length keys and signatures
//   - Protocol assumes fixed 128-byte authenticated handshake (ephemeral:32 + identity:32 + sig:64)
//   - Adding RSA/ECDSA support would require OpenSSL and protocol changes
//
typedef enum {
  KEY_TYPE_UNKNOWN = 0,
  KEY_TYPE_ED25519, // ssh-ed25519 (converts to X25519)
  KEY_TYPE_X25519,  // Native X25519 (raw hex or base64)
  KEY_TYPE_GPG      // GPG key (Ed25519 variant, derived to X25519)
} key_type_t;

// Public key structure (simple - just 32 bytes!)
typedef struct {
  key_type_t type;
  uint8_t key[32];   // Always 32 bytes (Ed25519, X25519, or GPG-derived)
  char comment[256]; // Key comment/label
} public_key_t;

// Private key structure (for server --ssh-key)
typedef struct {
  key_type_t type;
  union {
    uint8_t ed25519[64]; // Ed25519 seed (32) + public key (32) = 64 bytes
    uint8_t x25519[32];  // X25519 private key (32 bytes)
  } key;
  bool use_ssh_agent;     // If true, use SSH agent for signing
  bool use_gpg_agent;     // If true, use GPG agent for signing
  uint8_t public_key[32]; // Ed25519 public key (for agent mode or verification)
  char key_comment[256];  // SSH key comment (for agent identification)
  char gpg_keygrip[64];   // GPG keygrip (40 hex chars) for gpg-agent signing
} private_key_t;

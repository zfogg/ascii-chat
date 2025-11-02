#pragma once

/**
 * @file crypto/keys/types.h
 * @ingroup keys
 * @brief Key type definitions for modular key management
 *
 * This header contains the core key type definitions that can be shared
 * between the main keys.h and the specialized key modules without circular dependencies.
 *
 * All keys in this system are 32 bytes - Ed25519, X25519, and GPG-derived keys.
 * This fixed size simplifies protocol design and key management.
 *
 * @note Key Type Restriction: Only Ed25519 and X25519 are supported.
 *       RSA and ECDSA are NOT supported due to libsodium limitations and
 *       protocol design requiring fixed-size keys.
 *
 * @note GPG Support: GPG keys are parsed and converted to Ed25519/X25519 format.
 *       GPG agent support exists but is currently disabled.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @name Key Type Definitions
 * @{
 */

/**
 * @brief Key type enumeration
 *
 * Represents the type of cryptographic key being used.
 * All keys are ultimately converted to X25519 for key exchange.
 *
 * @note RSA and ECDSA are NOT supported because:
 *       - libsodium (our crypto library) only supports Ed25519/X25519
 *       - RSA/ECDSA require variable-length keys and signatures
 *       - Protocol assumes fixed 128-byte authenticated handshake (ephemeral:32 + identity:32 + sig:64)
 *       - Adding RSA/ECDSA support would require OpenSSL and protocol changes
 *
 * @ingroup keys
 */
typedef enum {
  KEY_TYPE_UNKNOWN = 0, /**< Unknown or invalid key type */
  KEY_TYPE_ED25519,     /**< SSH Ed25519 key (converts to X25519 for key exchange) */
  KEY_TYPE_X25519,      /**< Native X25519 key (raw hex or base64) */
  KEY_TYPE_GPG          /**< GPG key (Ed25519 variant, derived to X25519) */
} key_type_t;

/**
 * @brief Public key structure
 *
 * Simplified public key structure for all key types.
 * All keys are 32 bytes regardless of source (Ed25519, X25519, or GPG-derived).
 *
 * @note Key size: Always 32 bytes (simplifies protocol and key management)
 * @note Comment: Optional label for the key (max 256 chars including null terminator)
 * @note Conversion: Ed25519 and GPG keys are converted to X25519 for key exchange
 *
 * @ingroup keys
 */
typedef struct {
  key_type_t type;   /**< Key type (Ed25519, X25519, or GPG) */
  uint8_t key[32];   /**< Public key data (always 32 bytes) */
  char comment[256]; /**< Key comment/label (e.g., "user@hostname") */
} public_key_t;

/**
 * @brief Private key structure (for server --ssh-key)
 *
 * Private key structure supporting both Ed25519 and X25519 keys.
 * Includes agent support flags and metadata for signing operations.
 *
 * @note Ed25519 keys: 64 bytes (32-byte seed + 32-byte public key)
 * @note X25519 keys: 32 bytes (private scalar)
 * @note Agent support: Can use SSH agent or GPG agent for signing (keys stay in agent)
 * @note GPG support: use_gpg_agent flag exists but GPG agent support is currently disabled
 *
 * @warning GPG agent: use_gpg_agent flag exists but functionality is currently disabled.
 *          Setting use_gpg_agent=true will not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
typedef struct {
  key_type_t type; /**< Key type (Ed25519, X25519, or GPG) */
  union {
    uint8_t ed25519[64];  /**< Ed25519 seed (32) + public key (32) = 64 bytes */
    uint8_t x25519[32];   /**< X25519 private key (32 bytes) */
  } key;                  /**< Private key data (union based on key type) */
  bool use_ssh_agent;     /**< If true, use SSH agent for signing (key stays in agent) */
  bool use_gpg_agent;     /**< If true, use GPG agent for signing (currently disabled) */
  uint8_t public_key[32]; /**< Ed25519 public key (for agent mode or verification) */
  char key_comment[256];  /**< SSH key comment (for agent identification) */
  char gpg_keygrip[64];   /**< GPG keygrip (40 hex chars + null) for gpg-agent signing */
} private_key_t;

/** @} */

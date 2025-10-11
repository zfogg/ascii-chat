#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// libsodium includes
#include <sodium.h>

// Include required types
#include "keys/types.h" // For private_key_t
#include "../common.h"  // For asciichat_error_t

// Key sizes for X25519 key exchange
#define CRYPTO_PUBLIC_KEY_SIZE crypto_box_PUBLICKEYBYTES  // 32 bytes
#define CRYPTO_PRIVATE_KEY_SIZE crypto_box_SECRETKEYBYTES // 32 bytes
#define CRYPTO_SHARED_KEY_SIZE crypto_box_BEFORENMBYTES   // 32 bytes

// Ed25519 signature constants
#define CRYPTO_ED25519_PUBLIC_KEY_SIZE 32  // Ed25519 public key size
#define CRYPTO_ED25519_PRIVATE_KEY_SIZE 64 // Ed25519 private key size (seed + public)
#define CRYPTO_ED25519_SIGNATURE_SIZE 64   // Ed25519 signature size

// Encryption constants
#define CRYPTO_NONCE_SIZE crypto_box_NONCEBYTES              // 24 bytes
#define CRYPTO_SALT_SIZE crypto_pwhash_SALTBYTES             // 32 bytes
#define CRYPTO_ENCRYPTION_KEY_SIZE crypto_secretbox_KEYBYTES // 32 bytes
#define CRYPTO_MAC_SIZE crypto_box_MACBYTES                  // 16 bytes
#define CRYPTO_HMAC_SIZE crypto_auth_hmacsha256_BYTES        // 32 bytes

// Maximum sizes for encrypted data
#define CRYPTO_MAX_PLAINTEXT_SIZE ((size_t)1024 * 1024) // 1MB max
#define CRYPTO_MAX_CIPHERTEXT_SIZE (CRYPTO_MAX_PLAINTEXT_SIZE + CRYPTO_MAC_SIZE)

// Crypto context for managing keys and state
typedef struct {
  // X25519 key exchange keys
  uint8_t public_key[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t private_key[CRYPTO_PRIVATE_KEY_SIZE];
  uint8_t peer_public_key[CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t shared_key[CRYPTO_SHARED_KEY_SIZE];

  // Password-derived key (optional)
  uint8_t password_key[CRYPTO_ENCRYPTION_KEY_SIZE];
  uint8_t password_salt[CRYPTO_SALT_SIZE];

  // State tracking
  bool initialized;
  bool has_password;
  bool key_exchange_complete;
  bool peer_key_received;
  bool handshake_complete;

  // Authentication
  uint8_t auth_nonce[32]; // Server-generated nonce
  uint8_t auth_hmac[32];  // Client's HMAC response

  // Security parameters
  uint64_t nonce_counter; // Prevent nonce reuse within session
  uint8_t session_id[16]; // Unique session ID to prevent cross-session replay attacks

  // Performance tracking
  uint64_t bytes_encrypted;
  uint64_t bytes_decrypted;
} crypto_context_t;

// Crypto initialization result
typedef enum {
  CRYPTO_OK = 0,
  CRYPTO_ERROR_INIT_FAILED = -1,
  CRYPTO_ERROR_INVALID_PARAMS = -2,
  CRYPTO_ERROR_MEMORY = -3,
  CRYPTO_ERROR_LIBSODIUM = -4,
  CRYPTO_ERROR_KEY_GENERATION = -5,
  CRYPTO_ERROR_PASSWORD_DERIVATION = -6,
  CRYPTO_ERROR_ENCRYPTION = -7,
  CRYPTO_ERROR_DECRYPTION = -8,
  CRYPTO_ERROR_INVALID_MAC = -9,
  CRYPTO_ERROR_BUFFER_TOO_SMALL = -10,
  CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE = -11,
  CRYPTO_ERROR_NONCE_EXHAUSTED = -12
} crypto_result_t;

// =============================================================================
// Core initialization and setup
// =============================================================================

// Initialize libsodium and crypto context
crypto_result_t crypto_init(crypto_context_t *ctx);

// Initialize with password-based encryption
crypto_result_t crypto_init_with_password(crypto_context_t *ctx, const char *password);

// Cleanup crypto context (secure memory wiping)
void crypto_cleanup(crypto_context_t *ctx);

// Generate new key pair for key exchange
crypto_result_t crypto_generate_keypair(crypto_context_t *ctx);

// =============================================================================
// Key exchange protocol (automatic HTTPS-like key exchange)
// =============================================================================

// Get public key for sending to peer (step 1 of handshake)
crypto_result_t crypto_get_public_key(const crypto_context_t *ctx, uint8_t *public_key_out);

// Set peer's public key and compute shared secret (step 2 of handshake)
crypto_result_t crypto_set_peer_public_key(crypto_context_t *ctx, const uint8_t *peer_public_key);

// Check if key exchange is complete and ready for encryption
bool crypto_is_ready(const crypto_context_t *ctx);

// =============================================================================
// Password-based encryption (optional additional layer)
// =============================================================================

// Validate password length requirements
crypto_result_t crypto_validate_password(const char *password);

// Derive key from password using Argon2id (memory-hard, secure)
crypto_result_t crypto_derive_password_key(crypto_context_t *ctx, const char *password);

// Verify password matches stored salt/key
bool crypto_verify_password(const crypto_context_t *ctx, const char *password);

// Derive a deterministic encryption key from password for handshake
crypto_result_t crypto_derive_password_encryption_key(const char *password,
                                                      uint8_t encryption_key[CRYPTO_ENCRYPTION_KEY_SIZE]);

// =============================================================================
// Encryption/Decryption operations
// =============================================================================

// Encrypt data using either shared key (if available) or password key
crypto_result_t crypto_encrypt(crypto_context_t *ctx, const uint8_t *plaintext, size_t plaintext_len,
                               uint8_t *ciphertext_out, size_t ciphertext_out_size, size_t *ciphertext_len_out);

// Decrypt data using either shared key (if available) or password key
crypto_result_t crypto_decrypt(crypto_context_t *ctx, const uint8_t *ciphertext, size_t ciphertext_len,
                               uint8_t *plaintext_out, size_t plaintext_out_size, size_t *plaintext_len_out);

// =============================================================================
// Utility functions
// =============================================================================

// Convert crypto result to human-readable string
const char *crypto_result_to_string(crypto_result_t result);

// Get crypto context status information for debugging
void crypto_get_status(const crypto_context_t *ctx, char *status_buffer, size_t buffer_size);

// Secure comparison of byte arrays (constant-time)
bool crypto_secure_compare(const uint8_t *lhs, const uint8_t *rhs, size_t len);

// Generate cryptographically secure random bytes
crypto_result_t crypto_random_bytes(uint8_t *buffer, size_t len);

// =============================================================================
// Authentication and handshake
// =============================================================================

// Generate random nonce for authentication
crypto_result_t crypto_generate_nonce(uint8_t nonce[32]);

// Compute HMAC for authentication (fixed 32-byte data)
crypto_result_t crypto_compute_hmac(const uint8_t key[32], const uint8_t data[32], uint8_t hmac[32]);

// Compute HMAC for variable-length data (for binding multiple values)
crypto_result_t crypto_compute_hmac_ex(const uint8_t key[32], const uint8_t *data, size_t data_len, uint8_t hmac[32]);

// Verify HMAC (fixed 32-byte data)
bool crypto_verify_hmac(const uint8_t key[32], const uint8_t data[32], const uint8_t expected_hmac[32]);

// Verify HMAC for variable-length data
bool crypto_verify_hmac_ex(const uint8_t key[32], const uint8_t *data, size_t data_len,
                           const uint8_t expected_hmac[32]);

// =============================================================================
// High-level authentication helpers (shared between client and server)
// =============================================================================

/**
 * Compute authentication response HMAC bound to DH shared_secret.
 * Uses password_key if available, otherwise uses shared_key.
 *
 * Computes: HMAC(auth_key, nonce || shared_secret)
 * This binds the password/key authentication to the DH exchange, preventing MITM.
 *
 * @param ctx Crypto context with keys
 * @param nonce Challenge nonce (32 bytes)
 * @param hmac_out Output HMAC (32 bytes)
 * @return CRYPTO_OK on success
 */
crypto_result_t crypto_compute_auth_response(const crypto_context_t *ctx, const uint8_t nonce[32],
                                             uint8_t hmac_out[32]);

/**
 * Verify authentication response HMAC bound to DH shared_secret.
 * Uses password_key if available, otherwise uses shared_key.
 *
 * Verifies: HMAC(auth_key, nonce || shared_secret)
 *
 * @param ctx Crypto context with keys
 * @param nonce Challenge nonce (32 bytes)
 * @param expected_hmac Expected HMAC to verify (32 bytes)
 * @return true if HMAC is valid
 */
bool crypto_verify_auth_response(const crypto_context_t *ctx, const uint8_t nonce[32], const uint8_t expected_hmac[32]);

// Create authentication challenge packet
crypto_result_t crypto_create_auth_challenge(const crypto_context_t *ctx, uint8_t *packet_out, size_t packet_size,
                                             size_t *packet_len_out);

// Process authentication challenge packet
crypto_result_t crypto_process_auth_challenge(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len);

// Process authentication response packet
crypto_result_t crypto_process_auth_response(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len);

// =============================================================================
// Network integration helpers
// =============================================================================

// Create public key packet for network transmission
crypto_result_t crypto_create_public_key_packet(const crypto_context_t *ctx, uint8_t *packet_out, size_t packet_size,
                                                size_t *packet_len_out);

// Process received public key packet from peer
crypto_result_t crypto_process_public_key_packet(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len);

// Create encrypted data packet for network transmission
crypto_result_t crypto_create_encrypted_packet(crypto_context_t *ctx, const uint8_t *data, size_t data_len,
                                               uint8_t *packet_out, size_t packet_size, size_t *packet_len_out);

// Process received encrypted packet from peer
crypto_result_t crypto_process_encrypted_packet(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len,
                                                uint8_t *data_out, size_t data_size, size_t *data_len_out);

// =============================================================================
// Shared Cryptographic Operations
// =============================================================================

// Compute password-based HMAC for authentication
asciichat_error_t crypto_compute_password_hmac(const uint8_t *password_key, const uint8_t *nonce,
                                               const uint8_t *shared_secret, uint8_t *hmac_out);

// Verify peer's signature on ephemeral key
asciichat_error_t crypto_verify_peer_signature(const uint8_t *peer_public_key, const uint8_t *ephemeral_key,
                                               size_t ephemeral_key_size, const uint8_t *signature);

// Sign ephemeral key with private key
asciichat_error_t crypto_sign_ephemeral_key(const private_key_t *private_key, const uint8_t *ephemeral_key,
                                            size_t ephemeral_key_size, uint8_t *signature_out);

// Combine HMAC and challenge nonce for transmission
void crypto_combine_auth_data(const uint8_t *hmac, const uint8_t *challenge_nonce, uint8_t *combined_out);

// Extract HMAC and challenge nonce from combined data
void crypto_extract_auth_data(const uint8_t *combined_data, uint8_t *hmac_out, uint8_t *challenge_out);

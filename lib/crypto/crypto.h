#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// libsodium includes
#include <sodium.h>

// Key sizes for X25519 key exchange
#define CRYPTO_PUBLIC_KEY_SIZE crypto_box_PUBLICKEYBYTES  // 32 bytes
#define CRYPTO_PRIVATE_KEY_SIZE crypto_box_SECRETKEYBYTES // 32 bytes
#define CRYPTO_SHARED_KEY_SIZE crypto_box_BEFORENMBYTES   // 32 bytes

// Encryption constants
#define CRYPTO_NONCE_SIZE crypto_box_NONCEBYTES              // 24 bytes
#define CRYPTO_SALT_SIZE crypto_pwhash_SALTBYTES             // 32 bytes
#define CRYPTO_ENCRYPTION_KEY_SIZE crypto_secretbox_KEYBYTES // 32 bytes
#define CRYPTO_MAC_SIZE crypto_box_MACBYTES                  // 16 bytes

// Maximum sizes for encrypted data
#define CRYPTO_MAX_PLAINTEXT_SIZE ((size_t)1024 * 1024) // 1MB max
#define CRYPTO_MAX_CIPHERTEXT_SIZE (CRYPTO_MAX_PLAINTEXT_SIZE + CRYPTO_MAC_SIZE)

// Key exchange packet types (for network protocol integration)
typedef enum {
  CRYPTO_PACKET_PUBLIC_KEY = 100,     // Send public key during handshake
  CRYPTO_PACKET_KEY_EXCHANGE = 101,   // Complete key exchange
  CRYPTO_PACKET_ENCRYPTED_DATA = 102, // Encrypted application data

  // New crypto handshake packets
  CRYPTO_PACKET_AUTH_CHALLENGE = 103,     // Server -> Client: {nonce[32]}
  CRYPTO_PACKET_AUTH_RESPONSE = 104,      // Client -> Server: {HMAC[32]}
  CRYPTO_PACKET_HANDSHAKE_COMPLETE = 105, // Server -> Client: "encryption ready"
  CRYPTO_PACKET_AUTH_FAILED = 106         // Server -> Client: "authentication failed"
} crypto_packet_type_t;

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
  uint64_t nonce_counter; // Prevent nonce reuse

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

// Compute HMAC for authentication
crypto_result_t crypto_compute_hmac(const uint8_t key[32], const uint8_t data[32], uint8_t hmac[32]);

// Verify HMAC
bool crypto_verify_hmac(const uint8_t key[32], const uint8_t data[32], const uint8_t expected_hmac[32]);

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

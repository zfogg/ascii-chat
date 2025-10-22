#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// libsodium includes
#include <sodium.h>

// Include required types
#include "keys/types.h" // For private_key_t
#include "../common.h"  // For asciichat_error_t

// =============================================================================
// ascii-chat Cryptographic Handshake Protocol Definitions
// =============================================================================

// Password requirements
#define MIN_PASSWORD_LENGTH 8   // Minimum password length
#define MAX_PASSWORD_LENGTH 256 // Maximum password length

// Algorithm-specific key sizes
#define X25519_KEY_SIZE 32          // X25519 key size
#define ED25519_PUBLIC_KEY_SIZE 32  // Ed25519 public key size
#define ED25519_PRIVATE_KEY_SIZE 64 // Ed25519 private key (seed + public)
#define ED25519_SIGNATURE_SIZE 64   // Ed25519 signature size
#define XSALSA20_NONCE_SIZE 24      // XSalsa20 nonce size
#define POLY1305_MAC_SIZE 16        // Poly1305 MAC size
#define HMAC_SHA256_SIZE 32         // HMAC-SHA256 output size
#define ARGON2ID_SALT_SIZE 32       // Argon2id salt size
#define SECRETBOX_KEY_SIZE 32       // Secretbox key size

// Constants main constants for the project to stay abstracted from the underlying algorithms
#define CRYPTO_PUBLIC_KEY_SIZE X25519_KEY_SIZE
#define CRYPTO_PRIVATE_KEY_SIZE X25519_KEY_SIZE
#define CRYPTO_SHARED_KEY_SIZE X25519_KEY_SIZE
#define CRYPTO_ED25519_PUBLIC_KEY_SIZE ED25519_PUBLIC_KEY_SIZE
#define CRYPTO_ED25519_PRIVATE_KEY_SIZE ED25519_PRIVATE_KEY_SIZE
#define CRYPTO_ED25519_SIGNATURE_SIZE ED25519_SIGNATURE_SIZE
#define CRYPTO_NONCE_SIZE XSALSA20_NONCE_SIZE
#define CRYPTO_SALT_SIZE ARGON2ID_SALT_SIZE
#define CRYPTO_ENCRYPTION_KEY_SIZE SECRETBOX_KEY_SIZE
#define CRYPTO_MAC_SIZE POLY1305_MAC_SIZE
#define CRYPTO_HMAC_SIZE HMAC_SHA256_SIZE

// Authentication packet sizes
#define AUTH_HMAC_SIZE 32                                                        // HMAC size in auth packets
#define AUTH_CHALLENGE_SIZE 32                                                   // Challenge nonce size
#define AUTH_COMBINED_SIZE (AUTH_HMAC_SIZE + AUTH_CHALLENGE_SIZE)                // Combined auth data size
#define AUTH_SIGNATURE_SIZE 64                                                   // Ed25519 signature size
#define AUTH_SIGNATURE_COMBINED_SIZE (AUTH_SIGNATURE_SIZE + AUTH_CHALLENGE_SIZE) // Signature + challenge

// Authentication challenge packet structure
#define AUTH_CHALLENGE_FLAGS_SIZE 1                                                  // 1 byte for auth flags
#define AUTH_CHALLENGE_PACKET_SIZE (AUTH_CHALLENGE_FLAGS_SIZE + AUTH_CHALLENGE_SIZE) // 1 + 32 = 33 bytes

// Authentication response packet sizes
#define AUTH_RESPONSE_PASSWORD_SIZE (AUTH_HMAC_SIZE + AUTH_CHALLENGE_SIZE)       // 32 + 32 = 64 bytes
#define AUTH_RESPONSE_SIGNATURE_SIZE (AUTH_SIGNATURE_SIZE + AUTH_CHALLENGE_SIZE) // 64 + 32 = 96 bytes

// Server authentication response
#define SERVER_AUTH_RESPONSE_SIZE AUTH_HMAC_SIZE // 32 bytes

// Packet size limits
#define MAX_AUTH_FAILED_PACKET_SIZE 256 // Maximum AUTH_FAILED packet size
#define MAX_ENCRYPTED_PACKET_SIZE 65536 // 64KB max for encrypted packets

// Buffer sizes for hex string conversion
#define HEX_STRING_SIZE_32 (32 * 2 + 1) // 32 bytes -> 64 hex chars + null
#define HEX_STRING_SIZE_64 (64 * 2 + 1) // 64 bytes -> 128 hex chars + null

// Password buffer sizes
#define PASSWORD_BUFFER_SIZE 256 // Password input buffer size

// Zero key for no-identity entries
#define ZERO_KEY_SIZE X25519_KEY_SIZE // Size of zero key array

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

  // Dynamic crypto parameters (negotiated during handshake)
  uint8_t nonce_size;          // e.g., 24 for XSalsa20, 12 for ChaCha20
  uint8_t mac_size;            // e.g., 16 for Poly1305, 16 for GCM
  uint8_t hmac_size;           // e.g., 32 for HMAC-SHA256, 64 for HMAC-SHA512
  uint8_t encryption_key_size; // e.g., 32 for XSalsa20, 32 for AES-256

  // Key sizes for dynamic negotiation
  uint16_t public_key_size;  // e.g., 32 for X25519, 1568 for Kyber1024
  uint16_t private_key_size; // e.g., 32 for X25519, 1568 for Kyber1024
  uint16_t shared_key_size;  // e.g., 32 for X25519, 32 for Kyber1024
  uint16_t salt_size;        // e.g., 16 for Argon2id
  uint16_t signature_size;   // e.g., 64 for Ed25519, 3309 for Dilithium3

  // State tracking
  bool initialized;
  bool has_password;
  bool key_exchange_complete;
  bool peer_key_received;
  bool handshake_complete;

  // Authentication
  uint8_t auth_nonce[AUTH_CHALLENGE_SIZE]; // Server-generated nonce
  uint8_t auth_hmac[CRYPTO_HMAC_SIZE];     // Client's HMAC response

  // Security parameters
  uint64_t nonce_counter; // Prevent nonce reuse within session
  uint8_t session_id[16]; // Unique session ID to prevent cross-session replay attacks

  // Session rekeying state
  uint64_t rekey_packet_count;    // Packets encrypted since last rekey/handshake
  time_t rekey_last_time;         // Timestamp of last rekey (or initial handshake)
  bool rekey_in_progress;         // Rekey handshake currently in progress
  uint8_t rekey_failure_count;    // Consecutive rekey failures (for exponential backoff)

  // Temporary keys during rekeying transition
  uint8_t temp_public_key[CRYPTO_PUBLIC_KEY_SIZE];   // New ephemeral public key
  uint8_t temp_private_key[CRYPTO_PRIVATE_KEY_SIZE]; // New ephemeral private key
  uint8_t temp_shared_key[CRYPTO_SHARED_KEY_SIZE];   // New shared secret (not yet active)
  bool has_temp_key;                                   // True if temp keys are valid

  // Configurable rekeying thresholds
  uint64_t rekey_packet_threshold; // Rekey after N packets (default: 1,000,000)
  time_t rekey_time_threshold;     // Rekey after N seconds (default: 3600 = 1 hour)

  // Performance tracking
  uint64_t bytes_encrypted;
  uint64_t bytes_decrypted;
  uint64_t rekey_count; // Number of successful rekeys performed
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
  CRYPTO_ERROR_NONCE_EXHAUSTED = -12,
  CRYPTO_ERROR_REKEY_IN_PROGRESS = -13,   // Rekey already in progress
  CRYPTO_ERROR_REKEY_FAILED = -14,        // Rekey handshake failed
  CRYPTO_ERROR_REKEY_RATE_LIMITED = -15   // Too many rekey attempts
} crypto_result_t;

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
crypto_result_t crypto_compute_hmac(crypto_context_t *ctx, const uint8_t key[32], const uint8_t data[32],
                                    uint8_t hmac[32]);

// Compute HMAC for variable-length data (for binding multiple values)
crypto_result_t crypto_compute_hmac_ex(const crypto_context_t *ctx, const uint8_t key[32], const uint8_t *data,
                                       size_t data_len, uint8_t hmac[32]);

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
asciichat_error_t crypto_compute_password_hmac(crypto_context_t *ctx, const uint8_t *password_key, const uint8_t *nonce,
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

// =============================================================================
// Session Rekeying Protocol
// =============================================================================

// Rekeying constants
#define REKEY_MIN_INTERVAL 3                  // Minimum 3 seconds for TESTING (normally 60 for anti-DoS)
#define REKEY_DEFAULT_TIME_THRESHOLD 3600     // Default: 1 hour
#define REKEY_DEFAULT_PACKET_THRESHOLD 1000000 // Default: 1 million packets
#define REKEY_TEST_TIME_THRESHOLD 30          // Test mode: 30 seconds
#define REKEY_TEST_PACKET_THRESHOLD 1000      // Test mode: 1000 packets
#define REKEY_MAX_FAILURE_COUNT 10            // Max consecutive failures before giving up

/**
 * Check if rekeying should be triggered based on time or packet count thresholds.
 * Should be called after each packet encryption.
 *
 * @param ctx Crypto context
 * @return true if rekey should be initiated, false otherwise
 */
bool crypto_should_rekey(const crypto_context_t *ctx);

/**
 * Initiate rekeying by generating new ephemeral keys.
 * This is called by the initiator (client or server).
 *
 * @param ctx Crypto context
 * @return CRYPTO_OK on success, error code on failure
 */
crypto_result_t crypto_rekey_init(crypto_context_t *ctx);

/**
 * Process REKEY_REQUEST from peer (responder side).
 * Generates new ephemeral keys and computes new shared secret.
 *
 * @param ctx Crypto context
 * @param peer_new_public_key Peer's new ephemeral public key (32 bytes)
 * @return CRYPTO_OK on success, error code on failure
 */
crypto_result_t crypto_rekey_process_request(crypto_context_t *ctx, const uint8_t *peer_new_public_key);

/**
 * Process REKEY_RESPONSE from peer (initiator side).
 * Computes new shared secret from peer's new public key.
 *
 * @param ctx Crypto context
 * @param peer_new_public_key Peer's new ephemeral public key (32 bytes)
 * @return CRYPTO_OK on success, error code on failure
 */
crypto_result_t crypto_rekey_process_response(crypto_context_t *ctx, const uint8_t *peer_new_public_key);

/**
 * Commit to new keys after successful REKEY_COMPLETE.
 * Switches from old shared_key to temp_shared_key, resets counters.
 *
 * @param ctx Crypto context
 * @return CRYPTO_OK on success, error code on failure
 */
crypto_result_t crypto_rekey_commit(crypto_context_t *ctx);

/**
 * Abort rekeying and fallback to old keys.
 * Called on rekey failure (timeout, bad keys, decryption failure, etc.)
 *
 * @param ctx Crypto context
 */
void crypto_rekey_abort(crypto_context_t *ctx);

/**
 * Get the current rekeying state for debugging/logging.
 *
 * @param ctx Crypto context
 * @param status_buffer Output buffer for status string
 * @param buffer_size Size of status buffer
 */
void crypto_get_rekey_status(const crypto_context_t *ctx, char *status_buffer, size_t buffer_size);

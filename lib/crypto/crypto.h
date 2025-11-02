#pragma once

/**
 * @file crypto/crypto.h
 * @defgroup crypto Cryptography Module
 * @ingroup crypto
 * @brief Core cryptographic operations for ASCII-Chat
 *
 * This header provides the core cryptographic operations for secure communication
 * in ASCII-Chat, including key exchange, encryption/decryption, authentication,
 * and session rekeying.
 *
 * The interface provides:
 * - Cryptographic context management
 * - X25519 key exchange (Diffie-Hellman)
 * - XSalsa20-Poly1305 encryption/decryption
 * - Argon2id password-based key derivation
 * - HMAC-SHA256 authentication
 * - Session rekeying protocol
 *
 * @note Key Exchange: Uses ephemeral X25519 keys for perfect forward secrecy.
 *       Each connection generates new keys automatically.
 *
 * @note Encryption: XSalsa20-Poly1305 provides authenticated encryption with
 *       automatic MAC verification. Nonces are generated as session_id || counter
 *       to prevent replay attacks.
 *
 * @note Password Authentication: Optional password-based encryption using Argon2id
 *       for memory-hard key derivation. Passwords are bound to DH shared secrets
 *       to prevent MITM attacks.
 *
 * @note Rekeying: Automatic periodic key rotation after time threshold (1 hour)
 *       OR packet count threshold (1 million), whichever comes first. Test mode
 *       uses reduced thresholds (30 seconds / 1000 packets).
 *
 * @note Test Environment: Automatically detects test environment via CRITERION_TEST
 *       or TESTING environment variables and adjusts thresholds accordingly.
 *
 * @note Byte Order: Client must convert network byte order to host byte order for
 *       crypto parameters. Server uses host byte order directly.
 *
 * @note Key Exchange Formats:
 *       - Simple format: Only ephemeral public key (when server has no identity key)
 *       - Authenticated format: Ephemeral key + identity key + signature (when server has identity key)
 *
 * @warning Always use crypto_secure_compare() for comparing sensitive data (keys, MACs, HMACs).
 *          Do NOT use regular memcmp() as it is vulnerable to timing attacks.
 *
 * @warning Nonce counter starts at 1 (0 is reserved for testing). Returns CRYPTO_ERROR_NONCE_EXHAUSTED
 *          if counter reaches 0 or UINT64_MAX (extremely unlikely, but triggers rekeying).
 *
 * @warning Password salt is deterministic ("ascii-chat-password-salt-v1") for consistent
 *          key derivation across client/server. Only use for session encryption, not long-term storage.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// libsodium includes
#include <sodium.h>

// Include required types
#include "keys/types.h" // For private_key_t
#include "../common.h"  // For asciichat_error_t

/**
 * @name Password Requirements
 * @{
 */

/** @brief Minimum password length (8 characters) */
#define MIN_PASSWORD_LENGTH 8
/** @brief Maximum password length (256 characters) */
#define MAX_PASSWORD_LENGTH 256

/** @} */

/**
 * @name Algorithm-Specific Key Sizes
 * @{
 */

/** @brief X25519 key size in bytes */
#define X25519_KEY_SIZE 32
/** @brief Ed25519 public key size in bytes */
#define ED25519_PUBLIC_KEY_SIZE 32
/** @brief Ed25519 private key size (seed + public) in bytes */
#define ED25519_PRIVATE_KEY_SIZE 64
/** @brief Ed25519 signature size in bytes */
#define ED25519_SIGNATURE_SIZE 64
/** @brief XSalsa20 nonce size in bytes */
#define XSALSA20_NONCE_SIZE 24
/** @brief Poly1305 MAC size in bytes */
#define POLY1305_MAC_SIZE 16
/** @brief HMAC-SHA256 output size in bytes */
#define HMAC_SHA256_SIZE 32
/** @brief Argon2id salt size in bytes */
#define ARGON2ID_SALT_SIZE 32
/** @brief Secretbox key size in bytes */
#define SECRETBOX_KEY_SIZE 32

/** @} */

/**
 * @name Abstracted Cryptographic Constants
 * @{
 *
 * These constants abstract the underlying algorithms to allow future changes.
 */

/** @brief Public key size (X25519) */
#define CRYPTO_PUBLIC_KEY_SIZE X25519_KEY_SIZE
/** @brief Private key size (X25519) */
#define CRYPTO_PRIVATE_KEY_SIZE X25519_KEY_SIZE
/** @brief Shared key size (X25519) */
#define CRYPTO_SHARED_KEY_SIZE X25519_KEY_SIZE
/** @brief Ed25519 public key size */
#define CRYPTO_ED25519_PUBLIC_KEY_SIZE ED25519_PUBLIC_KEY_SIZE
/** @brief Ed25519 private key size */
#define CRYPTO_ED25519_PRIVATE_KEY_SIZE ED25519_PRIVATE_KEY_SIZE
/** @brief Ed25519 signature size */
#define CRYPTO_ED25519_SIGNATURE_SIZE ED25519_SIGNATURE_SIZE
/** @brief Nonce size (XSalsa20) */
#define CRYPTO_NONCE_SIZE XSALSA20_NONCE_SIZE
/** @brief Salt size (Argon2id) */
#define CRYPTO_SALT_SIZE ARGON2ID_SALT_SIZE
/** @brief Encryption key size (XSalsa20-Poly1305) */
#define CRYPTO_ENCRYPTION_KEY_SIZE SECRETBOX_KEY_SIZE
/** @brief MAC size (Poly1305) */
#define CRYPTO_MAC_SIZE POLY1305_MAC_SIZE
/** @brief HMAC size (HMAC-SHA256) */
#define CRYPTO_HMAC_SIZE HMAC_SHA256_SIZE

/** @} */

/**
 * @name Authentication Packet Sizes
 * @{
 */

/** @brief HMAC size in authentication packets (32 bytes) */
#define AUTH_HMAC_SIZE 32
/** @brief Challenge nonce size (32 bytes) */
#define AUTH_CHALLENGE_SIZE 32
/** @brief Combined authentication data size (HMAC + challenge, 64 bytes) */
#define AUTH_COMBINED_SIZE (AUTH_HMAC_SIZE + AUTH_CHALLENGE_SIZE)
/** @brief Ed25519 signature size (64 bytes) */
#define AUTH_SIGNATURE_SIZE 64
/** @brief Combined signature + challenge size (96 bytes) */
#define AUTH_SIGNATURE_COMBINED_SIZE (AUTH_SIGNATURE_SIZE + AUTH_CHALLENGE_SIZE)

/** @} */

/**
 * @name Authentication Challenge Packet Structure
 * @{
 */

/** @brief Authentication flags size (1 byte) */
#define AUTH_CHALLENGE_FLAGS_SIZE 1
/** @brief Complete authentication challenge packet size (1 + 32 = 33 bytes) */
#define AUTH_CHALLENGE_PACKET_SIZE (AUTH_CHALLENGE_FLAGS_SIZE + AUTH_CHALLENGE_SIZE)

/** @} */

/**
 * @name Authentication Response Packet Sizes
 * @{
 */

/** @brief Password-based authentication response size (HMAC + challenge, 64 bytes) */
#define AUTH_RESPONSE_PASSWORD_SIZE (AUTH_HMAC_SIZE + AUTH_CHALLENGE_SIZE)
/** @brief Signature-based authentication response size (signature + challenge, 96 bytes) */
#define AUTH_RESPONSE_SIGNATURE_SIZE (AUTH_SIGNATURE_SIZE + AUTH_CHALLENGE_SIZE)

/** @} */

/** @brief Server authentication response size (32 bytes) */
#define SERVER_AUTH_RESPONSE_SIZE AUTH_HMAC_SIZE

/**
 * @name Packet Size Limits
 * @{
 */

/** @brief Maximum AUTH_FAILED packet size (256 bytes) */
#define MAX_AUTH_FAILED_PACKET_SIZE 256
/** @brief Maximum encrypted packet size (64KB) */
#define MAX_ENCRYPTED_PACKET_SIZE 65536

/** @} */

/**
 * @name Buffer Sizes
 * @{
 */

/** @brief Hex string size for 32-byte values (64 hex chars + null terminator) */
#define HEX_STRING_SIZE_32 (32 * 2 + 1)
/** @brief Hex string size for 64-byte values (128 hex chars + null terminator) */
#define HEX_STRING_SIZE_64 (64 * 2 + 1)
/** @brief Password input buffer size (256 bytes) */
#define PASSWORD_BUFFER_SIZE 256
/** @brief Zero key array size (32 bytes, used for no-identity entries) */
#define ZERO_KEY_SIZE X25519_KEY_SIZE

/** @} */

/**
 * @name Encryption Size Limits
 * @{
 */

/** @brief Maximum plaintext size (1MB) */
#define CRYPTO_MAX_PLAINTEXT_SIZE ((size_t)1024 * 1024)
/** @brief Maximum ciphertext size (plaintext + MAC, ~1MB + 16 bytes) */
#define CRYPTO_MAX_CIPHERTEXT_SIZE (CRYPTO_MAX_PLAINTEXT_SIZE + CRYPTO_MAC_SIZE)

/** @} */

/**
 * @brief Cryptographic context structure
 *
 * Manages all cryptographic state for a single connection, including
 * key exchange, encryption/decryption, authentication, and session rekeying.
 *
 * @note Nonce generation: Nonces are constructed as `session_id || counter`
 *       where session_id is 16 bytes and counter fills the remaining bytes.
 *       This prevents both within-session and cross-session replay attacks.
 *
 * @note Session ID: Generated once per connection and remains constant.
 *       Used to prevent cross-session replay attacks.
 *
 * @note Nonce counter: Starts at 1 (0 is reserved for testing) and increments
 *       for each encryption operation. Prevents nonce reuse within a session.
 *
 * @note Byte order: Client must convert network byte order to host byte order
 *       for crypto parameters. Server uses host byte order directly.
 *
 * @note Key exchange formats:
 *       - Simple format: Only ephemeral public key (when server has no identity key)
 *       - Authenticated format: Ephemeral key + identity key + signature (when server has identity key)
 *
 * @ingroup crypto
 */
typedef struct {
  /** X25519 key exchange keys */
  uint8_t public_key[CRYPTO_PUBLIC_KEY_SIZE];      /**< Our ephemeral public key */
  uint8_t private_key[CRYPTO_PRIVATE_KEY_SIZE];    /**< Our ephemeral private key */
  uint8_t peer_public_key[CRYPTO_PUBLIC_KEY_SIZE]; /**< Peer's ephemeral public key */
  uint8_t shared_key[CRYPTO_SHARED_KEY_SIZE];      /**< Computed shared secret from DH */

  /** Password-derived key (optional additional layer) */
  uint8_t password_key[CRYPTO_ENCRYPTION_KEY_SIZE]; /**< Argon2id-derived key from password */
  uint8_t password_salt[CRYPTO_SALT_SIZE];          /**< Salt used for password derivation */

  /** Dynamic crypto parameters (negotiated during handshake) */
  uint8_t nonce_size;          /**< Nonce size (e.g., 24 for XSalsa20, 12 for ChaCha20) */
  uint8_t mac_size;            /**< MAC size (e.g., 16 for Poly1305, 16 for GCM) */
  uint8_t hmac_size;           /**< HMAC size (e.g., 32 for HMAC-SHA256, 64 for HMAC-SHA512) */
  uint8_t encryption_key_size; /**< Encryption key size (e.g., 32 for XSalsa20, 32 for AES-256) */
  uint8_t auth_challenge_size; /**< Authentication challenge nonce size (e.g., 32) */

  /** Key sizes for dynamic negotiation (future: post-quantum crypto) */
  uint16_t public_key_size;      /**< Key exchange public key size (32 for X25519, 1568 for Kyber1024) */
  uint16_t private_key_size;     /**< Key exchange private key size (32 for X25519, 1568 for Kyber1024) */
  uint16_t shared_key_size;      /**< Shared secret size (32 for X25519, 32 for Kyber1024) */
  uint16_t auth_public_key_size; /**< Authentication public key size (32 for Ed25519, 1952 for Dilithium3) */
  uint16_t salt_size;            /**< Salt size (32 for Argon2id) */
  uint16_t signature_size;       /**< Signature size (64 for Ed25519, 3309 for Dilithium3) */

  /** State tracking flags */
  bool initialized;           /**< Whether context has been initialized */
  bool has_password;          /**< Whether password-based encryption is enabled */
  bool key_exchange_complete; /**< Whether DH key exchange is complete */
  bool peer_key_received;     /**< Whether peer's public key has been received */
  bool handshake_complete;    /**< Whether full handshake is complete */

  /** Authentication state */
  uint8_t auth_nonce[AUTH_CHALLENGE_SIZE]; /**< Server-generated challenge nonce */
  uint8_t auth_hmac[CRYPTO_HMAC_SIZE];     /**< Client's HMAC response (or expected on server) */

  /** Security parameters */
  uint64_t nonce_counter; /**< Nonce counter (starts at 1, increments per encryption) */
  uint8_t session_id[16]; /**< Unique session ID (16 bytes, prevents cross-session replay) */

  /** Session rekeying state */
  uint64_t rekey_packet_count;    /**< Packets encrypted since last rekey/handshake */
  time_t rekey_last_time;         /**< Timestamp of last successful rekey (or initial handshake) */
  time_t rekey_last_request_time; /**< Timestamp of last rekey request (for DDoS protection) */
  bool rekey_in_progress;         /**< Rekey handshake currently in progress */
  uint8_t rekey_failure_count;    /**< Consecutive rekey failures (for exponential backoff) */

  /** Temporary keys during rekeying transition */
  uint8_t temp_public_key[CRYPTO_PUBLIC_KEY_SIZE];   /**< New ephemeral public key (not yet active) */
  uint8_t temp_private_key[CRYPTO_PRIVATE_KEY_SIZE]; /**< New ephemeral private key (not yet active) */
  uint8_t temp_shared_key[CRYPTO_SHARED_KEY_SIZE];   /**< New shared secret (not yet active) */
  bool has_temp_key;                                 /**< True if temporary keys are valid */

  /** Configurable rekeying thresholds */
  uint64_t rekey_packet_threshold; /**< Rekey after N packets (default: 1,000,000) */
  time_t rekey_time_threshold;     /**< Rekey after N seconds (default: 3600 = 1 hour) */

  /** Performance tracking */
  uint64_t bytes_encrypted; /**< Total bytes encrypted */
  uint64_t bytes_decrypted; /**< Total bytes decrypted */
  uint64_t rekey_count;     /**< Number of successful rekeys performed */
} crypto_context_t;

/**
 * @brief Cryptographic operation result codes
 *
 * @ingroup crypto
 */
typedef enum {
  CRYPTO_OK = 0,                              /**< Operation succeeded */
  CRYPTO_ERROR_INIT_FAILED = -1,              /**< Initialization failed */
  CRYPTO_ERROR_INVALID_PARAMS = -2,           /**< Invalid parameters provided */
  CRYPTO_ERROR_MEMORY = -3,                   /**< Memory allocation failed */
  CRYPTO_ERROR_LIBSODIUM = -4,                /**< libsodium operation failed */
  CRYPTO_ERROR_KEY_GENERATION = -5,           /**< Key generation failed */
  CRYPTO_ERROR_PASSWORD_DERIVATION = -6,      /**< Password-based key derivation failed */
  CRYPTO_ERROR_ENCRYPTION = -7,               /**< Encryption operation failed */
  CRYPTO_ERROR_DECRYPTION = -8,               /**< Decryption operation failed */
  CRYPTO_ERROR_INVALID_MAC = -9,              /**< MAC verification failed (possible tampering) */
  CRYPTO_ERROR_BUFFER_TOO_SMALL = -10,        /**< Output buffer too small */
  CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE = -11, /**< Key exchange not complete */
  CRYPTO_ERROR_NONCE_EXHAUSTED = -12,         /**< Nonce counter exhausted (should rekey) */
  CRYPTO_ERROR_REKEY_IN_PROGRESS = -13,       /**< Rekey already in progress */
  CRYPTO_ERROR_REKEY_FAILED = -14,            /**< Rekey handshake failed */
  CRYPTO_ERROR_REKEY_RATE_LIMITED = -15       /**< Too many rekey attempts (DDoS protection) */
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

/**
 * @name Core Initialization and Setup
 * @{
 */

/**
 * @brief Initialize libsodium and crypto context
 * @param ctx Crypto context to initialize
 * @return CRYPTO_OK on success, error code on failure
 *
 * Initializes libsodium (thread-safe, idempotent) and generates a new
 * X25519 key pair for key exchange.
 *
 * @note libsodium initialization is global and thread-safe. Multiple calls
 *       to crypto_init() will only initialize libsodium once.
 *
 * @note Generates a new ephemeral key pair automatically. The nonce counter
 *       starts at 1 (0 is reserved for testing).
 *
 * @ingroup crypto
 */
crypto_result_t crypto_init(crypto_context_t *ctx);

/**
 * @brief Initialize with password-based encryption
 * @param ctx Crypto context to initialize
 * @param password Password for key derivation
 * @return CRYPTO_OK on success, error code on failure
 *
 * Initializes context and derives encryption key from password using Argon2id.
 * The password is used as an additional layer on top of DH key exchange.
 *
 * @note Password must meet length requirements (8-256 characters).
 *
 * @ingroup crypto
 */
crypto_result_t crypto_init_with_password(crypto_context_t *ctx, const char *password);

/**
 * @brief Cleanup crypto context with secure memory wiping
 * @param ctx Crypto context to cleanup
 *
 * Securely zeroes all sensitive data (keys, salts, etc.) before freeing.
 * Always call this when done with a crypto context.
 *
 * @note Uses sodium_memzero() to prevent key material from persisting in memory.
 *
 * @ingroup crypto
 */
void crypto_cleanup(crypto_context_t *ctx);

/**
 * @brief Generate new X25519 key pair for key exchange
 * @param ctx Crypto context
 * @return CRYPTO_OK on success, error code on failure
 *
 * Generates a new ephemeral X25519 key pair. Called automatically by crypto_init().
 * Can be called again to regenerate keys (e.g., for rekeying).
 *
 * @ingroup crypto
 */
crypto_result_t crypto_generate_keypair(crypto_context_t *ctx);

/** @} */

/**
 * @name Key Exchange Protocol
 * @{
 *
 * Automatic HTTPS-like key exchange using X25519 Diffie-Hellman.
 * Both parties exchange ephemeral public keys and compute a shared secret.
 */

/**
 * @brief Get public key for sending to peer (step 1 of handshake)
 * @param ctx Crypto context
 * @param public_key_out Output buffer for public key (must be CRYPTO_PUBLIC_KEY_SIZE bytes)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Retrieves our ephemeral public key for transmission to the peer.
 * This is the first step in the key exchange handshake.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_get_public_key(const crypto_context_t *ctx, uint8_t *public_key_out);

/**
 * @brief Set peer's public key and compute shared secret (step 2 of handshake)
 * @param ctx Crypto context
 * @param peer_public_key Peer's ephemeral public key (CRYPTO_PUBLIC_KEY_SIZE bytes)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Receives peer's public key and computes the shared secret using X25519.
 * After this call, crypto_is_ready() will return true.
 *
 * @note This function computes the shared secret immediately. The shared secret
 *       is used for encryption/decryption after key exchange is complete.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_set_peer_public_key(crypto_context_t *ctx, const uint8_t *peer_public_key);

/**
 * @brief Check if key exchange is complete and ready for encryption
 * @param ctx Crypto context
 * @return true if key exchange is complete, false otherwise
 *
 * Returns true only after both parties have exchanged public keys and
 * the shared secret has been computed.
 *
 * @ingroup crypto
 */
bool crypto_is_ready(const crypto_context_t *ctx);

/** @} */

/**
 * @name Password-Based Encryption
 * @{
 *
 * Optional additional encryption layer using password-derived keys.
 * Uses Argon2id for memory-hard key derivation, providing resistance
 * to offline brute-force attacks.
 */

/**
 * @brief Validate password length requirements
 * @param password Password to validate
 * @return CRYPTO_OK if valid, CRYPTO_ERROR_INVALID_PARAMS if too short/long
 *
 * Validates that password meets length requirements (MIN_PASSWORD_LENGTH to
 * MAX_PASSWORD_LENGTH characters).
 *
 * @note Password must be between MIN_PASSWORD_LENGTH (8) and MAX_PASSWORD_LENGTH (256) characters.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_validate_password(const char *password);

/**
 * @brief Derive key from password using Argon2id
 * @param ctx Crypto context (must be initialized)
 * @param password Password to derive key from
 * @return CRYPTO_OK on success, error code on failure
 *
 * Derives a 32-byte encryption key from the password using Argon2id KDF.
 * The salt is deterministic ("ascii-chat-password-salt-v1") for consistent
 * key derivation across client/server.
 *
 * @note Argon2id is memory-hard, making offline brute-force attacks expensive.
 *
 * @note Salt: Uses deterministic salt ("ascii-chat-password-salt-v1") padded to
 *       ARGON2ID_SALT_SIZE (32 bytes) with zeros. This ensures the same password
 *       produces the same key on both client and server.
 *
 * @note Argon2id parameters: Uses INTERACTIVE limits (~0.1 seconds, ~64MB memory).
 *
 * @warning Deterministic salt: Same password always produces same key.
 *          Only use for session encryption, not long-term key storage.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_derive_password_key(crypto_context_t *ctx, const char *password);

/**
 * @brief Verify password matches stored salt/key
 * @param ctx Crypto context (must be initialized and have password)
 * @param password Password to verify
 * @return true if password matches, false otherwise
 *
 * Verifies that the provided password, when derived with the stored salt,
 * produces the same key as stored in the context.
 *
 * @note Uses constant-time comparison to prevent timing attacks.
 *
 * @note Salt: Uses same deterministic salt as crypto_derive_password_key().
 *
 * @ingroup crypto
 */
bool crypto_verify_password(const crypto_context_t *ctx, const char *password);

/**
 * @brief Derive deterministic encryption key from password for handshake
 * @param password Password to derive key from
 * @param encryption_key Output buffer for derived key (CRYPTO_ENCRYPTION_KEY_SIZE bytes)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Derives a deterministic key using a fixed salt (for handshake purposes).
 * This allows password-protected sessions without requiring key exchange
 * to be completed first.
 *
 * @note Uses same deterministic salt ("ascii-chat-password-salt-v1") as
 *       crypto_derive_password_key() for consistency.
 *
 * @note Same password always produces same key. Only use for handshake encryption,
 *       not for long-term key storage.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_derive_password_encryption_key(const char *password,
                                                      uint8_t encryption_key[CRYPTO_ENCRYPTION_KEY_SIZE]);

/**
 * @name Encryption/Decryption Operations
 * @{
 *
 * Encrypt/decrypt data using XSalsa20-Poly1305 (via libsodium secretbox).
 * Automatically handles nonce generation and MAC verification.
 */

/**
 * @brief Encrypt data using XSalsa20-Poly1305
 * @param ctx Crypto context (must be initialized and ready)
 * @param plaintext Plaintext data to encrypt
 * @param plaintext_len Length of plaintext (must be > 0 and <= CRYPTO_MAX_PLAINTEXT_SIZE)
 * @param ciphertext_out Output buffer for ciphertext
 * @param ciphertext_out_size Size of output buffer
 * @param ciphertext_len_out Output parameter for actual ciphertext length
 * @return CRYPTO_OK on success, error code on failure
 *
 * Encrypts data using XSalsa20-Poly1305 authenticated encryption.
 * Uses shared_key if key exchange is complete, otherwise falls back to password_key.
 *
 * @note Nonce generation: Automatically generates nonce as `session_id || counter`.
 *       Nonce is prepended to ciphertext: [nonce:24][encrypted_data + MAC].
 *       Counter increments after each encryption to prevent nonce reuse.
 *
 * @note Ciphertext format: [nonce:nonce_size][encrypted_data][MAC:mac_size]
 *       Total size = plaintext_len + nonce_size + mac_size
 *
 * @note Buffer requirements: ciphertext_out_size must be >= plaintext_len + nonce_size + mac_size
 *
 * @note Nonce counter: Starts at 1 (0 reserved for testing). Returns CRYPTO_ERROR_NONCE_EXHAUSTED
 *       if counter reaches 0 or UINT64_MAX (extremely unlikely, but triggers rekeying).
 *
 * @note Maximum plaintext size: CRYPTO_MAX_PLAINTEXT_SIZE (1MB)
 *
 * @note Rekeying: Automatically increments rekey_packet_count. Check crypto_should_rekey()
 *       after encryption to determine if rekeying should be initiated.
 *
 * @warning Context must be ready (crypto_is_ready() returns true) before calling this function.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_encrypt(crypto_context_t *ctx, const uint8_t *plaintext, size_t plaintext_len,
                               uint8_t *ciphertext_out, size_t ciphertext_out_size, size_t *ciphertext_len_out);

/**
 * @brief Decrypt data using XSalsa20-Poly1305
 * @param ctx Crypto context (must be initialized and ready)
 * @param ciphertext Ciphertext data to decrypt
 * @param ciphertext_len Length of ciphertext (must be >= nonce_size + mac_size)
 * @param plaintext_out Output buffer for plaintext
 * @param plaintext_out_size Size of output buffer
 * @param plaintext_len_out Output parameter for actual plaintext length
 * @return CRYPTO_OK on success, CRYPTO_ERROR_INVALID_MAC if MAC verification fails
 *
 * Decrypts data using XSalsa20-Poly1305 authenticated encryption.
 * Uses shared_key if key exchange is complete, otherwise falls back to password_key.
 *
 * @note Ciphertext format: [nonce:nonce_size][encrypted_data][MAC:mac_size]
 *       Nonce is extracted from the first nonce_size bytes of ciphertext.
 *
 * @note MAC verification: Automatically verified during decryption. Returns CRYPTO_ERROR_INVALID_MAC
 *       if MAC verification fails (indicating tampering or wrong key).
 *
 * @note Plaintext size: ciphertext_len - nonce_size - mac_size
 *
 * @note Buffer requirements: plaintext_out_size must be >= ciphertext_len - nonce_size - mac_size
 *
 * @warning Context must be ready (crypto_is_ready() returns true) before calling this function.
 * @warning Always check return value. CRYPTO_ERROR_INVALID_MAC indicates tampering or wrong key.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_decrypt(crypto_context_t *ctx, const uint8_t *ciphertext, size_t ciphertext_len,
                               uint8_t *plaintext_out, size_t plaintext_out_size, size_t *plaintext_len_out);

/** @} */

/**
 * @name Utility Functions
 * @{
 */

/**
 * @brief Convert crypto result to human-readable string
 * @param result Crypto result code
 * @return Human-readable error string (never NULL)
 *
 * Returns a descriptive string for each crypto_result_t value.
 * Useful for logging and error reporting.
 *
 * @ingroup crypto
 */
const char *crypto_result_to_string(crypto_result_t result);

/**
 * @brief Get crypto context status information for debugging
 * @param ctx Crypto context
 * @param status_buffer Output buffer for status string
 * @param buffer_size Size of status buffer
 *
 * Formats a human-readable status string with context state information:
 * - Initialization status
 * - Password status
 * - Key exchange status
 * - Ready status
 * - Encrypted/decrypted byte counts
 * - Nonce counter value
 *
 * @note Safe to call with NULL ctx or buffer (does nothing).
 *
 * @ingroup crypto
 */
void crypto_get_status(const crypto_context_t *ctx, char *status_buffer, size_t buffer_size);

/**
 * @brief Secure constant-time comparison of byte arrays
 * @param lhs First byte array
 * @param rhs Second byte array
 * @param len Length of arrays to compare
 * @return true if arrays are equal, false otherwise
 *
 * Uses constant-time comparison (sodium_memcmp) to prevent timing attacks.
 * Always compares all bytes, regardless of where difference is found.
 *
 * @note Use this for comparing sensitive data: keys, MACs, HMACs, signatures.
 *       Do NOT use regular memcmp() for cryptographic comparisons.
 *
 * @warning Returns false if either pointer is NULL.
 *
 * @ingroup crypto
 */
bool crypto_secure_compare(const uint8_t *lhs, const uint8_t *rhs, size_t len);

/**
 * @brief Generate cryptographically secure random bytes
 * @param buffer Output buffer for random bytes
 * @param len Number of random bytes to generate (must be > 0)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Uses libsodium's secure random number generator (randombytes_buf).
 * Suitable for generating nonces, keys, salts, and other cryptographic material.
 *
 * @note Automatically initializes libsodium if not already initialized.
 *
 * @warning Returns CRYPTO_ERROR_INVALID_PARAMS if buffer is NULL or len is 0.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_random_bytes(uint8_t *buffer, size_t len);

/** @} */

/**
 * @name Authentication and Handshake
 * @{
 *
 * HMAC-based authentication using HMAC-SHA256.
 * Used for password authentication and challenge-response protocols.
 */

/**
 * @brief Generate random nonce for authentication
 * @param nonce Output buffer for 32-byte random nonce
 * @return CRYPTO_OK on success, error code on failure
 *
 * Generates a cryptographically secure random nonce for authentication challenges.
 * Uses libsodium's secure random number generator.
 *
 * @note Automatically initializes libsodium if not already initialized.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_generate_nonce(uint8_t nonce[32]);

/**
 * @brief Compute HMAC-SHA256 for fixed 32-byte data
 * @param ctx Crypto context (for libsodium initialization check)
 * @param key HMAC key (32 bytes)
 * @param data Data to authenticate (32 bytes)
 * @param hmac Output buffer for HMAC (32 bytes)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Computes HMAC-SHA256 over exactly 32 bytes of data.
 * Useful for authenticating challenge nonces and other fixed-size values.
 *
 * @note Automatically initializes libsodium if not already initialized.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_compute_hmac(crypto_context_t *ctx, const uint8_t key[32], const uint8_t data[32],
                                    uint8_t hmac[32]);

/**
 * @brief Compute HMAC-SHA256 for variable-length data
 * @param ctx Crypto context (for libsodium initialization check)
 * @param key HMAC key (32 bytes)
 * @param data Data to authenticate (variable length)
 * @param data_len Length of data (must be > 0)
 * @param hmac Output buffer for HMAC (32 bytes)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Computes HMAC-SHA256 over variable-length data.
 * Useful for authenticating combined values (e.g., nonce || shared_secret).
 *
 * @note Automatically initializes libsodium if not already initialized.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_compute_hmac_ex(const crypto_context_t *ctx, const uint8_t key[32], const uint8_t *data,
                                       size_t data_len, uint8_t hmac[32]);

/**
 * @brief Verify HMAC-SHA256 for fixed 32-byte data
 * @param key HMAC key (32 bytes)
 * @param data Data that was authenticated (32 bytes)
 * @param expected_hmac Expected HMAC to verify (32 bytes)
 * @return true if HMAC is valid, false otherwise
 *
 * Verifies HMAC-SHA256 using constant-time comparison.
 * Prevents timing attacks during verification.
 *
 * @note Returns false if any parameter is NULL.
 *
 * @ingroup crypto
 */
bool crypto_verify_hmac(const uint8_t key[32], const uint8_t data[32], const uint8_t expected_hmac[32]);

/**
 * @brief Verify HMAC-SHA256 for variable-length data
 * @param key HMAC key (32 bytes)
 * @param data Data that was authenticated (variable length)
 * @param data_len Length of data (must be > 0)
 * @param expected_hmac Expected HMAC to verify (32 bytes)
 * @return true if HMAC is valid, false otherwise
 *
 * Verifies HMAC-SHA256 using constant-time comparison.
 * Prevents timing attacks during verification.
 *
 * @note Returns false if any parameter is NULL or data_len is 0.
 *
 * @ingroup crypto
 */
bool crypto_verify_hmac_ex(const uint8_t key[32], const uint8_t *data, size_t data_len,
                           const uint8_t expected_hmac[32]);

/** @} */

// =============================================================================
// High-level authentication helpers (shared between client and server)
// =============================================================================

/**
 * @name High-Level Authentication Helpers
 * @{
 *
 * Authentication helpers that bind password/key authentication to the DH key exchange,
 * preventing man-in-the-middle attacks.
 */

/**
 * @brief Compute authentication response HMAC bound to DH shared_secret
 * @param ctx Crypto context with keys (must have completed key exchange)
 * @param nonce Challenge nonce from server (32 bytes)
 * @param hmac_out Output HMAC (32 bytes)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Computes: HMAC(auth_key, nonce || shared_secret)
 * This binds the password/key authentication to the DH exchange, preventing MITM.
 *
 * @note Key selection: Uses password_key if available, otherwise uses shared_key.
 *       This allows password authentication to be optional while still binding to DH.
 *
 * @note Key exchange requirement: ctx->key_exchange_complete must be true.
 *       Returns CRYPTO_ERROR_INVALID_PARAMS if key exchange is not complete.
 *
 * @note MITM prevention: By including shared_secret in the HMAC computation, an attacker
 *       cannot intercept and replay authentication responses without knowing the shared secret.
 *
 * @warning Context must have completed key exchange before calling this function.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_compute_auth_response(const crypto_context_t *ctx, const uint8_t nonce[32],
                                             uint8_t hmac_out[32]);

/**
 * @brief Verify authentication response HMAC bound to DH shared_secret
 * @param ctx Crypto context with keys (must have completed key exchange)
 * @param nonce Challenge nonce that was sent (32 bytes)
 * @param expected_hmac Expected HMAC to verify (32 bytes)
 * @return true if HMAC is valid, false otherwise
 *
 * Verifies: HMAC(auth_key, nonce || shared_secret)
 *
 * @note Key selection: Uses password_key if available, otherwise uses shared_key.
 *       Must match the key used by crypto_compute_auth_response() on the peer side.
 *
 * @note Key exchange requirement: ctx->key_exchange_complete must be true.
 *       Returns false if key exchange is not complete.
 *
 * @warning Always check return value. False indicates authentication failure or wrong key.
 *
 * @ingroup crypto
 */
bool crypto_verify_auth_response(const crypto_context_t *ctx, const uint8_t nonce[32], const uint8_t expected_hmac[32]);

/**
 * @brief Create authentication challenge packet
 * @param ctx Crypto context (must be initialized)
 * @param packet_out Output buffer for challenge packet
 * @param packet_size Size of output buffer
 * @param packet_len_out Output parameter for actual packet length
 * @return CRYPTO_OK on success, error code on failure
 *
 * Creates an authentication challenge packet: [type:4][nonce:auth_challenge_size]
 * Generates a random nonce and stores it in ctx->auth_nonce for later verification.
 *
 * @note Packet format: [PACKET_TYPE_AUTH_CHALLENGE:4][random_nonce:32]
 *       Total size: sizeof(uint32_t) + ctx->auth_challenge_size (typically 36 bytes)
 *
 * @note The generated nonce is stored in ctx->auth_nonce and should be used later
 *       to verify the authentication response.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_create_auth_challenge(const crypto_context_t *ctx, uint8_t *packet_out, size_t packet_size,
                                             size_t *packet_len_out);

/**
 * @brief Process authentication challenge packet
 * @param ctx Crypto context (must be initialized)
 * @param packet Challenge packet received from peer
 * @param packet_len Length of challenge packet
 * @return CRYPTO_OK on success, error code on failure
 *
 * Processes an authentication challenge packet: [type:4][nonce:auth_challenge_size]
 * Extracts the nonce and stores it in ctx->auth_nonce for generating the response.
 *
 * @note Packet format: [PACKET_TYPE_AUTH_CHALLENGE:4][nonce:32]
 *       Expected size: sizeof(uint32_t) + ctx->auth_challenge_size (typically 36 bytes)
 *
 * @note The extracted nonce is stored in ctx->auth_nonce and should be used with
 *       crypto_compute_auth_response() to generate the authentication response.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_process_auth_challenge(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len);

/**
 * @brief Process authentication response packet
 * @param ctx Crypto context (must be initialized and have completed key exchange)
 * @param packet Response packet received from peer
 * @param packet_len Length of response packet
 * @return CRYPTO_OK on success, error code on failure
 *
 * Processes an authentication response packet containing HMAC.
 * Verifies the HMAC using crypto_verify_auth_response().
 *
 * @note Packet format depends on authentication method:
 *       - Password: [HMAC:32][challenge_nonce:32] (64 bytes)
 *       - Signature: [signature:64][challenge_nonce:32] (96 bytes)
 *
 * @warning Context must have completed key exchange before calling this function.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_process_auth_response(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len);

/** @} */

/**
 * @name Network Integration Helpers
 * @{
 *
 * Packet creation and processing functions for network transmission.
 * Handles packet formatting, encryption, and decryption automatically.
 */

/**
 * @brief Create public key packet for network transmission
 * @param ctx Crypto context (must be initialized)
 * @param packet_out Output buffer for packet
 * @param packet_size Size of output buffer
 * @param packet_len_out Output parameter for actual packet length
 * @return CRYPTO_OK on success, error code on failure
 *
 * Creates a public key packet: [type:4][public_key:public_key_size]
 * Used during key exchange handshake to send ephemeral public key to peer.
 *
 * @note Packet format: [PACKET_TYPE_PUBLIC_KEY:4][ephemeral_public_key:32]
 *       Total size: sizeof(uint32_t) + ctx->public_key_size (typically 36 bytes)
 *
 * @note This packet is NOT encrypted - it's part of the key exchange protocol.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_create_public_key_packet(const crypto_context_t *ctx, uint8_t *packet_out, size_t packet_size,
                                                size_t *packet_len_out);

/**
 * @brief Process received public key packet from peer
 * @param ctx Crypto context (must be initialized)
 * @param packet Public key packet received from peer
 * @param packet_len Length of packet
 * @return CRYPTO_OK on success, error code on failure
 *
 * Processes a public key packet: [type:4][public_key:public_key_size]
 * Extracts peer's public key and computes shared secret automatically.
 *
 * @note Packet format: [PACKET_TYPE_PUBLIC_KEY:4][peer_public_key:32]
 *       Expected size: sizeof(uint32_t) + ctx->public_key_size (typically 36 bytes)
 *
 * @note Automatically computes shared secret via crypto_set_peer_public_key().
 *       After this call, crypto_is_ready() may return true if key exchange is complete.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_process_public_key_packet(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len);

/**
 * @brief Create encrypted data packet for network transmission
 * @param ctx Crypto context (must be initialized and ready)
 * @param data Plaintext data to encrypt
 * @param data_len Length of plaintext data
 * @param packet_out Output buffer for encrypted packet
 * @param packet_size Size of output buffer
 * @param packet_len_out Output parameter for actual packet length
 * @return CRYPTO_OK on success, error code on failure
 *
 * Creates an encrypted data packet: [type:4][length:4][encrypted_data:var]
 * Encrypts the data using crypto_encrypt() and prepends packet type and length.
 *
 * @note Packet format: [PACKET_TYPE_ENCRYPTED_DATA:4][data_length:4][encrypted_data]
 *       Encrypted data format: [nonce:24][encrypted_data][MAC:16]
 *       Total size: sizeof(uint32_t) + sizeof(uint32_t) + data_len + nonce_size + mac_size
 *
 * @note Context must be ready (crypto_is_ready() returns true) before calling.
 *       This requires either completed key exchange or password-based encryption.
 *
 * @warning Ensure packet_size is large enough for encrypted data + headers.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_create_encrypted_packet(crypto_context_t *ctx, const uint8_t *data, size_t data_len,
                                               uint8_t *packet_out, size_t packet_size, size_t *packet_len_out);

/**
 * @brief Process received encrypted packet from peer
 * @param ctx Crypto context (must be initialized and ready)
 * @param packet Encrypted packet received from peer
 * @param packet_len Length of packet
 * @param data_out Output buffer for decrypted plaintext
 * @param data_size Size of output buffer
 * @param data_len_out Output parameter for actual plaintext length
 * @return CRYPTO_OK on success, CRYPTO_ERROR_INVALID_MAC if MAC verification fails
 *
 * Processes an encrypted data packet: [type:4][length:4][encrypted_data:var]
 * Decrypts the data using crypto_decrypt().
 *
 * @note Packet format: [PACKET_TYPE_ENCRYPTED_DATA:4][data_length:4][encrypted_data]
 *       Encrypted data format: [nonce:24][encrypted_data][MAC:16]
 *       Plaintext size: data_length - nonce_size - mac_size
 *
 * @note Context must be ready (crypto_is_ready() returns true) before calling.
 *
 * @warning Always check return value. CRYPTO_ERROR_INVALID_MAC indicates tampering or wrong key.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_process_encrypted_packet(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len,
                                                uint8_t *data_out, size_t data_size, size_t *data_len_out);

/** @} */

/**
 * @name Shared Cryptographic Operations
 * @{
 *
 * Low-level cryptographic operations used by both client and server
 * for authentication and key exchange.
 */

/**
 * @brief Compute password-based HMAC for authentication
 * @param ctx Crypto context
 * @param password_key Password-derived key (32 bytes)
 * @param nonce Challenge nonce (32 bytes)
 * @param shared_secret DH shared secret (32 bytes)
 * @param hmac_out Output buffer for HMAC (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Computes: HMAC(password_key, nonce || shared_secret)
 * Binds password authentication to DH key exchange, preventing MITM.
 *
 * @note Used by authentication handlers to prove knowledge of password AND shared secret.
 *
 * @note Combined data: nonce || shared_secret (64 bytes total)
 *
 * @ingroup crypto
 */
asciichat_error_t crypto_compute_password_hmac(crypto_context_t *ctx, const uint8_t *password_key, const uint8_t *nonce,
                                               const uint8_t *shared_secret, uint8_t *hmac_out);

/**
 * @brief Verify peer's signature on ephemeral key
 * @param peer_public_key Peer's Ed25519 public key (32 bytes)
 * @param ephemeral_key Ephemeral public key that was signed (variable size)
 * @param ephemeral_key_size Size of ephemeral key (typically 32 bytes for X25519)
 * @param signature Ed25519 signature (64 bytes)
 * @return ASCIICHAT_OK if signature is valid, error code on failure
 *
 * Verifies Ed25519 signature on ephemeral public key using peer's identity key.
 * Used during authenticated key exchange to verify server identity.
 *
 * @note Signature is over the ephemeral public key itself, proving ownership
 *       of the identity key without revealing it.
 *
 * @note Used in authenticated key exchange format (ephemeral + identity + signature).
 *
 * @ingroup crypto
 */
asciichat_error_t crypto_verify_peer_signature(const uint8_t *peer_public_key, const uint8_t *ephemeral_key,
                                               size_t ephemeral_key_size, const uint8_t *signature);

/**
 * @brief Sign ephemeral key with private key
 * @param private_key Ed25519 private key for signing (64 bytes)
 * @param ephemeral_key Ephemeral public key to sign (variable size)
 * @param ephemeral_key_size Size of ephemeral key (typically 32 bytes for X25519)
 * @param signature_out Output buffer for Ed25519 signature (64 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Signs ephemeral public key with Ed25519 identity key.
 * Used during authenticated key exchange to prove server identity.
 *
 * @note Signature is over the ephemeral public key itself.
 *       Allows verification without revealing the identity key.
 *
 * @note Used in authenticated key exchange format (ephemeral + identity + signature).
 *
 * @ingroup crypto
 */
asciichat_error_t crypto_sign_ephemeral_key(const private_key_t *private_key, const uint8_t *ephemeral_key,
                                            size_t ephemeral_key_size, uint8_t *signature_out);

/**
 * @brief Combine HMAC and challenge nonce for transmission
 * @param hmac HMAC to transmit (32 bytes)
 * @param challenge_nonce Challenge nonce to transmit (32 bytes)
 * @param combined_out Output buffer for combined data (64 bytes)
 *
 * Combines HMAC and challenge nonce into a single buffer: [HMAC:32][nonce:32]
 * Used to pack authentication response data for transmission.
 *
 * @note Format: [HMAC:32][challenge_nonce:32] (64 bytes total)
 *
 * @ingroup crypto
 */
void crypto_combine_auth_data(const uint8_t *hmac, const uint8_t *challenge_nonce, uint8_t *combined_out);

/**
 * @brief Extract HMAC and challenge nonce from combined data
 * @param combined_data Combined data received from peer (64 bytes)
 * @param hmac_out Output buffer for HMAC (32 bytes)
 * @param challenge_out Output buffer for challenge nonce (32 bytes)
 *
 * Extracts HMAC and challenge nonce from combined buffer: [HMAC:32][nonce:32]
 * Used to unpack authentication response data received from peer.
 *
 * @note Format: [HMAC:32][challenge_nonce:32] (64 bytes total)
 *
 * @ingroup crypto
 */
void crypto_extract_auth_data(const uint8_t *combined_data, uint8_t *hmac_out, uint8_t *challenge_out);

/** @} */

/**
 * @name Session Rekeying Protocol
 * @{
 *
 * Periodic key rotation to limit exposure if keys are compromised.
 * Rekeys after time threshold (default: 1 hour) OR packet count threshold (default: 1 million),
 * whichever comes first.
 *
 * @note Test environment detection: If CRITERION_TEST or TESTING environment variable is set,
 *       rekey thresholds are reduced to 30 seconds / 1000 packets for faster testing.
 *
 * @note Rekeying flow:
 *       1. Initiator calls crypto_rekey_init() and sends REKEY_REQUEST
 *       2. Responder processes request, calls crypto_rekey_process_request(), sends REKEY_RESPONSE
 *       3. Initiator processes response, calls crypto_rekey_process_response(), sends REKEY_COMPLETE
 *       4. Responder verifies REKEY_COMPLETE decrypts with new key, calls crypto_rekey_commit()
 *       5. Initiator calls crypto_rekey_commit() after receiving confirmation
 *
 * @note Old keys remain active until REKEY_COMPLETE is verified, ensuring no service interruption.
 */

/** @brief Minimum time interval between rekey requests (3 seconds for testing, 60 for production) */
#define REKEY_MIN_INTERVAL 3
/** @brief Default rekey time threshold (1 hour in seconds) */
#define REKEY_DEFAULT_TIME_THRESHOLD 3600
/** @brief Default rekey packet threshold (1 million packets) */
#define REKEY_DEFAULT_PACKET_THRESHOLD 1000000
/** @brief Test mode rekey time threshold (30 seconds) */
#define REKEY_TEST_TIME_THRESHOLD 30
/** @brief Test mode rekey packet threshold (1000 packets) */
#define REKEY_TEST_PACKET_THRESHOLD 1000
/** @brief Maximum consecutive rekey failures before giving up */
#define REKEY_MAX_FAILURE_COUNT 10
/** @brief Minimum interval between rekey requests (60 seconds, DDoS protection) */
#define REKEY_MIN_REQUEST_INTERVAL 60

/**
 * @brief Check if rekeying should be triggered based on time or packet count thresholds
 * @param ctx Crypto context
 * @return true if rekey should be initiated, false otherwise
 *
 * Checks if rekeying should be triggered based on:
 * - Time since last rekey >= rekey_time_threshold
 * - Packet count since last rekey >= rekey_packet_threshold
 * - Minimum interval since last rekey request (DDoS protection)
 *
 * @note Should be called after each packet encryption.
 *
 * @note Test environment: Automatically uses test thresholds if CRITERION_TEST or TESTING env var is set.
 *
 * @note DDoS protection: Requires at least REKEY_MIN_REQUEST_INTERVAL seconds between requests.
 *
 * @ingroup crypto
 */
bool crypto_should_rekey(const crypto_context_t *ctx);

/**
 * @brief Initiate rekeying by generating new ephemeral keys
 * @param ctx Crypto context
 * @return CRYPTO_OK on success, error code on failure
 *
 * Generates new ephemeral key pair and stores in temp_* fields.
 * Called by the initiator (client or server) before sending REKEY_REQUEST.
 *
 * @note New keys are stored in temp_public_key and temp_private_key.
 *       They are NOT active until crypto_rekey_commit() is called.
 *
 * @note Sets ctx->rekey_in_progress = true and updates rekey_last_request_time.
 *
 * @warning Do not call if rekey is already in progress.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_rekey_init(crypto_context_t *ctx);

/**
 * @brief Process REKEY_REQUEST from peer (responder side)
 * @param ctx Crypto context
 * @param peer_new_public_key Peer's new ephemeral public key (32 bytes)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Processes peer's new ephemeral public key from REKEY_REQUEST.
 * Generates our own new ephemeral keys and computes new shared secret.
 *
 * @note Generates new temp_* keys and computes temp_shared_key.
 *       Keys are NOT active until crypto_rekey_commit() is called.
 *
 * @note Should send REKEY_RESPONSE with our new public key after this call.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_rekey_process_request(crypto_context_t *ctx, const uint8_t *peer_new_public_key);

/**
 * @brief Process REKEY_RESPONSE from peer (initiator side)
 * @param ctx Crypto context
 * @param peer_new_public_key Peer's new ephemeral public key (32 bytes)
 * @return CRYPTO_OK on success, error code on failure
 *
 * Processes peer's new ephemeral public key from REKEY_RESPONSE.
 * Computes new shared secret using our temp_private_key and peer's temp_public_key.
 *
 * @note Computes temp_shared_key. Keys are NOT active until crypto_rekey_commit() is called.
 *
 * @note Should send REKEY_COMPLETE encrypted with NEW key after this call.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_rekey_process_response(crypto_context_t *ctx, const uint8_t *peer_new_public_key);

/**
 * @brief Commit to new keys after successful REKEY_COMPLETE
 * @param ctx Crypto context
 * @return CRYPTO_OK on success, error code on failure
 *
 * Switches from old shared_key to temp_shared_key, resets counters.
 * Called after REKEY_COMPLETE is verified (decrypts successfully with new key).
 *
 * @note Replaces shared_key with temp_shared_key.
 * @note Resets rekey_packet_count, rekey_last_time, and rekey_in_progress.
 * @note Clears temp_* keys (they are now the active keys).
 *
 * @warning Only call after verifying REKEY_COMPLETE decrypts with new key.
 *
 * @ingroup crypto
 */
crypto_result_t crypto_rekey_commit(crypto_context_t *ctx);

/**
 * @brief Abort rekeying and fallback to old keys
 * @param ctx Crypto context
 *
 * Aborts ongoing rekey and clears temp_* keys.
 * Called on rekey failure (timeout, bad keys, decryption failure, etc.).
 *
 * @note Clears temp_* keys and sets rekey_in_progress = false.
 * @note Old keys remain active (no service interruption).
 * @note Increments rekey_failure_count for exponential backoff.
 *
 * @ingroup crypto
 */
void crypto_rekey_abort(crypto_context_t *ctx);

/**
 * @brief Get the current rekeying state for debugging/logging
 * @param ctx Crypto context
 * @param status_buffer Output buffer for status string
 * @param buffer_size Size of status buffer
 *
 * Formats a human-readable status string with rekeying state:
 * - Packet count since last rekey
 * - Time since last rekey
 * - Rekey in progress status
 * - Failure count
 * - Threshold values
 *
 * @note Safe to call with NULL ctx or buffer (does nothing).
 *
 * @ingroup crypto
 */
void crypto_get_rekey_status(const crypto_context_t *ctx, char *status_buffer, size_t buffer_size);

/** @} */

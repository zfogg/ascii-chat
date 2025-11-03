#pragma once

/**
 * @file crypto/handshake.h
 * @ingroup handshake
 * @brief Cryptographic handshake implementation for key exchange and authentication
 *
 * This header provides the cryptographic handshake protocol implementation,
 * managing the complete handshake flow from key exchange to authentication
 * completion.
 *
 * For detailed documentation on the handshake protocol, see @ref topic_handshake "Handshake Protocol".
 *
 * The handshake protocol:
 * 1. Capabilities negotiation (crypto algorithms supported)
 * 2. Parameters negotiation (algorithm-specific sizes)
 * 3. Key exchange (X25519 Diffie-Hellman)
 * 4. Authentication (password or client key)
 * 5. Handshake completion
 *
 * @note Key Exchange Formats:
 *       - Simple format: Only ephemeral public key (when server has no identity key)
 *       - Authenticated format: Ephemeral key + identity key + signature (when server has identity key)
 *
 * @note Byte Order: Client must convert network byte order to host byte order for
 *       crypto parameters. Server uses host byte order directly.
 *
 * @note State Machine: Handshake follows a strict state machine. Functions validate
 *       current state before proceeding. Invalid state transitions return errors.
 *
 * @note Packet Size Validation: All packets are validated against negotiated parameters.
 *       Simple and authenticated formats are both supported for key exchange packets.
 *
 * @warning GPG Key Support: Currently disabled. Code exists but is not active.
 *          See gpg.h for GPG-related functions.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include "platform/socket.h"
#include "crypto.h"
#include "keys/keys.h"
#include "network/packet_types.h" // For crypto_parameters_packet_t

/**
 * @name Authentication Requirement Flags
 * @{
 *
 * Flags sent in AUTH_CHALLENGE packet to indicate server requirements.
 */

/** @brief Server requires password authentication */
#define AUTH_REQUIRE_PASSWORD 0x01
/** @brief Server requires client key authentication (whitelist) */
#define AUTH_REQUIRE_CLIENT_KEY 0x02

/** @} */

/**
 * @brief Cryptographic handshake state enumeration
 *
 * Represents the current state of the handshake protocol.
 * Handshake follows a strict state machine - invalid transitions return errors.
 *
 * @ingroup handshake
 */
typedef enum {
  CRYPTO_HANDSHAKE_DISABLED = 0,   /**< No encryption (handshake disabled) */
  CRYPTO_HANDSHAKE_INIT,           /**< Initial state (ready to start handshake) */
  CRYPTO_HANDSHAKE_KEY_EXCHANGE,   /**< DH key exchange in progress */
  CRYPTO_HANDSHAKE_AUTHENTICATING, /**< Authentication challenge/response */
  CRYPTO_HANDSHAKE_READY,          /**< Handshake complete, encryption ready */
  CRYPTO_HANDSHAKE_FAILED          /**< Handshake failed (cannot recover) */
} crypto_handshake_state_t;

/**
 * @brief Cryptographic handshake context structure
 *
 * Manages the complete handshake state for a single connection, including
 * key exchange, authentication, and connection metadata.
 *
 * @note Server/Client specific fields:
 *       - Server-only: server_public_key, server_private_key, client_whitelist
 *       - Client-only: client_public_key, client_private_key, expected_server_key
 *
 * @note Dynamic crypto parameters: All size fields are stored in crypto_ctx
 *       and accessed via ctx->crypto_ctx.* (public_key_size, auth_public_key_size, etc.)
 *
 * @note Authentication modes:
 *       - Password: Uses Argon2id-derived key for authentication
 *       - Client key: Uses Ed25519 signature for client authentication (whitelist)
 *       - Server identity: Uses Ed25519 signature for server authentication (known_hosts)
 *
 * @note State validation: Functions validate current state before proceeding.
 *       Invalid state transitions return ERROR_INVALID_STATE.
 *
 * @ingroup handshake
 */
typedef struct crypto_handshake_context_t {
  /** Core cryptographic context */
  crypto_context_t crypto_ctx;    /**< Core crypto context (keys, encryption state) */
  crypto_handshake_state_t state; /**< Current handshake state (validated by each function) */
  bool is_server;                 /**< True if this is the server side */

  /** Server identity (server only) */
  public_key_t server_public_key;   /**< Server's long-term Ed25519 public key (identity) */
  private_key_t server_private_key; /**< Server's long-term Ed25519 private key (for signing) */

  /** Client identity (client only) */
  public_key_t client_public_key;   /**< Client's Ed25519 public key (for authentication) */
  private_key_t client_private_key; /**< Client's Ed25519 private key (for signing challenges) */
  char expected_server_key[256];    /**< Expected server key fingerprint (client only, for known_hosts) */

  /** Connection info for known_hosts */
  char server_hostname[256]; /**< Server hostname (user-provided) */
  char server_ip[256];       /**< Server IP address (resolved from connection) */
  uint16_t server_port;      /**< Server port */

  /** Authentication configuration */
  bool verify_server_key;                          /**< Client: verify server key against known_hosts */
  bool require_client_auth;                        /**< Server: require client authentication (whitelist) */
  bool server_uses_client_auth;                    /**< Client: whether server requested client authentication */
  char client_keys_path[PLATFORM_MAX_PATH_LENGTH]; /**< Server: client keys file path (whitelist) */

  /** Dynamic crypto parameters (from crypto_parameters_packet_t) */
  /** @note All size fields are stored in crypto_ctx and accessed via ctx->crypto_ctx.*
   *        (public_key_size, auth_public_key_size, shared_key_size, signature_size, etc.)
   */

  /** Client whitelist (server only) */
  public_key_t *client_whitelist;   /**< Pointer to whitelist array (server only) */
  size_t num_whitelisted_clients;   /**< Number of whitelisted clients */
  public_key_t client_ed25519_key;  /**< Client's Ed25519 key (received during handshake) */
  bool client_ed25519_key_verified; /**< Whether client's Ed25519 key was verified against whitelist */
  bool client_sent_identity;        /**< Whether client provided an identity key during handshake */

  /** Password authentication */
  bool has_password;  /**< Whether password authentication is enabled */
  char password[256]; /**< Password for authentication (temporary storage, cleared after use) */

  /** Mutual authentication (client challenges server) */
  uint8_t client_challenge_nonce[32]; /**< Client-generated nonce for server to prove knowledge of shared secret */

} crypto_handshake_context_t;

/**
 * @name Handshake Initialization
 * @{
 */

/**
 * @brief Initialize crypto handshake context
 * @param ctx Handshake context to initialize (must not be NULL)
 * @param is_server True if this is the server side, false for client
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes a handshake context and sets initial state to CRYPTO_HANDSHAKE_INIT.
 * Generates ephemeral key pair automatically via crypto_init().
 *
 * @note Server-specific initialization: If is_server is true, server identity keys
 *       should be loaded separately before starting handshake.
 *
 * @note Client-specific initialization: If is_server is false, known_hosts verification
 *       should be configured separately.
 *
 * @note State: Sets state to CRYPTO_HANDSHAKE_INIT. Handshake functions validate
 *       state before proceeding.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_init(crypto_handshake_context_t *ctx, bool is_server);

/**
 * @brief Set crypto parameters from crypto_parameters_packet_t
 * @param ctx Handshake context
 * @param params Negotiated crypto parameters (from capabilities negotiation)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Updates crypto context with negotiated parameters from capabilities negotiation.
 * Client converts network byte order to host byte order; server uses host byte order.
 *
 * @note Byte order: Client must convert network->host byte order using ntohs().
 *       Server uses host byte order directly (no conversion).
 *
 * @note Parameters set:
 *       - Key sizes (public_key_size, auth_public_key_size, shared_key_size, signature_size)
 *       - Algorithm sizes (nonce_size, mac_size, hmac_size)
 *       - Derived sizes (encryption_key_size = shared_key_size, private_key_size = public_key_size)
 *
 * @warning Must be called before key exchange. Packet validation uses these parameters.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_set_parameters(crypto_handshake_context_t *ctx,
                                                  const crypto_parameters_packet_t *params);

/**
 * @brief Validate crypto packet size based on session parameters
 * @param ctx Handshake context (must have parameters set)
 * @param packet_type Packet type to validate
 * @param packet_size Actual packet size received
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates packet size against negotiated parameters. Supports both simple and
 * authenticated formats for key exchange packets.
 *
 * @note Key exchange packets have two formats:
 *       - Simple: Only ephemeral public key (when server has no identity key)
 *       - Authenticated: Ephemeral key + identity key + signature (when server has identity key)
 *
 * @note Supported packet types:
 *       - PACKET_TYPE_CRYPTO_CAPABILITIES
 *       - PACKET_TYPE_CRYPTO_PARAMETERS
 *       - PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT (simple or authenticated)
 *       - PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP (simple or authenticated)
 *       - PACKET_TYPE_CRYPTO_AUTH_CHALLENGE
 *       - PACKET_TYPE_CRYPTO_AUTH_RESPONSE
 *       - PACKET_TYPE_CRYPTO_AUTH_FAILED
 *       - PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP
 *       - PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE
 *       - PACKET_TYPE_CRYPTO_NO_ENCRYPTION
 *       - PACKET_TYPE_ENCRYPTED
 *
 * @warning Must be called after crypto_handshake_set_parameters(). Validation uses
 *          negotiated parameters from ctx->crypto_ctx.*
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_validate_packet_size(const crypto_handshake_context_t *ctx, uint16_t packet_type,
                                                        size_t packet_size);

/**
 * @brief Initialize crypto handshake context with password authentication
 * @param ctx Handshake context to initialize (must not be NULL)
 * @param is_server True if this is the server side, false for client
 * @param password Password for authentication (must meet length requirements)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes handshake context with password-based authentication.
 * Derives encryption key from password using Argon2id.
 *
 * @note Password is stored temporarily in ctx->password and cleared after use.
 *
 * @note Password requirements: Must be between MIN_PASSWORD_LENGTH (8) and
 *       MAX_PASSWORD_LENGTH (256) characters.
 *
 * @note State: Sets state to CRYPTO_HANDSHAKE_INIT and has_password to true.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_init_with_password(crypto_handshake_context_t *ctx, bool is_server,
                                                      const char *password);

/**
 * @brief Cleanup crypto handshake context with secure memory wiping
 * @param ctx Handshake context to cleanup
 *
 * Securely zeroes all sensitive data (keys, passwords, etc.) before cleanup.
 * Always call this when done with a handshake context.
 *
 * @note Uses sodium_memzero() to prevent key material from persisting in memory.
 * @note Safe to call with NULL ctx (does nothing).
 *
 * @ingroup handshake
 */
void crypto_handshake_cleanup(crypto_handshake_context_t *ctx);

/** @} */

/**
 * @name Handshake Protocol Flow
 * @{
 *
 * Handshake functions that implement the cryptographic handshake protocol.
 * Functions must be called in order and validate state before proceeding.
 */

/**
 * @brief Server: Start crypto handshake by sending public key
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_INIT state)
 * @param client_socket Socket to send on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Server initiates handshake by sending KEY_EXCHANGE_INIT packet.
 * Supports both simple and authenticated formats based on server identity key.
 *
 * @note Packet formats:
 *       - Simple: [ephemeral_key:public_key_size] (when server has no identity key)
 *       - Authenticated: [ephemeral_key:public_key_size][identity_key:auth_public_key_size][signature:signature_size]
 *         (when server has Ed25519 identity key)
 *
 * @note State transition: CRYPTO_HANDSHAKE_INIT -> CRYPTO_HANDSHAKE_KEY_EXCHANGE
 *
 * @note Signature: If server has identity key, signs ephemeral key with Ed25519 identity key.
 *       Signature proves ownership of identity key without revealing private key.
 *
 * @warning Must have crypto parameters set (via crypto_handshake_set_parameters()) before calling.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_server_start(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Client: Process server's public key and send our public key
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_INIT state)
 * @param client_socket Socket to send/receive on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Client processes server's KEY_EXCHANGE_INIT packet and responds with KEY_EXCHANGE_RESP.
 * Supports both simple and authenticated formats. Verifies server signature if present.
 *
 * @note Packet formats (response):
 *       - Simple: [ephemeral_key:public_key_size] (when server sent simple format)
 *       - Authenticated: [ephemeral_key:public_key_size][client_auth_key:32][client_sig:64]
 *         (when client has identity key and server requested authentication)
 *
 * @note Server key verification: If server sent authenticated format, verifies signature
 *       using server's identity public key. Updates known_hosts if verification succeeds.
 *
 * @note State transition: CRYPTO_HANDSHAKE_INIT -> CRYPTO_HANDSHAKE_KEY_EXCHANGE
 *
 * @warning Must have crypto parameters set and server hostname/IP configured for known_hosts.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_client_key_exchange(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Server: Process client's public key and send auth challenge
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_KEY_EXCHANGE state)
 * @param client_socket Socket to send/receive on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Server processes client's KEY_EXCHANGE_RESP packet and sends AUTH_CHALLENGE.
 * Computes shared secret and generates authentication challenge nonce.
 *
 * @note Shared secret: Computed automatically from client's ephemeral public key.
 *       After this call, key exchange is complete.
 *
 * @note Authentication requirements: Sends AUTH_CHALLENGE with flags indicating requirements:
 *       - AUTH_REQUIRE_PASSWORD: Server requires password authentication
 *       - AUTH_REQUIRE_CLIENT_KEY: Server requires client key authentication (whitelist)
 *
 * @note Client identity: If client sent authenticated format, extracts and verifies
 *       client's Ed25519 key against whitelist (if require_client_auth is true).
 *
 * @note State transition: CRYPTO_HANDSHAKE_KEY_EXCHANGE -> CRYPTO_HANDSHAKE_AUTHENTICATING
 *
 * @warning Must have computed shared secret before generating auth challenge.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_server_auth_challenge(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Client: Process auth challenge and send response
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_KEY_EXCHANGE state)
 * @param client_socket Socket to send/receive on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Client processes server's AUTH_CHALLENGE packet and sends AUTH_RESPONSE.
 * Generates HMAC bound to shared secret using password or client key.
 *
 * @note Authentication methods:
 *       - Password: HMAC(password_key, nonce || shared_secret)
 *       - Client key: Ed25519 signature(client_private_key, nonce || shared_secret)
 *
 * @note HMAC binding: Authentication is bound to DH shared secret to prevent MITM.
 *       Attacker cannot replay authentication without knowing shared secret.
 *
 * @note State transition: CRYPTO_HANDSHAKE_KEY_EXCHANGE -> CRYPTO_HANDSHAKE_AUTHENTICATING
 *
 * @warning Must have computed shared secret before generating auth response.
 * @warning Authentication method must match server requirements (password or client key).
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_client_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Client: Wait for handshake complete confirmation
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_AUTHENTICATING state)
 * @param client_socket Socket to receive on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Client waits for server's HANDSHAKE_COMPLETE packet (empty payload).
 * After receiving, handshake is complete and encryption is ready.
 *
 * @note Empty packet: HANDSHAKE_COMPLETE has no payload (0 bytes).
 *
 * @note State transition: CRYPTO_HANDSHAKE_AUTHENTICATING -> CRYPTO_HANDSHAKE_READY
 *
 * @warning After this call, handshake is complete. Use crypto_handshake_is_ready() to verify.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_client_complete(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Server: Process auth response and complete handshake
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_AUTHENTICATING state)
 * @param client_socket Socket to send/receive on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Server processes client's AUTH_RESPONSE packet and sends HANDSHAKE_COMPLETE.
 * Verifies authentication HMAC or signature and completes handshake.
 *
 * @note Authentication verification:
 *       - Password: Verifies HMAC(password_key, nonce || shared_secret)
 *       - Client key: Verifies Ed25519 signature against client whitelist
 *
 * @note Client whitelist: If require_client_auth is true, verifies client's Ed25519 key
 *       against whitelist. Returns error if client not whitelisted.
 *
 * @note State transition: CRYPTO_HANDSHAKE_AUTHENTICATING -> CRYPTO_HANDSHAKE_READY
 *
 * @warning Returns error if authentication fails. Client must retry with correct credentials.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_server_complete(crypto_handshake_context_t *ctx, socket_t client_socket);

/** @} */

/**
 * @name Handshake Status and Encryption
 * @{
 */

/**
 * @brief Check if handshake is complete and encryption is ready
 * @param ctx Handshake context
 * @return true if handshake is complete and encryption is ready, false otherwise
 *
 * Returns true only when handshake state is CRYPTO_HANDSHAKE_READY.
 * Use this before attempting to encrypt/decrypt data.
 *
 * @note State check: Verifies ctx->state == CRYPTO_HANDSHAKE_READY.
 *
 * @ingroup handshake
 */
bool crypto_handshake_is_ready(const crypto_handshake_context_t *ctx);

/**
 * @brief Get the crypto context for encryption/decryption
 * @param ctx Handshake context
 * @return Pointer to crypto context, or NULL if ctx is NULL
 *
 * Returns pointer to the underlying crypto_context_t for direct access.
 * Use this to access crypto functions that require crypto_context_t.
 *
 * @note Returns pointer to ctx->crypto_ctx.
 *
 * @ingroup handshake
 */
const crypto_context_t *crypto_handshake_get_context(const crypto_handshake_context_t *ctx);

/**
 * @brief Encrypt a packet using the established crypto context
 * @param ctx Handshake context (must be ready)
 * @param plaintext Plaintext data to encrypt
 * @param plaintext_len Length of plaintext data
 * @param ciphertext Output buffer for ciphertext
 * @param ciphertext_size Size of output buffer
 * @param ciphertext_len Output parameter for actual ciphertext length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Encrypts data using the established crypto context (via crypto_encrypt()).
 * Handshake must be complete (crypto_handshake_is_ready() returns true).
 *
 * @note Ciphertext format: [nonce:nonce_size][encrypted_data][MAC:mac_size]
 *       Total size = plaintext_len + nonce_size + mac_size
 *
 * @note Buffer requirements: ciphertext_size must be >= plaintext_len + nonce_size + mac_size
 *
 * @warning Handshake must be complete before calling this function.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_encrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *plaintext,
                                                  size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                                                  size_t *ciphertext_len);

/**
 * @brief Decrypt a packet using the established crypto context
 * @param ctx Handshake context (must be ready)
 * @param ciphertext Ciphertext data to decrypt
 * @param ciphertext_len Length of ciphertext data
 * @param plaintext Output buffer for plaintext
 * @param plaintext_size Size of output buffer
 * @param plaintext_len Output parameter for actual plaintext length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Decrypts data using the established crypto context (via crypto_decrypt()).
 * Handshake must be complete (crypto_handshake_is_ready() returns true).
 *
 * @note Ciphertext format: [nonce:nonce_size][encrypted_data][MAC:mac_size]
 *       Plaintext size = ciphertext_len - nonce_size - mac_size
 *
 * @note MAC verification: Automatically verified during decryption.
 *       Returns error if MAC verification fails (tampering or wrong key).
 *
 * @note Buffer requirements: plaintext_size must be >= ciphertext_len - nonce_size - mac_size
 *
 * @warning Handshake must be complete before calling this function.
 * @warning Always check return value. MAC verification failure indicates tampering.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_decrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *ciphertext,
                                                  size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                                                  size_t *plaintext_len);

/**
 * @brief Encrypt with automatic passthrough if crypto not ready
 * @param ctx Handshake context
 * @param crypto_ready True if crypto is ready, false to passthrough
 * @param plaintext Plaintext data to encrypt
 * @param plaintext_len Length of plaintext data
 * @param ciphertext Output buffer for ciphertext or plaintext (if passthrough)
 * @param ciphertext_size Size of output buffer
 * @param ciphertext_len Output parameter for actual length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Encrypts data if crypto is ready, otherwise passes through plaintext unchanged.
 * Useful for mixed-mode operation where some packets are encrypted and others are not.
 *
 * @note Passthrough: If crypto_ready is false, plaintext is copied to ciphertext unchanged.
 *       ciphertext_len is set to plaintext_len.
 *
 * @note Encryption: If crypto_ready is true, encrypts using crypto_handshake_encrypt_packet().
 *
 * @warning crypto_ready must accurately reflect handshake state.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_encrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                                       const uint8_t *plaintext, size_t plaintext_len,
                                                       uint8_t *ciphertext, size_t ciphertext_size,
                                                       size_t *ciphertext_len);

/**
 * @brief Decrypt with automatic passthrough if crypto not ready
 * @param ctx Handshake context
 * @param crypto_ready True if crypto is ready, false to passthrough
 * @param ciphertext Ciphertext or plaintext data to decrypt
 * @param ciphertext_len Length of ciphertext/plaintext data
 * @param plaintext Output buffer for plaintext
 * @param plaintext_size Size of output buffer
 * @param plaintext_len Output parameter for actual plaintext length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Decrypts data if crypto is ready, otherwise passes through unchanged.
 * Useful for mixed-mode operation where some packets are encrypted and others are not.
 *
 * @note Passthrough: If crypto_ready is false, ciphertext is copied to plaintext unchanged.
 *       plaintext_len is set to ciphertext_len.
 *
 * @note Decryption: If crypto_ready is true, decrypts using crypto_handshake_decrypt_packet().
 *
 * @warning crypto_ready must accurately reflect handshake state.
 * @warning Always check return value. MAC verification failure indicates tampering.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_decrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                                       const uint8_t *ciphertext, size_t ciphertext_len,
                                                       uint8_t *plaintext, size_t plaintext_size,
                                                       size_t *plaintext_len);

/** @} */

/**
 * @name Session Rekeying Protocol
 * @{
 *
 * Periodic key rotation during active session to limit exposure if keys are compromised.
 * Rekeys after time threshold (1 hour) OR packet count threshold (1 million), whichever comes first.
 *
 * @note Rekeying flow:
 *       1. Initiator calls crypto_handshake_rekey_request() and sends REKEY_REQUEST
 *       2. Responder processes request, calls crypto_handshake_process_rekey_request(), sends REKEY_RESPONSE
 *       3. Initiator processes response, calls crypto_handshake_process_rekey_response(), sends REKEY_COMPLETE
 *       4. Responder verifies REKEY_COMPLETE decrypts with new key, calls crypto_handshake_process_rekey_complete()
 *       5. Both sides commit to new keys (old keys remain active until verification)
 *
 * @note Old keys remain active until REKEY_COMPLETE is verified, ensuring no service interruption.
 *
 * @note REKEY_COMPLETE is encrypted with NEW shared secret to prove both sides computed the same secret.
 */

/**
 * @brief Send REKEY_REQUEST packet (initiator side)
 * @param ctx Crypto handshake context (must be ready)
 * @param socket Socket to send on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initiates rekeying by sending REKEY_REQUEST with new ephemeral public key.
 * Generates new ephemeral key pair and stores in temp_* fields.
 *
 * @note Packet format: [new_ephemeral_public_key:32] (unencrypted)
 *
 * @note New keys: Generated via crypto_rekey_init() and stored in temp_* fields.
 *       Keys are NOT active until crypto_rekey_commit() is called.
 *
 * @note Should be called after crypto_handshake_should_rekey() returns true.
 *
 * @warning Handshake must be complete before calling this function.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_rekey_request(crypto_handshake_context_t *ctx, socket_t socket);

/**
 * @brief Send REKEY_RESPONSE packet (responder side)
 * @param ctx Crypto handshake context (must be ready)
 * @param socket Socket to send on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Responds to REKEY_REQUEST by sending REKEY_RESPONSE with new ephemeral public key.
 * Generates new ephemeral key pair and computes new shared secret.
 *
 * @note Packet format: [new_ephemeral_public_key:32] (unencrypted)
 *
 * @note New keys: Generated via crypto_rekey_process_request() and stored in temp_* fields.
 *       Computes temp_shared_key using peer's new public key.
 *       Keys are NOT active until crypto_rekey_commit() is called.
 *
 * @warning Must have processed REKEY_REQUEST before calling this function.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_rekey_response(crypto_handshake_context_t *ctx, socket_t socket);

/**
 * @brief Send REKEY_COMPLETE packet (initiator side)
 * @param ctx Crypto handshake context (must be ready)
 * @param socket Socket to send on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Completes rekeying by sending REKEY_COMPLETE encrypted with NEW shared secret.
 * CRITICAL: This packet proves that both sides have computed the same shared secret.
 *
 * @note Packet format: [empty_payload:0] encrypted with NEW shared secret
 *
 * @note Encryption: Uses temp_shared_key (not old shared_key) to encrypt empty payload.
 *       If peer can decrypt this, it proves both sides computed the same new shared secret.
 *
 * @warning Must have processed REKEY_RESPONSE before calling this function.
 * @warning This packet MUST be encrypted with temp_shared_key, not old shared_key.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_rekey_complete(crypto_handshake_context_t *ctx, socket_t socket);

/**
 * @brief Process received REKEY_REQUEST packet (responder side)
 * @param ctx Crypto handshake context (must be ready)
 * @param packet Packet payload (32-byte public key)
 * @param packet_len Packet length (should be 32)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Processes peer's REKEY_REQUEST packet and generates new ephemeral keys.
 * Extracts peer's new ephemeral public key and computes new shared secret.
 *
 * @note Packet format: [peer_new_ephemeral_public_key:32]
 *       Expected size: 32 bytes (X25519 public key size)
 *
 * @note New keys: Generated via crypto_rekey_process_request() and stored in temp_* fields.
 *       Computes temp_shared_key using peer's new public key.
 *
 * @warning After this call, should send REKEY_RESPONSE with our new public key.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_process_rekey_request(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                         size_t packet_len);

/**
 * @brief Process received REKEY_RESPONSE packet (initiator side)
 * @param ctx Crypto handshake context (must be ready)
 * @param packet Packet payload (32-byte public key)
 * @param packet_len Packet length (should be 32)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Processes peer's REKEY_RESPONSE packet and computes new shared secret.
 * Extracts peer's new ephemeral public key and computes temp_shared_key.
 *
 * @note Packet format: [peer_new_ephemeral_public_key:32]
 *       Expected size: 32 bytes (X25519 public key size)
 *
 * @note New shared secret: Computed via crypto_rekey_process_response() and stored in temp_shared_key.
 *
 * @warning After this call, should send REKEY_COMPLETE encrypted with NEW key (temp_shared_key).
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_process_rekey_response(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                          size_t packet_len);

/**
 * @brief Process received REKEY_COMPLETE packet (responder side)
 * @param ctx Crypto handshake context (must be ready)
 * @param packet Encrypted packet (empty payload, encrypted with NEW key)
 * @param packet_len Packet length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Processes peer's REKEY_COMPLETE packet and verifies it decrypts with new shared secret.
 * If successful, commits to the new key by replacing old shared_key with temp_shared_key.
 *
 * @note Packet format: [encrypted_empty_payload] encrypted with temp_shared_key
 *
 * @note Verification: Attempts to decrypt with temp_shared_key. If decryption succeeds,
 *       both sides have computed the same new shared secret. Commits to new key.
 *
 * @note Key commitment: Calls crypto_rekey_commit() to replace shared_key with temp_shared_key.
 *       Old keys remain active until this point (no service interruption).
 *
 * @warning Only call after processing REKEY_RESPONSE. Returns error if decryption fails.
 *
 * @ingroup handshake
 */
asciichat_error_t crypto_handshake_process_rekey_complete(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                          size_t packet_len);

/**
 * @brief Check if rekeying should be triggered for this handshake context
 * @param ctx Crypto handshake context
 * @return true if rekey should be initiated, false otherwise
 *
 * Wrapper around crypto_should_rekey() for handshake context.
 * Checks time threshold and packet count threshold.
 *
 * @note Should be called after each packet encryption.
 *
 * @note Test environment: Automatically uses test thresholds if CRITERION_TEST or TESTING env var is set.
 *
 * @ingroup handshake
 */
bool crypto_handshake_should_rekey(const crypto_handshake_context_t *ctx);

/** @} */

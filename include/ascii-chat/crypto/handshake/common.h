#pragma once

/**
 * @file crypto/handshake/common.h
 * @brief Common declarations and data structures for cryptographic handshake
 * @ingroup handshake
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../platform/socket.h"
#include "../../crypto/crypto.h"
#include "../../crypto/keys.h"
#include "../../network/packet.h"

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
  char client_gpg_key_id[41]; /**< Client's GPG key ID (8/16/40 hex chars + null terminator, for server verification) */

  /** Connection info for known_hosts */
  char server_hostname[256]; /**< Server hostname (user-provided) */
  char server_ip[256];       /**< Server IP address (resolved from connection) */
  uint16_t server_port;      /**< Server port */

  /** Authentication configuration */
  bool verify_server_key;                          /**< Client: verify server key against known_hosts */
  bool require_client_auth;                        /**< Server: require client authentication (whitelist) */
  bool server_uses_client_auth;                    /**< Client: whether server requested client authentication */
  char client_keys_path[PLATFORM_MAX_PATH_LENGTH]; /**< Server: client keys file path (whitelist) */

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
 * @name Common Handshake Functions
 * @{
 */

/**
 * @brief Initialize crypto handshake context
 * @param ctx Handshake context to initialize (must not be NULL)
 * @param is_server True if this is the server side, false for client
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_init(crypto_handshake_context_t *ctx, bool is_server);

/**
 * @brief Set crypto parameters from crypto_parameters_packet_t
 * @param ctx Handshake context
 * @param params Negotiated crypto parameters (from capabilities negotiation)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_set_parameters(crypto_handshake_context_t *ctx,
                                                  const crypto_parameters_packet_t *params);

/**
 * @brief Validate crypto packet size based on session parameters
 * @param ctx Handshake context (must have parameters set)
 * @param packet_type Packet type to validate
 * @param packet_size Actual packet size received
 * @return ASCIICHAT_OK if valid, error code on failure
 */
asciichat_error_t crypto_handshake_validate_packet_size(const crypto_handshake_context_t *ctx, uint16_t packet_type,
                                                        size_t packet_size);

/**
 * @brief Initialize crypto handshake context with password authentication
 * @param ctx Handshake context to initialize (must not be NULL)
 * @param is_server True if this is the server side, false for client
 * @param password Password for authentication (must meet length requirements)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_init_with_password(crypto_handshake_context_t *ctx, bool is_server,
                                                      const char *password);

/**
 * @brief Cleanup crypto handshake context with secure memory wiping
 * @param ctx Handshake context to cleanup
 */
void crypto_handshake_destroy(crypto_handshake_context_t *ctx);

/**
 * @brief Check if handshake is complete and encryption is ready
 * @param ctx Handshake context
 * @return true if handshake is complete and encryption is ready, false otherwise
 */
bool crypto_handshake_is_ready(const crypto_handshake_context_t *ctx);

/**
 * @brief Get the crypto context for encryption/decryption
 * @param ctx Handshake context
 * @return Pointer to crypto context, or NULL if ctx is NULL
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
 */
asciichat_error_t crypto_decrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                                       const uint8_t *ciphertext, size_t ciphertext_len,
                                                       uint8_t *plaintext, size_t plaintext_size,
                                                       size_t *plaintext_len);

/**
 * @name Session Rekeying Functions
 * @{
 */

/**
 * @brief Send REKEY_REQUEST packet (initiator side)
 * @param ctx Crypto handshake context (must be ready)
 * @param socket Socket to send on
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_rekey_request(crypto_handshake_context_t *ctx, socket_t socket);

/**
 * @brief Send REKEY_RESPONSE packet (responder side)
 * @param ctx Crypto handshake context (must be ready)
 * @param socket Socket to send on
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_rekey_response(crypto_handshake_context_t *ctx, socket_t socket);

/**
 * @brief Send REKEY_COMPLETE packet (initiator side)
 * @param ctx Crypto handshake context (must be ready)
 * @param socket Socket to send on
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_rekey_complete(crypto_handshake_context_t *ctx, socket_t socket);

/**
 * @brief Process received REKEY_REQUEST packet (responder side)
 * @param ctx Crypto handshake context (must be ready)
 * @param packet Packet payload (32-byte public key)
 * @param packet_len Packet length (should be 32)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_process_rekey_request(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                         size_t packet_len);

/**
 * @brief Process received REKEY_RESPONSE packet (initiator side)
 * @param ctx Crypto handshake context (must be ready)
 * @param packet Packet payload (32-byte public key)
 * @param packet_len Packet length (should be 32)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_process_rekey_response(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                          size_t packet_len);

/**
 * @brief Process received REKEY_COMPLETE packet (responder side)
 * @param ctx Crypto handshake context (must be ready)
 * @param packet Encrypted packet (empty payload, encrypted with NEW key)
 * @param packet_len Packet length
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t crypto_handshake_process_rekey_complete(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                          size_t packet_len);

/**
 * @brief Check if rekeying should be triggered for this handshake context
 * @param ctx Crypto handshake context
 * @return true if rekey should be initiated, false otherwise
 */
bool crypto_handshake_should_rekey(const crypto_handshake_context_t *ctx);

/** @} */

/** @} */

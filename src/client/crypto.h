/**
 * @file client/crypto.h
 * @ingroup client_crypto
 * @brief ascii-chat Client Cryptography Handler Interface
 *
 * Provides client-side cryptographic handshake coordination, key loading,
 * and encryption context management for secure server communication.
 *
 * ## Cryptographic Protocol
 *
 * **Key Exchange**: X25519 elliptic curve Diffie-Hellman (ephemeral key pairs)
 * **Encryption**: XSalsa20-Poly1305 AEAD cipher (authenticated encryption)
 * **Authentication**: Ed25519 digital signatures (optional, can be passwordless)
 * **KDF**: Argon2i (password-based key derivation, if using password auth)
 *
 * ## Authentication Modes
 *
 * 1. **Ephemeral DH** (default): Encrypted but no identity verification
 * 2. **Password Authentication**: Shared password + PBKDF → session key
 * 3. **SSH Key Authentication**: Ed25519 signatures + known_hosts verification
 * 4. **Passwordless ECDH**: Encryption only, no authentication
 *
 * ## Handshake Flow
 *
 * @code
 * Client                                  Server
 *   |------ CLIENT_HELLO (pubkey) ------->|
 *   |                                      |
 *   |<----- SERVER_HELLO (pubkey) --------|
 *   |                                      |
 *   | [Derive shared secret via ECDH]      |
 *   |                                      |
 *   |<---- SERVER_AUTH (signature) -------|
 *   |                                      |
 *   |--- CLIENT_AUTH (signature/pass) --->|
 *   |                                      |
 *   |<---- SERVER_READY (encrypted) ------|
 *   |                                      |
 *   | [Encryption ready for payload]      |
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 *
 * @see topic_client_crypto "Cryptography Details"
 * @see topic_client_connection "Connection Management (which calls handshake)"
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/platform/socket.h>

/**
 * @brief Set crypto mode for handshake (encryption + authentication)
 *
 * Sets the crypto mode bitmask (ACIP_CRYPTO_*) to negotiate with server.
 * Determines which security levels are requested in the handshake.
 * Must be called before client_crypto_init().
 *
 * **Supported Modes**:
 * - `ACIP_CRYPTO_NONE`: No encryption (insecure, testing only)
 * - `ACIP_CRYPTO_ENCRYPT`: Encryption only, no authentication
 * - `ACIP_CRYPTO_AUTH`: Authentication only (unusual)
 * - `ACIP_CRYPTO_FULL`: Both encryption AND authentication (recommended)
 *
 * @param mode Crypto mode bitmask to request
 *
 * @note The final negotiated mode is the intersection of client and server requests.
 *       Both must agree on encryption level or handshake fails.
 *
 * @ingroup client_crypto
 */
void client_crypto_set_mode(uint8_t mode);

/**
 * @brief Initialize client crypto handshake
 *
 * Sets up the client-side cryptographic context with authentication
 * credentials and generates ephemeral key pair for key exchange.
 *
 * **Options affecting initialization**:
 * - `--client-key <path>`: SSH private key for Ed25519 authentication
 * - `--server-key <path|url>`: Server public key for verification
 * - `--password <pass>`: Password for authentication (stored in options)
 *
 * **Supported Key Types**:
 * - OpenSSH Ed25519 private keys (encrypted or plaintext)
 * - OpenSSH RSA private keys (converted to Ed25519)
 * - GitHub/GitLab user keys (fetched via HTTPS)
 * - Agent-based keys (ssh-agent or gpg-agent)
 *
 * **Key Decryption**:
 * - Encrypted OpenSSH keys: Uses bcrypt_pbkdf + AES-256-CTR
 * - Environment variable: `$ASCII_CHAT_KEY_PASSWORD`
 * - SSH agent: `$SSH_AUTH_SOCK` (automatic fallback)
 * - GPG agent: `$GPG_AGENT_INFO` (automatic fallback)
 *
 * @return 0 on success, -1 on failure:
 *         - Key file not found
 *         - Key decryption failed (wrong password)
 *         - Invalid key format
 *
 * @note This is called before the handshake, during connection setup.
 *       Failures here are fatal and non-retryable.
 *
 * @ingroup client_crypto
 *
 * @see client_crypto_handshake "Perform actual handshake"
 */
int client_crypto_init(void);

/**
 * @brief Perform crypto handshake with server
 *
 * Executes the complete cryptographic handshake protocol with the server,
 * including protocol negotiation, key exchange, and authentication.
 *
 * **Handshake Steps**:
 * 1. Send CLIENT_HELLO with ephemeral public key
 * 2. Receive SERVER_HELLO with server's ephemeral public key
 * 3. Derive shared secret using ECDH (both sides)
 * 4. Receive SERVER_AUTH (server's identity proof)
 * 5. Verify server's signature against known_hosts or provided key
 * 6. Send CLIENT_AUTH (client's identity proof or password)
 * 7. Server verifies client authentication
 * 8. Encryption keys ready for payload exchange
 *
 * **Error Handling**:
 * - **Protocol Error**: Invalid format, timeout, socket error → CONNECTION_ERROR_GENERIC
 * - **Auth Failure**: Invalid password, wrong signature → CONNECTION_ERROR_AUTH_FAILED
 * - **Host Key Failure**: Server key not in known_hosts → CONNECTION_ERROR_HOST_KEY_FAILED
 *
 * **Retryability**:
 * - `CONNECTION_ERROR_GENERIC`: Retryable (network issue)
 * - `CONNECTION_ERROR_AUTH_FAILED`: Non-retryable (wrong password/key)
 * - `CONNECTION_ERROR_HOST_KEY_FAILED`: Non-retryable (need known_hosts update)
 *
 * @param socket Connected TCP socket to server (must be connected before calling)
 * @return Connection status:
 *         - 0: Handshake successful, encryption ready
 *         - CONNECTION_ERROR_GENERIC: Network or protocol error (retryable)
 *         - CONNECTION_ERROR_AUTH_FAILED: Authentication failed (non-retryable)
 *         - CONNECTION_ERROR_HOST_KEY_FAILED: Host key verification failed (non-retryable)
 *
 * @note Called by server_connection_establish() after TCP connection succeeds.
 *       If this fails with AUTH_FAILED or HOST_KEY_FAILED, the connection attempt
 *       is abandoned and the main thread does NOT retry.
 *
 * @ingroup client_crypto
 *
 * @see client_crypto_init "Initialize crypto context"
 * @see server_connection_establish "Which calls this handshake"
 */
int client_crypto_handshake(socket_t socket);

/**
 * @brief Check if crypto handshake is ready
 *
 * @return true if encryption is ready, false otherwise
 *
 * @ingroup client_crypto
 */
bool crypto_client_is_ready(void);

/**
 * @brief Get crypto context for encryption/decryption
 *
 * @return crypto context or NULL if not ready
 *
 * @ingroup client_crypto
 */
const crypto_context_t *crypto_client_get_context(void);

/**
 * @brief Encrypt a packet for transmission
 *
 * @param plaintext Plaintext data to encrypt
 * @param plaintext_len Length of plaintext data
 * @param ciphertext Output buffer for encrypted data
 * @param ciphertext_size Size of output buffer
 * @param ciphertext_len Output length of encrypted data
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_encrypt_packet(const uint8_t *plaintext, size_t plaintext_len, uint8_t *ciphertext,
                                 size_t ciphertext_size, size_t *ciphertext_len);

/**
 * @brief Decrypt a received packet
 *
 * @param ciphertext Encrypted data to decrypt
 * @param ciphertext_len Length of encrypted data
 * @param plaintext Output buffer for decrypted data
 * @param plaintext_size Size of output buffer
 * @param plaintext_len Output length of decrypted data
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext,
                                 size_t plaintext_size, size_t *plaintext_len);

/**
 * @brief Cleanup crypto client resources
 *
 * @ingroup client_crypto
 */
void crypto_client_cleanup(void);

/**
 * @brief Check if session rekeying should be triggered
 *
 * @return true if rekey should be initiated, false otherwise
 *
 * @ingroup client_crypto
 */
bool crypto_client_should_rekey(void);

/**
 * @brief Initiate session rekeying (client-initiated)
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_initiate_rekey(void);

/**
 * @brief Process received REKEY_REQUEST packet from server
 *
 * @param packet Packet data
 * @param packet_len Packet length
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_process_rekey_request(const uint8_t *packet, size_t packet_len);

/**
 * @brief Send REKEY_RESPONSE packet to server
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_send_rekey_response(void);

/**
 * @brief Process received REKEY_RESPONSE packet from server
 *
 * @param packet Packet data
 * @param packet_len Packet length
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_process_rekey_response(const uint8_t *packet, size_t packet_len);

/**
 * @brief Send REKEY_COMPLETE packet to server and commit to new key
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int crypto_client_send_rekey_complete(void);

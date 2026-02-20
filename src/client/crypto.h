/**
 * @file client/crypto.h
 * @ingroup client_crypto
 * @brief ascii-chat Client Cryptography Handler Interface
 *
 * Provides client-side cryptographic handshake coordination, key loading,
 * and encryption context management for secure server communication.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/network/acip/transport.h>

/**
 * @brief Set crypto mode for handshake (encryption + authentication)
 *
 * Sets the crypto mode bitmask (ACIP_CRYPTO_*) to negotiate with server.
 * Must be called before client_crypto_init().
 *
 * @param mode Crypto mode bitmask (ACIP_CRYPTO_NONE, ACIP_CRYPTO_ENCRYPT, ACIP_CRYPTO_AUTH, ACIP_CRYPTO_FULL)
 *
 * @ingroup client_crypto
 */
void client_crypto_set_mode(uint8_t mode);

/**
 * @brief Initialize client crypto handshake
 *
 * Sets up the client-side cryptographic context with authentication
 * credentials. Supports SSH key, password, and passwordless modes.
 *
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int client_crypto_init(void);

/**
 * @brief Perform crypto handshake with server
 *
 * Executes the complete cryptographic handshake protocol with the server,
 * including protocol negotiation, key exchange, and authentication.
 *
 * @param socket Connected socket to server
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int client_crypto_handshake(socket_t socket);

/**
 * @brief Perform initial crypto handshake phase on transport
 *
 * Performs the protocol negotiation phase (protocol version + crypto capabilities)
 * on a WebSocket or other transport-based connection. This establishes the baseline
 * for encrypted communication on non-socket transports.
 *
 * After this completes, the remaining handshake phases will proceed through the
 * normal protocol packet handling layer.
 *
 * @param transport ACIP transport (e.g., WebSocket) for handshake
 * @return 0 on success, -1 on failure
 *
 * @ingroup client_crypto
 */
int client_crypto_handshake_initial_phase_with_transport(acip_transport_t *transport);

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

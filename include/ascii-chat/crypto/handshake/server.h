#pragma once

/**
 * @file crypto/handshake/server.h
 * @brief Server-side handshake functions
 * @ingroup handshake
 */

#include "../../common.h"
#include "../../crypto/handshake/common.h"
#include "../../network/acip/transport.h"

/**
 * @name Server Handshake Protocol
 * @{
 */

/**
 * @brief Server: Start crypto handshake by sending public key
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_INIT state)
 * @param transport ACIP transport to send on
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
 */
asciichat_error_t crypto_handshake_server_start(crypto_handshake_context_t *ctx, acip_transport_t *transport);

/**
 * @brief Server: Send CRYPTO_PARAMETERS and set handshake context sizes
 * @param ctx Handshake context
 * @param transport ACIP transport to send on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Must be called before crypto_handshake_server_start(). Builds crypto
 * parameters based on server state (identity key presence), sends them
 * to the client, and updates context sizes to match.
 *
 * TCP server path calls this indirectly via its own negotiation in
 * src/server/crypto.c (which customizes parameters based on client
 * capabilities). WebSocket and discovery-service paths call this directly.
 */
asciichat_error_t crypto_handshake_server_send_parameters(crypto_handshake_context_t *ctx, acip_transport_t *transport);

/**
 * @brief Server: Process client's public key and send auth challenge
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_KEY_EXCHANGE state)
 * @param transport ACIP transport to send on
 * @param packet_type Packet type received (should be PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP or
 * PACKET_TYPE_CRYPTO_NO_ENCRYPTION)
 * @param payload Received packet payload
 * @param payload_len Length of received payload
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Server processes client's KEY_EXCHANGE_RESP packet and sends AUTH_CHALLENGE.
 * Computes shared secret and generates authentication challenge nonce.
 *
 * @note Packet must be received by ACIP handler before calling this function
 * @note State transition: CRYPTO_HANDSHAKE_KEY_EXCHANGE -> CRYPTO_HANDSHAKE_AUTHENTICATING
 */
asciichat_error_t crypto_handshake_server_auth_challenge(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                         packet_type_t packet_type, const uint8_t *payload,
                                                         size_t payload_len);

/**
 * @brief Server: Process auth response and complete handshake
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_AUTHENTICATING state)
 * @param transport ACIP transport to send on
 * @param packet_type Packet type received (should be PACKET_TYPE_CRYPTO_AUTH_RESPONSE)
 * @param payload Received packet payload
 * @param payload_len Length of received payload
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Server processes client's AUTH_RESPONSE packet and sends SERVER_AUTH_RESP.
 * Verifies authentication HMAC or signature and completes handshake.
 *
 * @note Packet must be received by ACIP handler before calling this function
 * @note State transition: CRYPTO_HANDSHAKE_AUTHENTICATING -> CRYPTO_HANDSHAKE_READY
 */
asciichat_error_t crypto_handshake_server_complete(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                   packet_type_t packet_type, const uint8_t *payload,
                                                   size_t payload_len);

/** @} */

#pragma once

/**
 * @file crypto/handshake/server.h
 * @brief Server-side handshake functions
 * @ingroup handshake
 */

#include "core/common.h"

/**
 * @name Server Handshake Protocol
 * @{
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
 */
asciichat_error_t crypto_handshake_server_start(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Server: Process client's public key and send auth challenge
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_KEY_EXCHANGE state)
 * @param client_socket Socket to send/receive on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Server processes client's KEY_EXCHANGE_RESP packet and sends AUTH_CHALLENGE.
 * Computes shared secret and generates authentication challenge nonce.
 *
 * @note State transition: CRYPTO_HANDSHAKE_KEY_EXCHANGE -> CRYPTO_HANDSHAKE_AUTHENTICATING
 */
asciichat_error_t crypto_handshake_server_auth_challenge(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Server: Process auth response and complete handshake
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_AUTHENTICATING state)
 * @param client_socket Socket to send/receive on
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Server processes client's AUTH_RESPONSE packet and sends HANDSHAKE_COMPLETE.
 * Verifies authentication HMAC or signature and completes handshake.
 *
 * @note State transition: CRYPTO_HANDSHAKE_AUTHENTICATING -> CRYPTO_HANDSHAKE_READY
 */
asciichat_error_t crypto_handshake_server_complete(crypto_handshake_context_t *ctx, socket_t client_socket);

/** @} */

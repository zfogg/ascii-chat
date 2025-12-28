#pragma once

/**
 * @file crypto/handshake/server.h
 * @brief Server-side cryptographic handshake functions
 * @ingroup handshake
 * @addtogroup handshake
 * @{
 *
 * This header provides server-specific handshake functions that implement
 * the server side of the cryptographic key exchange and authentication protocol.
 *
 * Server handshake flow:
 * 1. crypto_handshake_server_start() - Send ephemeral key to client
 * 2. crypto_handshake_server_auth_challenge() - Receive client key, send auth challenge
 * 3. crypto_handshake_server_complete() - Verify auth response, complete handshake
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include "common.h"
#include "platform/socket.h"

/**
 * @name Server Handshake Protocol Flow
 * @{
 *
 * Handshake functions that implement the server side of the cryptographic handshake protocol.
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

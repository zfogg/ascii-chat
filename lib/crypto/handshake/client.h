#pragma once

/**
 * @file crypto/handshake/client.h
 * @brief Client-side cryptographic handshake functions
 * @ingroup handshake
 * @addtogroup handshake
 * @{
 *
 * This header provides client-specific handshake functions that implement
 * the client side of the cryptographic key exchange and authentication protocol.
 *
 * Client handshake flow:
 * 1. crypto_handshake_client_key_exchange() - Receive server key, send our key
 * 2. crypto_handshake_client_auth_response() - Receive auth challenge, send response
 * 3. crypto_handshake_client_complete() - Receive handshake complete confirmation
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include "common.h"
#include "platform/socket.h"

/**
 * @name Client Handshake Protocol Flow
 * @{
 *
 * Handshake functions that implement the client side of the cryptographic handshake protocol.
 */

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

/** @} */

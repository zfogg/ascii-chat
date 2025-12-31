#pragma once

/**
 * @file crypto/handshake/client.h
 * @brief Client-side handshake functions
 * @ingroup handshake
 */

#include "common.h"

/**
 * @name Client Handshake Protocol
 * @{
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
 * @note State transition: CRYPTO_HANDSHAKE_INIT -> CRYPTO_HANDSHAKE_KEY_EXCHANGE
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
 * @note State transition: CRYPTO_HANDSHAKE_KEY_EXCHANGE -> CRYPTO_HANDSHAKE_AUTHENTICATING
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
 * @note State transition: CRYPTO_HANDSHAKE_AUTHENTICATING -> CRYPTO_HANDSHAKE_READY
 */
asciichat_error_t crypto_handshake_client_complete(crypto_handshake_context_t *ctx, socket_t client_socket);

/** @} */

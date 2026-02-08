#pragma once

/**
 * @file crypto/handshake/client.h
 * @brief Client-side handshake functions
 * @ingroup handshake
 */

#include "../../common.h"
#include "../../crypto/handshake/common.h"
#include "../../network/acip/transport.h"

/**
 * @name Client Handshake Protocol
 * @{
 */

/**
 * @brief Client: Process server's public key and send our public key
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_INIT state)
 * @param transport ACIP transport to send on
 * @param packet_type Packet type received (should be PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT)
 * @param payload Received packet payload
 * @param payload_len Length of received payload
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Client processes server's KEY_EXCHANGE_INIT packet and responds with KEY_EXCHANGE_RESP.
 * Supports both simple and authenticated formats. Verifies server signature if present.
 *
 * @note Packet must be received by ACIP handler before calling this function
 * @note State transition: CRYPTO_HANDSHAKE_INIT -> CRYPTO_HANDSHAKE_KEY_EXCHANGE
 */
asciichat_error_t crypto_handshake_client_key_exchange(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                       packet_type_t packet_type, const uint8_t *payload,
                                                       size_t payload_len);

/**
 * @brief Client: Process auth challenge and send response
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_KEY_EXCHANGE state)
 * @param transport ACIP transport to send on
 * @param packet_type Packet type received (should be PACKET_TYPE_CRYPTO_AUTH_CHALLENGE or
 * PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE)
 * @param payload Received packet payload
 * @param payload_len Length of received payload
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Client processes server's AUTH_CHALLENGE packet and sends AUTH_RESPONSE.
 * Generates HMAC bound to shared secret using password or client key.
 *
 * @note Packet must be received by ACIP handler before calling this function
 * @note State transition: CRYPTO_HANDSHAKE_KEY_EXCHANGE -> CRYPTO_HANDSHAKE_AUTHENTICATING
 */
asciichat_error_t crypto_handshake_client_auth_response(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                        packet_type_t packet_type, const uint8_t *payload,
                                                        size_t payload_len);

/**
 * @brief Client: Wait for handshake complete confirmation
 * @param ctx Handshake context (must be in CRYPTO_HANDSHAKE_AUTHENTICATING state)
 * @param transport ACIP transport (unused, for consistency)
 * @param packet_type Packet type received (should be PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP or
 * PACKET_TYPE_CRYPTO_AUTH_FAILED)
 * @param payload Received packet payload
 * @param payload_len Length of received payload
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Client processes server's SERVER_AUTH_RESP or AUTH_FAILED packet.
 * After receiving, handshake is complete and encryption is ready.
 *
 * @note Packet must be received by ACIP handler before calling this function
 * @note State transition: CRYPTO_HANDSHAKE_AUTHENTICATING -> CRYPTO_HANDSHAKE_READY
 */
asciichat_error_t crypto_handshake_client_complete(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                   packet_type_t packet_type, const uint8_t *payload,
                                                   size_t payload_len);

/**
 * @name Legacy TCP Socket Wrappers (Backward Compatibility)
 * @{
 *
 * These wrappers maintain the old socket-based interface for TCP clients.
 * They will be removed in Phase 5 when TCP client code is refactored.
 */

/**
 * @brief Legacy wrapper: Key exchange using socket (TCP clients only)
 * @deprecated Use crypto_handshake_client_key_exchange() with acip_transport_t instead
 */
asciichat_error_t crypto_handshake_client_key_exchange_socket(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Legacy wrapper: Auth response using socket (TCP clients only)
 * @deprecated Use crypto_handshake_client_auth_response() with acip_transport_t instead
 */
asciichat_error_t crypto_handshake_client_auth_response_socket(crypto_handshake_context_t *ctx, socket_t client_socket);

/**
 * @brief Legacy wrapper: Complete handshake using socket (TCP clients only)
 * @deprecated Use crypto_handshake_client_complete() with acip_transport_t instead
 */
asciichat_error_t crypto_handshake_client_complete_socket(crypto_handshake_context_t *ctx, socket_t client_socket);

/** @} */

/** @} */

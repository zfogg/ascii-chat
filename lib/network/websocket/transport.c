/**
 * @file network/acip/transport_websocket.c
 * @brief WebSocket transport implementation for ACIP protocol
 *
 * FUTURE IMPLEMENTATION - WebSocket support for browser clients.
 *
 * This will allow browsers to connect to ascii-chat servers using
 * WebSocket protocol, enabling web-based clients.
 *
 * IMPLEMENTATION NOTES:
 * ====================
 * - WebSocket handshake must be completed before creating transport
 * - Frame protocol: opcode, masking, fragmentation
 * - Text frames for JSON, binary frames for ACIP packets
 * - Ping/pong for keepalive
 * - Close handshake for clean disconnection
 *
 * DEPENDENCIES:
 * =============
 * - lib/network/websockets/ for frame protocol implementation
 * - HTTP upgrade handshake handling
 * - Base64 encoding for Sec-WebSocket-Accept
 * - SHA-1 for handshake validation
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/log/logging.h>

acip_transport_t *acip_websocket_transport_create(socket_t sockfd, crypto_context_t *crypto_ctx) {
  (void)sockfd;
  (void)crypto_ctx;

  log_error("WebSocket transport not yet implemented");
  SET_ERRNO(ERROR_INTERNAL, "WebSocket transport is not yet implemented");

  return NULL;
}

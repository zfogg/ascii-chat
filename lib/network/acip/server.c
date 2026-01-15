/**
 * @file network/acip/server.c
 * @brief ACIP server-side protocol implementation
 * @ingroup acip
 *
 * Server-side packet sending and receiving for ascii-chat protocol.
 * Handles server→client communication (ASCII frames, state updates, console control).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "network/acip/server.h"
#include "network/acip/send.h"
#include "network/acip/handlers.h"
#include "network/packet.h"
#include "buffer_pool.h"
#include "log/logging.h"
#include "common.h"
#include "util/endian.h"
#include "util/overflow.h"
#include "network/crc32.h"
#include <string.h>

// =============================================================================
// Server Receive (from client)
// =============================================================================

asciichat_error_t acip_server_receive_and_dispatch(acip_transport_t *transport, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }

  // Get socket from transport for low-level packet reception
  socket_t sock = transport->methods->get_socket(transport);
  if (sock == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_NETWORK, "Transport has no valid socket");
  }

  // Check if transport is connected
  if (!transport->methods->is_connected(transport)) {
    return SET_ERRNO(ERROR_NETWORK, "Transport not connected");
  }

  // Receive packet with automatic decryption
  packet_envelope_t envelope;
  bool enforce_encryption = (transport->crypto_ctx != NULL);
  packet_recv_result_t result = receive_packet_secure(sock, transport->crypto_ctx, enforce_encryption, &envelope);

  // Handle receive errors
  if (result != PACKET_RECV_SUCCESS) {
    if (result == PACKET_RECV_EOF) {
      return SET_ERRNO(ERROR_NETWORK, "Connection closed (EOF)");
    } else if (result == PACKET_RECV_SECURITY_VIOLATION) {
      return SET_ERRNO(ERROR_CRYPTO, "Security violation: unencrypted packet when encryption required");
    } else {
      return SET_ERRNO(ERROR_NETWORK, "Failed to receive packet");
    }
  }

  // Dispatch packet to appropriate ACIP handler
  asciichat_error_t dispatch_result =
      acip_handle_server_packet(transport, envelope.type, envelope.data, envelope.len, client_ctx, callbacks);

  // Always free the allocated buffer (even if handler failed)
  if (envelope.allocated_buffer) {
    buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
  }

  // Return handler result
  return dispatch_result;
}

// =============================================================================
// Server Send (to client)
// =============================================================================

asciichat_error_t acip_send_ascii_frame(acip_transport_t *transport, const char *frame_data, size_t frame_size,
                                        uint32_t width, uint32_t height) {
  if (!transport || !frame_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or frame_data");
  }

  if (frame_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty frame data");
  }

  // Calculate CRC32 checksum of frame data for integrity verification
  uint32_t checksum_value = asciichat_crc32(frame_data, frame_size);

  // Create ASCII frame packet header
  ascii_frame_packet_t header;
  header.width = HOST_TO_NET_U32(width);
  header.height = HOST_TO_NET_U32(height);
  header.original_size = HOST_TO_NET_U32((uint32_t)frame_size);
  header.compressed_size = 0;
  header.checksum = HOST_TO_NET_U32(checksum_value);
  header.flags = 0;

  // Calculate total packet size
  size_t total_size;
  if (checked_size_add(sizeof(header), frame_size, &total_size) != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Packet size overflow");
  }

  // Allocate buffer
  uint8_t *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer: %zu bytes", total_size);
  }

  // Build packet: header + data
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), frame_data, frame_size);

  // Send via transport
  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_ASCII_FRAME, buffer, total_size);

  buffer_pool_free(NULL, buffer, total_size);
  return result;
}

asciichat_error_t acip_send_clear_console(acip_transport_t *transport) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0);
}

asciichat_error_t acip_send_server_state(acip_transport_t *transport, const server_state_packet_t *state) {
  if (!transport || !state) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or state");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_SERVER_STATE, state, sizeof(*state));
}

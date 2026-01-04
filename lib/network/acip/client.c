/**
 * @file network/acip/client.c
 * @brief ACIP client-side protocol implementation
 * @ingroup acip
 *
 * Client-side packet sending and receiving for ascii-chat protocol.
 * Handles client→server communication (image frames, join/leave, streaming control).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "network/acip/client.h"
#include "network/acip/send.h"
#include "network/acip/handlers.h"
#include "network/packet.h"
#include "buffer_pool.h"
#include "log/logging.h"
#include "common.h"
#include "util/endian.h"
#include "util/overflow.h"
#include <string.h>

// =============================================================================
// Client Receive (from server)
// =============================================================================

asciichat_error_t acip_client_receive_and_dispatch(acip_transport_t *transport,
                                                   const acip_client_callbacks_t *callbacks) {
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
      acip_handle_client_packet(transport, envelope.type, envelope.data, envelope.len, callbacks);

  // Always free the allocated buffer (even if handler failed)
  if (envelope.allocated_buffer) {
    buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
  }

  // Return handler result
  return dispatch_result;
}

// =============================================================================
// Client Send (to server)
// =============================================================================

asciichat_error_t acip_send_image_frame(acip_transport_t *transport, const void *pixel_data, uint32_t width,
                                        uint32_t height, uint32_t pixel_format) {
  if (!transport || !pixel_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or pixel_data");
  }

  // Calculate pixel data size (3 bytes per pixel for RGB24)
  size_t pixel_size = (size_t)width * height * 3;

  // Create image frame packet header
  image_frame_packet_t header;
  header.width = HOST_TO_NET_U32(width);
  header.height = HOST_TO_NET_U32(height);
  header.pixel_format = HOST_TO_NET_U32(pixel_format);
  header.compressed_size = 0;
  header.checksum = 0;
  header.timestamp = 0;

  // Calculate total size
  size_t total_size;
  if (checked_size_add(sizeof(header), pixel_size, &total_size) != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Packet size overflow");
  }

  // Allocate buffer
  uint8_t *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer: %zu bytes", total_size);
  }

  // Build packet
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), pixel_data, pixel_size);

  // Send via transport
  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_IMAGE_FRAME, buffer, total_size);

  buffer_pool_free(NULL, buffer, total_size);
  return result;
}

asciichat_error_t acip_send_client_join(acip_transport_t *transport, uint8_t capabilities) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Simple capability byte payload
  return packet_send_via_transport(transport, PACKET_TYPE_CLIENT_JOIN, &capabilities, sizeof(capabilities));
}

asciichat_error_t acip_send_client_leave(acip_transport_t *transport) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Leave has no payload
  return packet_send_via_transport(transport, PACKET_TYPE_CLIENT_LEAVE, NULL, 0);
}

asciichat_error_t acip_send_stream_start(acip_transport_t *transport, uint8_t stream_types) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Server expects uint32_t (4 bytes), not uint8_t
  uint32_t stream_types_net = HOST_TO_NET_U32((uint32_t)stream_types);
  return packet_send_via_transport(transport, PACKET_TYPE_STREAM_START, &stream_types_net, sizeof(stream_types_net));
}

asciichat_error_t acip_send_stream_stop(acip_transport_t *transport, uint8_t stream_types) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Server expects uint32_t (4 bytes), not uint8_t
  uint32_t stream_types_net = HOST_TO_NET_U32((uint32_t)stream_types);
  return packet_send_via_transport(transport, PACKET_TYPE_STREAM_STOP, &stream_types_net, sizeof(stream_types_net));
}

asciichat_error_t acip_send_capabilities(acip_transport_t *transport, const void *cap_data, size_t cap_len) {
  if (!transport || !cap_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or cap_data");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_CLIENT_CAPABILITIES, cap_data, cap_len);
}

asciichat_error_t acip_send_protocol_version(acip_transport_t *transport, const protocol_version_packet_t *version) {
  if (!transport || !version) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or version");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, version, sizeof(*version));
}

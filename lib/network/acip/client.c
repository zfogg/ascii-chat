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

#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/overflow.h>
#include <string.h>

// =============================================================================
// Client Receive (from server)
// =============================================================================

asciichat_error_t acip_client_receive_and_dispatch(acip_transport_t *transport,
                                                   const acip_client_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    log_error("[ACIP_RECV] ❌ INVALID_PARAMS: transport=%p, callbacks=%p", (void *)transport, (void *)callbacks);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }

  log_debug("[ACIP_RECV] 📥 RECV_START: transport=%p, checking connection state", (void *)transport);

  // Check if transport is connected
  if (!transport->methods->is_connected(transport)) {
    log_error("[ACIP_RECV] ❌ NOT_CONNECTED: transport not ready for receive");
    return SET_ERRNO(ERROR_NETWORK, "Transport not connected");
  }

  log_debug("[ACIP_RECV] ✅ CONNECTED: transport ready, checking transport type");

  packet_envelope_t envelope;
  bool enforce_encryption = (transport->crypto_ctx != NULL);
  log_debug("[ACIP_RECV] 🔐 CRYPTO_MODE: enforce_encryption=%s", enforce_encryption ? "yes" : "no");

  // Try to get socket from transport
  socket_t sock = transport->methods->get_socket(transport);
  log_debug("[ACIP_RECV] 🔌 TRANSPORT_TYPE: socket=%d (socket=TCP, INVALID=WebRTC/other)", sock);

  if (sock != INVALID_SOCKET_VALUE) {
    // Socket-based transport (TCP): use receive_packet_secure() for socket I/O + parsing
    log_info("[ACIP_RECV] 🔌 TCP_PATH: using receive_packet_secure(), sockfd=%d", sock);
    packet_recv_result_t result = receive_packet_secure(sock, transport->crypto_ctx, enforce_encryption, &envelope);

    log_info("[ACIP_RECV] 📥 TCP_RECV_RESULT: code=%d (0=success, 1=eof, 2=crypto_error, 3=other)", result);

    // Handle receive errors
    if (result != PACKET_RECV_SUCCESS) {
      if (result == PACKET_RECV_EOF) {
        log_warn("[ACIP_RECV] ⚠️  EOF: Server closed connection");
        return SET_ERRNO(ERROR_NETWORK, "Connection closed (EOF)");
      } else if (result == PACKET_RECV_SECURITY_VIOLATION) {
        log_error("[ACIP_RECV] ❌ SECURITY_VIOLATION: Encryption policy violated");
        return SET_ERRNO(ERROR_CRYPTO, "Security violation: unencrypted packet when encryption required");
      } else {
        log_error("[ACIP_RECV] ❌ TCP_RECV_FAILED: receive_packet_secure returned %d", result);
        return SET_ERRNO(ERROR_NETWORK, "Failed to receive packet");
      }
    }
  } else {
    // Non-socket transport (WebRTC): use transport's recv() method to get complete packet
    log_info("[ACIP_RECV] 🌐 WEBRTC_PATH: using transport->methods->recv()");
    void *packet_data = NULL;
    void *allocated_buffer = NULL;
    size_t packet_len = 0;

    log_debug("[ACIP_RECV] 🌐 WEBRTC_RECV: calling recv() method");
    asciichat_error_t recv_result = transport->methods->recv(transport, &packet_data, &packet_len, &allocated_buffer);
    log_info("[ACIP_RECV] 🌐 WEBRTC_RECV_RESULT: error=%d, packet_len=%zu, data=%p, alloc=%p", recv_result, packet_len,
             packet_data, allocated_buffer);

    if (recv_result != ASCIICHAT_OK) {
      log_error("[ACIP_RECV] ❌ WEBRTC_RECV_FAILED: error code %d", recv_result);
      return SET_ERRNO(ERROR_NETWORK, "Transport recv() failed");
    }

    // Parse packet header
    log_debug("[ACIP_RECV] 🌐 HEADER_CHECK: packet_len=%zu, header_size=%zu", packet_len, sizeof(packet_header_t));
    if (packet_len < sizeof(packet_header_t)) {
      log_error("[ACIP_RECV] ❌ HEADER_TOO_SMALL: %zu < %zu", packet_len, sizeof(packet_header_t));
      buffer_pool_free(NULL, allocated_buffer, packet_len);
      return SET_ERRNO(ERROR_NETWORK, "Packet too small: %zu < %zu", packet_len, sizeof(packet_header_t));
    }

    const packet_header_t *header = (const packet_header_t *)packet_data;
    envelope.type = NET_TO_HOST_U16(header->type);
    envelope.len = NET_TO_HOST_U32(header->length);
    envelope.data = (uint8_t *)packet_data + sizeof(packet_header_t);
    envelope.allocated_buffer = allocated_buffer;
    envelope.allocated_size = packet_len;
    envelope.was_encrypted = false; // WebRTC currently doesn't support encryption in this path

    log_info("[ACIP_RECV] 🌐 HEADER_PARSED: type=%u (0x%04x), len=%u, total_size=%zu", envelope.type, envelope.type,
             envelope.len, packet_len);

    log_dev_every(4500 * US_PER_MS_INT, "WebRTC received packet: type=%u, len=%u, total_size=%zu", envelope.type,
                  envelope.len, packet_len);

    // Validate packet length
    log_debug("[ACIP_RECV] 🌐 LENGTH_VALIDATION: payload_size=%u vs actual=%zu", envelope.len,
              packet_len - sizeof(packet_header_t));
    if (envelope.len != packet_len - sizeof(packet_header_t)) {
      log_error("[ACIP_RECV] ❌ LENGTH_MISMATCH: header=%u, actual=%zu", envelope.len,
                packet_len - sizeof(packet_header_t));
      buffer_pool_free(NULL, allocated_buffer, packet_len);
      return SET_ERRNO(ERROR_NETWORK, "Packet length mismatch: header says %u, actual %zu", envelope.len,
                       packet_len - sizeof(packet_header_t));
    }
  }

  // Dispatch packet to appropriate ACIP handler
  log_info("[ACIP_RECV] 🎯 DISPATCH_START: type=%u (0x%04x), data_len=%u, callbacks=%p", envelope.type, envelope.type,
           envelope.len, (void *)callbacks);
  asciichat_error_t dispatch_result =
      acip_handle_client_packet(transport, envelope.type, envelope.data, envelope.len, callbacks);

  if (dispatch_result != ASCIICHAT_OK) {
    log_error("[ACIP_RECV] ❌ DISPATCH_FAILED: type=%u, error=%d", envelope.type, dispatch_result);
  } else {
    log_info("[ACIP_RECV] ✅ DISPATCH_OK: type=%u (0x%04x) handled by callbacks", envelope.type, envelope.type);
  }

  // Always free the allocated buffer (even if handler failed)
  log_debug("[ACIP_RECV] 🗑️  CLEANUP: freeing buffer %p (size=%zu)", envelope.allocated_buffer,
            envelope.allocated_size);
  if (envelope.allocated_buffer) {
    buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
  }

  log_debug("[ACIP_RECV] ✅ RECV_COMPLETE: type=%u, result=%d", envelope.type, dispatch_result != ASCIICHAT_OK ? -1 : 0);

  // Return handler result
  return dispatch_result;
}

// =============================================================================
// Client Send (to server)
// =============================================================================

asciichat_error_t acip_send_image_frame(acip_transport_t *transport, const void *pixel_data, uint32_t width,
                                        uint32_t height, uint32_t pixel_format) {
  log_debug("★ ACIP_SEND_IMAGE_FRAME: Called with %ux%u, transport=%p, pixel_data=%p", width, height, (void *)transport,
            pixel_data);

  if (!transport || !pixel_data) {
    log_debug("★ ACIP_SEND_IMAGE_FRAME: Invalid params");
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or pixel_data");
  }

  // Calculate pixel data size (3 bytes per pixel for RGB24)
  size_t pixel_size = (size_t)width * height * 3;
  log_debug("★ ACIP_SEND_IMAGE_FRAME: pixel_size=%zu", pixel_size);

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
    log_debug("★ ACIP_SEND_IMAGE_FRAME: Overflow in size calculation");
    return SET_ERRNO(ERROR_INVALID_PARAM, "Packet size overflow");
  }

  log_debug("★ ACIP_SEND_IMAGE_FRAME: total_size=%zu, allocating buffer", total_size);

  // Allocate buffer
  uint8_t *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    log_debug("★ ACIP_SEND_IMAGE_FRAME: Failed to allocate %zu bytes", total_size);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer: %zu bytes", total_size);
  }

  // Build packet
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), pixel_data, pixel_size);

  log_debug("★ ACIP_SEND_IMAGE_FRAME: About to send packet");
  // Send via transport
  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_IMAGE_FRAME, buffer, total_size, 0);

  log_debug("★ ACIP_SEND_IMAGE_FRAME: packet_send_via_transport returned %d", result);

  buffer_pool_free(NULL, buffer, total_size);
  return result;
}

asciichat_error_t acip_send_client_join(acip_transport_t *transport, uint8_t capabilities) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Simple capability byte payload
  return packet_send_via_transport(transport, PACKET_TYPE_CLIENT_JOIN, &capabilities, sizeof(capabilities), 0);
}

asciichat_error_t acip_send_client_leave(acip_transport_t *transport) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Leave has no payload
  return packet_send_via_transport(transport, PACKET_TYPE_CLIENT_LEAVE, NULL, 0, 0);
}

asciichat_error_t acip_send_stream_start(acip_transport_t *transport, uint8_t stream_types) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Server expects uint32_t (4 bytes), not uint8_t
  uint32_t stream_types_net = HOST_TO_NET_U32((uint32_t)stream_types);
  return packet_send_via_transport(transport, PACKET_TYPE_STREAM_START, &stream_types_net, sizeof(stream_types_net), 0);
}

asciichat_error_t acip_send_stream_stop(acip_transport_t *transport, uint8_t stream_types) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Server expects uint32_t (4 bytes), not uint8_t
  uint32_t stream_types_net = HOST_TO_NET_U32((uint32_t)stream_types);
  return packet_send_via_transport(transport, PACKET_TYPE_STREAM_STOP, &stream_types_net, sizeof(stream_types_net), 0);
}

asciichat_error_t acip_send_capabilities(acip_transport_t *transport, const void *cap_data, size_t cap_len) {
  if (!transport || !cap_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or cap_data");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_CLIENT_CAPABILITIES, cap_data, cap_len, 0);
}

asciichat_error_t acip_send_protocol_version(acip_transport_t *transport, const protocol_version_packet_t *version) {
  if (!transport || !version) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or version");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, version, sizeof(*version), 0);
}

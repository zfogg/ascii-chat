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

#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/crypto/crypto.h>
#include <string.h>

// =============================================================================
// Server Receive (from client)
// =============================================================================

asciichat_error_t acip_server_receive_and_dispatch(acip_transport_t *transport, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  uint64_t dispatch_start_ns = time_get_ns();
  log_dev_every(4500 * US_PER_MS_INT,
                "ACIP_SERVER_DISPATCH: Entry, transport=%p, client_ctx=%p, callbacks=%p, timestamp=%llu",
                (void *)transport, client_ctx, (const void *)callbacks, (unsigned long long)dispatch_start_ns);

  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport (NULL)");
  }
  if (!callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid callbacks (NULL)");
  }

  // Check if transport is connected
  if (!transport->methods->is_connected(transport)) {
    return SET_ERRNO(ERROR_NETWORK, "Transport not connected");
  }

  packet_envelope_t envelope;
  bool enforce_encryption = (transport->crypto_ctx != NULL);

  // Try to get socket from transport
  socket_t sock = transport->methods->get_socket(transport);
  log_dev_every(4500 * US_PER_MS_INT, "ACIP_SERVER_DISPATCH: Socket=%d (INVALID=%d)", sock, INVALID_SOCKET_VALUE);

  if (sock != INVALID_SOCKET_VALUE) {
    // Socket-based transport (TCP): use receive_packet_secure() for socket I/O + parsing
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
  } else {
    // Non-socket transport (WebRTC): use transport's recv() method to get complete packet
    void *packet_data = NULL;
    void *allocated_buffer = NULL;
    size_t packet_len = 0;

    uint64_t recv_start_ns = time_get_ns();
    asciichat_error_t recv_result = transport->methods->recv(transport, &packet_data, &packet_len, &allocated_buffer);
    uint64_t recv_end_ns = time_get_ns();

    if (recv_result != ASCIICHAT_OK) {
      char recv_duration_str[32];
      format_duration_ns((double)(recv_end_ns - recv_start_ns), recv_duration_str, sizeof(recv_duration_str));
      log_warn("[ACIP_RECV_ERROR] recv() failed after %s (result=%d)", recv_duration_str, recv_result);
      return SET_ERRNO(ERROR_NETWORK, "Transport recv() failed");
    }

    char recv_duration_str[32];
    format_duration_ns((double)(recv_end_ns - recv_start_ns), recv_duration_str, sizeof(recv_duration_str));
    log_info("[ACIP_RECV_SUCCESS] Received %zu bytes in %s", packet_len, recv_duration_str);

    // Parse packet header
    if (packet_len < sizeof(packet_header_t)) {
      buffer_pool_free(NULL, allocated_buffer, packet_len);
      return SET_ERRNO(ERROR_NETWORK, "Packet too small: %zu < %zu", packet_len, sizeof(packet_header_t));
    }

    const packet_header_t *header = (const packet_header_t *)packet_data;
    envelope.type = NET_TO_HOST_U16(header->type);
    envelope.len = NET_TO_HOST_U32(header->length);
    envelope.data = (uint8_t *)packet_data + sizeof(packet_header_t);
    envelope.allocated_buffer = allocated_buffer;
    envelope.allocated_size = packet_len;

    log_dev_every(4500 * US_PER_MS_INT, "ACIP_SERVER_DISPATCH: WebRTC packet parsed: type=%d, len=%u", envelope.type,
                  envelope.len);

    // Handle PACKET_TYPE_ENCRYPTED from WebSocket clients that encrypt at application layer
    if (envelope.type == PACKET_TYPE_ENCRYPTED && transport->crypto_ctx) {
      uint64_t decrypt_start_ns = time_get_ns();
      uint8_t *ciphertext = (uint8_t *)envelope.data;
      size_t ciphertext_len = envelope.len;

      // Decrypt to get inner plaintext packet (header + payload)
      size_t plaintext_size = ciphertext_len + 1024;
      uint8_t *plaintext = buffer_pool_alloc(NULL, plaintext_size);
      if (!plaintext) {
        buffer_pool_free(NULL, allocated_buffer, packet_len);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate plaintext buffer for decryption");
      }

      size_t plaintext_len;
      crypto_result_t crypto_result =
          crypto_decrypt(transport->crypto_ctx, ciphertext, ciphertext_len, plaintext, plaintext_size, &plaintext_len);

      // Free the original encrypted buffer
      buffer_pool_free(NULL, allocated_buffer, packet_len);

      if (crypto_result != CRYPTO_OK) {
        buffer_pool_free(NULL, plaintext, plaintext_size);
        return SET_ERRNO(ERROR_CRYPTO, "Failed to decrypt WebSocket packet: %s",
                         crypto_result_to_string(crypto_result));
      }

      if (plaintext_len < sizeof(packet_header_t)) {
        buffer_pool_free(NULL, plaintext, plaintext_size);
        return SET_ERRNO(ERROR_CRYPTO, "Decrypted packet too small: %zu < %zu", plaintext_len, sizeof(packet_header_t));
      }

      // Parse the inner (decrypted) header
      const packet_header_t *inner_header = (const packet_header_t *)plaintext;
      envelope.type = NET_TO_HOST_U16(inner_header->type);
      envelope.len = NET_TO_HOST_U32(inner_header->length);
      envelope.data = plaintext + sizeof(packet_header_t);
      envelope.allocated_buffer = plaintext;
      envelope.allocated_size = plaintext_size;

      uint64_t decrypt_end_ns = time_get_ns();
      char decrypt_duration_str[32];
      format_duration_ns((double)(decrypt_end_ns - decrypt_start_ns), decrypt_duration_str,
                         sizeof(decrypt_duration_str));
      log_info("[WS_TIMING] Decrypt %zu bytes → %zu bytes in %s (inner_type=%d)", ciphertext_len, plaintext_len,
               decrypt_duration_str, envelope.type);
    }
  }

  // Dispatch packet to appropriate ACIP handler
  // Server receives packets FROM clients, so use server packet handler
  log_info("ACIP_DISPATCH_PKT: type=%d, len=%zu, client_ctx=%p", envelope.type, envelope.len, client_ctx);
  log_info("📤 [FRAME_DISPATCH] Starting handler dispatch for packet type=%d (0x%04x), payload=%zu bytes",
           envelope.type, envelope.type, envelope.len);
  uint64_t dispatch_handler_start_ns = time_get_ns();
  asciichat_error_t dispatch_result =
      acip_handle_server_packet(transport, envelope.type, envelope.data, envelope.len, client_ctx, callbacks);
  uint64_t dispatch_handler_end_ns = time_get_ns();
  char handler_duration_str[32];
  format_duration_ns((double)(dispatch_handler_end_ns - dispatch_handler_start_ns), handler_duration_str,
                     sizeof(handler_duration_str));
  log_info("[WS_TIMING] Handler for type=%d took %s (result=%d)", envelope.type, handler_duration_str, dispatch_result);

  if (dispatch_result != ASCIICHAT_OK) {
    log_error("❌ [FRAME_DISPATCH_FAILED] Handler returned error for type=%d: %d", envelope.type, dispatch_result);
  } else {
    log_info("✅ [FRAME_DISPATCH_SUCCESS] Handler completed successfully for type=%d", envelope.type);
  }

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
                                        uint32_t width, uint32_t height, uint32_t client_id) {
  if (!transport || !frame_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or frame_data");
  }

  if (frame_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty frame data");
  }

  log_info("★ SEND_ASCII_FRAME START: client_id=%u, width=%u, height=%u, frame_size=%zu bytes", client_id, width,
           height, frame_size);

  // Calculate CRC32 checksum of frame data for integrity verification
  uint32_t checksum_value = asciichat_crc32(frame_data, frame_size);
  log_debug("★ SEND_ASCII_FRAME: CRC32 checksum calculated: 0x%08x for %zu bytes", checksum_value, frame_size);

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
    log_error("★ SEND_ASCII_FRAME: Packet size overflow when adding header (%zu) + frame (%zu)", sizeof(header),
              frame_size);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Packet size overflow");
  }

  log_debug("★ SEND_ASCII_FRAME: Building packet - header=%zu bytes, frame=%zu bytes, total=%zu bytes", sizeof(header),
            frame_size, total_size);

  // Allocate buffer
  uint8_t *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    log_error("★ SEND_ASCII_FRAME: Memory allocation FAILED for %zu bytes", total_size);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer: %zu bytes", total_size);
  }

  log_debug("★ SEND_ASCII_FRAME: Buffer allocated at %p", (void *)buffer);

  // Build packet: header + data
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), frame_data, frame_size);

  // Send via transport with client_id
  log_info("★ SEND_ASCII_FRAME: Calling packet_send_via_transport with PACKET_TYPE_ASCII_FRAME");
  asciichat_error_t result =
      packet_send_via_transport(transport, PACKET_TYPE_ASCII_FRAME, buffer, total_size, client_id);

  if (result == ASCIICHAT_OK) {
    log_info("★ SEND_ASCII_FRAME COMPLETE: SUCCESS for client_id=%u, sent %zu bytes total", client_id, total_size);
  } else {
    log_error("★ SEND_ASCII_FRAME FAILED: Error code %d (%s) for client_id=%u", result, asciichat_error_string(result),
              client_id);
  }

  buffer_pool_free(NULL, buffer, total_size);
  return result;
}

asciichat_error_t acip_send_clear_console(acip_transport_t *transport) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0, 0);
}

asciichat_error_t acip_send_server_state(acip_transport_t *transport, const server_state_packet_t *state) {
  if (!transport || !state) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or state");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_SERVER_STATE, state, sizeof(*state), 0);
}

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

#include <ascii-chat/otel.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/crypto/crypto.h>
#include <string.h>

// =============================================================================
// Server Receive (from client)
// =============================================================================

asciichat_error_t acip_server_receive_and_dispatch(acip_transport_t *transport, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks) {
  ASCII_OTEL_SCOPE(ACIP_SERVER_RECEIVE_AND_DISPATCH);
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

  packet_envelope_t envelope = {0};
  asciichat_error_t received = packet_receive_via_transport(transport, &envelope.type, &envelope.data, &envelope.len,
                                                            &envelope.allocated_buffer);
  if (received != ASCIICHAT_OK)
    return received;

  // Dispatch packet to appropriate ACIP handler
  // Server receives packets FROM clients, so use server packet handler
  log_info("ACIP_DISPATCH_PKT: type=%d, len=%zu, client_ctx=%p", envelope.type, envelope.len, client_ctx);
  log_info("📤 [FRAME_DISPATCH] Starting handler dispatch for packet type=%d (0x%04x), payload=%zu bytes",
           envelope.type, envelope.type, envelope.len);
  uint64_t dispatch_handler_start_ns = time_get_ns();
  asciichat_error_t dispatch_result =
      acip_handle_server_packet(transport, envelope.type, envelope.data, envelope.len, client_ctx, callbacks);
  uint64_t dispatch_handler_end_ns = time_get_ns();
  char handler_duration_str[TIME_DURATION_BUFFER_SIZE];
  time_pretty((uint64_t)(dispatch_handler_end_ns - dispatch_handler_start_ns), -1, handler_duration_str,
              sizeof(handler_duration_str));
  log_info("[WS_TIMING] Handler for type=%d took %s (result=%d)", envelope.type, handler_duration_str, dispatch_result);

  if (dispatch_result != ASCIICHAT_OK) {
    log_error("❌ [FRAME_DISPATCH_FAILED] Handler returned error for type=%d: %s", envelope.type,
              asciichat_error_format(NULL, dispatch_result));
  } else {
    log_info("✅ [FRAME_DISPATCH_SUCCESS] Handler completed successfully for type=%d", envelope.type);
  }

  // Always free the allocated buffer (even if handler failed)
  if (envelope.allocated_buffer) {
    buffer_release(envelope.allocated_buffer);
  }

  // Return handler result
  return dispatch_result;
}

// =============================================================================
// Server Send (to client)
// =============================================================================

asciichat_error_t acip_send_ascii_frame_contract(acip_transport_t *transport, acip_frame_workspace_t *workspace,
                                                 const char *frame_data, size_t frame_size, uint32_t width,
                                                 uint32_t height, const acip_render_settings_t *settings,
                                                 uint32_t effects, const char *client_id) {
  ASCII_OTEL_SCOPE(ACIP_SEND_ASCII_FRAME_CONTRACT);
  if (!transport || !frame_data || !workspace) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or frame_data");
  }

  if (frame_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty frame data");
  }

  if (frame_size > UINT32_MAX) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "ASCII frame is too large: %zu bytes", frame_size);
  }

  log_dev("SEND_ASCII_FRAME: client_id=%s, width=%u, height=%u, frame_size=%zu bytes", client_id, width, height,
          frame_size);

  // Calculate CRC32 checksum of frame data for integrity verification
  uint32_t checksum_value = asciichat_crc32(frame_data, frame_size);
  log_debug("★ SEND_ASCII_FRAME: CRC32 checksum calculated: 0x%08x for %zu bytes", checksum_value, frame_size);

  // Compress ANSI text before sending it. Native and browser clients decode zstd frames.
  const void *payload_data = frame_data;
  size_t payload_size = frame_size;
  bool is_compressed = false;
  if (frame_size >= COMPRESSION_MIN_SIZE &&
      compress_workspace(&workspace->compression, frame_data, frame_size, 1) == ASCIICHAT_OK &&
      should_compress(frame_size, workspace->compression.output.length)) {
    payload_data = workspace->compression.output.data;
    payload_size = workspace->compression.output.length;
    is_compressed = true;
  }

  // Create ASCII frame packet header
  ascii_frame_packet_t header;
  header.width = HOST_TO_NET_U32(width);
  header.height = HOST_TO_NET_U32(height);
  header.original_size = HOST_TO_NET_U32((uint32_t)frame_size);
  header.compressed_size = HOST_TO_NET_U32(is_compressed ? (uint32_t)payload_size : 0);
  header.checksum = HOST_TO_NET_U32(checksum_value);
  uint8_t contract[FRAME_RENDER_CONTRACT_MAX];
  size_t contract_length = 0;
  if (settings) {
    asciichat_error_t encoded = frame_render_contract_encode(settings, effects, contract, &contract_length);
    if (encoded != ASCIICHAT_OK)
      return encoded;
  }
  header.flags =
      HOST_TO_NET_U32((is_compressed ? FRAME_FLAG_IS_COMPRESSED : 0) | (settings ? FRAME_FLAG_RENDER_CONTRACT : 0));

  socket_buffer_t slices[3] = {{&header, sizeof(header)}};
  size_t count = 1;
  if (contract_length)
    slices[count++] = (socket_buffer_t){contract, contract_length};
  slices[count++] = (socket_buffer_t){payload_data, payload_size};
  asciichat_error_t result = packet_send_via_transportv(transport, PACKET_TYPE_ASCII_FRAME, slices, count, 0);
  return result;
}

asciichat_error_t acip_send_ascii_frame(acip_transport_t *transport, acip_frame_workspace_t *workspace,
                                        const char *text, size_t length, uint32_t width, uint32_t height,
                                        const char *client_id) {
  return acip_send_ascii_frame_contract(transport, workspace, text, length, width, height, NULL, 0, client_id);
}

void acip_frame_workspace_destroy(acip_frame_workspace_t *workspace) {
  if (!workspace)
    return;
  compression_workspace_destroy(&workspace->compression);
}

asciichat_error_t acip_send_clear_console(acip_transport_t *transport) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0, 0);
}

asciichat_error_t acip_send_server_state(acip_transport_t *transport, const server_state_packet_t *state) {
  ASCII_OTEL_SCOPE(ACIP_SEND_SERVER_STATE);
  if (!transport || !state) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or state");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_SERVER_STATE, state, sizeof(*state), 0);
}

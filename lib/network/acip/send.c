/**
 * @file network/acip/send.c
 * @brief ACIP protocol packet sending functions (transport-agnostic)
 *
 * Implementation of send functions that work with any transport.
 * Refactored from lib/network/av.c to use transport abstraction.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/audio/audio.h>
#include <string.h>
#include <ascii-chat/network/crc32.h>
#include <time.h>
// =============================================================================
// Packet Helper (wraps payload with header and sends via transport)
// =============================================================================

/**
 * @brief Send packet via transport with proper header (exported for generic wrappers)
 *
 * Wraps payload in ACIP packet header and sends via transport.
 * Handles CRC32 calculation and network byte order conversion.
 *
 * @param transport Transport instance
 * @param type Packet type
 * @param payload Payload data (may be NULL if payload_len is 0)
 * @param payload_len Payload length
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t packet_send_via_transport(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  log_debug("★ PACKET_SEND_VIA_TRANSPORT: type=%d, payload_len=%zu, transport=%p", type, payload_len,
            (void *)transport);

  // Build packet header
  packet_header_t header;
  header.magic = HOST_TO_NET_U64(PACKET_MAGIC);
  header.type = HOST_TO_NET_U16(type);
  header.length = HOST_TO_NET_U32((uint32_t)payload_len);
  header.client_id = 0; // Set by caller if needed

  // Calculate CRC32 if we have payload
  if (payload && payload_len > 0) {
    header.crc32 = HOST_TO_NET_U32(asciichat_crc32((const uint8_t *)payload, payload_len));
  } else {
    header.crc32 = 0;
  }

  log_debug("★ PKT_SEND: type=%d, magic=0x%016llx, length=%u, crc32=0x%08x", type, header.magic, header.length,
            header.crc32);

  // Calculate total packet size
  size_t total_size = sizeof(header) + payload_len;

  // Allocate buffer for complete packet
  uint8_t *packet = buffer_pool_alloc(NULL, total_size);
  if (!packet) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate packet buffer");
  }

  // Build complete packet: header + payload
  memcpy(packet, &header, sizeof(header));
  if (payload && payload_len > 0) {
    memcpy(packet + sizeof(header), payload, payload_len);
  }

  log_debug("★ PACKET_SEND: total_size=%zu, calling acip_transport_send...", total_size);
  // Send via transport (transport handles encryption if crypto_ctx present)
  asciichat_error_t result = acip_transport_send(transport, packet, total_size);

  if (result == ASCIICHAT_OK) {
    log_debug("★ PACKET_SEND: SUCCESS - sent %zu bytes (type=%d)", total_size, type);
  } else {
    log_error("★ PACKET_SEND: FAILED - acip_transport_send returned %d (%s)", result, asciichat_error_string(result));
  }

  buffer_pool_free(NULL, packet, total_size);
  return result;
}

// ASCII/Video frame functions moved to:
// - acip_send_ascii_frame → lib/network/acip/server.c (server → client)
// - acip_send_image_frame → lib/network/acip/client.c (client → server)

// =============================================================================
// Audio Sending
// =============================================================================

asciichat_error_t acip_send_audio_batch(acip_transport_t *transport, const float *samples, uint32_t num_samples,
                                        uint32_t batch_count) {
  if (!transport || !samples || num_samples == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Build batch header
  audio_batch_packet_t header;
  header.batch_count = batch_count;
  header.total_samples = (uint32_t)num_samples;
  header.sample_rate = (uint32_t)AUDIO_SAMPLE_RATE;
  header.channels = 1;

  // Calculate total size (header + float samples)
  size_t samples_size = num_samples * sizeof(float);
  size_t total_size = sizeof(header) + samples_size;

  uint8_t *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer");
  }

  // Copy header and samples
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), samples, samples_size);

  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_AUDIO_BATCH, buffer, total_size);

  buffer_pool_free(NULL, buffer, total_size);
  return result;
}

asciichat_error_t acip_send_audio_opus(acip_transport_t *transport, const void *opus_data, size_t opus_len) {
  if (!transport || !opus_data || opus_len == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Convert single opus frame to batch format (frame_count=1) with standard parameters
  // Standard: 48kHz sample rate, 20ms frame duration
  uint16_t frame_sizes[1] = {(uint16_t)opus_len};
  return acip_send_audio_opus_batch(transport, opus_data, opus_len, frame_sizes, 1, 48000, 20);
}

asciichat_error_t acip_send_audio_opus_batch(acip_transport_t *transport, const void *opus_data, size_t opus_len,
                                             const uint16_t *frame_sizes, uint32_t frame_count, uint32_t sample_rate,
                                             uint32_t frame_duration) {
  if (!transport || !opus_data || !frame_sizes || frame_count == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Build batch header (16 bytes)
  uint8_t header[16];
  *(uint32_t *)(header + 0) = HOST_TO_NET_U32(sample_rate);
  *(uint32_t *)(header + 4) = HOST_TO_NET_U32(frame_duration);
  *(uint32_t *)(header + 8) = HOST_TO_NET_U32(frame_count);
  *(uint32_t *)(header + 12) = 0; // reserved

  // Calculate sizes array size
  size_t sizes_len = frame_count * sizeof(uint16_t);
  size_t total_size = sizeof(header) + sizes_len + opus_len;

  uint8_t *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer");
  }

  // Build packet: header + sizes + opus_data
  memcpy(buffer, header, sizeof(header));

  // Convert frame sizes to network byte order before copying
  // IMPORTANT: Server expects network byte order and will apply NET_TO_HOST_U16()
  uint16_t *sizes_buf = (uint16_t *)(buffer + sizeof(header));
  for (uint32_t i = 0; i < frame_count; i++) {
    sizes_buf[i] = HOST_TO_NET_U16(frame_sizes[i]);
  }

  memcpy(buffer + sizeof(header) + sizes_len, opus_data, opus_len);

  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_AUDIO_OPUS_BATCH, buffer, total_size);

  buffer_pool_free(NULL, buffer, total_size);
  return result;
}

// =============================================================================
// Control/Signaling
// =============================================================================

asciichat_error_t acip_send_ping(acip_transport_t *transport) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Ping has no payload
  return packet_send_via_transport(transport, PACKET_TYPE_PING, NULL, 0);
}

asciichat_error_t acip_send_pong(acip_transport_t *transport) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Pong has no payload
  return packet_send_via_transport(transport, PACKET_TYPE_PONG, NULL, 0);
}

// Client control functions moved to lib/network/acip/client.c:
// - acip_send_client_join, acip_send_client_leave
// - acip_send_stream_start, acip_send_stream_stop
// - acip_send_capabilities, acip_send_protocol_version
//
// Server control functions moved to lib/network/acip/server.c:
// - acip_send_clear_console, acip_send_server_state

// =============================================================================
// Messages/Errors
// =============================================================================

asciichat_error_t acip_send_error(acip_transport_t *transport, uint32_t error_code, const char *message) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Calculate message length (max MAX_ERROR_MESSAGE_LENGTH)
  size_t msg_len = 0;
  if (message) {
    msg_len = strlen(message);
    if (msg_len > MAX_ERROR_MESSAGE_LENGTH) {
      msg_len = MAX_ERROR_MESSAGE_LENGTH;
    }
  }

  error_packet_t header;
  header.error_code = HOST_TO_NET_U32(error_code);
  header.message_length = HOST_TO_NET_U32((uint32_t)msg_len);

  // Allocate buffer for header + message
  size_t total_size = sizeof(header) + msg_len;
  uint8_t *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer");
  }

  // Build packet
  memcpy(buffer, &header, sizeof(header));
  if (msg_len > 0) {
    memcpy(buffer + sizeof(header), message, msg_len);
  }

  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_ERROR_MESSAGE, buffer, total_size);

  buffer_pool_free(NULL, buffer, total_size);
  return result;
}

asciichat_error_t acip_send_remote_log(acip_transport_t *transport, uint8_t log_level, uint8_t direction,
                                       const char *message) {
  if (!transport || !message) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or message");
  }

  size_t msg_len = strlen(message);

  remote_log_packet_t header;
  header.log_level = log_level;
  header.direction = direction;
  header.flags = 0;
  header.message_length = HOST_TO_NET_U32((uint32_t)msg_len);

  size_t total_size = sizeof(header) + msg_len;

  uint8_t *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer");
  }

  // Build packet
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), message, msg_len);

  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_REMOTE_LOG, buffer, total_size);

  buffer_pool_free(NULL, buffer, total_size);
  return result;
}

// =============================================================================
// ACDS (Discovery Server) Response Sending
// =============================================================================

asciichat_error_t acip_send_session_created(acip_transport_t *transport, const acip_session_created_t *response) {
  if (!transport || !response) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or response");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_CREATED, response, sizeof(*response));
}

asciichat_error_t acip_send_session_info(acip_transport_t *transport, const acip_session_info_t *info) {
  if (!transport || !info) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or info");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_INFO, info, sizeof(*info));
}

asciichat_error_t acip_send_session_joined(acip_transport_t *transport, const acip_session_joined_t *response) {
  if (!transport || !response) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or response");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_JOINED, response, sizeof(*response));
}

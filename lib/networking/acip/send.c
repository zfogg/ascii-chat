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

#include "networking/acip/send.h"
#include "networking/acip/transport.h"
#include "network/packet.h"
#include "buffer_pool.h"
#include "util/overflow.h"
#include "util/endian.h"
#include "asciichat_errno.h"
#include "log/logging.h"
#include "audio/audio.h"
#include <string.h>
#include "network/crc32.h"
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

  // Build packet header
  packet_header_t header;
  header.magic = HOST_TO_NET_U32(PACKET_MAGIC);
  header.type = HOST_TO_NET_U16(type);
  header.length = HOST_TO_NET_U32((uint32_t)payload_len);
  header.client_id = 0; // Set by caller if needed

  // Calculate CRC32 if we have payload
  if (payload && payload_len > 0) {
    header.crc32 = HOST_TO_NET_U32(asciichat_crc32((const uint8_t *)payload, payload_len));
  } else {
    header.crc32 = 0;
  }

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

  // Send via transport (transport handles encryption if crypto_ctx present)
  asciichat_error_t result = acip_transport_send(transport, packet, total_size);

  buffer_pool_free(NULL, packet, total_size);
  return result;
}

// =============================================================================
// ASCII/Video Frame Sending
// =============================================================================

asciichat_error_t acip_send_ascii_frame(acip_transport_t *transport, const char *frame_data, uint32_t width,
                                        uint32_t height) {
  if (!transport || !frame_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or frame_data");
  }

  size_t frame_size = strlen(frame_data);
  if (frame_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty frame data");
  }

  // Create ASCII frame packet header
  ascii_frame_packet_t header;
  header.width = HOST_TO_NET_U32(width);
  header.height = HOST_TO_NET_U32(height);
  header.original_size = HOST_TO_NET_U32((uint32_t)frame_size);
  header.compressed_size = 0;
  header.checksum = 0;
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

  return packet_send_via_transport(transport, PACKET_TYPE_AUDIO_OPUS, opus_data, opus_len);
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
  memcpy(buffer + sizeof(header), frame_sizes, sizes_len);
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

  return packet_send_via_transport(transport, PACKET_TYPE_STREAM_START, &stream_types, sizeof(stream_types));
}

asciichat_error_t acip_send_stream_stop(acip_transport_t *transport, uint8_t stream_types) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_STREAM_STOP, &stream_types, sizeof(stream_types));
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

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
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
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
 * @param client_id Client ID to include in packet header
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_transport_sendv(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  if (!transport || !transport->methods || !buffers || !count || count > SOCKET_IOV_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport vectors");
  size_t total = 0;
  for (size_t i = 0; i < count; ++i) {
    if ((buffers[i].len && !buffers[i].data) || buffers[i].len > SOCKET_SEND_BUFFER_MAX - total)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Invalid transport vector size");
    total += buffers[i].len;
  }
  if (transport->methods->sendv)
    return transport->methods->sendv(transport, buffers, count);
  // Message transports retain their existing queue and encryption contracts.
  uint8_t *packet = buffer_pool_alloc(NULL, total);
  if (!packet)
    return SET_ERRNO(ERROR_MEMORY, "Cannot assemble transport packet");
  size_t offset = 0;
  for (size_t i = 0; i < count; ++i) {
    if (buffers[i].len)
      memcpy(packet + offset, buffers[i].data, buffers[i].len);
    offset += buffers[i].len;
  }
  asciichat_error_t result = acip_transport_send(transport, packet, total);
  buffer_pool_free(NULL, packet, total);
  return result;
}

asciichat_error_t packet_send_via_transportv(acip_transport_t *transport, packet_type_t type,
                                             const socket_buffer_t *payload, size_t count, uint32_t client_id) {
  if (!transport || !payload || !count || count >= SOCKET_IOV_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid packet vectors");
  size_t len = 0;
  uint32_t crc = 0;
  for (size_t i = 0; i < count; ++i) {
    if ((payload[i].len && !payload[i].data) || payload[i].len > MAX_PACKET_SIZE - len)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Invalid packet payload size");
    len += payload[i].len;
    crc = asciichat_crc32_update(crc, payload[i].data, payload[i].len);
  }
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(type),
                            .length = HOST_TO_NET_U32((uint32_t)len),
                            .crc32 = HOST_TO_NET_U32(crc),
                            .client_id = client_id};
  socket_buffer_t slices[SOCKET_IOV_MAX] = {{&header, sizeof(header)}};
  memcpy(slices + 1, payload, count * sizeof(*payload));
  asciichat_error_t result = acip_transport_sendv(transport, slices, count + 1);
  stats_runtime_packet(transport->stats_peer, type, len, true, result == ASCIICHAT_OK);
  return result;
}

asciichat_error_t packet_send_via_transport(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len, uint32_t client_id) {
  socket_buffer_t slice = {payload, payload_len};
  return packet_send_via_transportv(transport, type, &slice, 1, client_id);
}

// =============================================================================
// Packet Receive Helper (receives via transport, parses header)
// =============================================================================

asciichat_error_t packet_receive_via_transport(acip_transport_t *transport, packet_type_t *type, void **payload,
                                               size_t *payload_len, void **alloc_buffer) {
  if (!transport || !type || !payload || !payload_len || !alloc_buffer) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for packet_receive_via_transport");
  }

  *type = 0;
  *payload = NULL;
  *payload_len = 0;
  *alloc_buffer = NULL;

  // Receive raw bytes via transport
  void *recv_buffer = NULL;
  size_t recv_len = 0;
  asciichat_error_t result = acip_transport_recv(transport, &recv_buffer, &recv_len, alloc_buffer);
  if (result != ASCIICHAT_OK) {
    if (result != ERROR_NETWORK_TIMEOUT)
      stats_runtime_packet(transport->stats_peer, 0, 0, false, false);
    return result;
  }

  // Parse packet header
  if (recv_len < sizeof(packet_header_t)) {
    log_warn("packet_receive_via_transport: packet too small (%zu bytes, need %zu)", recv_len, sizeof(packet_header_t));
    if (*alloc_buffer) {
      buffer_pool_free(NULL, *alloc_buffer, recv_len);
      *alloc_buffer = NULL;
    }
    stats_runtime_packet(transport->stats_peer, 0, 0, false, false);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Received packet smaller than header");
  }

  const packet_header_t *header = (const packet_header_t *)recv_buffer;
  *type = (packet_type_t)NET_TO_HOST_U16(header->type);
  size_t plen = NET_TO_HOST_U32(header->length);

  if (recv_len < sizeof(packet_header_t) + plen) {
    log_warn("packet_receive_via_transport: truncated packet (header says %zu payload, got %zu total)", plen, recv_len);
    if (*alloc_buffer) {
      buffer_pool_free(NULL, *alloc_buffer, recv_len);
      *alloc_buffer = NULL;
    }
    stats_runtime_packet(transport->stats_peer, 0, 0, false, false);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Truncated packet payload");
  }

  if (plen > 0) {
    *payload = (uint8_t *)recv_buffer + sizeof(packet_header_t);
  }
  *payload_len = plen;
  stats_runtime_packet(transport->stats_peer, *type, plen, false, true);

  return ASCIICHAT_OK;
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

  socket_buffer_t slices[] = {{&header, sizeof(header)}, {samples, (size_t)num_samples * sizeof(float)}};
  return packet_send_via_transportv(transport, PACKET_TYPE_AUDIO_BATCH, slices, 2, 0);
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

  if (frame_count > MAX_PACKET_SIZE / sizeof(uint16_t) || opus_len > MAX_PACKET_SIZE)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Opus batch too large");
  size_t sizes_len = (size_t)frame_count * sizeof(uint16_t);
  uint16_t *sizes = buffer_pool_alloc(NULL, sizes_len);
  if (!sizes)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate Opus lengths");
  for (uint32_t i = 0; i < frame_count; ++i)
    sizes[i] = HOST_TO_NET_U16(frame_sizes[i]);
  socket_buffer_t slices[] = {{header, sizeof(header)}, {sizes, sizes_len}, {opus_data, opus_len}};
  asciichat_error_t result = packet_send_via_transportv(transport, PACKET_TYPE_AUDIO_OPUS_BATCH, slices, 3, 0);
  buffer_pool_free(NULL, sizes, sizes_len);
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
  return packet_send_via_transport(transport, PACKET_TYPE_PING, NULL, 0, 0);
}

asciichat_error_t acip_send_pong(acip_transport_t *transport) {
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport");
  }

  // Pong has no payload
  return packet_send_via_transport(transport, PACKET_TYPE_PONG, NULL, 0, 0);
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

  error_code = network_error_public_code((asciichat_error_t)error_code);
  message = asciichat_error_string((asciichat_error_t)error_code);

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

  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_ERROR_MESSAGE, buffer, total_size, 0);

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
  // NOLINTNEXTLINE(bugprone-not-null-terminated-result) - binary packet data
  memcpy(buffer + sizeof(header), message, msg_len);

  asciichat_error_t result = packet_send_via_transport(transport, PACKET_TYPE_REMOTE_LOG, buffer, total_size, 0);

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

  return packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_CREATED, response, sizeof(*response), 0);
}

asciichat_error_t acip_send_session_info(acip_transport_t *transport, const acip_session_info_t *info) {
  if (!transport || !info) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or info");
  }

  return packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_INFO, info, sizeof(*info), 0);
}

asciichat_error_t acip_send_session_joined(acip_transport_t *transport, const acip_session_joined_t *response) {
  if (!transport || !response) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or response");
  }

  if (!response->success)
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_REQUEST_FAILURES, 1);
  return packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_JOINED, response, sizeof(*response), 0);
}

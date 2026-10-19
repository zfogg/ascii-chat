#include "session/protocol.h"
#include <ascii-chat/common.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/audio/audio.h>
#include <string.h>

static void receive_frame(const ascii_frame_packet_t *header, const void *data, size_t length, void *context) {
  session_protocol_t *protocol = context;
  bool compressed = (header->flags & FRAME_FLAG_IS_COMPRESSED) != 0;
  size_t expected = compressed ? header->compressed_size : header->original_size;
  if (!header->original_size || length != expected || (compressed && !header->compressed_size)) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid participant ASCII frame length");
    return;
  }
  char *text =
      packet_decode_frame_data_malloc(data, length, compressed, header->original_size, header->compressed_size);
  if (!text) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Could not decode participant ASCII frame");
    return;
  }
  if (asciichat_crc32(text, header->original_size) != header->checksum) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Participant ASCII frame checksum mismatch");
  } else {
    protocol->frames_received++;
    if (protocol->media.on_frame)
      protocol->callback_result = protocol->media.on_frame(header, text, protocol->media.context);
  }
  SAFE_FREE(text);
}

static void receive_samples(const audio_batch_packet_t *header, const float *samples, size_t count, void *context) {
  session_protocol_t *protocol = context;
  if (header && (header->sample_rate != AUDIO_SAMPLE_RATE || header->channels != 1)) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unsupported participant audio format");
    return;
  }
  if (protocol->media.on_samples)
    protocol->callback_result = protocol->media.on_samples(samples, count, protocol->media.context);
}

static void receive_opus(const void *data, size_t length, void *context) {
  session_protocol_t *protocol = context;
  if (!protocol->media.decode_opus || !protocol->media.on_samples)
    return;
  float samples[5760]; // 120 ms at 48 kHz.
  size_t decoded = 0;
  protocol->callback_result =
      protocol->media.decode_opus(data, length, samples, 5760, &decoded, protocol->media.context);
  if (protocol->callback_result != ASCIICHAT_OK)
    return;
  if (decoded > 5760) {
    protocol->callback_result = SET_ERRNO(ERROR_AUDIO, "Participant decoder exceeded sample capacity");
    return;
  }
  if (decoded)
    receive_samples(NULL, samples, decoded, context);
}

static void receive_opus_batch(const void *data, size_t length, void *context) {
  session_protocol_t *protocol = context;
  const uint8_t *opus;
  const uint16_t *sizes;
  size_t opus_length;
  int rate, duration, count;
  protocol->callback_result =
      packet_parse_opus_batch(data, length, &opus, &opus_length, &sizes, &rate, &duration, &count);
  if (protocol->callback_result != ASCIICHAT_OK)
    return;
  if (rate != AUDIO_SAMPLE_RATE || duration < 1 || duration > 120 || count < 1 || count > 256) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid participant Opus batch format");
    return;
  }
  // Validate the whole batch before delivering any samples, including unaligned input.
  size_t total = 0;
  for (int i = 0; i < count; i++) {
    uint16_t size;
    memcpy(&size, (const uint8_t *)sizes + (size_t)i * sizeof(size), sizeof(size));
    size = NET_TO_HOST_U16(size);
    if (!size || size > opus_length - total) {
      protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Truncated participant Opus batch");
      return;
    }
    total += size;
  }
  if (total != opus_length) {
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Trailing participant Opus batch data");
    return;
  }
  size_t offset = 0;
  for (int i = 0; i < count; i++) {
    uint16_t size;
    memcpy(&size, (const uint8_t *)sizes + (size_t)i * sizeof(size), sizeof(size));
    size = NET_TO_HOST_U16(size);
    receive_opus(opus + offset, size, context);
    if (protocol->callback_result != ASCIICHAT_OK)
      return;
    offset += size;
  }
}

static void receive_state(const server_state_packet_t *state, void *context) {
  session_protocol_t *protocol = context;
  uint32_t active = NET_TO_HOST_U32(state->active_client_count);
  if (!protocol->server_state_initialized || protocol->active_client_count != active)
    protocol->layout_changed = true;
  protocol->server_state_initialized = true;
  protocol->active_client_count = active;
  if (protocol->control.on_server_state)
    protocol->control.on_server_state(state, protocol->control.app_ctx);
}

static void receive_error(const error_packet_t *header, const char *message, void *context) {
  session_protocol_t *protocol = context;
  if (protocol->control.on_error)
    protocol->control.on_error(header, message, protocol->control.app_ctx);
  else
    protocol->callback_result = SET_ERRNO(ERROR_NETWORK, "Participant peer error: %s", message);
}

void session_protocol_init(session_protocol_t *protocol, acip_transport_t *transport, session_protocol_media_t media,
                           acip_client_callbacks_t control) {
  *protocol = (session_protocol_t){.transport = transport, .media = media, .control = control};
}

asciichat_error_t session_protocol_dispatch(session_protocol_t *protocol, packet_type_t type, const void *payload,
                                            size_t length) {
  if (!protocol || (!payload && length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid participant protocol packet");
  protocol->callback_result = ASCIICHAT_OK;
  acip_client_callbacks_t media = {.on_ascii_frame = receive_frame,
                                   .on_audio_batch = receive_samples,
                                   .on_audio_opus_batch = receive_opus_batch,
                                   .on_server_state = receive_state,
                                   .on_error = receive_error,
                                   .app_ctx = protocol};
  bool audio = type == PACKET_TYPE_AUDIO_BATCH || type == PACKET_TYPE_AUDIO_OPUS_BATCH;
  bool common =
      audio || type == PACKET_TYPE_ASCII_FRAME || type == PACKET_TYPE_SERVER_STATE || type == PACKET_TYPE_ERROR_MESSAGE;
  asciichat_error_t result;
  if (type == PACKET_TYPE_PING && !protocol->control.on_ping)
    result = acip_send_pong(protocol->transport);
  else
    result =
        acip_handle_client_packet(protocol->transport, type, payload, length, common ? &media : &protocol->control);
  if (result == ASCIICHAT_OK)
    result = protocol->callback_result;
  if (result == ASCIICHAT_OK) {
    protocol->packets_received++;
    if (audio) {
      protocol->audio_packets_received++;
      if (protocol->media.on_audio_packet)
        protocol->media.on_audio_packet(length, protocol->media.context);
    }
  }
  return result;
}

asciichat_error_t session_protocol_receive(session_protocol_t *protocol) {
  if (!protocol || !protocol->transport)
    return SET_ERRNO(ERROR_INVALID_STATE, "Participant protocol has no transport");
  packet_envelope_t envelope = {0};
  asciichat_error_t result = acip_client_receive_packet(protocol->transport, &envelope);
  if (result == ASCIICHAT_OK)
    result = session_protocol_dispatch(protocol, envelope.type, envelope.data, envelope.len);
  if (envelope.allocated_buffer)
    buffer_release(envelope.allocated_buffer);
  return result;
}

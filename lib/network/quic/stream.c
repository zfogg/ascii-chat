#include "stream.h"

#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

static const uint8_t stream_magic[4] = {'A', 'C', 'Q', 'S'};

asciichat_error_t acip_quic_stream_preamble(acip_traffic_class_t traffic_class,
                                            uint8_t output[ACIP_QUIC_STREAM_PREAMBLE_SIZE]) {
  if (!output || (unsigned)traffic_class > ACIP_TRAFFIC_VISUAL)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC stream traffic class");
  memcpy(output, stream_magic, sizeof(stream_magic));
  output[4] = 1;
  output[5] = (uint8_t)traffic_class;
  output[6] = output[7] = 0;
  return ASCIICHAT_OK;
}

typedef struct {
  acip_quic_stream_reader_t *reader;
  acip_quic_stream_emit_fn emit;
  void *context;
} emit_context_t;

static asciichat_error_t emit_packet(const void *packet, size_t size, void *arg) {
  emit_context_t *context = arg;
  if (size < sizeof(packet_header_t))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Truncated ACIP packet on QUIC stream");
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  uint16_t type = NET_TO_HOST_U16(header.type);
  if (type != PACKET_TYPE_ENCRYPTED && acip_packet_traffic_class(type) != context->reader->traffic_class)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "ACIP packet does not match QUIC stream class");
  return context->emit(context->reader->traffic_class, packet, size, context->context);
}

asciichat_error_t acip_quic_stream_read(acip_quic_stream_reader_t *reader, const void *bytes, size_t length,
                                        acip_quic_stream_emit_fn emit, void *context) {
  if (!reader || (!bytes && length) || !emit || reader->failed || reader->finished)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC stream input");
  const uint8_t *input = bytes;
  if (reader->preamble_used < sizeof(reader->preamble)) {
    size_t take = sizeof(reader->preamble) - reader->preamble_used;
    if (take > length)
      take = length;
    if (take) {
      memcpy(reader->preamble + reader->preamble_used, input, take);
      reader->preamble_used += take;
      input += take;
      length -= take;
    }
    if (reader->preamble_used < sizeof(reader->preamble))
      return ASCIICHAT_OK;
    if (memcmp(reader->preamble, stream_magic, sizeof(stream_magic)) != 0 || reader->preamble[4] != 1 ||
        reader->preamble[5] > ACIP_TRAFFIC_VISUAL || reader->preamble[6] || reader->preamble[7]) {
      reader->failed = true;
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid QUIC stream preamble");
    }
    reader->traffic_class = (acip_traffic_class_t)reader->preamble[5];
  }
  emit_context_t emit_context = {.reader = reader, .emit = emit, .context = context};
  asciichat_error_t result = packet_decoder_feed(&reader->packets, input, length, emit_packet, &emit_context);
  if (result != ASCIICHAT_OK)
    reader->failed = true;
  return result;
}

asciichat_error_t acip_quic_stream_finish(acip_quic_stream_reader_t *reader) {
  if (!reader || reader->failed || reader->finished)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC stream finish");
  if (reader->preamble_used != sizeof(reader->preamble) || packet_decoder_pending(&reader->packets)) {
    reader->failed = true;
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Truncated QUIC stream");
  }
  reader->finished = true;
  return ASCIICHAT_OK;
}

void acip_quic_stream_reset(acip_quic_stream_reader_t *reader) {
  if (reader) {
    packet_decoder_reset(&reader->packets);
    memset(reader, 0, sizeof(*reader));
  }
}

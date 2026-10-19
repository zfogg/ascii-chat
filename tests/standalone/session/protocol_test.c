#include "session/protocol.h"
#include <ascii-chat/common.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <string.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/compression.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "protocol line %d: %s", __LINE__, #expr);                                          \
  } while (0)

typedef struct {
  size_t frames, samples, decodes, pongs, controls;
  bool fail_audio;
} sink_t;

static asciichat_error_t frame(const ascii_frame_packet_t *header, const char *text, void *context) {
  sink_t *sink = context;
  CHECK(header->width == 4 && header->height == 1);
  CHECK(strcmp(text, "abcd") == 0);
  sink->frames++;
  return ASCIICHAT_OK;
}

static asciichat_error_t samples(const float *data, size_t count, void *context) {
  sink_t *sink = context;
  if (sink->fail_audio)
    return SET_ERRNO(ERROR_AUDIO, "Injected playback failure");
  CHECK(count == 2 && data[0] > 0.99f && data[1] < -0.99f);
  sink->samples += count;
  return ASCIICHAT_OK;
}

static asciichat_error_t decode(const void *data, size_t length, float *out, size_t capacity, size_t *count,
                                void *context) {
  sink_t *sink = context;
  CHECK(length == 1 && *(const uint8_t *)data == 42 && capacity >= 2);
  out[0] = 1.0f;
  out[1] = -1.0f;
  *count = 2;
  sink->decodes++;
  return ASCIICHAT_OK;
}

static void pong(void *context) {
  ((sink_t *)context)->controls++;
}

static asciichat_error_t send_vectors(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  CHECK(count == 2 && buffers[0].len == sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, buffers[0].data, sizeof(header));
  CHECK(NET_TO_HOST_U16(header.type) == PACKET_TYPE_PONG && NET_TO_HOST_U32(header.length) == 0);
  ((sink_t *)transport->impl_data)->pongs++;
  return ASCIICHAT_OK;
}

typedef struct {
  unsigned calls;
  bool fail;
  terminal_capabilities_packet_t packet;
} dimensions_sink_t;
static asciichat_error_t send_dimensions(const terminal_capabilities_packet_t *packet, void *context) {
  dimensions_sink_t *sink = context;
  sink->calls++;
  sink->packet = *packet;
  return sink->fail ? ERROR_NETWORK : ASCIICHAT_OK;
}
static void test_dimensions(void) {
  CHECK(options_state_init() == ASCIICHAT_OK);
  CHECK(options_set_int("width", 80) == ASCIICHAT_OK);
  CHECK(options_set_int("height", 24) == ASCIICHAT_OK);
  terminal_size_t client = {0}, discovery = {0};
  dimensions_sink_t sinks[2] = {0};
  CHECK(session_protocol_update_dimensions(&client, send_dimensions, &sinks[0]) == ASCIICHAT_OK);
  CHECK(session_protocol_update_dimensions(&discovery, send_dimensions, &sinks[1]) == ASCIICHAT_OK);
  CHECK(sinks[0].calls == 1 && sinks[1].calls == 1);
  CHECK(NET_TO_HOST_U16(sinks[1].packet.width) == 80 && NET_TO_HOST_U16(sinks[1].packet.height) == 24);
  CHECK(session_protocol_update_dimensions(&client, send_dimensions, &sinks[0]) == ASCIICHAT_OK);
  CHECK(sinks[0].calls == 1);
  CHECK(options_set_int("width", 100) == ASCIICHAT_OK);
  sinks[0].fail = true;
  CHECK(session_protocol_update_dimensions(&client, send_dimensions, &sinks[0]) == ERROR_NETWORK);
  CHECK(client.cols == 80);
  CHECK(session_protocol_update_dimensions(&discovery, send_dimensions, &sinks[1]) == ASCIICHAT_OK);
  CHECK(discovery.cols == 100 && client.cols == 80);
  CHECK(NET_TO_HOST_U16(sinks[1].packet.width) == 100 && NET_TO_HOST_U16(sinks[1].packet.height) == 24);
  sinks[0].fail = false;
  CHECK(session_protocol_update_dimensions(&client, send_dimensions, &sinks[0]) == ASCIICHAT_OK);
  CHECK(client.cols == 100 && sinks[0].calls == 3);
  options_state_destroy();
}

void test_session_protocol(void) {
  test_dimensions();
  sink_t sinks[2] = {0};
  const acip_transport_methods_t methods = {.sendv = send_vectors};
  acip_transport_t transports[2] = {{.methods = &methods, .impl_data = &sinks[0]},
                                    {.methods = &methods, .impl_data = &sinks[1]}};
  session_protocol_t protocols[2];
  for (int i = 0; i < 2; i++) {
    session_protocol_init(&protocols[i], &transports[i],
                          (session_protocol_media_t){
                              .on_frame = frame, .on_samples = samples, .decode_opus = decode, .context = &sinks[i]},
                          (acip_client_callbacks_t){.on_pong = pong, .app_ctx = &sinks[i]});
  }
  uint8_t payload[256];
  ascii_frame_packet_t header = {.width = HOST_TO_NET_U32(4),
                                 .height = HOST_TO_NET_U32(1),
                                 .original_size = HOST_TO_NET_U32(4),
                                 .checksum = HOST_TO_NET_U32(asciichat_crc32("abcd", 4))};
  memcpy(payload, &header, sizeof(header));
  memcpy(payload + sizeof(header), "abcd", 4);
  CHECK(session_protocol_dispatch(&protocols[0], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + 4) == ASCIICHAT_OK);
  CHECK(sinks[0].frames == 1 && sinks[1].frames == 0);
  CHECK(protocols[0].frames_received == 1 && protocols[1].frames_received == 0);
  payload[sizeof(header)] ^= 1;
  CHECK(session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + 4) ==
        ERROR_NETWORK_PROTOCOL);
  CHECK(sinks[1].frames == 0);
  CLEAR_ERRNO();
  void *encoded = NULL;
  size_t compressed = 0;
  CHECK(compress_data("abcd", 4, &encoded, &compressed, 1) == ASCIICHAT_OK);
  CHECK(compressed <= sizeof(payload) - sizeof(header));
  memcpy(payload + sizeof(header), encoded, compressed);
  buffer_release(encoded);
  header.flags = HOST_TO_NET_U32(FRAME_FLAG_IS_COMPRESSED);
  header.compressed_size = HOST_TO_NET_U32((uint32_t)compressed);
  memcpy(payload, &header, sizeof(header));
  CHECK(session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed) ==
        ASCIICHAT_OK);
  CHECK(sinks[1].frames == 1);
  CHECK(session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed - 1) ==
        ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();

  audio_batch_packet_t audio = {.batch_count = HOST_TO_NET_U32(1),
                                .total_samples = HOST_TO_NET_U32(2),
                                .sample_rate = HOST_TO_NET_U32(48000),
                                .channels = HOST_TO_NET_U32(1)};
  memcpy(payload, &audio, sizeof(audio));
  uint32_t quantized[2] = {HOST_TO_NET_U32(2147483647U), HOST_TO_NET_U32((uint32_t)-2147483647)};
  memcpy(payload + sizeof(audio), quantized, sizeof(quantized));
  CHECK(session_protocol_dispatch(&protocols[0], PACKET_TYPE_AUDIO_BATCH, payload, sizeof(audio) + sizeof(quantized)) ==
        ASCIICHAT_OK);
  CHECK(sinks[0].samples == 2 && sinks[1].samples == 0);
  sinks[0].fail_audio = true;
  CHECK(session_protocol_dispatch(&protocols[0], PACKET_TYPE_AUDIO_BATCH, payload, sizeof(audio) + sizeof(quantized)) ==
        ERROR_AUDIO);
  CLEAR_ERRNO();

  // Deliberately unaligned input checks size-table parsing without uint16_t loads.
  uint8_t storage[24] = {0};
  uint8_t *opus = storage + 1;
  uint32_t opus_header[4] = {HOST_TO_NET_U32(48000), HOST_TO_NET_U32(20), HOST_TO_NET_U32(2), 0};
  memcpy(opus, opus_header, sizeof(opus_header));
  uint16_t sizes[2] = {HOST_TO_NET_U16(1), HOST_TO_NET_U16(1)};
  memcpy(opus + 16, sizes, sizeof(sizes));
  opus[20] = opus[21] = 42;
  CHECK(session_protocol_dispatch(&protocols[1], PACKET_TYPE_AUDIO_OPUS_BATCH, opus, 22) == ASCIICHAT_OK);
  CHECK(sinks[1].decodes == 2 && sinks[1].samples == 4);
  CHECK(session_protocol_dispatch(&protocols[1], PACKET_TYPE_AUDIO_OPUS_BATCH, opus, 21) == ERROR_NETWORK_PROTOCOL);
  CHECK(sinks[1].decodes == 2); // Malformed batches deliver no partial audio.
  CLEAR_ERRNO();

  server_state_packet_t state = {.active_client_count = HOST_TO_NET_U32(3)};
  CHECK(session_protocol_dispatch(&protocols[1], PACKET_TYPE_SERVER_STATE, &state, sizeof(state)) == ASCIICHAT_OK);
  CHECK(protocols[1].active_client_count == 3 && protocols[1].layout_changed);
  CHECK(!protocols[0].server_state_initialized && !protocols[0].layout_changed);
  CHECK(session_protocol_dispatch(&protocols[0], PACKET_TYPE_PING, NULL, 0) == ASCIICHAT_OK);
  CHECK(sinks[0].pongs == 1 && sinks[1].pongs == 0);
  CHECK(session_protocol_dispatch(&protocols[1], PACKET_TYPE_PONG, NULL, 0) == ASCIICHAT_OK);
  CHECK(sinks[1].controls == 1 && sinks[0].controls == 0);
  log_info("PASS: shared participant protocol, compressed frames, CRC, PCM, Opus bounds, error propagation and owner "
           "isolation");
}

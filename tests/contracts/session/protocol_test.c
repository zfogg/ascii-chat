#include <criterion/criterion.h>
#include "session/protocol.h"
#include "session/participant.h"
#include <ascii-chat/common.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <string.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/compression.h>

typedef struct {
  size_t frames, samples, decodes, pongs, controls;
  bool fail_audio;
} sink_t;

static asciichat_error_t frame(const ascii_frame_packet_t *header, const char *text, void *context) {
  sink_t *sink = context;
  cr_assert(header->width == 4 && header->height == 1);
  cr_assert_eq(strcmp(text, "abcd"), 0);
  sink->frames++;
  return ASCIICHAT_OK;
}

static asciichat_error_t samples(const float *data, size_t count, void *context) {
  sink_t *sink = context;
  if (sink->fail_audio)
    return SET_ERRNO(ERROR_AUDIO, "Injected playback failure");
  cr_assert(count == 2 && data[0] > 0.99f && data[1] < -0.99f);
  sink->samples += count;
  return ASCIICHAT_OK;
}

static asciichat_error_t decode(const void *data, size_t length, float *out, size_t capacity, size_t *count,
                                void *context) {
  sink_t *sink = context;
  cr_assert(length == 1 && *(const uint8_t *)data == 42 && capacity >= 2);
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
  cr_assert(count == 2 && buffers[0].len == sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, buffers[0].data, sizeof(header));
  cr_assert(NET_TO_HOST_U16(header.type) == PACKET_TYPE_PONG && NET_TO_HOST_U32(header.length) == 0);
  ((sink_t *)transport->impl_data)->pongs++;
  return ASCIICHAT_OK;
}

typedef struct {
  unsigned calls;
  bool fail;
  options_t values;
} dimensions_sink_t;
static asciichat_error_t send_dimensions(const options_t *values, uint32_t request, void *context) {
  (void)request;
  dimensions_sink_t *sink = context;
  sink->calls++;
  sink->values = *values;
  return sink->fail ? ERROR_NETWORK : ASCIICHAT_OK;
}
static void test_dimensions(void) {
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("width", 80), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("height", 24), ASCIICHAT_OK);
  uint64_t revisions[2] = {UINT64_MAX, UINT64_MAX};
  terminal_size_t client = {0}, discovery = {0};
  dimensions_sink_t sinks[2] = {0};
  cr_assert_eq(session_protocol_update_settings(&client, &revisions[0], send_dimensions, &sinks[0]), ASCIICHAT_OK);
  cr_assert_eq(session_protocol_update_settings(&discovery, &revisions[1], send_dimensions, &sinks[1]), ASCIICHAT_OK);
  cr_assert(sinks[0].calls == 1 && sinks[1].calls == 1);
  cr_assert(sinks[1].values.width == 80 && sinks[1].values.height == 24);
  cr_assert_eq(session_protocol_update_settings(&client, &revisions[0], send_dimensions, &sinks[0]), ASCIICHAT_OK);
  cr_assert_eq(sinks[0].calls, 1);
  cr_assert_eq(options_set_int("width", 100), ASCIICHAT_OK);
  sinks[0].fail = true;
  cr_assert_eq(session_protocol_update_settings(&client, &revisions[0], send_dimensions, &sinks[0]), ERROR_NETWORK);
  cr_assert_eq(client.cols, 80);
  cr_assert_eq(session_protocol_update_settings(&discovery, &revisions[1], send_dimensions, &sinks[1]), ASCIICHAT_OK);
  cr_assert(discovery.cols == 100 && client.cols == 80);
  cr_assert(sinks[1].values.width == 100 && sinks[1].values.height == 24);
  sinks[0].fail = false;
  cr_assert_eq(session_protocol_update_settings(&client, &revisions[0], send_dimensions, &sinks[0]), ASCIICHAT_OK);
  cr_assert(client.cols == 100 && sinks[0].calls == 3);
  options_state_destroy();
}

typedef struct {
  acip_render_settings_t host;
  uint8_t reply[ACIP_SETTINGS_MAX_SIZE];
  size_t reply_length;
  unsigned callbacks;
} settings_roundtrip_t;

static bool mock_transport_is_connected(acip_transport_t *transport) {
  (void)transport;
  return true;
}

static asciichat_error_t settings_send(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  cr_assert_eq(count, 2);
  packet_header_t packet;
  memcpy(&packet, buffers[0].data, sizeof(packet));
  cr_assert_eq(NET_TO_HOST_U16(packet.type), PACKET_TYPE_ACIP_SETTINGS_SYNC);
  settings_roundtrip_t *test = transport->impl_data;
  return acip_settings_request(&test->host, buffers[1].data, buffers[1].len, test->reply, sizeof(test->reply),
                               &test->reply_length);
}

static void settings_changed(session_participant_t *participant, const session_settings_t *effective, void *context) {
  settings_roundtrip_t *test = context;
  session_settings_t readback;
  // Calling a reader from the callback verifies that notifications run unlocked.
  cr_assert_eq(session_participant_get_settings(participant, &readback), ASCIICHAT_OK);
  cr_assert(readback.version == effective->version && readback.width == effective->width);
  test->callbacks++;
}

static void test_participant_settings(void) {
  settings_roundtrip_t test = {0};
  const acip_transport_methods_t methods = {.sendv = settings_send, .is_connected = mock_transport_is_connected};
  acip_transport_t transport = {.methods = &methods, .impl_data = &test};
  session_participant_config_t config = {
      .address = "127.0.0.1", .port = 1, .callbacks.on_settings_changed = settings_changed, .user_data = &test};
  session_participant_t *participant = session_participant_create(&config);
  cr_assert(participant);
  session_settings_t requested = {
      .width = 80, .height = 24, .color_mode = TERM_COLOR_TRUECOLOR, .palette_type = PALETTE_STANDARD, .fps = 30};
  cr_assert_eq(session_participant_request_settings(participant, &requested), ERROR_INVALID_STATE);
  CLEAR_ERRNO();
  cr_assert_eq(session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  cr_assert_eq(session_participant_request_settings(participant, &requested), ASCIICHAT_OK);
  cr_assert(test.host.width == 80 && test.callbacks == 0);
  cr_assert_eq(session_participant_receive_settings(participant, test.reply, test.reply_length), ASCIICHAT_OK);
  cr_assert_eq(test.callbacks, 1);
  requested.width = 55;
  requested.palette_type = PALETTE_CUSTOM;
  SAFE_STRNCPY(requested.palette_custom, " .+#", sizeof(requested.palette_custom));
  cr_assert_eq(session_participant_request_settings(participant, &requested), ASCIICHAT_OK);
  cr_assert_eq(session_participant_receive_settings(participant, test.reply, test.reply_length), ASCIICHAT_OK);
  cr_assert(test.host.width == 55 && test.callbacks == 2 && !strcmp(test.host.palette, " .+#"));
  cr_assert_eq(session_participant_set_transport(participant, NULL, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  memset(&test.host, 0, sizeof(test.host));
  cr_assert_eq(session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  requested.width = 70;
  cr_assert_eq(session_participant_request_settings(participant, &requested), ASCIICHAT_OK);
  cr_assert_eq(session_participant_receive_settings(participant, test.reply, test.reply_length), ASCIICHAT_OK);
  cr_assert(test.callbacks == 3 && test.host.width == 70);
  cr_assert_eq(session_participant_set_transport(participant, NULL, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  session_participant_destroy(participant);

  // The public initializer supplies AUTO; callers can submit it unchanged.
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  participant = session_participant_create(&config);
  cr_assert(participant);
  session_settings_init(&requested);
  cr_assert_eq(requested.color_mode, TERM_COLOR_AUTO);
  terminal_capabilities_t caps = detect_terminal_capabilities();
  int expected_color = caps.detection_reliable ? caps.color_level : TERM_COLOR_NONE;
  memset(&test.host, 0, sizeof(test.host));
  cr_assert_eq(session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  cr_assert_eq(session_participant_request_settings(participant, &requested), ASCIICHAT_OK);
  cr_assert_eq(requested.color_mode, TERM_COLOR_AUTO);
  cr_assert_eq(test.host.caps.color_level, expected_color);
  cr_assert_eq(session_participant_receive_settings(participant, test.reply, test.reply_length), ASCIICHAT_OK);
  session_settings_t effective;
  cr_assert_eq(session_participant_get_settings(participant, &effective), ASCIICHAT_OK);
  cr_assert(effective.color_mode == expected_color && test.callbacks == 4);
  cr_assert_eq(session_participant_set_transport(participant, NULL, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  session_participant_destroy(participant);
  options_state_destroy();
}

void test_session_protocol(void) {
  test_dimensions();
  test_participant_settings();
  sink_t sinks[2] = {0};
  const acip_transport_methods_t methods = {.sendv = send_vectors};
  acip_transport_t transports[2] = {{.methods = &methods, .impl_data = &sinks[0]},
                                    {.methods = &methods, .impl_data = &sinks[1]}};
  session_protocol_t protocols[2];
  buffer_workspace_t ascii_workspace = {0};
  for (int i = 0; i < 2; i++) {
    session_protocol_init(&protocols[i], &transports[i],
                          (session_protocol_media_t){.on_frame = frame,
                                                     .on_samples = samples,
                                                     .decode_opus = decode,
                                                     .context = &sinks[i],
                                                     .frame_workspace = i == 0 ? &ascii_workspace : NULL},
                          (acip_client_callbacks_t){.on_pong = pong, .app_ctx = &sinks[i]});
  }
  uint8_t payload[256];
  ascii_frame_packet_t header = {.width = HOST_TO_NET_U32(4),
                                 .height = HOST_TO_NET_U32(1),
                                 .original_size = HOST_TO_NET_U32(4),
                                 .checksum = HOST_TO_NET_U32(asciichat_crc32("abcd", 4))};
  memcpy(payload, &header, sizeof(header));
  memcpy(payload + sizeof(header), "abcd", 4);
  cr_assert_eq(session_protocol_dispatch(&protocols[0], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + 4),
               ASCIICHAT_OK);
  cr_assert(sinks[0].frames == 1 && sinks[1].frames == 0);
  cr_assert(protocols[0].frames_received == 1 && protocols[1].frames_received == 0);
  payload[sizeof(header)] ^= 1;
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + 4),
               ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(sinks[1].frames, 0);
  CLEAR_ERRNO();
  void *encoded = NULL;
  size_t compressed = 0;
  cr_assert_eq(compress_data("abcd", 4, &encoded, &compressed, 1), ASCIICHAT_OK);
  cr_assert(compressed <= sizeof(payload) - sizeof(header));
  memcpy(payload + sizeof(header), encoded, compressed);
  buffer_release(encoded);
  header.flags = HOST_TO_NET_U32(FRAME_FLAG_IS_COMPRESSED);
  header.compressed_size = HOST_TO_NET_U32((uint32_t)compressed);
  memcpy(payload, &header, sizeof(header));
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed),
               ASCIICHAT_OK);
  cr_assert_eq(sinks[1].frames, 1);
  buffer_stats_t before, after;
  buffer_domain_stats(buffer_default_domain(), &before);
  for (unsigned i = 0; i < 100; ++i)
    cr_assert_eq(
        session_protocol_dispatch(&protocols[0], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed),
        ASCIICHAT_OK);
  buffer_domain_stats(buffer_default_domain(), &after);
  cr_assert(before.allocations == after.allocations && before.hits == after.hits);
  cr_assert_eq(
      session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed - 1),
      ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();

  audio_batch_packet_t audio = {.batch_count = HOST_TO_NET_U32(1),
                                .total_samples = HOST_TO_NET_U32(2),
                                .sample_rate = HOST_TO_NET_U32(48000),
                                .channels = HOST_TO_NET_U32(1)};
  memcpy(payload, &audio, sizeof(audio));
  uint32_t quantized[2] = {HOST_TO_NET_U32(2147483647U), HOST_TO_NET_U32((uint32_t)-2147483647)};
  memcpy(payload + sizeof(audio), quantized, sizeof(quantized));
  cr_assert_eq(
      session_protocol_dispatch(&protocols[0], PACKET_TYPE_AUDIO_BATCH, payload, sizeof(audio) + sizeof(quantized)),
      ASCIICHAT_OK);
  cr_assert(sinks[0].samples == 2 && sinks[1].samples == 0);
  sinks[0].fail_audio = true;
  cr_assert_eq(
      session_protocol_dispatch(&protocols[0], PACKET_TYPE_AUDIO_BATCH, payload, sizeof(audio) + sizeof(quantized)),
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
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_AUDIO_OPUS_BATCH, opus, 22), ASCIICHAT_OK);
  cr_assert(sinks[1].decodes == 2 && sinks[1].samples == 4);
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_AUDIO_OPUS_BATCH, opus, 21),
               ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(sinks[1].decodes, 2); // Malformed batches deliver no partial audio.
  CLEAR_ERRNO();

  server_state_packet_t state = {.active_client_count = HOST_TO_NET_U32(3)};
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_SERVER_STATE, &state, sizeof(state)), ASCIICHAT_OK);
  cr_assert(protocols[1].active_client_count == 3 && protocols[1].layout_changed);
  cr_assert(!protocols[0].server_state_initialized && !protocols[0].layout_changed);
  cr_assert_eq(session_protocol_dispatch(&protocols[0], PACKET_TYPE_PING, NULL, 0), ASCIICHAT_OK);
  cr_assert(sinks[0].pongs == 1 && sinks[1].pongs == 0);
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_PONG, NULL, 0), ASCIICHAT_OK);
  cr_assert(sinks[1].controls == 1 && sinks[0].controls == 0);
  buffer_workspace_destroy(&ascii_workspace);
  log_info("PASS: shared participant protocol, compressed frames, CRC, PCM, Opus bounds, error propagation and owner "
           "isolation");
}

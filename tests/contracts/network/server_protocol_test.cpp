#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "server/client.h"
#include "server/protocol.h"
#include "session/h265/server.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/media/codecs.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/video/rgba/video_frame.h>
#include <string.h>
#include <ascii-chat/video/h265/encoder.h>
#include <ascii-chat/video/h265/decoder.h>
#include <ascii-chat/audio/limits.h>

atomic_t g_should_exit;
client_manager_t g_client_manager;
h265_server_context_t *volatile g_h265_server;

static client_info_t *client;
static uint8_t last_packet[4096];
static size_t last_length, sends;
static asciichat_error_t capture(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  ASCIICHAT_REQUIRE_LE(length, sizeof(last_packet));
  memcpy(last_packet, data, length);
  last_length = length;
  sends++;
  return ASCIICHAT_OK;
}
static const acip_transport_methods_t methods = {.send = capture};
static acip_transport_t transport = {.methods = &methods};

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  memset(&g_client_manager, 0, sizeof(g_client_manager));
  client = &g_client_manager.clients[0];
  SAFE_STRNCPY(client->client_id, "fixture-client", sizeof(client->client_id));
  ASCIICHAT_REQUIRE_EQ(mutex_init(&client->send_mutex, "protocol-send"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(mutex_init(&client->client_state_mutex, "protocol-state"), ASCIICHAT_OK);
  client->transport = &transport;
  g_client_manager.client_count = 1;
  atomic_store_bool(&client->active, true);
  atomic_store_bool(&g_should_exit, false);
  sends = last_length = 0;
}

static void teardown(void) {
  h265_server_context_destroy(g_h265_server);
  g_h265_server = NULL;
  if (client->opus_decoder)
    opus_codec_destroy(client->opus_decoder);
  if (client->incoming_audio_buffer)
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
  if (client->incoming_video_buffer)
    video_frame_buffer_destroy(client->incoming_video_buffer);
  mutex_destroy(&client->send_mutex);
  mutex_destroy(&client->client_state_mutex);
  options_state_destroy();
  log_destroy();
}
static void reset_disconnect(void) {
  atomic_store_bool(&client->active, true);
  atomic_store_bool(&client->shutting_down, false);
  atomic_store_bool(&client->protocol_disconnect_requested, false);
}

static void expect_disconnect(void) {
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->protocol_disconnect_requested));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->shutting_down));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  ASCIICHAT_REQUIRE_GE(last_length, sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, last_packet, sizeof(header));
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(header.type), PACKET_TYPE_ERROR_MESSAGE);
}

TEST_CASE("server_protocol/join_capabilities_update_state_and_reject_invalid_flags", "[server_protocol]") {
  client_info_packet_t joined = {.capabilities =
                                     HOST_TO_NET_U32(CLIENT_CAP_VIDEO | CLIENT_CAP_AUDIO | CLIENT_CAP_STRETCH)};
  SAFE_STRNCPY(joined.display_name, "Alice", sizeof(joined.display_name));
  handle_client_join_packet(client, &joined, sizeof(joined));
  ASCIICHAT_REQUIRE_STR_EQ(client->display_name, "Alice");
  ASCIICHAT_REQUIRE(client->can_send_audio && client->can_send_video && client->wants_stretch);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  joined.capabilities = HOST_TO_NET_U32(CLIENT_CAP_AUDIO);
  handle_client_join_packet(client, &joined, sizeof(joined));
  ASCIICHAT_REQUIRE(client->can_send_audio);
  ASCIICHAT_REQUIRE_NOT(client->can_send_video || client->wants_stretch);
  const uint32_t invalid[] = {0, CLIENT_CAP_AUDIO | 0x80000000u};
  for (size_t i = 0; i < 2; ++i) {
    reset_disconnect();
    joined.capabilities = HOST_TO_NET_U32(invalid[i]);
    handle_client_join_packet(client, &joined, sizeof(joined));
    expect_disconnect();
    ASCIICHAT_REQUIRE_STR_EQ(client->display_name, "Alice");
  }
  reset_disconnect();
  joined.display_name[0] = '\0';
  handle_client_join_packet(client, &joined, sizeof(joined));
  expect_disconnect();
  reset_disconnect();
  handle_client_join_packet(client, NULL, 0);
  expect_disconnect();
  reset_disconnect();
  handle_client_join_packet(client, &joined, sizeof(joined) - 1);
  expect_disconnect();
}

TEST_CASE("server_protocol/stream_controls_toggle_media_and_keep_other_stream_enabled", "[server_protocol]") {
  uint32_t both = HOST_TO_NET_U32(STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO);
  handle_stream_start_packet(client, &both, sizeof(both));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->is_sending_audio));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->is_sending_video));
  ASCIICHAT_REQUIRE_NOT_NULL(client->opus_decoder);
  void *decoder = client->opus_decoder;
  handle_stream_start_packet(client, &both, sizeof(both));
  ASCIICHAT_REQUIRE_EQ(client->opus_decoder, decoder);
  uint32_t video = HOST_TO_NET_U32(STREAM_TYPE_VIDEO);
  handle_stream_stop_packet(client, &video, sizeof(video));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->is_sending_video));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->is_sending_audio));
  uint32_t audio = HOST_TO_NET_U32(STREAM_TYPE_AUDIO);
  handle_stream_stop_packet(client, &audio, sizeof(audio));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->is_sending_audio));
  uint32_t legacy = STREAM_TYPE_VIDEO;
  handle_stream_start_packet(client, &legacy, sizeof(legacy));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->is_sending_video));
  const uint32_t invalid[] = {0, STREAM_TYPE_VIDEO | 0x80};
  for (size_t i = 0; i < 2; ++i) {
    uint32_t flags = HOST_TO_NET_U32(invalid[i]);
    reset_disconnect();
    handle_stream_start_packet(client, &flags, sizeof(flags));
    expect_disconnect();
    reset_disconnect();
    handle_stream_stop_packet(client, &flags, sizeof(flags));
    expect_disconnect();
  }
}

TEST_CASE("server_protocol/terminal_resize_bounds_and_disconnect_are_idempotent", "[server_protocol]") {
  size_packet_t size = {.width = HOST_TO_NET_U16(120), .height = HOST_TO_NET_U16(40)};
  handle_size_packet(client, &size, sizeof(size));
  ASCIICHAT_REQUIRE_EQ(client->width, 120);
  ASCIICHAT_REQUIRE_EQ(client->height, 40);
  const uint16_t invalid[] = {0, 4097, UINT16_MAX};
  for (size_t i = 0; i < 3; ++i) {
    reset_disconnect();
    size.width = HOST_TO_NET_U16(invalid[i]);
    handle_size_packet(client, &size, sizeof(size));
    expect_disconnect();
    ASCIICHAT_REQUIRE_EQ(client->width, 120);
    ASCIICHAT_REQUIRE_EQ(client->height, 40);
  }
  size_t before = sends;
  disconnect_client_for_bad_data(client, "Repeated error");
  ASCIICHAT_REQUIRE_EQ(sends, before);
  disconnect_client_for_bad_data(NULL, NULL);
  reset_disconnect();
  disconnect_client_for_bad_data(client, NULL);
  expect_disconnect();
}

TEST_CASE("server_protocol/protocol_versions_remain_compatible_but_bad_payloads_disconnect", "[server_protocol]") {
  protocol_version_packet_t version = {.protocol_version = HOST_TO_NET_U16(PROTOCOL_VERSION_MAJOR),
                                       .protocol_revision = HOST_TO_NET_U16(PROTOCOL_VERSION_MINOR),
                                       .supports_encryption = 1,
                                       .compression_algorithms = 1,
                                       .feature_flags = HOST_TO_NET_U16(PROTOCOL_FEATURE_RECOVERABLE_ERRORS)};
  handle_protocol_version_packet(client, &version, sizeof(version));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->supports_recoverable_errors));
  version.protocol_revision = HOST_TO_NET_U16(PROTOCOL_VERSION_MINOR + 1);
  version.reserved[0] = 1;
  handle_protocol_version_packet(client, &version, sizeof(version));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  version.protocol_version = HOST_TO_NET_U16(PROTOCOL_VERSION_MAJOR + 1);
  handle_protocol_version_packet(client, &version, sizeof(version));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  handle_protocol_version_packet(client, NULL, 0);
  expect_disconnect();
  reset_disconnect();
  handle_protocol_version_packet(client, &version, sizeof(version) - 1);
  expect_disconnect();
}

TEST_CASE("server_protocol/leave_notifications_deactivate_without_requiring_a_reason", "[server_protocol]") {
  const uint8_t printable[] = "leaving\tsoon\n";
  const uint8_t binary[] = {1, 2, 3};
  handle_client_leave_packet(client, NULL, 0);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  reset_disconnect();
  handle_client_leave_packet(client, printable, sizeof(printable) - 1);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  reset_disconnect();
  handle_client_leave_packet(client, binary, sizeof(binary));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  reset_disconnect();
  handle_client_leave_packet(client, binary, 257);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  reset_disconnect();
  handle_client_leave_packet(client, NULL, 1);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->protocol_disconnect_requested));
  handle_client_leave_packet(NULL, NULL, 0);
}

TEST_CASE("server_protocol/media_packets_require_valid_payloads_and_announced_resources", "[server_protocol]") {
  float sample = 0;
  handle_audio_packet(client, &sample, sizeof(sample));
  expect_disconnect();
  reset_disconnect();
  atomic_store_bool(&client->is_sending_audio, true);
  handle_audio_packet(client, &sample, 0);
  expect_disconnect();
  reset_disconnect();
  handle_audio_packet(client, NULL, sizeof(sample));
  expect_disconnect();
  reset_disconnect();
  handle_audio_batch_packet(client, NULL, 0);
  expect_disconnect();
  reset_disconnect();
  handle_audio_batch_packet(client, &sample, sizeof(sample));
  expect_disconnect();
  reset_disconnect();
  handle_audio_opus_batch_packet(client, &sample, sizeof(sample));
  expect_disconnect();
  reset_disconnect();
  handle_audio_opus_packet(client, &sample, sizeof(sample));
  expect_disconnect();
  reset_disconnect();
  handle_image_frame_h265_packet(client, &sample, sizeof(sample));
  expect_disconnect();
  reset_disconnect();
  client->codec_capabilities_video = VIDEO_CODEC_CAP_RGBA;
  handle_image_frame_packet(client, NULL, 0);
  expect_disconnect();
}

TEST_CASE("server_protocol/legacy_rgb_frames_reach_owned_video_buffer_without_payload_changes", "[server_protocol]") {
  client->codec_capabilities_video = VIDEO_CODEC_CAP_RGBA;
  client->incoming_video_buffer = video_frame_buffer_create(client->client_id);
  ASCIICHAT_REQUIRE_NOT_NULL(client->incoming_video_buffer);
  uint8_t packet[] = {0, 0, 0, 2, 0, 0, 0, 1, 255, 0, 0, 0, 255, 0};
  handle_image_frame_packet(client, packet, sizeof(packet));
  const video_frame_t *frame = video_frame_get_latest(client->incoming_video_buffer);
  ASCIICHAT_REQUIRE_NOT_NULL(frame);
  ASCIICHAT_REQUIRE_EQ(frame->width, 2);
  ASCIICHAT_REQUIRE_EQ(frame->height, 1);
  ASCIICHAT_REQUIRE_EQ(frame->size, sizeof(packet));
  ASCIICHAT_REQUIRE_ARRAY_EQ(frame->data, packet, sizeof(packet));
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 1);
  handle_image_frame_packet(client, packet, sizeof(packet));
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 2);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->is_sending_video));
  reset_disconnect();
  handle_image_frame_packet(client, packet, sizeof(packet) - 1);
  expect_disconnect();
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 2);
  reset_disconnect();
  packet[3] = 0;
  handle_image_frame_packet(client, packet, sizeof(packet));
  expect_disconnect();
  reset_disconnect();
  client->codec_capabilities_video = 0;
  handle_image_frame_packet(client, packet, sizeof(packet));
  expect_disconnect();
}

TEST_CASE("server_protocol/raw_and_quantized_audio_are_stored_and_disabled_streams_rejected", "[server_protocol]") {
  client->incoming_audio_buffer = audio_ring_buffer_create_format(AUDIO_CHANNELS, true);
  ASCIICHAT_REQUIRE_NOT_NULL(client->incoming_audio_buffer);
  atomic_store_bool(&client->is_sending_audio, true);
  float input[] = {0, .5f, -1, 1}, output[4 * AUDIO_CHANNELS];
  handle_audio_packet(client, input, sizeof(input));
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, output, 4 * AUDIO_CHANNELS), 4 * AUDIO_CHANNELS);
  for (size_t i = 0; i < 4; i++) {
    ASCIICHAT_REQUIRE_EQ(output[i * 2], input[i]);
    ASCIICHAT_REQUIRE_EQ(output[i * 2 + 1], input[i]);
  }
  struct {
    audio_batch_packet_t header;
    uint32_t samples[4];
  } batch = {.header = {.batch_count = HOST_TO_NET_U32(1),
                        .total_samples = HOST_TO_NET_U32(4),
                        .sample_rate = HOST_TO_NET_U32(48000),
                        .channels = HOST_TO_NET_U32(1)},
             .samples = {HOST_TO_NET_U32(0), HOST_TO_NET_U32(0x40000000), HOST_TO_NET_U32(0x80000001),
                         HOST_TO_NET_U32(0x7fffffff)}};
  handle_audio_batch_packet(client, &batch, sizeof(batch));
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, output, 4 * AUDIO_CHANNELS), 4 * AUDIO_CHANNELS);
  for (size_t i = 0; i < 4; ++i) {
    ASCIICHAT_REQUIRE_FLOAT_EQ(input[i], output[i * 2], 1e-6);
    ASCIICHAT_REQUIRE_FLOAT_EQ(input[i], output[i * 2 + 1], 1e-6);
  }
  batch.header.channels = HOST_TO_NET_U32(2);
  handle_audio_batch_packet(client, &batch, sizeof(batch));
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, output, 4), 4);
  for (size_t i = 0; i < 4; i++)
    ASCIICHAT_REQUIRE_FLOAT_EQ(input[i], output[i], 1e-6);
  reset_disconnect();
  handle_audio_packet(client, input, sizeof(input) - 1);
  expect_disconnect();
  reset_disconnect();
  handle_audio_batch_packet(client, &batch, sizeof(batch) - 1);
  expect_disconnect();
  reset_disconnect();
  atomic_store_bool(&client->is_sending_audio, false);
  handle_audio_packet(client, input, sizeof(input));
  expect_disconnect();
}

TEST_CASE("server_protocol/opus_single_and_large_batches_decode_into_the_capture_ring", "[server_protocol]") {
  client->incoming_audio_buffer = audio_ring_buffer_create_format(2, true);
  client->opus_decoder = opus_codec_create_decoder(48000);
  opus_codec_t *encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
  ASCIICHAT_REQUIRE_NOT_NULL(client->incoming_audio_buffer);
  ASCIICHAT_REQUIRE_NOT_NULL(client->opus_decoder);
  ASCIICHAT_REQUIRE_NOT_NULL(encoder);
  ASCIICHAT_REQUIRE_EQ(opus_codec_set_dtx(encoder, 0), ASCIICHAT_OK);
  atomic_store_bool(&client->is_sending_audio, true);
  float samples[1920];
  for (size_t i = 0; i < 1920; ++i)
    samples[i] = (i % 48 < 24) ? .2f : -.2f;
  uint8_t encoded[1500];
  size_t encoded_size = opus_codec_encode(encoder, samples, 1920, encoded, sizeof(encoded));
  ASCIICHAT_REQUIRE_GT(encoded_size, 0);
  ASCIICHAT_REQUIRE_LE(encoded_size, sizeof(encoded));
  opus_codec_destroy(encoder);
  uint8_t single[1516] = {0};
  uint32_t rate = HOST_TO_NET_U32(48000), duration = HOST_TO_NET_U32(20);
  memcpy(single, &rate, 4);
  memcpy(single + 4, &duration, 4);
  memcpy(single + 16, encoded, encoded_size);
  handle_audio_opus_packet(client, single, 16 + encoded_size);
  float *output = SAFE_CALLOC(33 * 1920, sizeof(float), float *);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, output, 1920), 1920);
  double energy = 0;
  for (size_t i = 0; i < 1920; ++i)
    energy += output[i] * output[i];
  ASCIICHAT_REQUIRE_GT(energy, .01);
  opus_codec_t *reference = opus_codec_create_decoder(48000);
  ASCIICHAT_REQUIRE_NOT_NULL(reference);
  float expected[1920];
  ASCIICHAT_REQUIRE_EQ(opus_codec_decode(reference, encoded, encoded_size, expected, 1920), 1920);
  const size_t counts[] = {2, 33};
  for (size_t n = 0; n < 2; ++n) {
    size_t count = counts[n], length = 16 + count * (2 + encoded_size);
    uint8_t *batch = SAFE_CALLOC(length, 1, uint8_t *);
    uint32_t wire_count = HOST_TO_NET_U32(count);
    memcpy(batch, &rate, 4);
    memcpy(batch + 4, &duration, 4);
    memcpy(batch + 8, &wire_count, 4);
    uint16_t wire_size = HOST_TO_NET_U16(encoded_size);
    for (size_t i = 0; i < count; ++i) {
      memcpy(batch + 16 + 2 * i, &wire_size, 2);
      memcpy(batch + 16 + 2 * count + i * encoded_size, encoded, encoded_size);
    }
    handle_audio_opus_batch_packet(client, batch, length);
    ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->protocol_disconnect_requested));
    ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, output, count * 1920), count * 1920);
    energy = 0;
    for (size_t i = 0; i < count * 1920; ++i) {
      if (i % 1920 == 0)
        ASCIICHAT_REQUIRE_EQ(opus_codec_decode(reference, encoded, encoded_size, expected, 1920), 1920);
      ASCIICHAT_REQUIRE_FLOAT_EQ(output[i], expected[i % 1920], 1e-6);
      energy += output[i] * output[i];
    }
    ASCIICHAT_REQUIRE_GT(energy, .01);
    SAFE_FREE(batch);
  }
  opus_codec_destroy(reference);
  SAFE_FREE(output);
}

TEST_CASE("server_protocol/h265_frames_decode_to_grayscale_rgba_and_preserve_client_isolation", "[server_protocol]") {
  uint8_t input[32 * 16 * 3];
  for (size_t i = 0; i < sizeof(input); ++i)
    input[i] = (uint8_t)(32 + i % 96);
  h265_encoder_t *encoder = h265_encoder_create(32, 16);
  ASCIICHAT_REQUIRE_NOT_NULL(encoder);
  uint8_t packet[65536];
  size_t length = sizeof(packet);
  ASCIICHAT_REQUIRE_EQ(h265_encode(encoder, 32, 16, input, packet, &length), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_GT(length, 5);
  h265_encoder_destroy(encoder);
  h265_decoder_t *reference = h265_decoder_create();
  ASCIICHAT_REQUIRE_NOT_NULL(reference);
  uint8_t grayscale[32 * 16];
  uint16_t width = 0, height = 0;
  size_t size = sizeof(grayscale);
  ASCIICHAT_REQUIRE_EQ(h265_decode(reference, packet, length, &width, &height, grayscale, &size), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(width, 32);
  ASCIICHAT_REQUIRE_EQ(height, 16);
  h265_decoder_destroy(reference);
  g_h265_server = h265_server_context_create();
  ASCIICHAT_REQUIRE_NOT_NULL(g_h265_server);
  h265_server_client_t *first = h265_server_get_client_decoder(g_h265_server, 7);
  ASCIICHAT_REQUIRE_NOT_NULL(first);
  ASCIICHAT_REQUIRE_EQ(first, h265_server_get_client_decoder(g_h265_server, 7));
  h265_server_client_t *second = h265_server_get_client_decoder(g_h265_server, 8);
  ASCIICHAT_REQUIRE_NOT_NULL(second);
  ASCIICHAT_REQUIRE_NE(first, second);
  client->incoming_video_buffer = video_frame_buffer_create(client->client_id);
  ASCIICHAT_REQUIRE_NOT_NULL(client->incoming_video_buffer);
  client->codec_capabilities_video = VIDEO_CODEC_CAP_H265;
  handle_image_frame_h265_packet(client, packet, length);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->protocol_disconnect_requested));
  const video_frame_t *frame = video_frame_get_latest(client->incoming_video_buffer);
  ASCIICHAT_REQUIRE_NOT_NULL(frame);
  ASCIICHAT_REQUIRE_EQ(frame->width, width);
  ASCIICHAT_REQUIRE_EQ(frame->height, height);
  ASCIICHAT_REQUIRE_EQ(frame->size, sizeof(grayscale) * 4);
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 1);
  for (size_t i = 0; i < sizeof(grayscale); ++i) {
    ASCIICHAT_REQUIRE_EQ(((const uint8_t *)frame->data)[4 * i], grayscale[i]);
    ASCIICHAT_REQUIRE_EQ(((const uint8_t *)frame->data)[4 * i + 1], grayscale[i]);
    ASCIICHAT_REQUIRE_EQ(((const uint8_t *)frame->data)[4 * i + 2], grayscale[i]);
    ASCIICHAT_REQUIRE_EQ(((const uint8_t *)frame->data)[4 * i + 3], 255);
  }
  handle_image_frame_h265_packet(client, packet, length);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->protocol_disconnect_requested));
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 2);
  uint64_t frames = 99, keys = 99;
  h265_server_client_get_stats(second, &frames, &keys, &width, &height);
  ASCIICHAT_REQUIRE_EQ(frames, 0);
  h265_server_remove_client(g_h265_server, 7);
  h265_server_remove_client(g_h265_server, 7);
  ASCIICHAT_REQUIRE_NOT_NULL(h265_server_get_client_decoder(g_h265_server, 7));
}

TEST_CASE("server_protocol/repeated_capabilities_update_codecs_without_overwriting_committed_render_preferences", "[server_protocol]") {
  log_set_level(LOG_DEV);
  terminal_capabilities_packet_t caps = {.width = HOST_TO_NET_U16(40),
                                         .height = HOST_TO_NET_U16(12),
                                         .color_level = HOST_TO_NET_U32(TERM_COLOR_NONE),
                                         .palette_type = HOST_TO_NET_U32(PALETTE_STANDARD),
                                         .codec_capabilities_video = HOST_TO_NET_U32(VIDEO_CODEC_CAP_RGBA)};
  uint8_t request[ACIP_SETTINGS_MAX_SIZE];
  size_t length;
  acip_settings_header_t header = {.schema = 1, .expected_revision = UINT32_MAX};
  ASCIICHAT_REQUIRE_EQ(acip_settings_encode(&header, "", request, sizeof(request), &length), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(handle_runtime_settings(client, request, length), ERROR_INVALID_STATE);
  handle_client_capabilities_packet(client, &caps, sizeof(caps));
  ASCIICHAT_REQUIRE(client->has_terminal_caps);
  ASCIICHAT_REQUIRE_EQ(client->width, 40);
  ASCIICHAT_REQUIRE_EQ(client->height, 12);
  caps.width = HOST_TO_NET_U16(80);
  caps.codec_capabilities_video = HOST_TO_NET_U32(VIDEO_CODEC_CAP_RGBA | VIDEO_CODEC_CAP_H265);
  handle_client_capabilities_packet(client, &caps, sizeof(caps));
  ASCIICHAT_REQUIRE_EQ(client->width, 40);
  ASCIICHAT_REQUIRE_EQ(client->codec_capabilities_video, VIDEO_CODEC_CAP_RGBA | VIDEO_CODEC_CAP_H265);
  size_t before = sends;
  client->transport = NULL;
  ASCIICHAT_REQUIRE_EQ(handle_runtime_settings(client, request, length), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(sends, before);
  client->transport = &transport;
  atomic_store_bool(&client->shutting_down, true);
  ASCIICHAT_REQUIRE_EQ(handle_runtime_settings(client, request, length), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(sends, before);
}
TEST_CASE("server_protocol/invalid_capabilities_disconnect_before_committing_dimensions", "[server_protocol]") {
  log_set_level(LOG_DEV);
  handle_client_capabilities_packet(client, NULL, 0);
  expect_disconnect();
  reset_disconnect();
  terminal_capabilities_packet_t caps = {.width = HOST_TO_NET_U16(40), .height = 0};
  handle_client_capabilities_packet(client, &caps, sizeof(caps));
  expect_disconnect();
  ASCIICHAT_REQUIRE_NOT(client->has_terminal_caps);
  ASCIICHAT_REQUIRE_EQ(client->width, 0);
}
TEST_CASE("server_protocol/hevc_rejections_preserve_frame_count_and_handle_shutdown_without_a_decoder", "[server_protocol]") {
  log_set_level(LOG_DEV);
  client->codec_capabilities_video = VIDEO_CODEC_CAP_H265;
  const uint8_t malformed[] = {3, 0, 8, 0, 4, 0xff, 0xff};
  handle_image_frame_h265_packet(client, malformed, 4);
  expect_disconnect();
  reset_disconnect();
  handle_image_frame_h265_packet(client, malformed, sizeof(malformed));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->protocol_disconnect_requested));
  client->incoming_video_buffer = video_frame_buffer_create(client->client_id);
  ASCIICHAT_REQUIRE_NOT_NULL(client->incoming_video_buffer);
  handle_image_frame_h265_packet(client, malformed, sizeof(malformed));
  atomic_store_bool(&g_should_exit, true);
  handle_image_frame_h265_packet(client, malformed, sizeof(malformed));
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 0);
  atomic_store_bool(&g_should_exit, false);
  g_h265_server = h265_server_context_create();
  ASCIICHAT_REQUIRE_NOT_NULL(g_h265_server);
  SAFE_STRNCPY(client->client_id, "7", sizeof(client->client_id));
  handle_image_frame_h265_packet(client, malformed, sizeof(malformed));
  expect_disconnect();
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 0);
  ASCIICHAT_REQUIRE_NULL(video_frame_get_latest(client->incoming_video_buffer));
}
TEST_CASE("server_protocol/malformed_opus_batches_release_both_scratch_sizes_without_publishing_partial_audio", "[server_protocol]") {
  log_set_level(LOG_DEV);
  atomic_store_bool(&client->is_sending_audio, true);
  uint8_t single[18] = {0};
  uint32_t rate = HOST_TO_NET_U32(AUDIO_SAMPLE_RATE), duration = HOST_TO_NET_U32(20);
  memcpy(single, &rate, sizeof(rate));
  memcpy(single + 4, &duration, sizeof(duration));
  single[16] = single[17] = 0xff;
  handle_audio_opus_packet(client, single, sizeof(single));
  expect_disconnect();
  reset_disconnect();
  client->opus_decoder = opus_codec_create_decoder(AUDIO_SAMPLE_RATE);
  client->incoming_audio_buffer = audio_ring_buffer_create_format(AUDIO_CHANNELS, true);
  ASCIICHAT_REQUIRE_NOT_NULL(client->opus_decoder);
  ASCIICHAT_REQUIRE_NOT_NULL(client->incoming_audio_buffer);
  handle_audio_opus_batch_packet(client, single, 16);
  expect_disconnect();
  reset_disconnect();
  handle_audio_opus_packet(client, single, sizeof(single));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->protocol_disconnect_requested));
  const unsigned counts[] = {1, 33};
  for (size_t n = 0; n < ARRAY_SIZE(counts); ++n) {
    unsigned count = counts[n];
    uint8_t batch[16 + 33 * 4] = {0};
    memcpy(batch, &rate, sizeof(rate));
    memcpy(batch + 4, &duration, sizeof(duration));
    uint32_t wire_count = HOST_TO_NET_U32(count);
    memcpy(batch + 8, &wire_count, sizeof(wire_count));
    uint16_t frame_size = HOST_TO_NET_U16(2);
    for (unsigned i = 0; i < count; ++i) {
      memcpy(batch + 16 + 2 * i, &frame_size, sizeof(frame_size));
      memset(batch + 16 + 2 * count + 2 * i, 0xff, 2);
    }
    handle_audio_opus_batch_packet(client, batch, 16 + count * 4);
    ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->protocol_disconnect_requested));
    ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), 0);
  }
}
TEST_CASE("server_protocol/oversized_pcm_and_unsupported_formats_never_publish_audio", "[server_protocol]") {
  log_set_level(LOG_DEV);
  atomic_store_bool(&client->is_sending_audio, true);
  client->incoming_audio_buffer = audio_ring_buffer_create_format(AUDIO_CHANNELS, true);
  ASCIICHAT_REQUIRE_NOT_NULL(client->incoming_audio_buffer);
  const uint32_t count = AUDIO_BATCH_SAMPLES * 2 + 1;
  size_t length = sizeof(audio_batch_packet_t) + count * sizeof(uint32_t);
  uint8_t *packet = SAFE_CALLOC(length, 1, uint8_t *);
  audio_batch_packet_t header = {.batch_count = HOST_TO_NET_U32(1),
                                 .total_samples = HOST_TO_NET_U32(count),
                                 .sample_rate = HOST_TO_NET_U32(AUDIO_SAMPLE_RATE),
                                 .channels = HOST_TO_NET_U32(1)};
  memcpy(packet, &header, sizeof(header));
  handle_audio_batch_packet(client, packet, length);
  expect_disconnect();
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), 0);
  reset_disconnect();
  header.channels = HOST_TO_NET_U32(3);
  memcpy(packet, &header, sizeof(header));
  handle_audio_batch_packet(client, packet, length);
  expect_disconnect();
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), 0);
  SAFE_FREE(packet);
}
TEST_CASE("server_protocol/frames_without_storage_during_shutdown_and_obsolete_console_clear_do_not_send_data", "[server_protocol]") {
  log_set_level(LOG_DEV);
  client->codec_capabilities_video = VIDEO_CODEC_CAP_RGBA;
  uint8_t packet[] = {0, 0, 0, 2, 0, 0, 0, 1, 255, 0, 0, 0, 255, 0};
  handle_image_frame_packet(client, packet, sizeof(packet));
  ASCIICHAT_REQUIRE_EQ(GET_ERRNO(), ERROR_INVALID_STATE);
  CLEAR_ERRNO_ALL();
  atomic_store_bool(&g_should_exit, true);
  handle_image_frame_packet(client, packet, sizeof(packet));
  ASCIICHAT_REQUIRE_EQ(GET_ERRNO(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 0);
  size_t before = sends;
  broadcast_clear_console_to_all_clients();
  ASCIICHAT_REQUIRE_EQ(GET_ERRNO(), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(sends, before);
  ASCIICHAT_REQUIRE_EQ(send_server_state_to_client(NULL), -1);
}

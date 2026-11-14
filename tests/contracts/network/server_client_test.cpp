#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "server/main.h"
#include "server/render.h"
#include "server/protocol.h"
#include "server/connections.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/media/codecs.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/video/h265/encoder.h>
#include <string.h>

atomic_t g_should_exit;
rate_limiter_t *g_rate_limiter;
mixer_t *volatile g_audio_mixer;
h265_server_context_t *volatile g_h265_server;
bool g_server_encryption_enabled;

void process_decrypted_packet(client_info_t *, packet_type_t, void *, size_t);

static server_context_t server;
static unsigned starts, stops, sends;
static asciichat_error_t send_result;
static packet_type_t last_type;
static uint8_t last_payload[4096];
static size_t last_length;
static acip_transport_type_t transport_type;

// Worker scheduling is outside these registry and packet contracts. State,
// media buffers, codec work, crypto, packet parsing and serialization are real.
int create_client_render_threads(server_context_t *context, client_info_t *client) {
  ASCIICHAT_REQUIRE_EQ(context, &server);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  ++starts;
  return 0;
}
asciichat_error_t stop_client_render_threads(client_info_t *client) {
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  ++stops;
  return ASCIICHAT_OK;
}
static asciichat_error_t capture_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  ASCIICHAT_REQUIRE_GE(length, sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  last_type = NET_TO_HOST_U16(header.type);
  last_length = length - sizeof(header);
  ASCIICHAT_REQUIRE_LE(last_length, sizeof(last_payload));
  memcpy(last_payload, (const uint8_t *)data + sizeof(header), last_length);
  ++sends;
  return send_result;
}
static acip_transport_type_t get_type(acip_transport_t *transport) {
  (void)transport;
  return transport_type;
}
static const acip_transport_methods_t methods = {.send = capture_send, .get_type = get_type};
static acip_transport_t transport = {.methods = &methods};

static unsigned allocation_failure;
static video_frame_buffer_t *contract_video_buffer(const char *name) {
  return allocation_failure == 1 ? NULL : video_frame_buffer_create(name);
}
static audio_ring_buffer_t *contract_audio_buffer(unsigned channels, bool capture) {
  return allocation_failure == 2 ? NULL : audio_ring_buffer_create_format(channels, capture);
}
static asciichat_error_t contract_client_mutex(mutex_t *mutex, const char *name) {
  if ((allocation_failure == 3 && strcmp(name, "client_state") == 0) ||
      (allocation_failure == 4 && strcmp(name, "client_send") == 0))
    return ERROR_THREAD;
  return mutex_init(mutex, name);
}
#define video_frame_buffer_create contract_video_buffer
#define audio_ring_buffer_create_format contract_audio_buffer
#define mutex_init contract_client_mutex
#include "../../../src/server/client.c"
#undef video_frame_buffer_create
#undef audio_ring_buffer_create_format
#undef mutex_init

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_int("max_clients", 3), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(rwlock_init(&g_client_manager_rwlock, "client-registry-contract"), ASCIICHAT_OK);
  g_client_manager_rwlock_initialized = true;
  transport_type = ACIP_TRANSPORT_WEBRTC;
  send_result = ASCIICHAT_OK;
}
static void teardown(void) {
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (!g_client_manager.clients[i].client_id[0])
      continue;
    char id[MAX_CLIENT_ID_LEN];
    SAFE_STRNCPY(id, g_client_manager.clients[i].client_id, sizeof(id));
    atomic_store_bool(&g_client_manager.clients[i].shutting_down, false);
    ASCIICHAT_REQUIRE_EQ(remove_client(&server, id), 0);
  }
  ASCIICHAT_REQUIRE_EQ(g_client_manager.client_count, 0);
  ASCIICHAT_REQUIRE_NULL(g_client_manager.clients_by_id);
  rwlock_destroy(&g_client_manager_rwlock);
  g_client_manager_rwlock_initialized = false;
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static client_info_t *admit(void) {
  client_info_t *client = add_transport_client(&server, &transport, "127.0.0.1");
  ASCIICHAT_REQUIRE_NOT_NULL(client);
  return client;
}
static void dispatch(client_info_t *client, packet_type_t type, const void *data, size_t length) {
  ASCIICHAT_REQUIRE_EQ(server_dispatch_packet(client, type, data, length), ASCIICHAT_OK);
}
TEST_CASE("server_client/admission_limit_lookup_and_removal_reuse_the_slot_without_reusing_identity", "[server_client]") {
  client_info_t *first = admit();
  char id[MAX_CLIENT_ID_LEN];
  SAFE_STRNCPY(id, first->client_id, sizeof(id));
  ASCIICHAT_REQUIRE_EQ(find_client_by_id(id), first);
  ASCIICHAT_REQUIRE(first->transport_encrypted && first->security_ready);
  ASCIICHAT_REQUIRE_NOT(first->has_terminal_caps);
  ASCIICHAT_REQUIRE_NOT_NULL(first->incoming_video_buffer);
  ASCIICHAT_REQUIRE_NOT_NULL(first->incoming_audio_buffer);
  admit();
  admit();
  ASCIICHAT_REQUIRE_EQ(g_client_manager.client_count, 3);
  ASCIICHAT_REQUIRE_NULL(add_transport_client(&server, &transport, "127.0.0.1"));
  ASCIICHAT_REQUIRE_EQ(g_client_manager.client_count, 3);
  ASCIICHAT_REQUIRE_EQ(remove_client(&server, id), 0);
  ASCIICHAT_REQUIRE_NULL(find_client_by_id(id));
  ASCIICHAT_REQUIRE_EQ(stops, 1);
  client_info_t *replacement = admit();
  ASCIICHAT_REQUIRE_EQ(replacement, first);
  ASCIICHAT_REQUIRE_STR_NE(replacement->client_id, id);
  ASCIICHAT_REQUIRE_EQ(g_client_manager.client_count, 3);
  ASCIICHAT_REQUIRE_NULL(find_client_by_id(NULL));
  ASCIICHAT_REQUIRE_NULL(find_client_by_id(""));
}
TEST_CASE("server_client/worker_readiness_sends_state_before_start_and_counts_active_publishers", "[server_client]") {
  client_info_t *client = admit();
  char id[MAX_CLIENT_ID_LEN];
  SAFE_STRNCPY(id, client->client_id, sizeof(id));
  ASCIICHAT_REQUIRE_EQ(start_transport_client_threads(&server, id), 0);
  ASCIICHAT_REQUIRE_EQ(starts, 1);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->event_ready));
  ASCIICHAT_REQUIRE_EQ(last_type, PACKET_TYPE_SERVER_STATE);
  server_state_packet_t state;
  memcpy(&state, last_payload, sizeof(state));
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(state.connected_client_count), 1);
  uint32_t stream = HOST_TO_NET_U32(STREAM_TYPE_VIDEO);
  dispatch(client, PACKET_TYPE_STREAM_START, &stream, sizeof(stream));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->is_sending_video));
  ASCIICHAT_REQUIRE_EQ(server_send_state(&transport), ASCIICHAT_OK);
  memcpy(&state, last_payload, sizeof(state));
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U32(state.active_client_count), 1);
  dispatch(client, PACKET_TYPE_STREAM_STOP, &stream, sizeof(stream));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->is_sending_video));
  ASCIICHAT_REQUIRE_EQ(start_transport_client_threads(&server, "missing"), -1);
  ASCIICHAT_REQUIRE_EQ(start_transport_client_threads(NULL, id), -1);
}
TEST_CASE("server_client/raw_callback_owns_pixels_and_tracks_latest_frame_and_sequence", "[server_client]") {
  client_info_t *client = admit();
  struct {
    image_frame_packet_t header;
    uint8_t pixels[12];
  } packet = {.header = {.width = HOST_TO_NET_U32(2),
                         .height = HOST_TO_NET_U32(2),
                         .pixel_format = HOST_TO_NET_U32(PIXEL_FORMAT_RGB)},
              .pixels = {255, 0, 0, 0, 255, 0, 0, 0, 255, 80, 80, 80}};
  uint8_t expected[12];
  memcpy(expected, packet.pixels, sizeof(expected));
  dispatch(client, PACKET_TYPE_IMAGE_FRAME, &packet, sizeof(packet));
  const video_frame_t *frame = video_frame_get_latest(client->incoming_video_buffer);
  ASCIICHAT_REQUIRE_NOT_NULL(frame);
  ASCIICHAT_REQUIRE_EQ(frame->width, 2);
  ASCIICHAT_REQUIRE_EQ(frame->height, 2);
  ASCIICHAT_REQUIRE_EQ(frame->sequence_number, 1);
  ASCIICHAT_REQUIRE_ARRAY_EQ((uint8_t *)frame->data + 8, expected, sizeof(expected));
  memset(packet.pixels, 0, sizeof(packet.pixels));
  ASCIICHAT_REQUIRE_ARRAY_EQ((uint8_t *)frame->data + 8, expected, sizeof(expected));
  dispatch(client, PACKET_TYPE_IMAGE_FRAME, &packet, sizeof(packet));
  frame = video_frame_get_latest(client->incoming_video_buffer);
  ASCIICHAT_REQUIRE_EQ(frame->sequence_number, 2);
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 2);
}
TEST_CASE("server_client/audio_callbacks_ignore_unannounced_samples_then_deliver_to_capture_ring", "[server_client]") {
  client_info_t *client = admit();
  float samples[8] = {0.125f, -0.25f, 0.5f, -0.75f};
  struct {
    audio_batch_packet_t header;
    uint32_t samples[8];
  } packet = {0};
  packet.header.sample_rate = HOST_TO_NET_U32(AUDIO_SAMPLE_RATE);
  packet.header.channels = HOST_TO_NET_U32(1);
  packet.header.total_samples = HOST_TO_NET_U32(8);
  packet.header.batch_count = HOST_TO_NET_U32(1);
  for (size_t i = 0; i < 8; ++i)
    packet.samples[i] = HOST_TO_NET_U32((uint32_t)(int32_t)((double)samples[i] * 2147483647.0));
  dispatch(client, PACKET_TYPE_AUDIO_BATCH, &packet, sizeof(packet));
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), 0);
  uint32_t stream = HOST_TO_NET_U32(STREAM_TYPE_AUDIO);
  dispatch(client, PACKET_TYPE_STREAM_START, &stream, sizeof(stream));
  dispatch(client, PACKET_TYPE_AUDIO_BATCH, &packet, sizeof(packet));
  float actual[8 * AUDIO_CHANNELS];
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, actual, ARRAY_SIZE(actual)), ARRAY_SIZE(actual));
  for (size_t i = 0; i < ARRAY_SIZE(actual); ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(actual[i], samples[i / AUDIO_CHANNELS], 1e-4);
}
TEST_CASE("server_client/ping_is_serialized_and_shutdown_suppresses_the_reply", "[server_client]") {
  client_info_t *client = admit();
  sends = 0;
  dispatch(client, PACKET_TYPE_PING, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(sends, 1);
  ASCIICHAT_REQUIRE_EQ(last_type, PACKET_TYPE_PONG);
  ASCIICHAT_REQUIRE_EQ(last_length, 0);
  send_result = ERROR_NETWORK;
  dispatch(client, PACKET_TYPE_PING, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(sends, 2);
  atomic_store_bool(&client->shutting_down, true);
  dispatch(client, PACKET_TYPE_PING, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(sends, 2);
  atomic_store_bool(&client->shutting_down, false);
}

TEST_CASE("server_client/encoded_uploads_reuse_the_decoder_and_switch_between_hevc_and_h264", "[server_client]") {
  client_info_t *client = admit();
  client->codec_capabilities_video = VIDEO_CODEC_CAP_H265 | VIDEO_CODEC_CAP_H264;
  uint8_t rgb[64 * 32 * 3];
  memset(rgb, 128, sizeof(rgb));
  const video_codec_t codecs[] = {VIDEO_CODEC_H265, VIDEO_CODEC_H264, VIDEO_CODEC_H265};
  unsigned received = 0;
  for (size_t c = 0; c < sizeof(codecs) / sizeof(codecs[0]); ++c) {
    h265_encoder_t *encoder = video_encoder_create(codecs[c], 64, 32);
    ASCIICHAT_REQUIRE_NOT_NULL(encoder);
    for (unsigned i = 0; i < 2; ++i) {
      uint8_t encoded[65536];
      size_t length = sizeof(encoded);
      ASCIICHAT_REQUIRE_EQ(h265_encode(encoder, 64, 32, rgb, encoded, &length), ASCIICHAT_OK);
      ASCIICHAT_REQUIRE_GT(length, 5);
      dispatch(client, codecs[c] == VIDEO_CODEC_H265 ? PACKET_TYPE_IMAGE_FRAME_H265 : PACKET_TYPE_IMAGE_FRAME_H264,
               encoded, length);
      const video_frame_t *frame = video_frame_get_latest(client->incoming_video_buffer);
      ASCIICHAT_REQUIRE_NOT_NULL(frame);
      ASCIICHAT_REQUIRE_EQ(frame->width, 64);
      ASCIICHAT_REQUIRE_EQ(frame->height, 32);
      ASCIICHAT_REQUIRE_EQ(frame->size, sizeof(rgb) + 8);
      ASCIICHAT_REQUIRE_EQ(frame->sequence_number, ++received);
      const uint8_t *decoded = (const uint8_t *)frame->data + 8;
      for (size_t p = 0; p < sizeof(rgb); ++p)
        ASCIICHAT_REQUIRE(decoded[p] >= 123 && decoded[p] <= 133, "Unexpected RGB byte %zu: %u", p, decoded[p]);
      ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->protocol_disconnect_requested));
    }
    h265_encoder_destroy(encoder);
  }
  ASCIICHAT_REQUIRE_NOT_NULL(client->video_decoder_ctx);
  ASCIICHAT_REQUIRE_NOT_NULL(client->video_decode_frame);
  ASCIICHAT_REQUIRE_NOT_NULL(client->video_rgb_frame);
  ASCIICHAT_REQUIRE_NOT_NULL(client->video_sws_ctx);
  cleanup_client_media_buffers(client);
  ASCIICHAT_REQUIRE_NULL(client->video_decoder_ctx);
  ASCIICHAT_REQUIRE_NULL(client->video_decode_frame);
  ASCIICHAT_REQUIRE_NULL(client->video_rgb_frame);
  ASCIICHAT_REQUIRE_NULL(client->video_sws_ctx);
  cleanup_client_media_buffers(client);
}

TEST_CASE("server_client/unadvertised_encoded_upload_disconnects_without_creating_a_decoder", "[server_client]") {
  client_info_t *client = admit();
  client->codec_capabilities_video = VIDEO_CODEC_CAP_RGBA;
  uint8_t rgb[64 * 32 * 3] = {0}, packet[65536];
  h265_encoder_t *encoder = h265_encoder_create(64, 32);
  ASCIICHAT_REQUIRE_NOT_NULL(encoder);
  size_t length = sizeof(packet);
  ASCIICHAT_REQUIRE_EQ(h265_encode(encoder, 64, 32, rgb, packet, &length), ASCIICHAT_OK);
  dispatch(client, PACKET_TYPE_IMAGE_FRAME_H265, packet, length);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->protocol_disconnect_requested));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  ASCIICHAT_REQUIRE_NULL(client->video_decoder_ctx);
  ASCIICHAT_REQUIRE_NULL(video_frame_get_latest(client->incoming_video_buffer));
  h265_encoder_destroy(encoder);
}

static void resume_client(client_info_t *client) {
  atomic_store_bool(&client->active, true);
  atomic_store_bool(&client->protocol_disconnect_requested, false);
  atomic_store_bool(&client->shutting_down, false);
  atomic_store_bool(&client->is_sending_audio, true);
  atomic_store_bool(&client->is_sending_video, false);
  CLEAR_ERRNO_ALL();
}
static void expect_rejected(client_info_t *client) {
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->protocol_disconnect_requested));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
}
TEST_CASE("server_client/compatibility_ping_serializes_failures_and_shutdown_without_dropping_transport", "[server_client]") {
  client_info_t *client = admit();
  handle_ping_packet(client, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(last_type, PACKET_TYPE_PONG);
  unsigned before = sends;
  send_result = ERROR_NETWORK;
  handle_ping_packet(client, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(GET_ERRNO(), ERROR_NETWORK);
  CLEAR_ERRNO_ALL();
  atomic_store_bool(&client->shutting_down, true);
  handle_ping_packet(client, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(sends, before + 1);
  atomic_store_bool(&client->shutting_down, false);
  client->transport = NULL;
  handle_ping_packet(client, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(sends, before + 1);
  client->transport = &transport;
  handle_pong_packet(client, NULL, 0);
}
TEST_CASE("server_client/remote_logs_validate_direction_and_preserve_client_media", "[server_client]") {
  client_info_t *client = admit();
  struct {
    remote_log_packet_t header;
    char text[5];
  } packet = {.header = {.log_level = LOG_INFO,
                         .direction = REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER,
                         .message_length = HOST_TO_NET_U32(5)},
              .text = {'h', 'e', 'l', 'l', 'o'}};
  handle_remote_log_packet_from_client(client, &packet, sizeof(packet));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  SAFE_STRNCPY(client->display_name, "remote-test", sizeof(client->display_name));
  packet.header.flags = HOST_TO_NET_U16(REMOTE_LOG_FLAG_TRUNCATED);
  handle_remote_log_packet_from_client(client, &packet, sizeof(packet));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  packet.header.direction = REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT;
  handle_remote_log_packet_from_client(client, &packet, sizeof(packet));
  expect_rejected(client);
  resume_client(client);
  handle_remote_log_packet_from_client(client, &packet, 1);
  expect_rejected(client);
  handle_remote_log_packet_from_client(NULL, NULL, 0);
}
TEST_CASE("server_client/legacy_raw_frames_validate_dimensions_length_and_own_successful_pixels", "[server_client]") {
  client_info_t *client = admit();
  client->codec_capabilities_video = VIDEO_CODEC_CAP_RGBA;
  struct {
    uint32_t width, height;
    uint8_t pixels[12];
  } packet = {.width = HOST_TO_NET_U32(2), .height = HOST_TO_NET_U32(2), .pixels = {10, 20, 30}};
  const uint32_t bad[][2] = {{0, 2}, {2, 0}, {UINT32_MAX, 2}, {2, UINT32_MAX}};
  for (unsigned i = 0; i < ARRAY_SIZE(bad); ++i) {
    resume_client(client);
    packet.width = HOST_TO_NET_U32(bad[i][0]);
    packet.height = HOST_TO_NET_U32(bad[i][1]);
    handle_image_frame_packet(client, &packet, sizeof(packet));
    expect_rejected(client);
    ASCIICHAT_REQUIRE_NULL(video_frame_get_latest(client->incoming_video_buffer));
  }
  packet.width = packet.height = HOST_TO_NET_U32(2);
  resume_client(client);
  handle_image_frame_packet(client, &packet, sizeof(packet) - 1);
  expect_rejected(client);
  resume_client(client);
  handle_image_frame_packet(client, NULL, 0);
  expect_rejected(client);
  resume_client(client);
  handle_image_frame_packet(client, &packet, sizeof(packet));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  const video_frame_t *frame = video_frame_get_latest(client->incoming_video_buffer);
  ASCIICHAT_REQUIRE_NOT_NULL(frame);
  ASCIICHAT_REQUIRE_EQ(frame->width, 2);
  ASCIICHAT_REQUIRE_EQ(frame->height, 2);
  ASCIICHAT_REQUIRE_ARRAY_EQ((uint8_t *)frame->data + 8, packet.pixels, sizeof(packet.pixels));
  client->frames_received_logged = 24999;
  handle_image_frame_packet(client, &packet, sizeof(packet));
  ASCIICHAT_REQUIRE_EQ(client->frames_received_logged, 25000);
  client->codec_capabilities_video = 0;
  handle_image_frame_packet(client, &packet, sizeof(packet));
  expect_rejected(client);
}
TEST_CASE("server_client/legacy_pcm_validates_stream_alignment_limits_and_recovers_with_a_valid_batch", "[server_client]") {
  client_info_t *client = admit();
  float samples[AUDIO_SAMPLES_PER_PACKET + 1] = {0.25f, -0.5f};
  const size_t bad_sizes[] = {1, 0, sizeof(samples)};
  for (unsigned i = 0; i < ARRAY_SIZE(bad_sizes); ++i) {
    resume_client(client);
    handle_audio_packet(client, samples, bad_sizes[i]);
    expect_rejected(client);
    ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), 0);
  }
  resume_client(client);
  handle_audio_packet(client, NULL, sizeof(float));
  expect_rejected(client);
  resume_client(client);
  atomic_store_bool(&client->is_sending_audio, false);
  handle_audio_packet(client, samples, sizeof(float) * 2);
  expect_rejected(client);
  resume_client(client);
  handle_audio_packet(client, samples, sizeof(float) * 2);
  float actual[2 * AUDIO_CHANNELS];
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, actual, ARRAY_SIZE(actual)), ARRAY_SIZE(actual));
  for (size_t i = 0; i < ARRAY_SIZE(actual); ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(actual[i], samples[i / AUDIO_CHANNELS], 1e-6);
}
TEST_CASE("server_client/legacy_pcm_batch_rejects_bad_headers_and_counts_without_publishing_samples", "[server_client]") {
  client_info_t *client = admit();
  struct {
    audio_batch_packet_t header;
    uint32_t samples[4];
  } p = {0};
  const uint32_t counts[][2] = {{0, 4}, {1, 0}, {1, 4}, {1, UINT32_MAX}};
  for (unsigned i = 0; i < ARRAY_SIZE(counts); ++i) {
    resume_client(client);
    p.header = (audio_batch_packet_t){.batch_count = HOST_TO_NET_U32(counts[i][0]),
                                      .total_samples = HOST_TO_NET_U32(counts[i][1]),
                                      .sample_rate = HOST_TO_NET_U32(48000),
                                      .channels = HOST_TO_NET_U32(1)};
    handle_audio_batch_packet(client, &p, sizeof(p) - 1);
    expect_rejected(client);
    ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), 0);
  }
  resume_client(client);
  handle_audio_batch_packet(client, NULL, 0);
  expect_rejected(client);
  resume_client(client);
  p.header.total_samples = HOST_TO_NET_U32(4);
  p.header.batch_count = HOST_TO_NET_U32(1);
  p.samples[0] = HOST_TO_NET_U32(1073741823);
  handle_audio_batch_packet(client, &p, sizeof(p));
  float actual[4 * AUDIO_CHANNELS];
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, actual, ARRAY_SIZE(actual)), ARRAY_SIZE(actual));
  ASCIICHAT_REQUIRE_FLOAT_EQ(actual[0], 0.5, 1e-6);
}
TEST_CASE("server_client/single_opus_compatibility_rejects_bad_metadata_and_decodes_real_audio", "[server_client]") {
  client_info_t *client = admit();
  uint8_t packet[4096] = {0};
  client->opus_decoder = opus_codec_create_decoder(AUDIO_SAMPLE_RATE);
  ASCIICHAT_REQUIRE_NOT_NULL(client->opus_decoder);
  const uint32_t metadata[][2] = {{0, 20}, {192001, 20}, {48000, 0}, {48000, 121}, {192000, 120}, {1, 1}};
  for (unsigned i = 0; i < ARRAY_SIZE(metadata); ++i) {
    resume_client(client);
    uint32_t rate = HOST_TO_NET_U32(metadata[i][0]), duration = HOST_TO_NET_U32(metadata[i][1]);
    memcpy(packet, &rate, 4);
    memcpy(packet + 4, &duration, 4);
    handle_audio_opus_packet(client, packet, 17);
    expect_rejected(client);
  }
  resume_client(client);
  handle_audio_opus_packet(client, NULL, 0);
  expect_rejected(client);
  resume_client(client);
  handle_audio_opus_packet(client, packet, 16);
  expect_rejected(client);
  resume_client(client);
  atomic_store_bool(&client->is_sending_audio, false);
  handle_audio_opus_packet(client, packet, 17);
  expect_rejected(client);
  resume_client(client);
  float samples[OPUS_FRAME_SAMPLES * AUDIO_CHANNELS];
  for (unsigned i = 0; i < ARRAY_SIZE(samples); ++i)
    samples[i] = (i % 32 < 16 ? 0.25f : -0.25f);
  opus_codec_t *encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
  ASCIICHAT_REQUIRE_NOT_NULL(encoder);
  int size = opus_codec_encode(encoder, samples, ARRAY_SIZE(samples), packet + 16, sizeof(packet) - 16);
  ASCIICHAT_REQUIRE_GT(size, 0);
  uint32_t rate = HOST_TO_NET_U32(48000), duration = HOST_TO_NET_U32(20);
  memcpy(packet, &rate, 4);
  memcpy(packet + 4, &duration, 4);
  handle_audio_opus_packet(client, packet, 16 + size);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), OPUS_FRAME_SAMPLES * AUDIO_CHANNELS);
  opus_codec_destroy(encoder);
}

TEST_CASE("server_client/decrypted_dispatch_routes_controls_and_rejects_unknown_packets", "[server_client]") {
  client_info_t *client = admit();
  client_info_packet_t join = {.capabilities = HOST_TO_NET_U32(CLIENT_CAP_AUDIO | CLIENT_CAP_VIDEO)};
  SAFE_STRNCPY(join.display_name, "Decrypted", sizeof(join.display_name));
  process_decrypted_packet(client, PACKET_TYPE_CLIENT_JOIN, &join, sizeof(join));
  ASCIICHAT_REQUIRE_STR_EQ(client->display_name, "Decrypted");
  terminal_capabilities_packet_t caps = {
      .width = HOST_TO_NET_U16(80), .height = HOST_TO_NET_U16(24), .color_level = HOST_TO_NET_U32(TERM_COLOR_NONE)};
  process_decrypted_packet(client, PACKET_TYPE_CLIENT_CAPABILITIES, &caps, sizeof(caps));
  ASCIICHAT_REQUIRE(client->has_terminal_caps);
  uint32_t video = HOST_TO_NET_U32(STREAM_TYPE_VIDEO);
  process_decrypted_packet(client, PACKET_TYPE_STREAM_START, &video, sizeof(video));
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->is_sending_video));
  process_decrypted_packet(client, PACKET_TYPE_STREAM_STOP, &video, sizeof(video));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->is_sending_video));
  process_decrypted_packet(client, PACKET_TYPE_PING, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(last_type, PACKET_TYPE_PONG);
  process_decrypted_packet(client, PACKET_TYPE_PONG, NULL, 0);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  process_decrypted_packet(client, (packet_type_t)65000, NULL, 0);
  expect_rejected(client);
}
TEST_CASE("server_client/decrypted_dispatch_enforces_real_rate_limits_and_reports_backend_failure", "[server_client]") {
  client_info_t *client = admit();
  g_rate_limiter = rate_limiter_create_memory();
  ASCIICHAT_REQUIRE_NOT_NULL(g_rate_limiter);
  process_decrypted_packet(client, PACKET_TYPE_PING, NULL, 0);
  ASCIICHAT_REQUIRE_EQ(last_type, PACKET_TYPE_PONG);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  for (uint32_t i = 0; i < DEFAULT_RATE_LIMITS[RATE_EVENT_CLIENT_JOIN].max_events; ++i)
    ASCIICHAT_REQUIRE_EQ(rate_limiter_record(g_rate_limiter, client->client_ip, RATE_EVENT_CLIENT_JOIN), ASCIICHAT_OK);
  client_info_packet_t join = {.capabilities = HOST_TO_NET_U32(CLIENT_CAP_AUDIO)};
  SAFE_STRNCPY(join.display_name, "limited", sizeof(join.display_name));
  process_decrypted_packet(client, PACKET_TYPE_CLIENT_JOIN, &join, sizeof(join));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  ASCIICHAT_REQUIRE_EQ(last_type, PACKET_TYPE_ERROR_MESSAGE);
  rate_limiter_destroy(g_rate_limiter);
  g_rate_limiter = rate_limiter_create_sqlite(NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(g_rate_limiter);
  atomic_store_bool(&client->active, true);
  send_result = ERROR_NETWORK;
  process_decrypted_packet(client, PACKET_TYPE_PING, NULL, 0);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
  ASCIICHAT_REQUIRE_EQ(last_type, PACKET_TYPE_ERROR_MESSAGE);
  rate_limiter_destroy(g_rate_limiter);
  g_rate_limiter = NULL;
}
TEST_CASE("server_client/callback_dispatch_delivers_join_capabilities_keepalive_and_leave", "[server_client]") {
  client_info_t *client = admit();
  client_info_packet_t join = {.capabilities = HOST_TO_NET_U32(CLIENT_CAP_AUDIO | CLIENT_CAP_VIDEO)};
  SAFE_STRNCPY(join.display_name, "Callback", sizeof(join.display_name));
  dispatch(client, PACKET_TYPE_CLIENT_JOIN, &join, sizeof(join));
  ASCIICHAT_REQUIRE_STR_EQ(client->display_name, "Callback");
  terminal_capabilities_packet_t caps = {
      .width = HOST_TO_NET_U16(100), .height = HOST_TO_NET_U16(30), .color_level = HOST_TO_NET_U32(TERM_COLOR_NONE)};
  dispatch(client, PACKET_TYPE_CLIENT_CAPABILITIES, &caps, sizeof(caps));
  ASCIICHAT_REQUIRE_EQ(client->width, 100);
  ASCIICHAT_REQUIRE_EQ(client->height, 30);
  dispatch(client, PACKET_TYPE_PONG, NULL, 0);
  dispatch(client, PACKET_TYPE_CLIENT_LEAVE, NULL, 0);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&client->active));
}

TEST_CASE("server_client/admission_rolls_back_partial_buffers_and_sync_before_a_successful_retry", "[server_client]") {
  for (unsigned failure = 1; failure <= 4; ++failure) {
    allocation_failure = failure;
    ASCIICHAT_REQUIRE_NULL(add_transport_client(&server, &transport, "127.0.0.1"));
    ASCIICHAT_REQUIRE_EQ(g_client_manager.client_count, 0);
    ASCIICHAT_REQUIRE_NULL(g_client_manager.clients_by_id);
    ASCIICHAT_REQUIRE_EQ(g_client_manager.clients[0].client_id[0], 0);
    ASCIICHAT_REQUIRE_NULL(g_client_manager.clients[0].incoming_video_buffer);
    ASCIICHAT_REQUIRE_NULL(g_client_manager.clients[0].incoming_audio_buffer);
    CLEAR_ERRNO_ALL();
  }
  allocation_failure = 0;
  client_info_t *client = admit();
  ASCIICHAT_REQUIRE_EQ(find_client_by_id(client->client_id), client);
  ASCIICHAT_REQUIRE_NULL(add_transport_client(NULL, &transport, "127.0.0.1"));
  ASCIICHAT_REQUIRE_NULL(add_transport_client(&server, NULL, "127.0.0.1"));
  ASCIICHAT_REQUIRE_NULL(add_transport_client(&server, &transport, NULL));
  ASCIICHAT_REQUIRE_EQ(remove_client(NULL, client->client_id), -1);
  ASCIICHAT_REQUIRE_EQ(remove_client(&server, "missing"), -1);
  ASCIICHAT_REQUIRE_EQ(g_client_manager.client_count, 1);
}
TEST_CASE("server_client/concurrent_removal_is_idempotent_and_failed_state_send_does_not_skip_worker_start", "[server_client]") {
  client_info_t *client = admit();
  send_result = ERROR_NETWORK;
  ASCIICHAT_REQUIRE_EQ(start_transport_client_threads(&server, client->client_id), 0);
  ASCIICHAT_REQUIRE_EQ(starts, 1);
  atomic_store_bool(&client->shutting_down, true);
  ASCIICHAT_REQUIRE_EQ(remove_client(&server, client->client_id), 0);
  ASCIICHAT_REQUIRE_EQ(g_client_manager.client_count, 1);
  atomic_store_bool(&client->shutting_down, false);
}
TEST_CASE("server_client/single_opus_callback_wraps_real_audio_and_ignores_incompatible_metadata", "[server_client]") {
  client_info_t *client = admit();
  resume_client(client);
  client->opus_decoder = opus_codec_create_decoder(48000);
  ASCIICHAT_REQUIRE_NOT_NULL(client->opus_decoder);
  uint8_t packet[4096] = {0};
  acip_server_on_audio_opus(packet, 15, client, NULL);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), 0);
  float samples[OPUS_FRAME_SAMPLES * AUDIO_CHANNELS];
  for (unsigned i = 0; i < ARRAY_SIZE(samples); ++i)
    samples[i] = i % 32 < 16 ? 0.25f : -0.25f;
  opus_codec_t *encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
  ASCIICHAT_REQUIRE_NOT_NULL(encoder);
  int length = opus_codec_encode(encoder, samples, ARRAY_SIZE(samples), packet + 16, sizeof(packet) - 16);
  ASCIICHAT_REQUIRE_GT(length, 0);
  write_u32_unaligned(packet, HOST_TO_NET_U32(44100));
  write_u32_unaligned(packet + 4, HOST_TO_NET_U32(20));
  acip_server_on_audio_opus(packet, length + 16, client, NULL);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), 0);
  write_u32_unaligned(packet, HOST_TO_NET_U32(48000));
  acip_server_on_audio_opus(packet, length + 16, client, NULL);
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_available_read(client->incoming_audio_buffer), OPUS_FRAME_SAMPLES * AUDIO_CHANNELS);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  opus_codec_destroy(encoder);
}
TEST_CASE("server_client/compatibility_callbacks_validate_protocol_audio_and_remote_log_before_mutating_client", "[server_client]") {
  client_info_t *client = admit();
  protocol_version_packet_t version = {.protocol_version = HOST_TO_NET_U16(PROTOCOL_VERSION_MAJOR)};
  acip_server_on_protocol_version(&version, client, NULL);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  version.protocol_version = HOST_TO_NET_U16(PROTOCOL_VERSION_MAJOR + 1);
  acip_server_on_protocol_version(&version, client, NULL);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  resume_client(client);
  float samples[2] = {0.25f, -0.25f};
  acip_server_on_audio(samples, sizeof(samples), client, NULL);
  float actual[2 * AUDIO_CHANNELS];
  ASCIICHAT_REQUIRE_EQ(audio_ring_buffer_read(client->incoming_audio_buffer, actual, ARRAY_SIZE(actual)), ARRAY_SIZE(actual));
  for (size_t i = 0; i < ARRAY_SIZE(actual); ++i)
    ASCIICHAT_REQUIRE_FLOAT_EQ(actual[i], samples[i / AUDIO_CHANNELS], 1e-6);
  remote_log_packet_t header = {
      .log_level = LOG_INFO, .direction = REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER, .message_length = HOST_TO_NET_U32(5)};
  acip_server_on_remote_log(&header, "hello", client, NULL);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  header.direction = REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT;
  acip_server_on_remote_log(&header, "hello", client, NULL);
  expect_rejected(client);
}
TEST_CASE("server_client/image_callback_rejects_invalid_dimensions_and_handles_an_absent_video_buffer", "[server_client]") {
  client_info_t *client = admit();
  image_frame_packet_t header = {.width = 2, .height = 2};
  uint8_t pixels[12] = {0};
  acip_server_on_image_frame(NULL, pixels, sizeof(pixels), client, NULL);
  acip_server_on_image_frame(&header, NULL, sizeof(pixels), client, NULL);
  acip_server_on_image_frame(&header, pixels, 2, client, NULL);
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 0);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  header.width = 0;
  acip_server_on_image_frame(&header, pixels, sizeof(pixels), client, NULL);
  expect_rejected(client);
  resume_client(client);
  header.width = UINT32_MAX;
  acip_server_on_image_frame(&header, pixels, sizeof(pixels), client, NULL);
  expect_rejected(client);
  resume_client(client);
  header.width = 2;
  video_frame_buffer_destroy(client->incoming_video_buffer);
  client->incoming_video_buffer = NULL;
  acip_server_on_image_frame(&header, pixels, sizeof(pixels), client, NULL);
  ASCIICHAT_REQUIRE(atomic_load_bool(&client->active));
  ASCIICHAT_REQUIRE_EQ(client->frames_received, 0);
  ASCIICHAT_REQUIRE_EQ(client->frames_received_logged, 1);
}

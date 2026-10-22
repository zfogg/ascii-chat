#include <criterion/criterion.h>
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

atomic_t g_should_exit;
client_manager_t g_client_manager;
h265_server_context_t *volatile g_h265_server;

static client_info_t *client;
static uint8_t last_packet[4096];
static size_t last_length, sends;
static asciichat_error_t capture(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  cr_assert_leq(length, sizeof(last_packet));
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
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  memset(&g_client_manager, 0, sizeof(g_client_manager));
  client = &g_client_manager.clients[0];
  SAFE_STRNCPY(client->client_id, "fixture-client", sizeof(client->client_id));
  cr_assert_eq(mutex_init(&client->send_mutex, "protocol-send"), ASCIICHAT_OK);
  cr_assert_eq(mutex_init(&client->client_state_mutex, "protocol-state"), ASCIICHAT_OK);
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

TestSuite(server_protocol, .init = setup, .fini = teardown, .timeout = 15);

static void reset_disconnect(void) {
  atomic_store_bool(&client->active, true);
  atomic_store_bool(&client->shutting_down, false);
  atomic_store_bool(&client->protocol_disconnect_requested, false);
}

static void expect_disconnect(void) {
  cr_assert(atomic_load_bool(&client->protocol_disconnect_requested));
  cr_assert(atomic_load_bool(&client->shutting_down));
  cr_assert_not(atomic_load_bool(&client->active));
  cr_assert_geq(last_length, sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, last_packet, sizeof(header));
  cr_assert_eq(NET_TO_HOST_U16(header.type), PACKET_TYPE_ERROR_MESSAGE);
}

Test(server_protocol, join_capabilities_update_state_and_reject_invalid_flags) {
  client_info_packet_t joined = {.capabilities =
                                     HOST_TO_NET_U32(CLIENT_CAP_VIDEO | CLIENT_CAP_AUDIO | CLIENT_CAP_STRETCH)};
  SAFE_STRNCPY(joined.display_name, "Alice", sizeof(joined.display_name));
  handle_client_join_packet(client, &joined, sizeof(joined));
  cr_assert_str_eq(client->display_name, "Alice");
  cr_assert(client->can_send_audio && client->can_send_video && client->wants_stretch);
  cr_assert(atomic_load_bool(&client->active));
  joined.capabilities = HOST_TO_NET_U32(CLIENT_CAP_AUDIO);
  handle_client_join_packet(client, &joined, sizeof(joined));
  cr_assert(client->can_send_audio);
  cr_assert_not(client->can_send_video || client->wants_stretch);
  const uint32_t invalid[] = {0, CLIENT_CAP_AUDIO | 0x80000000u};
  for (size_t i = 0; i < 2; ++i) {
    reset_disconnect();
    joined.capabilities = HOST_TO_NET_U32(invalid[i]);
    handle_client_join_packet(client, &joined, sizeof(joined));
    expect_disconnect();
    cr_assert_str_eq(client->display_name, "Alice");
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

Test(server_protocol, stream_controls_toggle_media_and_keep_other_stream_enabled) {
  uint32_t both = HOST_TO_NET_U32(STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO);
  handle_stream_start_packet(client, &both, sizeof(both));
  cr_assert(atomic_load_bool(&client->is_sending_audio));
  cr_assert(atomic_load_bool(&client->is_sending_video));
  cr_assert_not_null(client->opus_decoder);
  void *decoder = client->opus_decoder;
  handle_stream_start_packet(client, &both, sizeof(both));
  cr_assert_eq(client->opus_decoder, decoder);
  uint32_t video = HOST_TO_NET_U32(STREAM_TYPE_VIDEO);
  handle_stream_stop_packet(client, &video, sizeof(video));
  cr_assert_not(atomic_load_bool(&client->is_sending_video));
  cr_assert(atomic_load_bool(&client->is_sending_audio));
  uint32_t audio = HOST_TO_NET_U32(STREAM_TYPE_AUDIO);
  handle_stream_stop_packet(client, &audio, sizeof(audio));
  cr_assert_not(atomic_load_bool(&client->is_sending_audio));
  uint32_t legacy = STREAM_TYPE_VIDEO;
  handle_stream_start_packet(client, &legacy, sizeof(legacy));
  cr_assert(atomic_load_bool(&client->is_sending_video));
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

Test(server_protocol, terminal_resize_bounds_and_disconnect_are_idempotent) {
  size_packet_t size = {.width = HOST_TO_NET_U16(120), .height = HOST_TO_NET_U16(40)};
  handle_size_packet(client, &size, sizeof(size));
  cr_assert_eq(client->width, 120);
  cr_assert_eq(client->height, 40);
  const uint16_t invalid[] = {0, 4097, UINT16_MAX};
  for (size_t i = 0; i < 3; ++i) {
    reset_disconnect();
    size.width = HOST_TO_NET_U16(invalid[i]);
    handle_size_packet(client, &size, sizeof(size));
    expect_disconnect();
    cr_assert_eq(client->width, 120);
    cr_assert_eq(client->height, 40);
  }
  size_t before = sends;
  disconnect_client_for_bad_data(client, "Repeated error");
  cr_assert_eq(sends, before);
  disconnect_client_for_bad_data(NULL, NULL);
  reset_disconnect();
  disconnect_client_for_bad_data(client, NULL);
  expect_disconnect();
}

Test(server_protocol, protocol_versions_remain_compatible_but_bad_payloads_disconnect) {
  protocol_version_packet_t version = {.protocol_version = HOST_TO_NET_U16(PROTOCOL_VERSION_MAJOR),
                                       .protocol_revision = HOST_TO_NET_U16(PROTOCOL_VERSION_MINOR),
                                       .supports_encryption = 1,
                                       .compression_algorithms = 1,
                                       .feature_flags = HOST_TO_NET_U16(PROTOCOL_FEATURE_RECOVERABLE_ERRORS)};
  handle_protocol_version_packet(client, &version, sizeof(version));
  cr_assert(atomic_load_bool(&client->active));
  cr_assert(atomic_load_bool(&client->supports_recoverable_errors));
  version.protocol_revision = HOST_TO_NET_U16(PROTOCOL_VERSION_MINOR + 1);
  version.reserved[0] = 1;
  handle_protocol_version_packet(client, &version, sizeof(version));
  cr_assert(atomic_load_bool(&client->active));
  version.protocol_version = HOST_TO_NET_U16(PROTOCOL_VERSION_MAJOR + 1);
  handle_protocol_version_packet(client, &version, sizeof(version));
  cr_assert(atomic_load_bool(&client->active));
  handle_protocol_version_packet(client, NULL, 0);
  expect_disconnect();
  reset_disconnect();
  handle_protocol_version_packet(client, &version, sizeof(version) - 1);
  expect_disconnect();
}

Test(server_protocol, leave_notifications_deactivate_without_requiring_a_reason) {
  const uint8_t printable[] = "leaving\tsoon\n";
  const uint8_t binary[] = {1, 2, 3};
  handle_client_leave_packet(client, NULL, 0);
  cr_assert_not(atomic_load_bool(&client->active));
  reset_disconnect();
  handle_client_leave_packet(client, printable, sizeof(printable) - 1);
  cr_assert_not(atomic_load_bool(&client->active));
  reset_disconnect();
  handle_client_leave_packet(client, binary, sizeof(binary));
  cr_assert_not(atomic_load_bool(&client->active));
  reset_disconnect();
  handle_client_leave_packet(client, binary, 257);
  cr_assert_not(atomic_load_bool(&client->active));
  reset_disconnect();
  handle_client_leave_packet(client, NULL, 1);
  cr_assert(atomic_load_bool(&client->active));
  cr_assert_not(atomic_load_bool(&client->protocol_disconnect_requested));
  handle_client_leave_packet(NULL, NULL, 0);
}

Test(server_protocol, media_packets_require_valid_payloads_and_announced_resources) {
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

Test(server_protocol, legacy_rgb_frames_reach_owned_video_buffer_without_payload_changes) {
  client->codec_capabilities_video = VIDEO_CODEC_CAP_RGBA;
  client->incoming_video_buffer = video_frame_buffer_create(client->client_id);
  cr_assert_not_null(client->incoming_video_buffer);
  uint8_t packet[] = {0, 0, 0, 2, 0, 0, 0, 1, 255, 0, 0, 0, 255, 0};
  handle_image_frame_packet(client, packet, sizeof(packet));
  const video_frame_t *frame = video_frame_get_latest(client->incoming_video_buffer);
  cr_assert_not_null(frame);
  cr_assert_eq(frame->width, 2);
  cr_assert_eq(frame->height, 1);
  cr_assert_eq(frame->size, sizeof(packet));
  cr_assert_arr_eq(frame->data, packet, sizeof(packet));
  cr_assert_eq(client->frames_received, 1);
  handle_image_frame_packet(client, packet, sizeof(packet));
  cr_assert_eq(client->frames_received, 2);
  cr_assert(atomic_load_bool(&client->is_sending_video));
  reset_disconnect();
  handle_image_frame_packet(client, packet, sizeof(packet) - 1);
  expect_disconnect();
  cr_assert_eq(client->frames_received, 2);
  reset_disconnect();
  packet[3] = 0;
  handle_image_frame_packet(client, packet, sizeof(packet));
  expect_disconnect();
  reset_disconnect();
  client->codec_capabilities_video = 0;
  handle_image_frame_packet(client, packet, sizeof(packet));
  expect_disconnect();
}

Test(server_protocol, raw_and_quantized_audio_are_stored_and_disabled_streams_rejected) {
  client->incoming_audio_buffer = audio_ring_buffer_create_for_capture();
  cr_assert_not_null(client->incoming_audio_buffer);
  atomic_store_bool(&client->is_sending_audio, true);
  float input[] = {0, .5f, -1, 1}, output[4];
  handle_audio_packet(client, input, sizeof(input));
  cr_assert_eq(audio_ring_buffer_read(client->incoming_audio_buffer, output, 4), 4);
  cr_assert_arr_eq(input, output, sizeof(input));
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
  cr_assert_eq(audio_ring_buffer_read(client->incoming_audio_buffer, output, 4), 4);
  for (size_t i = 0; i < 4; ++i)
    cr_assert_float_eq(input[i], output[i], 1e-6);
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

Test(server_protocol, opus_single_and_large_batches_decode_into_the_capture_ring) {
  client->incoming_audio_buffer = audio_ring_buffer_create_for_capture();
  client->opus_decoder = opus_codec_create_decoder(48000);
  opus_codec_t *encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
  cr_assert_not_null(client->incoming_audio_buffer);
  cr_assert_not_null(client->opus_decoder);
  cr_assert_not_null(encoder);
  cr_assert_eq(opus_codec_set_dtx(encoder, 0), ASCIICHAT_OK);
  atomic_store_bool(&client->is_sending_audio, true);
  float samples[960];
  for (size_t i = 0; i < 960; ++i)
    samples[i] = (i % 48 < 24) ? .2f : -.2f;
  uint8_t encoded[1500];
  size_t encoded_size = opus_codec_encode(encoder, samples, 960, encoded, sizeof(encoded));
  cr_assert_gt(encoded_size, 0);
  cr_assert_leq(encoded_size, sizeof(encoded));
  opus_codec_destroy(encoder);
  uint8_t single[1516] = {0};
  uint32_t rate = HOST_TO_NET_U32(48000), duration = HOST_TO_NET_U32(20);
  memcpy(single, &rate, 4);
  memcpy(single + 4, &duration, 4);
  memcpy(single + 16, encoded, encoded_size);
  handle_audio_opus_packet(client, single, 16 + encoded_size);
  float *output = SAFE_CALLOC(33 * 960, sizeof(float), float *);
  cr_assert_eq(audio_ring_buffer_read(client->incoming_audio_buffer, output, 960), 960);
  double energy = 0;
  for (size_t i = 0; i < 960; ++i)
    energy += output[i] * output[i];
  cr_assert_gt(energy, .01);
  opus_codec_t *reference = opus_codec_create_decoder(48000);
  cr_assert_not_null(reference);
  float expected[960];
  cr_assert_eq(opus_codec_decode(reference, encoded, encoded_size, expected, 960), 960);
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
    cr_assert_not(atomic_load_bool(&client->protocol_disconnect_requested));
    cr_assert_eq(audio_ring_buffer_read(client->incoming_audio_buffer, output, count * 960), count * 960);
    energy = 0;
    for (size_t i = 0; i < count * 960; ++i) {
      if (i % 960 == 0)
        cr_assert_eq(opus_codec_decode(reference, encoded, encoded_size, expected, 960), 960);
      cr_assert_float_eq(output[i], expected[i % 960], 1e-6);
      energy += output[i] * output[i];
    }
    cr_assert_gt(energy, .01);
    SAFE_FREE(batch);
  }
  opus_codec_destroy(reference);
  SAFE_FREE(output);
}

Test(server_protocol, h265_frames_decode_to_grayscale_rgba_and_preserve_client_isolation) {
  uint8_t input[32 * 16 * 3];
  for (size_t i = 0; i < sizeof(input); ++i)
    input[i] = (uint8_t)(32 + i % 96);
  h265_encoder_t *encoder = h265_encoder_create(32, 16);
  cr_assert_not_null(encoder);
  uint8_t packet[65536];
  size_t length = sizeof(packet);
  cr_assert_eq(h265_encode(encoder, 32, 16, input, packet, &length), ASCIICHAT_OK);
  cr_assert_gt(length, 5);
  h265_encoder_destroy(encoder);
  h265_decoder_t *reference = h265_decoder_create();
  cr_assert_not_null(reference);
  uint8_t grayscale[32 * 16];
  uint16_t width = 0, height = 0;
  size_t size = sizeof(grayscale);
  cr_assert_eq(h265_decode(reference, packet, length, &width, &height, grayscale, &size), ASCIICHAT_OK);
  cr_assert_eq(width, 32);
  cr_assert_eq(height, 16);
  h265_decoder_destroy(reference);
  g_h265_server = h265_server_context_create();
  cr_assert_not_null(g_h265_server);
  h265_server_client_t *first = h265_server_get_client_decoder(g_h265_server, 7);
  cr_assert_not_null(first);
  cr_assert_eq(first, h265_server_get_client_decoder(g_h265_server, 7));
  h265_server_client_t *second = h265_server_get_client_decoder(g_h265_server, 8);
  cr_assert_not_null(second);
  cr_assert_neq(first, second);
  client->incoming_video_buffer = video_frame_buffer_create(client->client_id);
  cr_assert_not_null(client->incoming_video_buffer);
  client->codec_capabilities_video = VIDEO_CODEC_CAP_H265;
  handle_image_frame_h265_packet(client, packet, length);
  cr_assert_not(atomic_load_bool(&client->protocol_disconnect_requested));
  const video_frame_t *frame = video_frame_get_latest(client->incoming_video_buffer);
  cr_assert_not_null(frame);
  cr_assert_eq(frame->width, width);
  cr_assert_eq(frame->height, height);
  cr_assert_eq(frame->size, sizeof(grayscale) * 4);
  cr_assert_eq(client->frames_received, 1);
  for (size_t i = 0; i < sizeof(grayscale); ++i) {
    cr_assert_eq(((const uint8_t *)frame->data)[4 * i], grayscale[i]);
    cr_assert_eq(((const uint8_t *)frame->data)[4 * i + 1], grayscale[i]);
    cr_assert_eq(((const uint8_t *)frame->data)[4 * i + 2], grayscale[i]);
    cr_assert_eq(((const uint8_t *)frame->data)[4 * i + 3], 255);
  }
  handle_image_frame_h265_packet(client, packet, length);
  cr_assert_not(atomic_load_bool(&client->protocol_disconnect_requested));
  cr_assert_eq(client->frames_received, 2);
  uint64_t frames = 99, keys = 99;
  h265_server_client_get_stats(second, &frames, &keys, &width, &height);
  cr_assert_eq(frames, 0);
  h265_server_remove_client(g_h265_server, 7);
  h265_server_remove_client(g_h265_server, 7);
  cr_assert_not_null(h265_server_get_client_decoder(g_h265_server, 7));
}

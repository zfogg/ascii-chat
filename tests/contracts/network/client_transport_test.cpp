#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include "client/connection_internal.h"
#include "client/server.h"
#include "client/display.h"
#include "client/keepalive.h"
#include "session/display.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/video/h265/encoder.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <string.h>
#include <math.h>

static client_connection_t connection;
static atomic_t exit_requested, send_count;
static asciichat_error_t send_result;
typedef struct {
  packet_type_t type;
  size_t length;
  uint8_t data[65536];
} record_t;
static record_t records[8];

bool should_exit(void) {
  return atomic_load_bool(&exit_requested);
}
asciichat_error_t client_connection_checkpoint(const char *stage) {
  (void)stage;
  return ASCIICHAT_OK;
}
// This callback can run on the keepalive worker. Inspect records only after
// joining it, and propagate transport failures through production state.
static asciichat_error_t capture_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  if (length < sizeof(packet_header_t))
    return ERROR_NETWORK_SIZE;
  uint64_t index = atomic_fetch_add_u64(&send_count, 1);
  if (index >= 8 || length - sizeof(packet_header_t) > sizeof(records[0].data))
    return ERROR_NETWORK_SIZE;
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  records[index].type = NET_TO_HOST_U16(header.type);
  records[index].length = length - sizeof(header);
  memcpy(records[index].data, (const uint8_t *)data + sizeof(header), records[index].length);
  return send_result;
}
static bool connected(acip_transport_t *transport) {
  (void)transport;
  return true;
}
static const acip_transport_methods_t methods = {.send = capture_send, .is_connected = connected};
static acip_transport_t transport = {.methods = &methods};
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  REQUIRE((server_connection_init(&connection)) == (0));
  server_connection_set_transport(&connection, &transport);
  send_result = ASCIICHAT_OK;
}
static void teardown(void) {
  server_connection_shutdown(&connection);
  if (connection.client_worker_pool) {
    REQUIRE((thread_pool_stop_all(connection.client_worker_pool)) == (ASCIICHAT_OK));
    keepalive_stop_thread(&connection);
    thread_pool_destroy(connection.client_worker_pool);
  }
  h265_encoder_destroy(connection.video_encoder);
  buffer_workspace_destroy(&connection.encoded_video_workspace);
  display_cleanup();
  mutex_destroy(&connection.send_mutex);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TEST_CASE("client_transport/endpoint_metadata_and_transport_selection_are_scoped_to_the_connection", "[client_transport]") {
  server_connection_set_ip(&connection, "::1");
  server_connection_set_port(&connection, 27001);
  connection.my_client_id = 42;
  REQUIRE(std::strcmp((server_connection_get_ip(&connection)), ("::1")) == 0);
  REQUIRE((server_connection_get_port(&connection)) == (27001));
  REQUIRE((server_connection_get_client_id(&connection)) == (42));
  server_connection_set_remote_error(&connection, ERROR_CRYPTO);
  REQUIRE((server_connection_get_remote_error(&connection)) == (ERROR_CRYPTO));
  server_connection_set_ip(&connection, NULL);
  REQUIRE((server_connection_get_ip)[0] == '\0');
  server_connection_lost(&connection);
  REQUIRE(server_connection_is_lost(&connection));
  REQUIRE(!(server_connection_is_active(&connection)));
  server_connection_set_transport(&connection, &transport);
  REQUIRE(server_connection_is_active(&connection));
  REQUIRE(!(server_connection_is_lost(&connection)));
  server_connection_set_transport(&connection, NULL);
  REQUIRE((server_connection_get_transport(&connection)) == nullptr);
  REQUIRE(!(server_connection_is_active(&connection)));
  REQUIRE(!(server_connection_is_active(NULL)));
  REQUIRE(server_connection_is_lost(NULL));
}
TEST_CASE("client_transport/control_packets_preserve_network_order_and_capability_acknowledgement_state", "[client_transport]") {
  REQUIRE((threaded_send_ping_packet(&connection)) == (ASCIICHAT_OK));
  REQUIRE((records[0].type) == (PACKET_TYPE_PING));
  REQUIRE((atomic_load_u64(&connection.telemetry_ping_ns)) > (0));
  REQUIRE((atomic_load_u64(&connection.telemetry_ping_ns)) != (UINT64_MAX));
  server_connection_pong_received(&connection);
  REQUIRE((atomic_load_u64(&connection.telemetry_ping_ns)) == (0));
  REQUIRE((threaded_send_pong_packet(&connection)) == (ASCIICHAT_OK));
  REQUIRE((records[1].type) == (PACKET_TYPE_PONG));
  REQUIRE((threaded_send_stream_start_packet(&connection, STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO)) == (ASCIICHAT_OK));
  uint32_t flags;
  memcpy(&flags, records[2].data, sizeof(flags));
  REQUIRE((NET_TO_HOST_U32(flags)) == (STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO));
  REQUIRE((threaded_send_terminal_size_with_auto_detect(&connection, 80, 24)) == (ASCIICHAT_OK));
  REQUIRE((records[3].type) == (PACKET_TYPE_CLIENT_CAPABILITIES));
  terminal_capabilities_packet_t capabilities;
  memcpy(&capabilities, records[3].data, sizeof(capabilities));
  REQUIRE((NET_TO_HOST_U16(capabilities.width)) == (80));
  REQUIRE((NET_TO_HOST_U16(capabilities.height)) == (24));
  REQUIRE((connection.advertised_dimensions.cols) == (80));
  send_result = ERROR_NETWORK;
  REQUIRE((threaded_send_terminal_size_with_auto_detect(&connection, 100, 30)) == (ERROR_NETWORK));
  REQUIRE((connection.advertised_dimensions.cols) == (80));
  REQUIRE((connection.advertised_dimensions.rows) == (24));
}
TEST_CASE("client_transport/zero_clock_value_does_not_collide_with_the_no_ping_sentinel", "[client_transport]") {
  client_connection_mark_ping_sent(&connection, 0);
  REQUIRE((atomic_load_u64(&connection.telemetry_ping_ns)) == (1));
  server_connection_pong_received(&connection);
  REQUIRE((atomic_load_u64(&connection.telemetry_ping_ns)) == (0));
  client_connection_mark_ping_sent(&connection, 0);
  client_connection_mark_ping_sent(&connection, 0);
  REQUIRE((atomic_load_u64(&connection.telemetry_ping_ns)) == (UINT64_MAX));
  atomic_store_u64(&connection.telemetry_ping_ns, 0);
  client_connection_mark_ping_sent(&connection, 42);
  REQUIRE((atomic_load_u64(&connection.telemetry_ping_ns)) == (42));
}
TEST_CASE("client_transport/local_payload_rejection_keeps_the_connection_but_network_failure_marks_it_lost", "[client_transport]") {
  const uint8_t pixel[3] = {10, 20, 30};
  REQUIRE((threaded_send_image_frame(&connection, NULL, 1, 1, PIXEL_FORMAT_RGB)) == (ERROR_INVALID_PARAM));
  REQUIRE(server_connection_is_active(&connection));
  send_result = ERROR_INVALID_PARAM;
  REQUIRE((threaded_send_packet(&connection, PACKET_TYPE_PING, NULL, 0)) == (ERROR_INVALID_PARAM));
  REQUIRE(server_connection_is_active(&connection));
  REQUIRE(!(server_connection_is_lost(&connection)));
  send_result = ERROR_NETWORK;
  REQUIRE((threaded_send_image_frame(&connection, pixel, 1, 1, PIXEL_FORMAT_RGB)) == (ERROR_NETWORK));
  REQUIRE(server_connection_is_lost(&connection));
  REQUIRE(!(server_connection_is_active(&connection)));
}
TEST_CASE("client_transport/single_opus_uses_one_frame_batch_framing_and_batch_keeps_frame_sizes", "[client_transport]") {
  const uint8_t encoded[6] = {1, 2, 3, 4, 5, 6};
  REQUIRE((threaded_send_audio_opus(&connection, encoded, sizeof(encoded), 48000, 20)) == (ASCIICHAT_OK));
  REQUIRE((records[0].type) == (PACKET_TYPE_AUDIO_OPUS_BATCH));
  REQUIRE((records[0].length) == (18 + sizeof(encoded)));
  uint32_t rate, duration;
  memcpy(&rate, records[0].data, sizeof(rate));
  memcpy(&duration, records[0].data + 4, sizeof(duration));
  REQUIRE((NET_TO_HOST_U32(rate)) == (48000));
  REQUIRE((NET_TO_HOST_U32(duration)) == (20));
  REQUIRE(std::memcmp((records[0].data + 18), (encoded), (sizeof(encoded))) == 0);
  const uint16_t sizes[] = {2, 4};
  REQUIRE((threaded_send_audio_opus_batch(&connection, encoded, sizeof(encoded), sizes, 2)) == (ASCIICHAT_OK));
  REQUIRE((records[1].type) == (PACKET_TYPE_AUDIO_OPUS_BATCH));
  REQUIRE((records[1].length) == (16 + sizeof(sizes) + sizeof(encoded)));
  uint16_t on_wire[2];
  memcpy(on_wire, records[1].data + 16, sizeof(on_wire));
  REQUIRE((NET_TO_HOST_U16(on_wire[0])) == (2));
  REQUIRE((NET_TO_HOST_U16(on_wire[1])) == (4));
}
TEST_CASE("client_transport/inactive_connection_rejects_all_audio_sends_without_touching_the_transport", "[client_transport]") {
  server_connection_set_transport(&connection, NULL);
  float sample = 0;
  uint8_t encoded = 0;
  uint16_t size = 1;
  REQUIRE((threaded_send_audio_batch_packet(&connection, &sample, 1, 1)) == (ERROR_NETWORK));
  REQUIRE((threaded_send_audio_opus(&connection, &encoded, 1, 48000, 20)) == (ERROR_NETWORK));
  REQUIRE((threaded_send_audio_opus_batch(&connection, &encoded, 1, &size, 1)) == (ERROR_NETWORK));
  REQUIRE((atomic_load_u64(&send_count)) == (0));
}
static void receive_pcm(const audio_batch_packet_t *header, const float *samples, size_t count, void *client, void *context) {
  (void)client;
  REQUIRE((count) == (4));
  REQUIRE((header->sample_rate) == (AUDIO_SAMPLE_RATE));
  REQUIRE((header->batch_count) == (1));
  const float *expected = context;
  for (size_t i = 0; i < count; ++i)
    REQUIRE((samples[i]) == Catch::Approx(expected[i]));
}
TEST_CASE("client_transport/pcm_batches_preserve_signed_samples_and_audio_send_errors_mark_connection_loss", "[client_transport]") {
  float samples[] = {0.0f, 0.5f, -0.5f, 0.25f};
  REQUIRE((threaded_send_audio_batch_packet(&connection, samples, 4, 1)) == (ASCIICHAT_OK));
  REQUIRE((records[0].type) == (PACKET_TYPE_AUDIO_BATCH));
  REQUIRE((records[0].length) == (sizeof(audio_batch_packet_t) + 4 * sizeof(uint32_t)));
  audio_batch_packet_t header;
  memcpy(&header, records[0].data, sizeof(header));
  REQUIRE((NET_TO_HOST_U32(header.total_samples)) == (4));
  REQUIRE((NET_TO_HOST_U32(header.batch_count)) == (1));
  uint32_t values[4];
  memcpy(values, records[0].data + sizeof(header), sizeof(values));
  for (unsigned i = 0; i < 4; ++i)
    REQUIRE(((double)(int32_t)NET_TO_HOST_U32(values[i]) / 2147483647.0) == Catch::Approx(samples[i]));
  acip_server_callbacks_t callbacks = {.on_audio_batch = receive_pcm, .app_ctx = samples};
  REQUIRE((acip_handle_server_packet(&transport, records[0].type, records[0].data, records[0].length, NULL, &callbacks)) == (ASCIICHAT_OK));
  send_result = ERROR_NETWORK;
  REQUIRE((threaded_send_audio_batch_packet(&connection, samples, 4, 1)) == (ERROR_NETWORK));
  REQUIRE(server_connection_is_lost(&connection));
  server_connection_set_transport(&connection, &transport);
  uint8_t encoded = 0;
  REQUIRE((threaded_send_audio_opus(&connection, &encoded, 1, 48000, 20)) == (ERROR_NETWORK));
  REQUIRE(server_connection_is_lost(&connection));
  server_connection_set_transport(&connection, &transport);
  uint16_t size = 1;
  REQUIRE((threaded_send_audio_opus_batch(&connection, &encoded, 1, &size, 1)) == (ERROR_NETWORK));
  REQUIRE(server_connection_is_lost(&connection));
}
TEST_CASE("client_transport/pcm_sender_bounds_payload_size_clips_finite_samples_and_rejects_nan_before_send", "[client_transport]") {
  float samples[OPUS_FRAME_SAMPLES + 1];
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    samples[i] = i & 1 ? 2.0f : -2.0f;
  REQUIRE((acip_send_audio_batch(&transport, samples, OPUS_FRAME_SAMPLES + 1, 1)) == (ASCIICHAT_OK));
  const uint8_t *wire = records[0].data + sizeof(audio_batch_packet_t);
  for (unsigned i = 0; i <= OPUS_FRAME_SAMPLES; ++i) {
    uint32_t encoded;
    memcpy(&encoded, wire + i * sizeof(encoded), sizeof(encoded));
    REQUIRE(((int32_t)NET_TO_HOST_U32(encoded)) == (i & 1 ? INT32_MAX : -INT32_MAX));
  }
  samples[OPUS_FRAME_SAMPLES] = NAN;
  REQUIRE((acip_send_audio_batch(&transport, samples, OPUS_FRAME_SAMPLES + 1, 1)) == (ERROR_INVALID_PARAM));
  REQUIRE((acip_send_audio_batch(&transport, samples, UINT32_MAX, 1)) == (ERROR_NETWORK_SIZE));
  REQUIRE((atomic_load_u64(&send_count)) == (1));
}
TEST_CASE("client_transport/malformed_single_frame_size_is_rejected_before_sending_and_does_not_lose_connection", "[client_transport]") {
  uint8_t sample = 0;
  REQUIRE((threaded_send_audio_opus(&connection, &sample, UINT16_MAX + 1u, 48000, 20)) == (ERROR_INVALID_PARAM));
  REQUIRE((atomic_load_u64(&send_count)) == (0));
  REQUIRE(server_connection_is_active(&connection));
  REQUIRE(!(server_connection_is_lost(&connection)));
}
TEST_CASE("client_transport/encoded_uploads_reuse_workspace_and_report_encoding_failure_separately_from_network_failure", "[client_transport]") {
  uint8_t rgb[64 * 32 * 3];
  memset(rgb, 128, sizeof(rgb));
  bool failed = true;
  REQUIRE((threaded_send_image_frame_encoded(&connection, rgb, UINT16_MAX + 1u, 32, VIDEO_CODEC_H265, &failed)) == (ERROR_INVALID_PARAM));
  REQUIRE(!(failed));
  REQUIRE((connection.video_encoder) == nullptr);
  REQUIRE((threaded_send_image_frame_encoded(&connection, rgb, 64, 32, VIDEO_CODEC_H265, NULL)) == (ERROR_INVALID_PARAM));
  const video_codec_t codecs[] = {VIDEO_CODEC_H264, VIDEO_CODEC_H265};
  for (unsigned c = 0; c < 2; ++c) {
    for (unsigned i = 0; i < 2; ++i) {
      REQUIRE((threaded_send_image_frame_encoded(&connection, rgb, 64, 32, codecs[c], &failed)) == (ASCIICHAT_OK));
      REQUIRE(!(failed));
      REQUIRE((records[c * 2 + i].type) == (codecs[c] == VIDEO_CODEC_H264 ? PACKET_TYPE_IMAGE_FRAME_H264 : PACKET_TYPE_IMAGE_FRAME_H265));
      REQUIRE((records[c * 2 + i].length) > (5));
      uint16_t width, height;
      memcpy(&width, records[c * 2 + i].data + 1, sizeof(width));
      memcpy(&height, records[c * 2 + i].data + 3, sizeof(height));
      REQUIRE((NET_TO_HOST_U16(width)) == (64));
      REQUIRE((NET_TO_HOST_U16(height)) == (32));
    }
    h265_encoder_destroy(connection.video_encoder);
    connection.video_encoder = NULL;
  }
  REQUIRE((connection.encoded_video_workspace.data) != nullptr);
  send_result = ERROR_NETWORK;
  REQUIRE((threaded_send_image_frame_encoded(&connection, rgb, 64, 32, VIDEO_CODEC_H265, &failed)) == (ERROR_NETWORK));
  REQUIRE(!(failed));
  REQUIRE(server_connection_is_lost(&connection));
}
TEST_CASE("client_transport/display_cleanup_retains_the_framework_owned_context_and_resets_first_frame_state", "[client_transport]") {
  session_display_config_t config = {.snapshot_mode = true, .palette_type = PALETTE_STANDARD, .color_mode = TERM_COLOR_NONE};
  session_display_ctx_t *display = session_display_create(&config);
  REQUIRE((display) != nullptr);
  connection.display_context = display;
  display_set_context(display);
  display_reset_for_new_connection(&connection);
  REQUIRE(atomic_load_bool(&connection.display_first_frame));
  display_disable_logging_for_first_frame(&connection);
  REQUIRE(!(atomic_load_bool(&connection.display_first_frame)));
  display_disable_logging_for_first_frame(&connection);
  display_render_frame(&connection, NULL);
  REQUIRE((GET_ERRNO()) == (ERROR_INVALID_PARAM));
  CLEAR_ERRNO_ALL();
  display_full_reset();
  display_poll_keyboard(&connection);
  display_cleanup();
  REQUIRE((session_display_get_caps(display)) != nullptr);
  REQUIRE(!(session_display_has_first_frame(display)));
  connection.display_context = NULL;
  session_display_destroy(display);
}
TEST_CASE("client_transport/failed_keepalive_send_exits_its_real_worker_and_repeated_start_is_idempotent", "[client_transport]") {
  connection.client_worker_pool = thread_pool_create("keepalive-contract");
  REQUIRE((connection.client_worker_pool) != nullptr);
  send_result = ERROR_NETWORK;
  REQUIRE((keepalive_start_thread(&connection)) == (0));
  REQUIRE((keepalive_start_thread(&connection)) == (0));
  uint64_t deadline = time_get_ns() + 3 * NS_PER_SEC_INT;
  while (!keepalive_thread_exited(&connection) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  REQUIRE(keepalive_thread_exited(&connection));
  REQUIRE((thread_pool_stop_all(connection.client_worker_pool)) == (ASCIICHAT_OK));
  REQUIRE(server_connection_is_lost(&connection));
  REQUIRE((atomic_load_u64(&send_count)) == (1));
  REQUIRE((records[0].type) == (PACKET_TYPE_PING));
  keepalive_stop_thread(&connection);
  keepalive_stop_thread(&connection);
  REQUIRE(!(connection.ping_thread_created));
}

#include <catch2/catch_test_macros.hpp>
#include "common/session/host.h"
#include "common/session/frame_validation.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <math.h>
#include <string.h>

static session_host_t *host;
static char *rendered;
static unsigned joins, leaves, renders, sends;
static uint8_t packet[8192];
static size_t packet_length;
static asciichat_error_t backend_result;

static void joined(session_host_t *owner, uint32_t id, void *context) {
  REQUIRE((owner) == (host));
  REQUIRE((context) == (&joins));
  session_host_client_info_t info;
  REQUIRE((session_host_find_client(owner, id, &info)) == (ASCIICHAT_OK));
  ++joins;
}
static void left(session_host_t *owner, uint32_t id, void *context) {
  REQUIRE((owner) == (host));
  REQUIRE((context) == (&joins));
  session_host_client_info_t info;
  REQUIRE((session_host_find_client(owner, id, &info)) == (ASCIICHAT_OK));
  ++leaves;
}
static asciichat_error_t capture_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  REQUIRE((length) <= (sizeof(packet)));
  memcpy(packet, data, length);
  packet_length = length;
  ++sends;
  return ASCIICHAT_OK;
}
static const acip_transport_methods_t methods = {.send = capture_send};
static acip_transport_t transport = {.methods = &methods};

static asciichat_error_t get_config(void *context, session_host_render_config_t *config) {
  REQUIRE((context) == (&renders));
  *config = (session_host_render_config_t){.width = 40, .height = 12, .palette = "@", .caps = {.color_level = TERM_COLOR_TRUECOLOR, .render_mode = RENDER_MODE_FOREGROUND, .utf8_support = true}};
  return backend_result;
}
static void render(void *context, const char *frame, size_t length, int width, int height) {
  REQUIRE((context) == (&renders));
  REQUIRE((session_frame_validate(frame, length, width, height)) == (ASCIICHAT_OK));
  SAFE_FREE(rendered);
  SAFE_STRDUP(rendered, frame);
  ++renders;
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  session_host_config_t config = {.max_clients = 3, .user_data = &joins, .callbacks = {.on_client_join = joined, .on_client_leave = left}};
  host = session_host_create(&config);
  REQUIRE((host) != nullptr);
  session_host_render_target_t target = {.get_config = get_config, .render_frame = render, .context = &renders};
  REQUIRE((session_host_set_render_target(host, &target)) == (ASCIICHAT_OK));
}
static void teardown(void) {
  REQUIRE((session_host_destroy(host)) == (ASCIICHAT_OK));
  SAFE_FREE(rendered);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static void tick(void) {
  time_sleep_ns(20 * NS_PER_MS_INT);
  REQUIRE((session_host_render_video(host)) == (ASCIICHAT_OK));
}
TEST_CASE("session_host_media/memory_admission_reuses_capacity_and_callbacks_can_query_the_host", "[session_host_media] [unit]") {
  uint32_t ids[3];
  ids[0] = session_host_add_memory_participant(host);
  REQUIRE((ids[0]) != (0));
  REQUIRE((session_host_add_memory_participant(host)) == (0)); // One local source per host.
  for (unsigned i = 1; i < 3; ++i) {
    ids[i] = session_host_add_client(host, &transport, ACIP_TRANSPORT_BORROWED, "127.0.0.1", 1234);
    REQUIRE((ids[i]) != (0));
  }
  REQUIRE((joins) == (3));
  REQUIRE((session_host_get_client_count(host)) == (3));
  REQUIRE((session_host_add_memory_participant(host)) == (0));
  uint32_t listed[3];
  REQUIRE((session_host_get_client_ids(host, listed, 2)) == (2));
  REQUIRE(std::memcmp((listed), (ids), (2 * sizeof(*ids))) == 0);
  REQUIRE((session_host_remove_client(host, ids[0])) == (ASCIICHAT_OK));
  REQUIRE((leaves) == (1));
  session_host_client_info_t info;
  REQUIRE((session_host_find_client(host, ids[0], &info)) == (ERROR_NOT_FOUND));
  REQUIRE((session_host_remove_client(host, ids[0])) == (ERROR_NOT_FOUND));
  uint32_t replacement = session_host_add_memory_participant(host);
  REQUIRE((replacement) != (0));
  REQUIRE((replacement) != (ids[0]));
  REQUIRE((session_host_get_client_count(host)) == (3));
  REQUIRE((joins) == (4));
}
TEST_CASE("session_host_media/injected_image_is_owned_and_backend_rejection_preserves_the_last_frame", "[session_host_media] [unit]") {
  uint32_t id = session_host_add_memory_participant(host);
  REQUIRE((id) != (0));
  image_t *image = image_new(2, 2);
  REQUIRE((image) != nullptr);
  for (int i = 0; i < 4; ++i)
    image->pixels[i] = (rgb_pixel_t){20, 90, 180};
  REQUIRE((session_host_inject_frame(host, id, image)) == (ASCIICHAT_OK));
  image_destroy(image);
  tick();
  REQUIRE((rendered) != nullptr);
  REQUIRE((strstr(rendered, ";2;20;90;180m")) != nullptr);
  unsigned before = renders;
  backend_result = ERROR_INVALID_STATE;
  tick();
  REQUIRE((renders) == (before));
  backend_result = ASCIICHAT_OK;
  tick();
  REQUIRE((renders) == (before + 1));
  REQUIRE((strstr(rendered, ";2;20;90;180m")) != nullptr);
}
TEST_CASE("session_host_media/rgb_bgr_rgba_and_bgra_packets_render_the_same_colors_and_bad_frames_do_not_replace_them", "[session_host_media] [unit]") {
  uint32_t id = session_host_add_client(host, &transport, ACIP_TRANSPORT_BORROWED, "127.0.0.1", 1234);
  REQUIRE((id) != (0));
  REQUIRE((session_host_receive_packet(host, id, PACKET_TYPE_STREAM_START, NULL, 0)) == (ASCIICHAT_OK));
  struct {
    image_frame_packet_t header;
    uint8_t pixels[16];
  } frame = {0};
  const uint32_t formats[] = {PIXEL_FORMAT_RGB, PIXEL_FORMAT_BGR, PIXEL_FORMAT_RGBA, PIXEL_FORMAT_BGRA};
  for (unsigned n = 0; n < 4; ++n) {
    frame.header = (image_frame_packet_t){.width = HOST_TO_NET_U32(2), .height = HOST_TO_NET_U32(2), .pixel_format = HOST_TO_NET_U32(formats[n])};
    size_t bytes = n < 2 ? 3 : 4;
    for (unsigned p = 0; p < 4; ++p) {
      frame.pixels[p * bytes] = n & 1 ? 180 : 20;
      frame.pixels[p * bytes + 1] = 90;
      frame.pixels[p * bytes + 2] = n & 1 ? 20 : 180;
      if (bytes == 4)
        frame.pixels[p * bytes + 3] = 17;
    }
    REQUIRE((session_host_receive_packet(host, id, PACKET_TYPE_IMAGE_FRAME, &frame, sizeof(frame.header) + 4 * bytes)) == (ASCIICHAT_OK));
    tick();
    REQUIRE((rendered) != nullptr);
    REQUIRE((strstr(rendered, ";2;20;90;180m")) != nullptr);
  }
  frame.header.width = 0;
  REQUIRE((session_host_receive_packet(host, id, PACKET_TYPE_IMAGE_FRAME, &frame, sizeof(frame))) == (ASCIICHAT_OK));
  tick();
  REQUIRE((strstr(rendered, ";2;20;90;180m")) != nullptr);
  REQUIRE((session_host_receive_packet(host, id, PACKET_TYPE_STREAM_STOP, NULL, 0)) == (ASCIICHAT_OK));
  session_host_client_info_t info;
  REQUIRE((session_host_find_client(host, id, &info)) == (ASCIICHAT_OK));
  REQUIRE((!(info.video_active || info.audio_active)));
}
TEST_CASE("session_host_media/mixed_memory_audio_is_encoded_and_delivered_to_a_network_participant", "[session_host_media] [unit]") {
  uint32_t source = session_host_add_memory_participant(host);
  uint32_t listener = session_host_add_client(host, &transport, ACIP_TRANSPORT_BORROWED, "127.0.0.1", 1234);
  REQUIRE((source && listener));
  float input[(OPUS_FRAME_SAMPLES * AUDIO_CHANNELS)];
  for (unsigned i = 0; i < (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS); ++i)
    input[i] = 0.5f * sinf((float)i * 0.07f);
  REQUIRE((session_host_inject_audio(host, source, input, (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS))) == (ASCIICHAT_OK));
  REQUIRE((session_host_render_audio(host)) == (ASCIICHAT_OK));
  REQUIRE((sends) > (0));
  REQUIRE((packet_length) > (sizeof(packet_header_t) + 18));
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  REQUIRE((NET_TO_HOST_U16(header.type)) == (PACKET_TYPE_AUDIO_OPUS_BATCH));
  const uint8_t *batch = packet + sizeof(header);
  uint16_t size;
  memcpy(&size, batch + 16, sizeof(size));
  size = NET_TO_HOST_U16(size);
  REQUIRE((packet_length) == (sizeof(header) + 18 + size));
  opus_codec_t *decoder = opus_codec_create_decoder(AUDIO_SAMPLE_RATE);
  REQUIRE((decoder) != nullptr);
  float decoded[(OPUS_FRAME_SAMPLES * AUDIO_CHANNELS)];
  REQUIRE((opus_codec_decode(decoder, batch + 18, size, decoded, (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS))) == ((OPUS_FRAME_SAMPLES * AUDIO_CHANNELS)));
  double energy = 0;
  for (unsigned i = 0; i < (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS); ++i)
    energy += decoded[i] * decoded[i];
  REQUIRE((energy) > (0.01));
  opus_codec_destroy(decoder);
}

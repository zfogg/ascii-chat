#include <ascii-chat/tests/catch2_test_helpers.hpp>
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
  ASCIICHAT_REQUIRE_EQ(owner, host);
  ASCIICHAT_REQUIRE_EQ(context, &joins);
  session_host_client_info_t info;
  ASCIICHAT_REQUIRE_EQ(session_host_find_client(owner, id, &info), ASCIICHAT_OK);
  ++joins;
}
static void left(session_host_t *owner, uint32_t id, void *context) {
  ASCIICHAT_REQUIRE_EQ(owner, host);
  ASCIICHAT_REQUIRE_EQ(context, &joins);
  session_host_client_info_t info;
  ASCIICHAT_REQUIRE_EQ(session_host_find_client(owner, id, &info), ASCIICHAT_OK);
  ++leaves;
}
static asciichat_error_t capture_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  ASCIICHAT_REQUIRE_LE(length, sizeof(packet));
  memcpy(packet, data, length);
  packet_length = length;
  ++sends;
  return ASCIICHAT_OK;
}
static const acip_transport_methods_t methods = {.send = capture_send};
static acip_transport_t transport = {.methods = &methods};

static asciichat_error_t get_config(void *context, session_host_render_config_t *config) {
  ASCIICHAT_REQUIRE_EQ(context, &renders);
  *config = (session_host_render_config_t){
      .width = 40,
      .height = 12,
      .palette = "@",
      .caps = {.color_level = TERM_COLOR_TRUECOLOR, .render_mode = RENDER_MODE_FOREGROUND, .utf8_support = true}};
  return backend_result;
}
static void render(void *context, const char *frame, size_t length, int width, int height) {
  ASCIICHAT_REQUIRE_EQ(context, &renders);
  ASCIICHAT_REQUIRE_EQ(session_frame_validate(frame, length, width, height), ASCIICHAT_OK);
  SAFE_FREE(rendered);
  SAFE_STRDUP(rendered, frame);
  ++renders;
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  session_host_config_t config = {
      .max_clients = 3, .user_data = &joins, .callbacks = {.on_client_join = joined, .on_client_leave = left}};
  host = session_host_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(host);
  session_host_render_target_t target = {.get_config = get_config, .render_frame = render, .context = &renders};
  ASCIICHAT_REQUIRE_EQ(session_host_set_render_target(host, &target), ASCIICHAT_OK);
}
static void teardown(void) {
  ASCIICHAT_REQUIRE_EQ(session_host_destroy(host), ASCIICHAT_OK);
  SAFE_FREE(rendered);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static void tick(void) {
  time_sleep_ns(20 * NS_PER_MS_INT);
  ASCIICHAT_REQUIRE_EQ(session_host_render_video(host), ASCIICHAT_OK);
}
TEST_CASE("session_host_media/memory_admission_reuses_capacity_and_callbacks_can_query_the_host", "[session_host_media]") {
  uint32_t ids[3];
  ids[0] = session_host_add_memory_participant(host);
  ASCIICHAT_REQUIRE_NE(ids[0], 0);
  ASCIICHAT_REQUIRE_EQ(session_host_add_memory_participant(host), 0); // One local source per host.
  for (unsigned i = 1; i < 3; ++i) {
    ids[i] = session_host_add_client(host, &transport, ACIP_TRANSPORT_BORROWED, "127.0.0.1", 1234);
    ASCIICHAT_REQUIRE_NE(ids[i], 0);
  }
  ASCIICHAT_REQUIRE_EQ(joins, 3);
  ASCIICHAT_REQUIRE_EQ(session_host_get_client_count(host), 3);
  ASCIICHAT_REQUIRE_EQ(session_host_add_memory_participant(host), 0);
  uint32_t listed[3];
  ASCIICHAT_REQUIRE_EQ(session_host_get_client_ids(host, listed, 2), 2);
  ASCIICHAT_REQUIRE_ARRAY_EQ(listed, ids, 2 * sizeof(*ids));
  ASCIICHAT_REQUIRE_EQ(session_host_remove_client(host, ids[0]), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(leaves, 1);
  session_host_client_info_t info;
  ASCIICHAT_REQUIRE_EQ(session_host_find_client(host, ids[0], &info), ERROR_NOT_FOUND);
  ASCIICHAT_REQUIRE_EQ(session_host_remove_client(host, ids[0]), ERROR_NOT_FOUND);
  uint32_t replacement = session_host_add_memory_participant(host);
  ASCIICHAT_REQUIRE_NE(replacement, 0);
  ASCIICHAT_REQUIRE_NE(replacement, ids[0]);
  ASCIICHAT_REQUIRE_EQ(session_host_get_client_count(host), 3);
  ASCIICHAT_REQUIRE_EQ(joins, 4);
}
TEST_CASE("session_host_media/injected_image_is_owned_and_backend_rejection_preserves_the_last_frame", "[session_host_media]") {
  uint32_t id = session_host_add_memory_participant(host);
  ASCIICHAT_REQUIRE_NE(id, 0);
  image_t *image = image_new(2, 2);
  ASCIICHAT_REQUIRE_NOT_NULL(image);
  for (int i = 0; i < 4; ++i)
    image->pixels[i] = (rgb_pixel_t){20, 90, 180};
  ASCIICHAT_REQUIRE_EQ(session_host_inject_frame(host, id, image), ASCIICHAT_OK);
  image_destroy(image);
  tick();
  ASCIICHAT_REQUIRE_NOT_NULL(rendered);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(rendered, ";2;20;90;180m"));
  unsigned before = renders;
  backend_result = ERROR_INVALID_STATE;
  tick();
  ASCIICHAT_REQUIRE_EQ(renders, before);
  backend_result = ASCIICHAT_OK;
  tick();
  ASCIICHAT_REQUIRE_EQ(renders, before + 1);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(rendered, ";2;20;90;180m"));
}
TEST_CASE("session_host_media/rgb_bgr_rgba_and_bgra_packets_render_the_same_colors_and_bad_frames_do_not_replace_them", "[session_host_media]") {
  uint32_t id = session_host_add_client(host, &transport, ACIP_TRANSPORT_BORROWED, "127.0.0.1", 1234);
  ASCIICHAT_REQUIRE_NE(id, 0);
  ASCIICHAT_REQUIRE_EQ(session_host_receive_packet(host, id, PACKET_TYPE_STREAM_START, NULL, 0), ASCIICHAT_OK);
  struct {
    image_frame_packet_t header;
    uint8_t pixels[16];
  } frame = {0};
  const uint32_t formats[] = {PIXEL_FORMAT_RGB, PIXEL_FORMAT_BGR, PIXEL_FORMAT_RGBA, PIXEL_FORMAT_BGRA};
  for (unsigned n = 0; n < 4; ++n) {
    frame.header = (image_frame_packet_t){
        .width = HOST_TO_NET_U32(2), .height = HOST_TO_NET_U32(2), .pixel_format = HOST_TO_NET_U32(formats[n])};
    size_t bytes = n < 2 ? 3 : 4;
    for (unsigned p = 0; p < 4; ++p) {
      frame.pixels[p * bytes] = n & 1 ? 180 : 20;
      frame.pixels[p * bytes + 1] = 90;
      frame.pixels[p * bytes + 2] = n & 1 ? 20 : 180;
      if (bytes == 4)
        frame.pixels[p * bytes + 3] = 17;
    }
    ASCIICHAT_REQUIRE_EQ(
        session_host_receive_packet(host, id, PACKET_TYPE_IMAGE_FRAME, &frame, sizeof(frame.header) + 4 * bytes),
        ASCIICHAT_OK);
    tick();
    ASCIICHAT_REQUIRE_NOT_NULL(rendered);
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(rendered, ";2;20;90;180m"));
  }
  frame.header.width = 0;
  ASCIICHAT_REQUIRE_EQ(session_host_receive_packet(host, id, PACKET_TYPE_IMAGE_FRAME, &frame, sizeof(frame)), ASCIICHAT_OK);
  tick();
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(rendered, ";2;20;90;180m"));
  ASCIICHAT_REQUIRE_EQ(session_host_receive_packet(host, id, PACKET_TYPE_STREAM_STOP, NULL, 0), ASCIICHAT_OK);
  session_host_client_info_t info;
  ASCIICHAT_REQUIRE_EQ(session_host_find_client(host, id, &info), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(info.video_active || info.audio_active);
}
TEST_CASE("session_host_media/mixed_memory_audio_is_encoded_and_delivered_to_a_network_participant", "[session_host_media]") {
  uint32_t source = session_host_add_memory_participant(host);
  uint32_t listener = session_host_add_client(host, &transport, ACIP_TRANSPORT_BORROWED, "127.0.0.1", 1234);
  ASCIICHAT_REQUIRE(source && listener);
  float input[(OPUS_FRAME_SAMPLES * AUDIO_CHANNELS)];
  for (unsigned i = 0; i < (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS); ++i)
    input[i] = 0.5f * sinf((float)i * 0.07f);
  ASCIICHAT_REQUIRE_EQ(session_host_inject_audio(host, source, input, (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(session_host_render_audio(host), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_GT(sends, 0);
  ASCIICHAT_REQUIRE_GT(packet_length, sizeof(packet_header_t) + 18);
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  ASCIICHAT_REQUIRE_EQ(NET_TO_HOST_U16(header.type), PACKET_TYPE_AUDIO_OPUS_BATCH);
  const uint8_t *batch = packet + sizeof(header);
  uint16_t size;
  memcpy(&size, batch + 16, sizeof(size));
  size = NET_TO_HOST_U16(size);
  ASCIICHAT_REQUIRE_EQ(packet_length, sizeof(header) + 18 + size);
  opus_codec_t *decoder = opus_codec_create_decoder(AUDIO_SAMPLE_RATE);
  ASCIICHAT_REQUIRE_NOT_NULL(decoder);
  float decoded[(OPUS_FRAME_SAMPLES * AUDIO_CHANNELS)];
  ASCIICHAT_REQUIRE_EQ(opus_codec_decode(decoder, batch + 18, size, decoded, (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS)),
               (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS));
  double energy = 0;
  for (unsigned i = 0; i < (OPUS_FRAME_SAMPLES * AUDIO_CHANNELS); ++i)
    energy += decoded[i] * decoded[i];
  ASCIICHAT_REQUIRE_GT(energy, 0.01);
  opus_codec_destroy(decoder);
}

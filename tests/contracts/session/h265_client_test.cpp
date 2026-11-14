#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "session/h265/client.h"
#include <ascii-chat/common.h>
#include <ascii-chat/video/h265/decoder.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

static h265_client_context_t *client;
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  client = h265_client_context_create(64, 32);
  ASCIICHAT_REQUIRE_NOT_NULL(client);
}
static void teardown(void) {
  h265_client_context_destroy(client);
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TEST_CASE("session_h265_client/neutral_rgb_frame_encodes_and_decodes_with_correct_geometry_and_keyframe_stats", "[session_h265_client]") {
  ASCIICHAT_REQUIRE_EQ(h265_client_init_media_source(client, H265_SOURCE_TEST_PATTERN, NULL), ASCIICHAT_OK);
  uint8_t packet[65536], output[64 * 32];
  size_t size = sizeof(packet), decoded_size = sizeof(output);
  ASCIICHAT_REQUIRE_EQ(h265_client_capture_and_encode(client, packet, &size), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_GT(size, 5);
  h265_decoder_t *decoder = h265_decoder_create();
  ASCIICHAT_REQUIRE_NOT_NULL(decoder);
  uint16_t width = 0, height = 0;
  ASCIICHAT_REQUIRE_EQ(h265_decode(decoder, packet, size, &width, &height, output, &decoded_size), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(width, 64);
  ASCIICHAT_REQUIRE_EQ(height, 32);
  ASCIICHAT_REQUIRE_EQ(decoded_size, sizeof(output));
  // RGB(128,128,128) becomes limited-range Y=126 before HEVC coding.
  for (size_t i = 0; i < decoded_size; ++i)
    ASCIICHAT_REQUIRE(output[i] >= 122 && output[i] <= 130, "Unexpected luma at %zu: %u", i, output[i]);
  h265_decoder_destroy(decoder);
  uint64_t frames = 0, keyframes = 0;
  uint32_t bitrate = 0;
  h265_client_get_stats(client, &frames, &keyframes, &bitrate);
  ASCIICHAT_REQUIRE_EQ(frames, 1);
  ASCIICHAT_REQUIRE_EQ(keyframes, 1);
  h265_client_request_keyframe(client);
  size = sizeof(packet);
  ASCIICHAT_REQUIRE_EQ(h265_client_capture_and_encode(client, packet, &size), ASCIICHAT_OK);
  h265_client_get_stats(client, &frames, &keyframes, &bitrate);
  ASCIICHAT_REQUIRE_EQ(frames, 2);
  ASCIICHAT_REQUIRE_EQ(keyframes, 2);
}
TEST_CASE("session_h265_client/insufficient_output_and_missing_arguments_do_not_advance_encoder_stats", "[session_h265_client]") {
  uint8_t packet[4] = {1, 2, 3, 4};
  size_t size = sizeof(packet);
  ASCIICHAT_REQUIRE_EQ(h265_client_capture_and_encode(client, packet, &size), ERROR_NETWORK_SIZE);
  ASCIICHAT_REQUIRE_EQ(size, sizeof(packet));
  ASCIICHAT_REQUIRE_EQ(packet[0], 1);
  ASCIICHAT_REQUIRE_EQ(h265_client_capture_and_encode(client, NULL, &size), ERROR_INTERNAL);
  ASCIICHAT_REQUIRE_EQ(h265_client_capture_and_encode(client, packet, NULL), ERROR_INTERNAL);
  ASCIICHAT_REQUIRE_EQ(h265_client_capture_and_encode(NULL, packet, &size), ERROR_INTERNAL);
  uint64_t frames = 42, keyframes = 42;
  h265_client_get_stats(client, &frames, &keyframes, NULL);
  ASCIICHAT_REQUIRE_EQ(frames, 0);
  ASCIICHAT_REQUIRE_EQ(keyframes, 0);
  ASCIICHAT_REQUIRE_NULL(h265_client_context_create(0, 32));
  ASCIICHAT_REQUIRE_NULL(h265_client_context_create(64, 0));
  h265_client_context_destroy(NULL);
  h265_client_request_keyframe(NULL);
}
TEST_CASE("session_h265_client/source_configuration_owns_caller_text_for_each_supported_kind", "[session_h265_client]") {
  const h265_media_source_t sources[] = {H265_SOURCE_FILE, H265_SOURCE_URL, H265_SOURCE_WEBCAM};
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
    h265_client_context_t *context = h265_client_context_create(64, 32);
    ASCIICHAT_REQUIRE_NOT_NULL(context);
    char location[] = "fixture-source";
    ASCIICHAT_REQUIRE_EQ(h265_client_init_media_source(context, sources[i], location), ASCIICHAT_OK);
    memset(location, 0, sizeof(location));
    // Destroying the context releases its copy, without freeing caller storage.
    h265_client_context_destroy(context);
  }
  ASCIICHAT_REQUIRE_EQ(h265_client_init_media_source(NULL, H265_SOURCE_TEST_PATTERN, NULL), ERROR_INTERNAL);
}

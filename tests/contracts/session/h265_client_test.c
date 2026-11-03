#include <criterion/criterion.h>
#include "session/h265/client.h"
#include <ascii-chat/common.h>
#include <ascii-chat/video/h265/decoder.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

static h265_client_context_t *client;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  client = h265_client_context_create(64, 32);
  cr_assert_not_null(client);
}
static void teardown(void) {
  h265_client_context_destroy(client);
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(session_h265_client, .init = setup, .fini = teardown);

Test(session_h265_client, neutral_rgb_frame_encodes_and_decodes_with_correct_geometry_and_keyframe_stats) {
  cr_assert_eq(h265_client_init_media_source(client, H265_SOURCE_TEST_PATTERN, NULL), ASCIICHAT_OK);
  uint8_t packet[65536], output[64 * 32];
  size_t size = sizeof(packet), decoded_size = sizeof(output);
  cr_assert_eq(h265_client_capture_and_encode(client, packet, &size), ASCIICHAT_OK);
  cr_assert_gt(size, 5);
  h265_decoder_t *decoder = h265_decoder_create();
  cr_assert_not_null(decoder);
  uint16_t width = 0, height = 0;
  cr_assert_eq(h265_decode(decoder, packet, size, &width, &height, output, &decoded_size), ASCIICHAT_OK);
  cr_assert_eq(width, 64);
  cr_assert_eq(height, 32);
  cr_assert_eq(decoded_size, sizeof(output));
  // RGB(128,128,128) becomes limited-range Y=126 before HEVC coding.
  for (size_t i = 0; i < decoded_size; ++i)
    cr_assert(output[i] >= 122 && output[i] <= 130, "Unexpected luma at %zu: %u", i, output[i]);
  h265_decoder_destroy(decoder);
  uint64_t frames = 0, keyframes = 0;
  uint32_t bitrate = 0;
  h265_client_get_stats(client, &frames, &keyframes, &bitrate);
  cr_assert_eq(frames, 1);
  cr_assert_eq(keyframes, 1);
  h265_client_request_keyframe(client);
  size = sizeof(packet);
  cr_assert_eq(h265_client_capture_and_encode(client, packet, &size), ASCIICHAT_OK);
  h265_client_get_stats(client, &frames, &keyframes, &bitrate);
  cr_assert_eq(frames, 2);
  cr_assert_eq(keyframes, 2);
}
Test(session_h265_client, insufficient_output_and_missing_arguments_do_not_advance_encoder_stats) {
  uint8_t packet[4] = {1, 2, 3, 4};
  size_t size = sizeof(packet);
  cr_assert_eq(h265_client_capture_and_encode(client, packet, &size), ERROR_NETWORK_SIZE);
  cr_assert_eq(size, sizeof(packet));
  cr_assert_eq(packet[0], 1);
  cr_assert_eq(h265_client_capture_and_encode(client, NULL, &size), ERROR_INTERNAL);
  cr_assert_eq(h265_client_capture_and_encode(client, packet, NULL), ERROR_INTERNAL);
  cr_assert_eq(h265_client_capture_and_encode(NULL, packet, &size), ERROR_INTERNAL);
  uint64_t frames = 42, keyframes = 42;
  h265_client_get_stats(client, &frames, &keyframes, NULL);
  cr_assert_eq(frames, 0);
  cr_assert_eq(keyframes, 0);
  cr_assert_null(h265_client_context_create(0, 32));
  cr_assert_null(h265_client_context_create(64, 0));
  h265_client_context_destroy(NULL);
  h265_client_request_keyframe(NULL);
}
Test(session_h265_client, source_configuration_owns_caller_text_for_each_supported_kind) {
  const h265_media_source_t sources[] = {H265_SOURCE_FILE, H265_SOURCE_URL, H265_SOURCE_WEBCAM};
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
    h265_client_context_t *context = h265_client_context_create(64, 32);
    cr_assert_not_null(context);
    char location[] = "fixture-source";
    cr_assert_eq(h265_client_init_media_source(context, sources[i], location), ASCIICHAT_OK);
    memset(location, 0, sizeof(location));
    // Destroying the context releases its copy, without freeing caller storage.
    h265_client_context_destroy(context);
  }
  cr_assert_eq(h265_client_init_media_source(NULL, H265_SOURCE_TEST_PATTERN, NULL), ERROR_INTERNAL);
}

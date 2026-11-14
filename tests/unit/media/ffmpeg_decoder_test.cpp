#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/platform/filesystem.h>
#include <libplatform/system.h>
#include <libplatform/atomic.h>
#include <string.h>

// A 2x2 RGB PNG with a zlib-compressed IDAT: red, green, blue, white.
// Keep the encoded fixture independent of the FFmpeg encoder configuration.
static const unsigned char png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73, 0x00, 0x00, 0x00, 0x12, 0x49,
    0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xc0, 0x00, 0xc2, 0x0c, 0xff, 0x81, 0x00, 0x00, 0x1f, 0xee,
    0x05, 0xfb, 0x0b, 0xd9, 0x68, 0x8b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

static char fixture_path[512];
static int fixture_fd = -1;
static ffmpeg_decoder_t *decoder;

static void cleanup(void) {
  errno_notifications_enable(false);
  ffmpeg_decoder_destroy(decoder);
  decoder = NULL;
  if (fixture_fd >= 0) {
    platform_close(fixture_fd);
    fixture_fd = -1;
  }
  if (fixture_path[0]) {
    platform_delete_temp_file(fixture_path);
    fixture_path[0] = '\0';
  }
}
TEST_CASE("ffmpeg_decoder/decodes_zlib_compressed_png", "[ffmpeg_decoder]") {
  const rgb_pixel_t expected[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(fixture_path, sizeof(fixture_path), "png-decoder", &fixture_fd), 0);
  ASCIICHAT_REQUIRE_EQ(platform_temp_file_open("png-decoder", fixture_path, &fixture_fd), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(platform_write(fixture_fd, png, sizeof(png)), (ssize_t)sizeof(png));
  ASCIICHAT_REQUIRE_EQ(platform_close(fixture_fd), 0);
  fixture_fd = -1;

  decoder = ffmpeg_decoder_create(fixture_path);
  ASCIICHAT_REQUIRE_NOT_NULL(decoder, "PNG decoding must be available in the linked FFmpeg build");
  ASCIICHAT_REQUIRE_EQ(ffmpeg_decoder_start_prefetch(decoder), ASCIICHAT_OK);
  image_t *frame = NULL;
  asciichat_error_context_t failure{};
  ffmpeg_frame_status_t status = FFMPEG_FRAME_WAITING;
  const uint64_t deadline = platform_get_monotonic_time_us() + 5000000;
  do {
    status = ffmpeg_decoder_poll_video(decoder, &frame, &failure);
    if (status != FFMPEG_FRAME_WAITING)
      break;
    platform_sleep_ms(1);
  } while (platform_get_monotonic_time_us() < deadline);
  ASCIICHAT_REQUIRE_EQ(status, FFMPEG_FRAME_READY, "PNG decode status %d: %s", status, failure.context_message);
  ASCIICHAT_REQUIRE_NOT_NULL(frame, "The compressed PNG must produce a video frame");
  ASCIICHAT_REQUIRE_EQ(frame->w, 2);
  ASCIICHAT_REQUIRE_EQ(frame->h, 2);
  ASCIICHAT_REQUIRE_ARRAY_EQ(frame->pixels, expected, sizeof(expected), "PNG decoding must preserve every RGB pixel");
}

static bool cancel_video_read(void *data) {
  if (atomic_fetch_add_int((atomic_t *)data, 1) == 0)
    SET_ERRNO(ERROR_MEDIA_DECODE, "Interrupted decoder read");
  return true;
}

TEST_CASE("ffmpeg_decoder/cancelled_prefetch_is_not_a_decode_failure", "[ffmpeg_decoder]") {
  static const char header[] = "YUV4MPEG2 W256 H256 F30:1 Ip A1:1 C420jpeg\n";
  static const char marker[] = "FRAME\n";
  unsigned char pixels[256 * 256 * 3 / 2];
  memset(pixels, 128, sizeof(pixels));
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(fixture_path, sizeof(fixture_path), "cancel-decoder", &fixture_fd), 0);
  ASCIICHAT_REQUIRE_EQ(platform_temp_file_open("cancel-decoder", fixture_path, &fixture_fd), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(platform_write(fixture_fd, header, sizeof(header) - 1), (ssize_t)sizeof(header) - 1);
  for (unsigned i = 0; i < 4; ++i) {
    ASCIICHAT_REQUIRE_EQ(platform_write(fixture_fd, marker, sizeof(marker) - 1), (ssize_t)sizeof(marker) - 1);
    ASCIICHAT_REQUIRE_EQ(platform_write(fixture_fd, pixels, sizeof(pixels)), (ssize_t)sizeof(pixels));
  }
  ASCIICHAT_REQUIRE_EQ(platform_close(fixture_fd), 0);
  fixture_fd = -1;
  decoder = ffmpeg_decoder_create(fixture_path);
  ASCIICHAT_REQUIRE_NOT_NULL(decoder);
  ASCIICHAT_REQUIRE_EQ(ffmpeg_decoder_seek_to_timestamp(decoder, 0), ASCIICHAT_OK);
  static atomic_t callbacks;
  atomic_store_int(&callbacks, 0);
  ffmpeg_decoder_set_exit_callback(decoder, cancel_video_read, &callbacks);
  errno_notifications_enable(true);
  ASCIICHAT_REQUIRE_EQ(ffmpeg_decoder_start_prefetch(decoder), ASCIICHAT_OK);
  uint64_t deadline = platform_get_monotonic_time_us() + 5000000;
  image_t *frame = NULL;
  asciichat_error_context_t failure{};
  while (atomic_load_int(&callbacks) < 2 && platform_get_monotonic_time_us() < deadline) {
    ASCIICHAT_REQUIRE_NE(ffmpeg_decoder_poll_video(decoder, &frame, &failure), FFMPEG_FRAME_FAILED, "%s",
                  failure.context_message);
    platform_sleep_ms(1);
  }
  // One callback interrupts FFmpeg; the worker confirms cancellation before
  // publishing its result. Stop joins it before inspecting the final state.
  ASCIICHAT_REQUIRE_GE(atomic_load_int(&callbacks), 2);
  ffmpeg_decoder_stop_prefetch(decoder);
  ASCIICHAT_REQUIRE_NOT(errno_notifications_pending(), "Cancellation must not leave an error popup");
  ASCIICHAT_REQUIRE_NE(ffmpeg_decoder_poll_video(decoder, &frame, &failure), FFMPEG_FRAME_FAILED, "%s",
                failure.context_message);
  asciichat_errno_history_t history[32];
  size_t count = errno_history_snapshot(history, 32);
  bool handled = false;
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(history[i].context.context_message, "Interrupted decoder read") == 0) {
      ASCIICHAT_REQUIRE_EQ(history[i].outcome, ASCIICHAT_ERRNO_HANDLED);
      handled = true;
    }
  }
  ASCIICHAT_REQUIRE(handled, "Cancelled read diagnostics should be resolved before the worker exits");
}

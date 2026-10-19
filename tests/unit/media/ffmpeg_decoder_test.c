#include <criterion/criterion.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>

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

TestSuite(ffmpeg_decoder, .fini = cleanup);

Test(ffmpeg_decoder, decodes_zlib_compressed_png) {
  const rgb_pixel_t expected[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
  cr_assert_eq(platform_create_temp_file(fixture_path, sizeof(fixture_path), "png-decoder", &fixture_fd), 0);
  cr_assert_eq(platform_temp_file_open("png-decoder", fixture_path, &fixture_fd), ASCIICHAT_OK);
  cr_assert_eq(platform_write(fixture_fd, png, sizeof(png)), (ssize_t)sizeof(png));
  cr_assert_eq(platform_close(fixture_fd), 0);
  fixture_fd = -1;

  decoder = ffmpeg_decoder_create(fixture_path);
  cr_assert_not_null(decoder, "PNG decoding must be available in the linked FFmpeg build");
  cr_assert_eq(ffmpeg_decoder_start_prefetch(decoder), ASCIICHAT_OK);
  image_t *frame = NULL;
  asciichat_error_context_t failure = {0};
  ffmpeg_frame_status_t status = FFMPEG_FRAME_WAITING;
  const uint64_t deadline = platform_get_monotonic_time_us() + 5000000;
  do {
    status = ffmpeg_decoder_poll_video(decoder, &frame, &failure);
    if (status != FFMPEG_FRAME_WAITING)
      break;
    platform_sleep_ms(1);
  } while (platform_get_monotonic_time_us() < deadline);
  cr_assert_eq(status, FFMPEG_FRAME_READY, "PNG decode status %d: %s", status, failure.context_message);
  cr_assert_not_null(frame, "The compressed PNG must produce a video frame");
  cr_assert_eq(frame->w, 2);
  cr_assert_eq(frame->h, 2);
  cr_assert_arr_eq(frame->pixels, expected, sizeof(expected), "PNG decoding must preserve every RGB pixel");
}

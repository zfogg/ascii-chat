#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/common.h>
#include "common/session/capture.h"
#include <ascii-chat/options/rcu.h>
#include <libplatform/filesystem.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static session_capture_ctx_t *capture;
static char red[PLATFORM_MAX_PATH_LENGTH], blue[PLATFORM_MAX_PATH_LENGTH];

static void fixture_video(char *path, const char *name, rgb_pixel_t color) {
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, PLATFORM_MAX_PATH_LENGTH, name, &fd)) == (0));
  REQUIRE((platform_close(fd)) == (0));
  FILE *file = platform_fopen(name, path, "wb");
  REQUIRE((file) != nullptr);
  // ppm_pipe reads concatenated PPM frames. Multiple frames keep automatic EOF
  // advance out of these manual-switch contracts, without an external encoder.
  const char header[] = "P6\n4 2\n255\n";
  for (unsigned frame = 0; frame < 20; ++frame) {
    REQUIRE((fwrite(header, 1, sizeof(header) - 1, file)) == (sizeof(header) - 1));
    for (unsigned pixel = 0; pixel < 8; ++pixel)
      REQUIRE((fwrite(&color, sizeof(color), 1, file)) == (1));
  }
  REQUIRE((fclose(file)) == (0));
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  options->test_pattern = true;
  options->audio_enabled = false;
  options->width = 40;
  options->height = 12;
  REQUIRE((options_state_set(options)) == (ASCIICHAT_OK));
  SAFE_FREE(options);
  fixture_video(red, "playlist red", (rgb_pixel_t){255, 0, 0});
  fixture_video(blue, "playlist blue", (rgb_pixel_t){0, 0, 255});
  session_capture_config_t config = {.type = MEDIA_SOURCE_FILE, .path = red, .target_fps = 60};
  capture = session_mirror_capture_create(&config);
  REQUIRE((capture) != nullptr);
  uint64_t deadline = time_get_ns() + NS_PER_SEC_INT;
  image_t *first = NULL;
  while (!first && time_get_ns() < deadline) {
    first = session_capture_read_frame(capture);
    if (!first)
      platform_sleep_ms(1);
  }
  REQUIRE((first) != nullptr);
  media_source_pause(static_cast<media_source_t *>(session_capture_get_media_source(capture)));
}
static void teardown(void) {
  session_capture_destroy(capture);
  platform_unlink(red);
  platform_unlink(blue);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static void await_color(uint64_t previous_epoch, rgb_pixel_t color) {
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (time_get_ns() < deadline) {
    image_t *frame = session_capture_read_frame(capture);
    uint64_t epoch = session_capture_source_epoch(capture);
    if (epoch != previous_epoch && frame) {
      REQUIRE((frame->w) == (4));
      REQUIRE((frame->h) == (2));
      REQUIRE(std::memcmp((&frame->pixels[0]), (&color), (sizeof(color))) == 0);
      media_source_pause(static_cast<media_source_t *>(session_capture_get_media_source(capture)));
      session_capture_complete_open(capture, epoch);
      return;
    }
    platform_sleep_ms(1);
  }
  FAIL("Media replacement never committed a frame");
}
TEST_CASE("session_capture_playlists/replacement_commits_a_new_source_once_and_rejects_concurrent_loads", "[session_capture_playlists]") {
  uint64_t epoch = session_capture_source_epoch(capture);
  REQUIRE((session_capture_open_media(capture, blue)) == (ASCIICHAT_OK));
  REQUIRE((session_capture_source_epoch(capture)) == (epoch));
  REQUIRE((session_capture_open_media(capture, red)) == (ERROR_INVALID_STATE));
  CLEAR_ERRNO_ALL();
  await_color(epoch, (rgb_pixel_t){0, 0, 255});
  REQUIRE((session_capture_source_epoch(capture)) == (epoch + 1));
  REQUIRE(!(session_capture_cancel_open(capture)));
  REQUIRE(session_capture_media_active(capture));
}
TEST_CASE("session_capture_playlists/cancellation_retains_the_old_source_even_if_the_worker_has_already_opened_the_file", "[session_capture_playlists]") {
  media_source_t *original = static_cast<media_source_t *>(session_capture_get_media_source(capture));
  uint64_t epoch = session_capture_source_epoch(capture);
  REQUIRE((session_capture_open_media(capture, blue)) == (ASCIICHAT_OK));
  REQUIRE(session_capture_cancel_open(capture));
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  for (;;) {
    session_capture_read_frame(capture);
    if (!session_capture_cancel_open(capture))
      break;
    REQUIRE((time_get_ns()) < (deadline));
    platform_sleep_ms(1);
  }
  REQUIRE((session_capture_get_media_source(capture)) == (original));
  REQUIRE((session_capture_source_epoch(capture)) == (epoch));
  REQUIRE(session_capture_is_valid(capture));
  REQUIRE((session_capture_open_media(capture, blue)) == (ASCIICHAT_OK));
  await_color(epoch, (rgb_pixel_t){0, 0, 255});
}
TEST_CASE("session_capture_playlists/quoted_playlist_supports_forward_and_backward_manual_selection", "[session_capture_playlists]") {
  char playlist[2 * PLATFORM_MAX_PATH_LENGTH + 8];
  safe_snprintf(playlist, sizeof(playlist), "\"%s\" \"%s\"", red, blue);
  uint64_t epoch = session_capture_source_epoch(capture);
  REQUIRE((session_capture_open_playlist(capture, playlist)) == (ASCIICHAT_OK));
  await_color(epoch, (rgb_pixel_t){255, 0, 0});
  epoch = session_capture_source_epoch(capture);
  session_capture_skip_media(capture, 1);
  await_color(epoch, (rgb_pixel_t){0, 0, 255});
  epoch = session_capture_source_epoch(capture);
  session_capture_skip_media(capture, -1);
  await_color(epoch, (rgb_pixel_t){255, 0, 0});
  REQUIRE(!(session_capture_at_end(capture)));
}
TEST_CASE("session_capture_playlists/invalid_playlist_and_missing_media_leave_the_current_source_usable", "[session_capture_playlists]") {
  uint64_t epoch = session_capture_source_epoch(capture);
  media_source_t *original = static_cast<media_source_t *>(session_capture_get_media_source(capture));
  REQUIRE((session_capture_open_playlist(capture, "\"unterminated")) != (ASCIICHAT_OK));
  REQUIRE((session_capture_get_media_source(capture)) == (original));
  REQUIRE((session_capture_source_epoch(capture)) == (epoch));
  CLEAR_ERRNO_ALL();
  REQUIRE((session_capture_open_media(capture, "/missing/coverage-playlist-file.mp4")) == (ASCIICHAT_OK));
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  for (;;) {
    session_capture_read_frame(capture);
    REQUIRE((session_capture_get_media_source(capture)) == (original));
    REQUIRE((session_capture_source_epoch(capture)) == (epoch));
    asciichat_error_t result = session_capture_open_media(capture, blue);
    if (result == ASCIICHAT_OK)
      break;
    REQUIRE((result) == (ERROR_INVALID_STATE));
    CLEAR_ERRNO_ALL();
    REQUIRE((time_get_ns()) < (deadline));
    platform_sleep_ms(1);
  }
  await_color(epoch, (rgb_pixel_t){0, 0, 255});
}

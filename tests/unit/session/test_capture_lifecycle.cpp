#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/common.h>
#include "common/session/capture.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <libplatform/filesystem.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static session_capture_ctx_t *capture;
static char path[PLATFORM_MAX_PATH_LENGTH];

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  options->test_pattern = true;
  options->width = 64;
  options->height = 24;
  options->audio_enabled = false;
  REQUIRE((options_state_set(options)) == (ASCIICHAT_OK));
  SAFE_FREE(options);
}

static void teardown(void) {
  session_capture_destroy(capture);
  if (path[0])
    platform_unlink(path);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static void create_pattern(bool resize) {
  session_capture_config_t config = {.type = MEDIA_SOURCE_TEST, .target_fps = 120, .resize_for_network = resize};
  capture = session_mirror_capture_create(&config);
  REQUIRE((capture) != nullptr);
  REQUIRE(session_capture_is_valid(capture));
}

TEST_CASE("session_capture_lifecycle/acquired_frames_advance_serial_but_transmission_copies_do_not", "[session_capture_lifecycle]") {
  create_pattern(false);
  REQUIRE((session_capture_get_target_fps(capture)) == (120));
  REQUIRE((session_capture_get_current_fps(capture)) == (0));
  uint64_t epoch = session_capture_source_epoch(capture);
  image_t *frame = session_capture_read_frame(capture);
  REQUIRE((frame) != nullptr);
  REQUIRE((session_capture_frame_serial(capture)) == (1));
  image_t *copy = session_capture_process_for_transmission(capture, frame);
  REQUIRE((copy) != nullptr);
  REQUIRE((copy) != (frame));
  REQUIRE((copy->w) == (frame->w));
  REQUIRE((copy->h) == (frame->h));
  REQUIRE(std::memcmp((copy->pixels), (frame->pixels), ((size_t)frame->w * frame->h * sizeof(rgb_pixel_t))) == 0);
  REQUIRE((session_capture_frame_serial(capture)) == (1));
  REQUIRE((session_capture_read_frame(capture)) != nullptr);
  REQUIRE((session_capture_frame_serial(capture)) == (2));
  REQUIRE((session_capture_source_epoch(capture)) == (epoch));
  REQUIRE((session_capture_get_current_fps(capture)) > (0));
  REQUIRE(!(session_capture_at_end(capture)));
  image_destroy(copy);
}

TEST_CASE("session_capture_lifecycle/transmission_workspace_reuses_storage_accepts_alias_and_resizes_to_network_budget", "[session_capture_lifecycle]") {
  create_pattern(true);
  image_t *frame = image_new(640, 360), *output = NULL;
  REQUIRE((frame) != nullptr);
  for (int i = 0; i < frame->w * frame->h; ++i)
    frame->pixels[i] = (rgb_pixel_t){30, 60, 90};
  REQUIRE((session_capture_process_for_transmission_into(capture, frame, &output)) == (ASCIICHAT_OK));
  REQUIRE((output) != nullptr);
  REQUIRE((output->w) == (320));
  REQUIRE((output->h) == (180));
  REQUIRE((output->pixels[0].r) == (30));
  REQUIRE((output->pixels[0].g) == (60));
  image_t *retained = output;
  REQUIRE((session_capture_process_for_transmission_into(capture, frame, &output)) == (ASCIICHAT_OK));
  REQUIRE((output) == (retained));
  REQUIRE((session_capture_process_for_transmission_into(capture, output, &output)) == (ASCIICHAT_OK));
  REQUIRE((output) == (retained));
  REQUIRE((output->pixels[0].b) == (90));
  REQUIRE((session_capture_process_for_transmission_into(capture, NULL, &output)) == (ERROR_INVALID_PARAM));
  REQUIRE((output) == (retained));
  image_destroy(output);
  image_destroy(frame);
}

TEST_CASE("session_capture_lifecycle/microphone_fallback_borrows_context_and_preserves_exact_captured_samples", "[session_capture_lifecycle]") {
  audio_context_t microphone = {0};
  REQUIRE((audio_init(&microphone)) == (ASCIICHAT_OK));
  session_capture_config_t config = {.type = MEDIA_SOURCE_TEST, .enable_audio = true, .audio_fallback_to_mic = true, .mic_audio_ctx = &microphone};
  capture = session_mirror_capture_create(&config);
  REQUIRE((capture) != nullptr);
  REQUIRE(session_capture_has_audio(capture));
  REQUIRE(!(session_capture_using_file_audio(capture)));
  float input[] = {0.125f, -0.25f, 0.5f, -0.125f, 0.25f, -0.5f}, output[6];
  REQUIRE((audio_ring_buffer_write(microphone.capture_buffer, input, ARRAY_SIZE(input))) == (ASCIICHAT_OK));
  REQUIRE((session_capture_read_audio(capture, output, ARRAY_SIZE(output))) == (ARRAY_SIZE(output)));
  REQUIRE(std::memcmp((input), (output), (sizeof(input))) == 0);
  session_capture_set_audio_context(capture, &microphone);
  REQUIRE((session_capture_get_audio_context(capture)) == (&microphone));
  session_capture_destroy(capture);
  capture = NULL;
  REQUIRE(microphone.initialized);
  audio_destroy(&microphone);
}

static bool cancel(void *context) {
  ++*(unsigned *)context;
  return true;
}
TEST_CASE("session_capture_lifecycle/cancelled_creation_and_empty_network_capture_do_not_open_local_sources", "[session_capture_lifecycle]") {
  unsigned calls = 0;
  session_capture_config_t config = {.type = MEDIA_SOURCE_TEST, .should_exit_callback = cancel, .callback_data = &calls};
  REQUIRE((session_mirror_capture_create(&config)) == nullptr);
  REQUIRE((calls) == (1));
  capture = session_network_capture_create(0);
  REQUIRE((capture) != nullptr);
  REQUIRE((session_capture_get_target_fps(capture)) == (60));
  REQUIRE((session_capture_get_media_source(capture)) == nullptr);
  REQUIRE(!(session_capture_is_valid(capture)));
  REQUIRE((session_capture_read_frame(capture)) == nullptr);
  REQUIRE(!(session_capture_has_audio(capture)));
  float output = 7;
  REQUIRE((session_capture_read_audio(capture, &output, 1)) == (0));
  REQUIRE((output) == (7));
  REQUIRE(!(session_capture_cancel_open(capture)));
}

TEST_CASE("session_capture_lifecycle/paused_file_redraw_retains_frame_without_counting_another_acquisition", "[session_capture_lifecycle]") {
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "capture-contract", &fd)) == (0));
  REQUIRE((platform_close(fd)) == (0));
  FILE *file = platform_fopen("capture-contract", path, "wb");
  REQUIRE((file) != nullptr);
  const char header[] = "P6\n4 2\n255\n";
  REQUIRE((fwrite(header, 1, sizeof(header) - 1, file)) == (sizeof(header) - 1));
  const rgb_pixel_t pixels[8] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {80, 80, 80}, {160, 160, 160}, {240, 240, 240}};
  REQUIRE((fwrite(pixels, sizeof(*pixels), 8, file)) == (8));
  REQUIRE((fclose(file)) == (0));
  session_capture_config_t config = {.type = MEDIA_SOURCE_FILE, .path = path, .target_fps = 60, .loop = true};
  capture = session_mirror_capture_create(&config);
  REQUIRE((capture) != nullptr);
  // File decoding can publish its first frame after the source is initialized.
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  image_t *frame = NULL;
  while (!frame && time_get_ns() < deadline) {
    frame = session_capture_read_frame(capture);
    if (!frame)
      platform_sleep_ms(1);
  }
  REQUIRE((frame) != nullptr);
  REQUIRE((frame->w) == (4));
  REQUIRE((frame->h) == (2));
  REQUIRE(std::memcmp((frame->pixels), (pixels), (sizeof(pixels))) == 0);
  uint64_t serial = session_capture_frame_serial(capture);
  media_source_t *source = static_cast<media_source_t *>(session_capture_get_media_source(capture));
  REQUIRE((source) != nullptr);
  media_source_pause(source);
  session_capture_redraw_media(capture);
  frame = session_capture_read_frame(capture);
  REQUIRE((frame) != nullptr);
  REQUIRE(std::memcmp((frame->pixels), (pixels), (sizeof(pixels))) == 0);
  REQUIRE((session_capture_frame_serial(capture)) == (serial));
  REQUIRE((session_capture_read_frame(capture)) == nullptr);
  REQUIRE((session_capture_frame_serial(capture)) == (serial));
  session_capture_redraw_media(capture);
  REQUIRE((session_capture_read_frame(capture)) != nullptr);
  REQUIRE((session_capture_frame_serial(capture)) == (serial));
  REQUIRE(!(session_capture_at_end(capture)));
}

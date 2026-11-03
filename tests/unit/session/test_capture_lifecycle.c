#include <criterion/criterion.h>
#include "common/session/capture.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static session_capture_ctx_t *capture;
static char path[PLATFORM_MAX_PATH_LENGTH];

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  options->test_pattern = true;
  options->width = 64;
  options->height = 24;
  options->audio_enabled = false;
  cr_assert_eq(options_state_set(options), ASCIICHAT_OK);
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
TestSuite(session_capture_lifecycle, .init = setup, .fini = teardown, .timeout = 20);

static void create_pattern(bool resize) {
  session_capture_config_t config = {.type = MEDIA_SOURCE_TEST, .target_fps = 120, .resize_for_network = resize};
  capture = session_mirror_capture_create(&config);
  cr_assert_not_null(capture);
  cr_assert(session_capture_is_valid(capture));
}

Test(session_capture_lifecycle, acquired_frames_advance_serial_but_transmission_copies_do_not) {
  create_pattern(false);
  cr_assert_eq(session_capture_get_target_fps(capture), 120);
  cr_assert_eq(session_capture_get_current_fps(capture), 0);
  uint64_t epoch = session_capture_source_epoch(capture);
  image_t *frame = session_capture_read_frame(capture);
  cr_assert_not_null(frame);
  cr_assert_eq(session_capture_frame_serial(capture), 1);
  image_t *copy = session_capture_process_for_transmission(capture, frame);
  cr_assert_not_null(copy);
  cr_assert_neq(copy, frame);
  cr_assert_eq(copy->w, frame->w);
  cr_assert_eq(copy->h, frame->h);
  cr_assert_arr_eq(copy->pixels, frame->pixels, (size_t)frame->w * frame->h * sizeof(rgb_pixel_t));
  cr_assert_eq(session_capture_frame_serial(capture), 1);
  cr_assert_not_null(session_capture_read_frame(capture));
  cr_assert_eq(session_capture_frame_serial(capture), 2);
  cr_assert_eq(session_capture_source_epoch(capture), epoch);
  cr_assert_gt(session_capture_get_current_fps(capture), 0);
  cr_assert_not(session_capture_at_end(capture));
  image_destroy(copy);
}

Test(session_capture_lifecycle, transmission_workspace_reuses_storage_accepts_alias_and_resizes_to_network_budget) {
  create_pattern(true);
  image_t *frame = image_new(640, 360), *output = NULL;
  cr_assert_not_null(frame);
  for (int i = 0; i < frame->w * frame->h; ++i)
    frame->pixels[i] = (rgb_pixel_t){30, 60, 90};
  cr_assert_eq(session_capture_process_for_transmission_into(capture, frame, &output), ASCIICHAT_OK);
  cr_assert_not_null(output);
  cr_assert_eq(output->w, 320);
  cr_assert_eq(output->h, 180);
  cr_assert_eq(output->pixels[0].r, 30);
  cr_assert_eq(output->pixels[0].g, 60);
  image_t *retained = output;
  cr_assert_eq(session_capture_process_for_transmission_into(capture, frame, &output), ASCIICHAT_OK);
  cr_assert_eq(output, retained);
  cr_assert_eq(session_capture_process_for_transmission_into(capture, output, &output), ASCIICHAT_OK);
  cr_assert_eq(output, retained);
  cr_assert_eq(output->pixels[0].b, 90);
  cr_assert_eq(session_capture_process_for_transmission_into(capture, NULL, &output), ERROR_INVALID_PARAM);
  cr_assert_eq(output, retained);
  image_destroy(output);
  image_destroy(frame);
}

Test(session_capture_lifecycle, microphone_fallback_borrows_context_and_preserves_exact_captured_samples) {
  audio_context_t microphone = {0};
  cr_assert_eq(audio_init(&microphone), ASCIICHAT_OK);
  session_capture_config_t config = {
      .type = MEDIA_SOURCE_TEST, .enable_audio = true, .audio_fallback_to_mic = true, .mic_audio_ctx = &microphone};
  capture = session_mirror_capture_create(&config);
  cr_assert_not_null(capture);
  cr_assert(session_capture_has_audio(capture));
  cr_assert_not(session_capture_using_file_audio(capture));
  float input[] = {0.125f, -0.25f, 0.5f}, output[3];
  cr_assert_eq(audio_ring_buffer_write(microphone.capture_buffer, input, 3), ASCIICHAT_OK);
  cr_assert_eq(session_capture_read_audio(capture, output, 3), 3);
  cr_assert_arr_eq(input, output, sizeof(input));
  session_capture_set_audio_context(capture, &microphone);
  cr_assert_eq(session_capture_get_audio_context(capture), &microphone);
  session_capture_destroy(capture);
  capture = NULL;
  cr_assert(microphone.initialized);
  audio_destroy(&microphone);
}

static bool cancel(void *context) {
  ++*(unsigned *)context;
  return true;
}
Test(session_capture_lifecycle, cancelled_creation_and_empty_network_capture_do_not_open_local_sources) {
  unsigned calls = 0;
  session_capture_config_t config = {
      .type = MEDIA_SOURCE_TEST, .should_exit_callback = cancel, .callback_data = &calls};
  cr_assert_null(session_mirror_capture_create(&config));
  cr_assert_eq(calls, 1);
  capture = session_network_capture_create(0);
  cr_assert_not_null(capture);
  cr_assert_eq(session_capture_get_target_fps(capture), 60);
  cr_assert_null(session_capture_get_media_source(capture));
  cr_assert_not(session_capture_is_valid(capture));
  cr_assert_null(session_capture_read_frame(capture));
  cr_assert_not(session_capture_has_audio(capture));
  float output = 7;
  cr_assert_eq(session_capture_read_audio(capture, &output, 1), 0);
  cr_assert_eq(output, 7);
  cr_assert_not(session_capture_cancel_open(capture));
}

Test(session_capture_lifecycle, paused_file_redraw_retains_frame_without_counting_another_acquisition) {
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "capture-contract", &fd), 0);
  cr_assert_eq(platform_close(fd), 0);
  FILE *file = platform_fopen("capture-contract", path, "wb");
  cr_assert_not_null(file);
  const char header[] = "P6\n4 2\n255\n";
  cr_assert_eq(fwrite(header, 1, sizeof(header) - 1, file), sizeof(header) - 1);
  const rgb_pixel_t pixels[8] = {{255, 0, 0}, {0, 255, 0},  {0, 0, 255},     {255, 255, 255},
                                 {0, 0, 0},   {80, 80, 80}, {160, 160, 160}, {240, 240, 240}};
  cr_assert_eq(fwrite(pixels, sizeof(*pixels), 8, file), 8);
  cr_assert_eq(fclose(file), 0);
  session_capture_config_t config = {.type = MEDIA_SOURCE_FILE, .path = path, .target_fps = 60, .loop = true};
  capture = session_mirror_capture_create(&config);
  cr_assert_not_null(capture);
  image_t *frame = session_capture_read_frame(capture);
  cr_assert_not_null(frame);
  cr_assert_eq(frame->w, 4);
  cr_assert_eq(frame->h, 2);
  cr_assert_arr_eq(frame->pixels, pixels, sizeof(pixels));
  uint64_t serial = session_capture_frame_serial(capture);
  media_source_t *source = session_capture_get_media_source(capture);
  cr_assert_not_null(source);
  media_source_pause(source);
  session_capture_redraw_media(capture);
  frame = session_capture_read_frame(capture);
  cr_assert_not_null(frame);
  cr_assert_arr_eq(frame->pixels, pixels, sizeof(pixels));
  cr_assert_eq(session_capture_frame_serial(capture), serial);
  cr_assert_null(session_capture_read_frame(capture));
  cr_assert_eq(session_capture_frame_serial(capture), serial);
  session_capture_redraw_media(capture);
  cr_assert_not_null(session_capture_read_frame(capture));
  cr_assert_eq(session_capture_frame_serial(capture), serial);
  cr_assert_not(session_capture_at_end(capture));
}

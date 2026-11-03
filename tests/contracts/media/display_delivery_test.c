#include <criterion/criterion.h>
#include "session/display.h"
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/util/endian.h>
#include <string.h>
#include <ascii-chat/media/render/renderer.h>
static bool fail_encoder;
static unsigned encoder_calls;
static int encoder_fps;
static asciichat_error_t delivery_encoder(const char *path, int cols, int rows, int fps, int theme,
                                          render_file_ctx_t **out) {
  ++encoder_calls;
  encoder_fps = fps;
  if (fail_encoder)
    return SET_ERRNO(ERROR_MEDIA_INIT, "fixture encoder failed");
  return render_file_create(path, cols, rows, fps, theme, out);
}
static bool delivery_interactive;
static asciichat_error_t delivery_result;
static unsigned delivery_writes, delivery_fresh, delivery_retained, delivery_exits;
static int delivery_fd;
static char delivery_bytes[32768];
static size_t delivery_length;
static bool delivery_terminal_interactive(void) {
  return delivery_interactive;
}
static asciichat_error_t delivery_write(int fd, const char *data, size_t length) {
  ++delivery_writes;
  delivery_fd = fd;
  cr_assert_lt(length, sizeof(delivery_bytes));
  memcpy(delivery_bytes, data, length);
  delivery_bytes[length] = 0;
  delivery_length = length;
  return delivery_result;
}
static asciichat_error_t delivery_present(ui_screen_t screen, int fd, terminal_size_t size, const char *data,
                                          size_t length) {
  cr_assert_eq(screen, UI_SCREEN_MEDIA);
  cr_assert_gt(size.cols, 0);
  cr_assert_gt(size.rows, 0);
  ++delivery_fresh;
  return delivery_write(fd, data, length);
}
static asciichat_error_t delivery_present_retained(ui_screen_t screen, int fd, terminal_size_t size, const char *data,
                                                   size_t length) {
  ++delivery_retained;
  return delivery_present(screen, fd, size, data, length);
}
static void delivery_exit(void) {
  ++delivery_exits;
}
#define render_file_create delivery_encoder
#define terminal_is_interactive delivery_terminal_interactive
#define ui_controller_write delivery_write
#define ui_controller_present delivery_present
#define ui_controller_present_retained delivery_present_retained
#include "../../../src/common/session/display.c"
#undef render_file_create
#undef terminal_is_interactive
#undef ui_controller_write
#undef ui_controller_present
#undef ui_controller_present_retained

static session_display_ctx_t *display;
static audio_context_t local_audio;
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  errno_suppress(true);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = options_t_new();
  v->width = 40;
  v->height = 12;
  v->color_mode = TERM_COLOR_NONE;
  v->audio_enabled = false;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  static app_callbacks_t callbacks = {.signal_exit = delivery_exit};
  app_callbacks_register(&callbacks);
}
static void teardown(void) {
  if (display)
    session_display_destroy(display);
  if (local_audio.initialized)
    audio_destroy(&local_audio);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(display_delivery, .init = setup, .fini = teardown, .timeout = 20);
static void create(bool snapshot) {
  session_display_config_t config = {
      .snapshot_mode = snapshot, .palette_type = PALETTE_STANDARD, .color_mode = TERM_COLOR_NONE};
  display = session_display_create(&config);
  cr_assert_not_null(display);
}
Test(display_delivery, piped_frames_are_delimited_and_failed_output_is_latched_once) {
  create(false);
  display->has_tty = false;
  delivery_interactive = false;
  session_display_write_ascii_frame(display, "frame", true);
  cr_assert_str_eq(delivery_bytes, "frame\n");
  cr_assert_eq(delivery_writes, 1);
  cr_assert_eq(delivery_fd, STDOUT_FILENO);
  cr_assert_not(atomic_load_bool(&display->first_frame));
  delivery_result = ERROR_TERMINAL;
  session_display_write_ascii_frame(display, "next", true);
  cr_assert(atomic_load_bool(&display->output_failed));
  cr_assert_eq(delivery_exits, 1);
  cr_assert_eq(delivery_writes, 2);
  session_display_write_ascii_frame(display, "never retried", true);
  cr_assert_eq(delivery_writes, 2);
  cr_assert_eq(delivery_exits, 1);
}
Test(display_delivery, interactive_pipe_preserves_newline_and_latches_broken_output) {
  create(true);
  display->has_tty = false;
  delivery_interactive = true;
  session_display_write_ascii_frame(display, "interactive", true);
  cr_assert_str_eq(delivery_bytes, "interactive\n");
  delivery_result = ERROR_TERMINAL;
  session_display_write_ascii_frame(display, "failure", true);
  cr_assert_eq(delivery_exits, 1);
  cr_assert(atomic_load_bool(&display->output_failed));
}
Test(display_delivery, tty_frames_use_cursor_prefix_reuse_storage_and_distinguish_retained_submissions) {
  create(false);
  display->has_tty = true;
  display->tty_info.fd = -1;
  delivery_interactive = true;
  session_display_write_ascii_frame(display, "short", true);
  cr_assert_str_eq(delivery_bytes, "\033[Hshort");
  cr_assert_eq(delivery_fresh, 1);
  char large[9000];
  memset(large, '#', sizeof(large) - 1);
  large[sizeof(large) - 1] = 0;
  session_display_write_ascii_frame(display, large, true);
  cr_assert_eq(delivery_length, strlen(large) + 3);
  cr_assert_geq(display->tty_frame_capacity, delivery_length);
  char *workspace = display->tty_frame_buffer;
  size_t capacity = display->tty_frame_capacity;
  session_display_write_ascii_frame(display, "retained", false);
  cr_assert_str_eq(delivery_bytes, "\033[Hretained");
  cr_assert_eq(delivery_retained, 1);
  cr_assert_eq(display->tty_frame_buffer, workspace);
  cr_assert_eq(display->tty_frame_capacity, capacity);
  cr_assert(session_display_has_tty(display));
  cr_assert_eq(session_display_get_tty_fd(display), -1);
  display->has_tty = false;
}
Test(display_delivery, ansi_stripping_expands_rep_before_discarding_color_sequences) {
  create(false);
  display->has_tty = false;
  cr_assert_eq(options_set_bool("strip_ansi", true), ASCIICHAT_OK);
  session_display_write_ascii_frame(display, "\033[31m#\033[3b\033[0m", true);
  cr_assert_str_eq(delivery_bytes, "####\n");
  cr_assert_eq(delivery_length, 5);
}
Test(display_delivery, local_audio_playback_uses_the_borrowed_ring_without_network_jitter) {
  cr_assert_eq(audio_init(&local_audio), ASCIICHAT_OK);
  audio_ring_buffer_destroy(local_audio.playback_buffer);
  local_audio.playback_buffer = audio_ring_buffer_create_for_capture();
  cr_assert_not_null(local_audio.playback_buffer);
  session_display_config_t config = {.snapshot_mode = true,
                                     .palette_type = PALETTE_STANDARD,
                                     .color_mode = TERM_COLOR_NONE,
                                     .enable_audio_playback = true,
                                     .audio_ctx = &local_audio};
  display = session_display_create(&config);
  cr_assert_not_null(display);
  cr_assert(session_display_has_audio_playback(display));
  float samples[128];
  for (unsigned i = 0; i < ARRAY_SIZE(samples); ++i)
    samples[i] = i & 1 ? 0.25f : -0.25f;
  cr_assert_eq(session_display_write_audio(display, samples, ARRAY_SIZE(samples)), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_available_read(local_audio.playback_buffer), ARRAY_SIZE(samples));
  cr_assert_not(local_audio.playback_buffer->jitter_buffer_enabled);
  float actual[128];
  cr_assert_eq(audio_ring_buffer_read(local_audio.playback_buffer, actual, 128), 128);
  cr_assert_arr_eq(actual, samples, sizeof(samples));
  size_t count = AUDIO_RING_BUFFER_SIZE - 16;
  float *large = SAFE_MALLOC(count * sizeof(float), float *);
  for (size_t i = 0; i < count; ++i)
    large[i] = 0.125f;
  cr_assert_eq(session_display_write_audio(display, large, count), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_read(local_audio.playback_buffer, large, count), count);
  for (size_t i = 0; i < count; ++i)
    cr_assert_float_eq(large[i], 0.125f, 1e-6);
  uint64_t before = atomic_load_u64(&local_audio.playback_buffer->write_index);
  cr_assert_eq(session_display_write_audio(display, samples, AUDIO_RING_BUFFER_SIZE), ASCIICHAT_OK);
  cr_assert_eq(atomic_load_u64(&local_audio.playback_buffer->write_index), before);
  SAFE_FREE(large);
  session_display_destroy(display);
  display = NULL;
  cr_assert(local_audio.initialized);
}
Test(display_delivery, constructor_rejects_incompatible_unicode_policy_and_cancellation_before_allocating) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->zalgo = 1;
  v->force_utf8 = UTF8_SETTING_FALSE;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  cr_assert_null(session_display_create(NULL));
  cr_assert_eq(GET_ERRNO(), ERROR_USAGE);
  CLEAR_ERRNO_ALL();
  v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->zalgo = 0;
  v->palette_custom_set = true;
  SAFE_STRNCPY(v->palette_custom, " .#", sizeof(v->palette_custom));
  v->palette_type = PALETTE_CUSTOM;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  display = session_display_create(NULL);
  cr_assert_not_null(display);
  cr_assert_str_eq(session_display_get_palette_chars(display), " .#");
}

Test(display_delivery, raw_delivery_preserves_bytes_and_selects_the_owned_terminal_descriptor) {
  create(false);
  display->has_tty = false;
  const char bytes[] = {'a', 0, 'b'};
  session_display_write_raw(display, bytes, sizeof(bytes));
  cr_assert_eq(delivery_fd, STDOUT_FILENO);
  cr_assert_eq(delivery_length, sizeof(bytes));
  cr_assert_arr_eq(delivery_bytes, bytes, sizeof(bytes));
  display->has_tty = true;
  display->tty_info.fd = 99;
  session_display_write_raw(display, "tty", 3);
  cr_assert_eq(delivery_fd, 99);
  cr_assert_arr_eq(delivery_bytes, "tty", 3);
  display->has_tty = false;
  display->tty_info.fd = -1;
}
Test(display_delivery, rain_and_zalgo_effects_are_owned_by_the_display_and_work_for_received_text) {
  cr_assert_eq(options_set_bool("matrix_rain", true), ASCIICHAT_OK);
  create(false);
  cr_assert_not_null(display->digital_rain);
  display->has_tty = false;
  session_display_write_ascii_frame(display, "hello\nworld", true);
  cr_assert_eq(delivery_writes, 1);
  cr_assert_gt(delivery_length, 0);
  cr_assert_eq(delivery_bytes[delivery_length - 1], '\n');
  cr_assert_eq(options_set_bool("matrix_rain", false), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("zalgo", 2), ASCIICHAT_OK);
  session_display_write_ascii_frame(display, "test", true);
  cr_assert_eq(delivery_writes, 2);
  cr_assert_gt(delivery_length, 5);
  cr_assert_eq(delivery_bytes[delivery_length - 1], '\n');
}
Test(display_delivery, global_help_cancel_is_consumed_once_and_snapshot_tty_submission_starts_the_clock) {
  create(true);
  session_display_set_global_context(display);
  cr_assert_eq(session_display_get_global_context(), display);
  keyboard_help_toggle_global();
  cr_assert(keyboard_help_check_signal_cancel());
  cr_assert_not(keyboard_help_check_signal_cancel());
  session_display_set_global_context_public(NULL);
  cr_assert_null(session_display_get_global_context());
  cr_assert_not(keyboard_help_is_active(NULL));
  CLEAR_ERRNO_ALL();
  cr_assert_eq(options_set_bool("snapshot_mode", true), ASCIICHAT_OK);
  cr_assert_not(keyboard_help_is_active(display));
  display->has_tty = true;
  display->tty_info.fd = -1;
  delivery_interactive = true;
  g_snapshot_first_frame_rendered = false;
  session_display_write_ascii_frame(display, "snapshot", true);
  cr_assert(g_snapshot_first_frame_rendered);
  cr_assert_gt(g_snapshot_first_frame_rendered_ns, 0);
  display->has_tty = false;
}
Test(display_delivery, invalid_conversion_and_recording_inputs_preserve_existing_display_state) {
  create(false);
  image_t *image = image_new(4, 4);
  cr_assert_not_null(image);
  cr_assert_null(convert_to_ascii(NULL, image, false));
  CLEAR_ERRNO_ALL();
  display->initialized = false;
  cr_assert_null(convert_to_ascii(display, image, false));
  CLEAR_ERRNO_ALL();
  display->initialized = true;
  cr_assert_eq(session_display_encode_frame(display, NULL, 0), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  cr_assert_eq(session_display_encode_frame(display, image, 0), ASCIICHAT_OK);
  session_display_write_ascii_frame(display, NULL, true);
  cr_assert_eq(delivery_writes, 0);
  CLEAR_ERRNO_ALL();
  image_t *workspace = display_image_workspace(&display->filter_workspace, 2, 2);
  cr_assert_not_null(workspace);
  cr_assert_eq(display_image_workspace(&display->filter_workspace, 2, 2), workspace);
  workspace = display_image_workspace(&display->filter_workspace, 3, 4);
  cr_assert_eq(workspace->w, 3);
  cr_assert_eq(workspace->h, 4);
  image_destroy(image);
}

static void encoder_options(int fps) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  SAFE_STRNCPY(v->render_file, "fixture-output.mp4", sizeof(v->render_file));
  v->fps = fps;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
}
Test(display_delivery, encoder_start_failure_rolls_back_constructor_and_preserves_the_cause) {
  encoder_options(30);
  fail_encoder = true;
  cr_assert_null(session_display_create(NULL));
  cr_assert_eq(GET_ERRNO(), ERROR_MEDIA_INIT);
  asciichat_error_context_t cause;
  cr_assert(HAS_ERRNO(&cause));
  cr_assert_not_null(strstr(cause.context_message, "fixture encoder failed"));
  cr_assert_eq(encoder_calls, 1);
  cr_assert_eq(encoder_fps, 30);
}
Test(display_delivery, deferred_encoder_selects_media_then_option_then_default_fps_and_remains_retryable) {
  encoder_options(0);
  fail_encoder = true;
  session_display_config_t config = {.snapshot_mode = true,
                                     .palette_type = PALETTE_STANDARD,
                                     .color_mode = TERM_COLOR_NONE,
                                     .defer_render_file = true};
  display = session_display_create(&config);
  cr_assert_not_null(display);
  cr_assert_eq(encoder_calls, 0);
  cr_assert_eq(session_display_init_render_file(display, 0), ERROR_MEDIA_INIT);
  cr_assert_eq(encoder_fps, 60);
  cr_assert_not(session_display_has_render_file(display));
  CLEAR_ERRNO_ALL();
  encoder_options(25);
  cr_assert_eq(session_display_init_render_file(display, 0), ERROR_MEDIA_INIT);
  cr_assert_eq(encoder_fps, 25);
  CLEAR_ERRNO_ALL();
  cr_assert_eq(session_display_init_render_file(display, 50), ERROR_MEDIA_INIT);
  cr_assert_eq(encoder_fps, 50);
  cr_assert_not(session_display_has_render_file(display));
  cr_assert_eq(encoder_calls, 3);
}

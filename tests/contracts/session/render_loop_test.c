#include <criterion/criterion.h>
#include "session/render.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/log/log.h>
#include <string.h>

static session_display_ctx_t *display;
static image_t *image;
static FILE *output;
static int saved_stdout = -1;
static unsigned captures, sleeps, limit;
static bool fail_capture, gap_first, exit_immediately;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("width", 40), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("height", 12), ASCIICHAT_OK);
  cr_assert_eq(options_set_bool("stretch", true), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("render_mode", RENDER_MODE_FOREGROUND), ASCIICHAT_OK);
  ascii_simd_init();
  output = platform_tmpfile();
  cr_assert_not_null(output);
  saved_stdout = platform_dup(1);
  cr_assert_geq(saved_stdout, 0);
  cr_assert_eq(platform_dup2(fileno(output), 1), ASCIICHAT_OK);
  image = image_new(8, 4);
  cr_assert_not_null(image);
  for (unsigned i = 0; i < 32; ++i)
    image->pixels[i] = (rgb_pixel_t){.r = 120, .g = 80, .b = 40};
  session_display_config_t config = {
      .palette_type = PALETTE_CUSTOM, .custom_palette = "XX", .color_mode = TERM_COLOR_NONE};
  display = session_display_create(&config);
  cr_assert_not_null(display);
  captures = sleeps = 0;
  limit = 3;
  fail_capture = gap_first = exit_immediately = false;
}
static void teardown(void) {
  session_display_destroy(display);
  image_destroy(image);
  if (saved_stdout >= 0) {
    platform_dup2(saved_stdout, 1);
    platform_close(saved_stdout);
  }
  if (output)
    fclose(output);
  options_state_destroy();
  log_destroy();
}
TestSuite(render_loop_contract, .init = setup, .fini = teardown, .timeout = 15);
static bool done(void *context) {
  cr_assert_eq(context, &captures);
  return exit_immediately || captures >= limit;
}
static void pace(void *context) {
  cr_assert_eq(context, &captures);
  cr_assert_eq(sleeps, captures);
  ++sleeps;
}
static image_t *capture(void *context) {
  cr_assert_eq(context, &captures);
  cr_assert_eq(sleeps, captures + 1);
  ++captures;
  if (fail_capture) {
    SET_ERRNO(ERROR_NETWORK, "Fixture capture failed");
    return NULL;
  }
  if (gap_first && captures == 1)
    return NULL;
  return image;
}
static asciichat_error_t run(void) {
  return session_render_loop(NULL, display, done, capture, pace, NULL, &captures);
}
static size_t count_pixels(void) {
  fflush(output);
  rewind(output);
  size_t count = 0;
  int ch, previous = 0;
  while ((ch = fgetc(output)) != EOF) {
    if (ch == '\033' && fgetc(output) == '[') {
      unsigned repeat = 0;
      while ((ch = fgetc(output)) >= '0' && ch <= '9')
        repeat = repeat * 10 + (unsigned)(ch - '0');
      if (ch == 'b' && previous == 'X')
        count += repeat ? repeat : 1;
    } else {
      count += ch == 'X';
      previous = ch;
    }
  }
  return count;
}
Test(render_loop_contract, event_callbacks_deliver_frames_and_temporary_gaps_do_not_exit) {
  gap_first = true;
  SET_ERRNO(ERROR_MEDIA_DECODE, "An older failure must not poison a successful capture");
  cr_assert_eq(run(), ASCIICHAT_OK);
  cr_assert_eq(captures, 3);
  cr_assert_eq(sleeps, 3);
  cr_assert_eq(count_pixels(), 2 * 40 * 12, "pixel count: %zu", count_pixels());
  cr_assert_null(session_display_get_global_context());
}
Test(render_loop_contract, capture_failure_propagates_and_unregisters_display) {
  fail_capture = true;
  cr_assert_eq(run(), ERROR_NETWORK);
  cr_assert_eq(captures, 1);
  cr_assert_eq(sleeps, 1);
  cr_assert_eq(count_pixels(), 0);
  cr_assert_null(session_display_get_global_context());
}
Test(render_loop_contract, preexisting_exit_does_not_capture_or_render) {
  exit_immediately = true;
  cr_assert_eq(run(), ASCIICHAT_OK);
  cr_assert_eq(captures, 0);
  cr_assert_eq(sleeps, 0);
  cr_assert_eq(count_pixels(), 0);
}
Test(render_loop_contract, callback_pairs_and_required_parameters_are_validated_before_use) {
  cr_assert_eq(session_render_loop(NULL, NULL, done, capture, pace, NULL, &captures), ERROR_INVALID_PARAM);
  cr_assert_eq(session_render_loop(NULL, display, NULL, capture, pace, NULL, &captures), ERROR_INVALID_PARAM);
  cr_assert_eq(session_render_loop(NULL, display, done, NULL, NULL, NULL, &captures), ERROR_INVALID_PARAM);
  cr_assert_eq(session_render_loop(NULL, display, done, capture, NULL, NULL, &captures), ERROR_INVALID_PARAM);
  cr_assert_eq(captures, 0);
}

Test(render_loop_contract, immediate_snapshot_renders_one_frame_and_starts_its_timer) {
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  *values = *options_get();
  values->snapshot_mode = true;
  values->snapshot_delay = 0;
  cr_assert_eq(options_state_set(values), ASCIICHAT_OK);
  SAFE_FREE(values);
  session_display_destroy(display);
  session_display_config_t config = {
      .snapshot_mode = true, .palette_type = PALETTE_CUSTOM, .custom_palette = "XX", .color_mode = TERM_COLOR_NONE};
  display = session_display_create(&config);
  cr_assert_not_null(display);
  g_snapshot_first_frame_rendered = false;
  g_snapshot_first_frame_rendered_ns = 0;
  limit = 5;
  cr_assert_eq(run(), ASCIICHAT_OK);
  cr_assert_eq(captures, 1);
  cr_assert_eq(sleeps, 1);
  cr_assert(g_snapshot_first_frame_rendered);
  cr_assert_gt(g_snapshot_first_frame_rendered_ns, 0);
  cr_assert_eq(count_pixels(), 40 * 12, "pixel count: %zu", count_pixels());
  cr_assert_null(session_display_get_global_context());
}

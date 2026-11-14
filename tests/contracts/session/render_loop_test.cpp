#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "session/render.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/runtime.h>
#include <libplatform/system.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/log/log.h>
#include <string.h>
#include <ascii-chat/tui/input.h>
static keyboard_key_t render_key(ui_screen_t screen) {
  ASCIICHAT_REQUIRE_EQ(screen, UI_SCREEN_MEDIA);
  return (keyboard_key_t)'q';
}
#define ui_input_read_key render_key
#include "../../../src/common/session/render.c"
#undef ui_input_read_key

static session_display_ctx_t *display;
static image_t *image;
static FILE *output;
static int saved_stdout = -1;
static unsigned captures, sleeps, limit;
static bool fail_capture, gap_first, exit_immediately;
static char output_preview[129];
static long output_size;
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_int("width", 40), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_int("height", 12), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("stretch", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("palette_explicit", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_int("render_mode", RENDER_MODE_FOREGROUND), ASCIICHAT_OK);
  options_edit_t palette_edits[] = {
      {.name = "palette_type", .type = OPTIONS_VALUE_INT, .value.integer = PALETTE_CUSTOM},
      {.name = "palette_custom", .type = OPTIONS_VALUE_STRING, .value.string = "XX"},
  };
  options_patch_t palette_patch = {.edits = palette_edits, .count = 2, .expected_revision = OPTIONS_REVISION_ANY};
  ASCIICHAT_REQUIRE_EQ(options_apply_patch(&palette_patch, NULL), ASCIICHAT_OK);
  ascii_simd_init();
  output = platform_tmpfile();
  ASCIICHAT_REQUIRE_NOT_NULL(output);
  saved_stdout = platform_dup(1);
  ASCIICHAT_REQUIRE_GE(saved_stdout, 0);
  ASCIICHAT_REQUIRE_EQ(platform_dup2(fileno(output), 1), ASCIICHAT_OK);
  image = image_new(8, 4);
  ASCIICHAT_REQUIRE_NOT_NULL(image);
  for (unsigned i = 0; i < 32; ++i)
    image->pixels[i] = (rgb_pixel_t){.r = 120, .g = 80, .b = 40};
  session_display_config_t config = {
      .palette_type = PALETTE_CUSTOM, .custom_palette = "XX", .color_mode = TERM_COLOR_NONE};
  display = session_display_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(display);
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
static bool done(void *context) {
  ASCIICHAT_REQUIRE_EQ(context, &captures);
  return exit_immediately || captures >= limit;
}
static void pace(void *context) {
  ASCIICHAT_REQUIRE_EQ(context, &captures);
  ASCIICHAT_REQUIRE_EQ(sleeps, captures);
  ++sleeps;
}
static image_t *capture(void *context) {
  ASCIICHAT_REQUIRE_EQ(context, &captures);
  ASCIICHAT_REQUIRE_EQ(sleeps, captures + 1);
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
  ASCIICHAT_REQUIRE_EQ(fseek(output, 0, SEEK_END), 0);
  output_size = ftell(output);
  rewind(output);
  unsigned char preview[32];
  size_t preview_len = fread(preview, 1, sizeof(preview), output);
  for (size_t i = 0; i < preview_len; ++i)
    snprintf(output_preview + i * 3, sizeof(output_preview) - i * 3, "%02x ", preview[i]);
  output_preview[preview_len * 3] = '\0';
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
TEST_CASE("render_loop_contract/event_callbacks_deliver_frames_and_temporary_gaps_do_not_exit", "[render_loop_contract]") {
  gap_first = true;
  SET_ERRNO(ERROR_MEDIA_DECODE, "An older failure must not poison a successful capture");
  ASCIICHAT_REQUIRE_EQ(run(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(captures, 3);
  ASCIICHAT_REQUIRE_EQ(sleeps, 3);
  ASCIICHAT_REQUIRE_EQ(count_pixels(), 2 * 40 * 12, "pixel count: %zu, output bytes: %ld, prefix: %s", count_pixels(),
               output_size, output_preview);
  ASCIICHAT_REQUIRE_NULL(session_display_get_global_context());
}
TEST_CASE("render_loop_contract/capture_failure_propagates_and_unregisters_display", "[render_loop_contract]") {
  fail_capture = true;
  ASCIICHAT_REQUIRE_EQ(run(), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(captures, 1);
  ASCIICHAT_REQUIRE_EQ(sleeps, 1);
  ASCIICHAT_REQUIRE_EQ(count_pixels(), 0);
  ASCIICHAT_REQUIRE_NULL(session_display_get_global_context());
}
TEST_CASE("render_loop_contract/preexisting_exit_does_not_capture_or_render", "[render_loop_contract]") {
  exit_immediately = true;
  ASCIICHAT_REQUIRE_EQ(run(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(captures, 0);
  ASCIICHAT_REQUIRE_EQ(sleeps, 0);
  ASCIICHAT_REQUIRE_EQ(count_pixels(), 0);
}
TEST_CASE("render_loop_contract/callback_pairs_and_required_parameters_are_validated_before_use", "[render_loop_contract]") {
  ASCIICHAT_REQUIRE_EQ(session_render_loop(NULL, NULL, done, capture, pace, NULL, &captures), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(session_render_loop(NULL, display, NULL, capture, pace, NULL, &captures), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(session_render_loop(NULL, display, done, NULL, NULL, NULL, &captures), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(session_render_loop(NULL, display, done, capture, NULL, NULL, &captures), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(captures, 0);
}

TEST_CASE("render_loop_contract/immediate_snapshot_renders_one_frame_and_starts_its_timer", "[render_loop_contract]") {
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  *values = *options_get();
  values->snapshot_mode = true;
  values->snapshot_delay = 0;
  ASCIICHAT_REQUIRE_EQ(options_state_set(values), ASCIICHAT_OK);
  SAFE_FREE(values);
  session_display_destroy(display);
  session_display_config_t config = {
      .snapshot_mode = true, .palette_type = PALETTE_CUSTOM, .custom_palette = "XX", .color_mode = TERM_COLOR_NONE};
  display = session_display_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(display);
  g_snapshot_first_frame_rendered = false;
  g_snapshot_first_frame_rendered_ns = 0;
  limit = 5;
  ASCIICHAT_REQUIRE_EQ(run(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(captures, 1);
  ASCIICHAT_REQUIRE_EQ(sleeps, 1);
  ASCIICHAT_REQUIRE(g_snapshot_first_frame_rendered);
  ASCIICHAT_REQUIRE_GT(g_snapshot_first_frame_rendered_ns, 0);
  ASCIICHAT_REQUIRE_EQ(count_pixels(), 40 * 12, "pixel count: %zu, output bytes: %ld, prefix: %s", count_pixels(), output_size,
               output_preview);
  ASCIICHAT_REQUIRE_NULL(session_display_get_global_context());
}

static unsigned keyboard_events;
static void on_key(session_capture_ctx_t *source, int key, void *context) {
  ASCIICHAT_REQUIRE_NULL(source);
  ASCIICHAT_REQUIRE_EQ(context, &captures);
  ASCIICHAT_REQUIRE_EQ(key, 'q');
  ++keyboard_events;
}
TEST_CASE("render_loop_contract/event_mode_delivers_keys_without_requiring_a_native_terminal", "[render_loop_contract]") {
  ASCIICHAT_REQUIRE_EQ(session_render_loop(NULL, display, done, capture, pace, on_key, &captures), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(keyboard_events, 3);
  ASCIICHAT_REQUIRE_EQ(captures, 3);
  ASCIICHAT_REQUIRE_EQ(count_pixels(), 3 * 40 * 12, "output bytes: %ld, prefix: %s", output_size, output_preview);
  ASCIICHAT_REQUIRE_NULL(session_display_get_global_context());
}
TEST_CASE("render_loop_contract/mixed_capture_modes_and_unpaired_sleep_are_rejected_before_capture", "[render_loop_contract]") {
  session_capture_ctx_t *sentinel = (session_capture_ctx_t *)&captures;
  ASCIICHAT_REQUIRE_EQ(session_render_loop(sentinel, display, done, capture, pace, NULL, &captures), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_EQ(session_render_loop(sentinel, display, done, NULL, pace, NULL, &captures), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(captures, 0);
  ASCIICHAT_REQUIRE_EQ(sleeps, 0);
  ASCIICHAT_REQUIRE_EQ(count_pixels(), 0);
}

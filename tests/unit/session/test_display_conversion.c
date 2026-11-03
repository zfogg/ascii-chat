#include <criterion/criterion.h>
#include "common/session/display.h"
#include "common/session/frame_validation.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <string.h>

static session_display_ctx_t *display;
static image_t *image;

static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  options->width = 64;
  options->height = 24;
  options->stretch = true;
  options->color_mode = TERM_COLOR_NONE;
  options->render_mode = RENDER_MODE_FOREGROUND;
  options->force_utf8 = UTF8_SETTING_TRUE;
  cr_assert_eq(options_state_set(options), ASCIICHAT_OK);
  SAFE_FREE(options);
  session_display_config_t config = {
      .snapshot_mode = true, .palette_type = PALETTE_CUSTOM, .custom_palette = ".#", .color_mode = TERM_COLOR_NONE};
  display = session_display_create(&config);
  cr_assert_not_null(display);
  image = image_new(8, 4);
  cr_assert_not_null(image);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 8; ++x) {
      uint8_t gray = y < 2 ? (x < 4 ? 0 : 255) : (x < 4 ? 170 : 85);
      image->pixels[y * 8 + x] = (rgb_pixel_t){gray, gray, gray};
    }
}

static void teardown(void) {
  image_destroy(image);
  session_display_set_global_context_public(NULL);
  session_display_destroy(display);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(session_display_conversion, .init = setup, .fini = teardown);

Test(session_display_conversion, flips_match_explicit_quadrants_and_never_modify_the_input) {
  rgb_pixel_t original[32];
  memcpy(original, image->pixels, sizeof(original));
  const uint8_t quadrants[4][4] = {{0, 255, 170, 85}, {255, 0, 85, 170}, {170, 85, 0, 255}, {85, 170, 255, 0}};
  image_t *expected = image_new(8, 4);
  cr_assert_not_null(expected);
  for (unsigned flips = 0; flips < 4; ++flips) {
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 8; ++x) {
        uint8_t gray = quadrants[flips][(y >= 2 ? 2 : 0) + (x >= 4)];
        expected->pixels[y * 8 + x] = (rgb_pixel_t){gray, gray, gray};
      }
    cr_assert_eq(options_set_bool("flip_x", false), ASCIICHAT_OK);
    cr_assert_eq(options_set_bool("flip_y", false), ASCIICHAT_OK);
    char *reference = session_display_convert_to_ascii(display, expected);
    cr_assert_not_null(reference);
    cr_assert_eq(options_set_bool("flip_x", (flips & 1) != 0), ASCIICHAT_OK);
    cr_assert_eq(options_set_bool("flip_y", (flips & 2) != 0), ASCIICHAT_OK);
    char *actual = session_display_convert_to_ascii_reuse(display, image);
    cr_assert_not_null(actual);
    cr_assert_str_eq(actual, reference);
    cr_assert_arr_eq(image->pixels, original, sizeof(original));
    session_display_release_ascii(display, actual);
    SAFE_FREE(reference);
  }
  image_destroy(expected);
}

Test(session_display_conversion, reused_output_can_be_followed_by_owned_output_and_a_size_change) {
  char *owned = session_display_convert_to_ascii(display, image);
  cr_assert_not_null(owned);
  char *reused = session_display_convert_to_ascii_reuse(display, image);
  cr_assert_not_null(reused);
  cr_assert_str_eq(reused, owned);
  session_display_release_ascii(display, reused);
  cr_assert_eq(options_set_int("width", 40), ASCIICHAT_OK);
  char *smaller = session_display_convert_to_ascii_reuse(display, image);
  cr_assert_not_null(smaller);
  cr_assert_str_neq(smaller, owned);
  cr_assert_eq(session_frame_validate(smaller, strlen(smaller), 40, 24), ASCIICHAT_OK);
  session_display_release_ascii(display, smaller);
  cr_assert_eq(options_set_int("width", 64), ASCIICHAT_OK);
  reused = session_display_convert_to_ascii_reuse(display, image);
  cr_assert_not_null(reused);
  cr_assert_str_eq(reused, owned);
  session_display_release_ascii(display, reused);
  SAFE_FREE(owned);
}

Test(session_display_conversion, filters_and_dynamic_rain_leave_the_source_image_intact) {
  rgb_pixel_t original[32];
  memcpy(original, image->pixels, sizeof(original));
  char *plain = session_display_convert_to_ascii(display, image);
  cr_assert_not_null(plain);
  const color_filter_t filters[] = {COLOR_FILTER_RED, COLOR_FILTER_RAINBOW, COLOR_FILTER_NONE};
  for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); ++i) {
    cr_assert_eq(options_set_int("color_filter", filters[i]), ASCIICHAT_OK);
    char *ascii = session_display_convert_to_ascii_reuse(display, image);
    cr_assert_not_null(ascii);
    cr_assert_arr_eq(image->pixels, original, sizeof(original));
    session_display_release_ascii(display, ascii);
  }
  cr_assert_eq(options_set_bool("matrix_rain", true), ASCIICHAT_OK);
  char *rain = session_display_convert_to_ascii_reuse(display, image);
  asciichat_error_context_t error = {0};
  errno_has(&error);
  cr_assert_not_null(rain, "%s", error.context_message);
  session_display_release_ascii(display, rain);
  cr_assert_eq(options_set_bool("matrix_rain", false), ASCIICHAT_OK);
  char *restored = session_display_convert_to_ascii_reuse(display, image);
  cr_assert_not_null(restored);
  cr_assert_str_eq(restored, plain);
  session_display_release_ascii(display, restored);
  cr_assert_arr_eq(image->pixels, original, sizeof(original));
  SAFE_FREE(plain);
}

Test(session_display_conversion, display_queries_track_borrowed_reader_and_frame_rate_without_recording) {
  cr_assert_str_eq(session_display_get_palette_chars(display), ".#");
  cr_assert_eq(session_display_get_palette_len(display), 2);
  cr_assert_not_null(session_display_get_luminance_palette(display));
  cr_assert_eq(session_display_get_caps(display)->color_level, TERM_COLOR_NONE);
  cr_assert_not(session_display_has_render_file(display));
  cr_assert_not(session_display_has_audio_playback(display));
  cr_assert_not(session_display_has_first_frame(display));
  session_display_reset_first_frame(display);
  session_display_set_render_fps(display, 24);
  cr_assert_eq(session_display_get_render_fps(display), 24);
  session_display_set_render_fps(display, 0);
  cr_assert_eq(session_display_get_render_fps(display), 0);
  int reader;
  session_display_set_stdin_reader(display, &reader);
  cr_assert_eq(session_display_get_stdin_reader(display), &reader);
  session_display_set_stdin_reader(display, NULL);
  cr_assert_null(session_display_get_stdin_reader(display));
  session_display_set_global_context_public(display);
  cr_assert_eq(session_display_get_global_context(), display);
  session_display_set_snapshot_actual_duration(display, 0.5);
  cr_assert_eq(session_display_finish_render_file(display), ASCIICHAT_OK);
  float sample = 0.5f;
  cr_assert_eq(session_display_write_audio(display, &sample, 1), ASCIICHAT_OK);
  cr_assert_null(session_display_convert_to_ascii(display, NULL));
}

static bool cancel(void *context) {
  ++*(unsigned *)context;
  return true;
}
Test(session_display_conversion, cancellation_prevents_context_creation_and_invalid_palette_falls_back) {
  unsigned calls = 0;
  session_display_config_t config = {.should_exit_callback = cancel, .callback_data = &calls};
  cr_assert_null(session_display_create(&config));
  cr_assert_eq(calls, 1);
  config = (session_display_config_t){
      .snapshot_mode = true, .palette_type = PALETTE_CUSTOM, .custom_palette = "", .color_mode = TERM_COLOR_NONE};
  session_display_ctx_t *fallback = session_display_create(&config);
  cr_assert_not_null(fallback);
  cr_assert_str_eq(session_display_get_palette_chars(fallback), get_builtin_palette(PALETTE_STANDARD)->chars);
  session_display_destroy(fallback);
}

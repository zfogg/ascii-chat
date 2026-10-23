#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/zalgo.h>
#include <string.h>
#include <ascii-chat/font.h>
#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/tui/controller.h>

Test(zalgo, respects_output_budget) {
  char *output = NULL;
  cr_assert_eq(zalgo_apply("@", 1, 10, SIZE_MAX, &output), ASCIICHAT_OK);
  size_t exact_size = strlen(output);
  SAFE_FREE(output);
  cr_assert_eq(zalgo_apply("@", 1, 10, exact_size, &output), ASCIICHAT_OK);
  SAFE_FREE(output);
  cr_assert_neq(zalgo_apply("@", 1, 10, exact_size - 1, &output), ASCIICHAT_OK);
  cr_assert_null(output);
  cr_assert_neq(zalgo_apply("abc", 3, 1, 2, &output), ASCIICHAT_OK);
  cr_assert_null(output);
}

Test(zalgo, rejects_dense_frame_exceeding_presenter_budget) {
  size_t length = 1024 * 1024 - 1;
  char *input = SAFE_MALLOC(length + 1, char *);
  memset(input, '@', length);
  input[length] = '\0';
  char *output = NULL;
  cr_assert_neq(zalgo_apply(input, length, 10, UI_CONTROLLER_MAX_FRAME_BYTES - 7, &output), ASCIICHAT_OK);
  cr_assert_null(output);
  SAFE_FREE(input);
}

Test(zalgo, preserves_whitespace_unicode_and_controls) {
  const char *source = " \t\r\n\xc3\xa9\x1b[31m\x1b]0;title\a\x1b[0m";
  char *output = NULL;
  cr_assert_eq(zalgo_apply(source, strlen(source), 10, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_str_eq(output, source);
  SAFE_FREE(output);
}

Test(zalgo, expands_rep_before_decoration) {
  char *compressed = NULL, *plain = NULL;
  const char *source = "A\x1b[19b\n";
  const char *expanded = "AAAAAAAAAAAAAAAAAAAA\n";
  cr_assert_eq(zalgo_apply(source, strlen(source), 10, SIZE_MAX, &compressed), ASCIICHAT_OK);
  cr_assert_eq(zalgo_apply(expanded, strlen(expanded), 10, SIZE_MAX, &plain), ASCIICHAT_OK);
  cr_assert_str_eq(compressed, plain);
  SAFE_FREE(compressed);
  SAFE_FREE(plain);
}

Test(zalgo, rejects_invalid_levels) {
  char *output = NULL;
  cr_assert_neq(zalgo_apply("abc", 3, 0, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_null(output);
  cr_assert_neq(zalgo_apply("abc", 3, 11, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_null(output);
}

Test(zalgo, rejects_unbounded_rep) {
  const char *source = "A\x1b[4294967295b";
  char *output = NULL;
  cr_assert_neq(zalgo_apply(source, strlen(source), 10, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_null(output);
  source = "A\x1b[999999999999999999999b";
  cr_assert_neq(zalgo_apply(source, strlen(source), 10, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_null(output);
}

Test(zalgo, deterministic_bounded_and_increasing) {
  char input[1001];
  memset(input, '@', 1000);
  input[1000] = '\0';
  size_t previous = 1000;
  for (int level = 1; level <= 10; level++) {
    char *first = NULL, *second = NULL;
    cr_assert_eq(zalgo_apply(input, 1000, level, SIZE_MAX, &first), ASCIICHAT_OK);
    cr_assert_eq(zalgo_apply(input, 1000, level, SIZE_MAX, &second), ASCIICHAT_OK);
    cr_assert_str_eq(first, second);
    size_t length = strlen(first);
    cr_assert_geq(length, previous);
    cr_assert_leq(length, 21000);
    previous = length;
    size_t bases = 0, marks = 0;
    for (size_t i = 0; i < length; i++) {
      if (first[i] == '@') {
        cr_assert_leq(marks, 10);
        bases++;
        marks = 0;
      } else {
        cr_assert_eq((unsigned char)first[i], 0xcc);
        cr_assert_lt(++i, length);
        cr_assert_geq((unsigned char)first[i], 0x80);
        cr_assert_leq((unsigned char)first[i], 0xbf);
        marks++;
      }
    }
    cr_assert_eq(bases, 1000);
    cr_assert_leq(marks, 10);
    SAFE_FREE(first);
    SAFE_FREE(second);
  }
}

Test(zalgo, raster_levels_are_distinct_and_disable_restores_frame) {
  term_renderer_config_t cfg = {.cols = 80,
                                .rows = 24,
                                .font_size_pt = 12.0,
                                .theme = TERM_RENDERER_THEME_DARK,
                                .font_data = g_font_default,
                                .font_data_size = g_font_default_size};
  terminal_renderer_t *renderer = NULL;
  cr_assert_eq(term_renderer_create(&cfg, &renderer), ASCIICHAT_OK);
  const char *frame =
      "\x1b[0m\x1b[H\x1b[37m@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@";
  cr_assert_eq(term_renderer_feed(renderer, frame, strlen(frame)), ASCIICHAT_OK);
  size_t bytes = (size_t)term_renderer_pitch(renderer) * term_renderer_height_px(renderer);
  uint8_t *baseline = SAFE_MALLOC(bytes, uint8_t *);
  uint8_t *previous = SAFE_MALLOC(bytes, uint8_t *);
  memcpy(baseline, term_renderer_pixels(renderer), bytes);
  memcpy(previous, baseline, bytes);
  for (int level = 1; level <= 10; level++) {
    cr_assert_eq(term_renderer_set_zalgo(renderer, level), ASCIICHAT_OK);
    cr_assert_eq(term_renderer_feed(renderer, frame, strlen(frame)), ASCIICHAT_OK);
    cr_assert_neq(memcmp(previous, term_renderer_pixels(renderer), bytes), 0, "level %d", level);
    memcpy(previous, term_renderer_pixels(renderer), bytes);
  }
  cr_assert_neq(term_renderer_set_zalgo(renderer, 11), ASCIICHAT_OK);
  cr_assert_eq(term_renderer_set_zalgo(renderer, 0), ASCIICHAT_OK);
  cr_assert_eq(term_renderer_feed(renderer, frame, strlen(frame)), ASCIICHAT_OK);
  cr_assert_eq(memcmp(baseline, term_renderer_pixels(renderer), bytes), 0);
  term_renderer_destroy(renderer);
  SAFE_FREE(baseline);
  SAFE_FREE(previous);
}

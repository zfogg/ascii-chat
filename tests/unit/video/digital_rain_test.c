#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/common.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#ifdef __EMSCRIPTEN__
#include <assert.h>
#define Test(suite, name) static void test_##name(void)
#define cr_assert(condition) assert(condition)
#define cr_assert_not_null(value) assert((value) != NULL)
#define cr_assert_float_eq(a, b, epsilon) assert(fabsf((a) - (b)) <= (epsilon))
#define cr_assert_str_eq(a, b) assert(strcmp((a), (b)) == 0)
#else
#include <criterion/criterion.h>
#endif

static void glyphs(const char *input, char *out) {
  while (*input) {
    if (*input == '\033' && input[1] == '[') {
      input += 2;
      while (*input && !(*input >= '@' && *input <= '~'))
        input++;
      if (*input)
        input++;
    } else
      *out++ = *input++;
  }
  *out = 0;
}

Test(digital_rain, ansi_encoding_does_not_change_smoothing) {
  digital_rain_t *a = digital_rain_init(2, 1), *b = digital_rain_init(2, 1);
  for (int frame = 0; frame < 15; frame++) {
    char *x = digital_rain_apply(a, "\033[38;2;255;255;255mxx", 1.0f / 60);
    char *y = digital_rain_apply(b, "\033[38;2;255;255;255mx\033[38;2;255;255;255mx", 1.0f / 60);
    cr_assert_not_null(x);
    cr_assert_not_null(y);
    cr_assert_str_eq(x, y);
    cr_assert_float_eq(a->previous_brightness[1], b->previous_brightness[1], 1e-6f);
    SAFE_FREE(x);
    SAFE_FREE(y);
  }
  digital_rain_destroy(a);
  digital_rain_destroy(b);
}

Test(digital_rain, smoothing_tracks_elapsed_time) {
  digital_rain_t *a = digital_rain_init(16, 1), *b = digital_rain_init(16, 1);
  a->fall_speed = b->fall_speed = 0;
  a->first_frame = b->first_frame = false;
  const char *frame = "xxxxxxxxxxxxxxxx";
  char *output = digital_rain_apply(a, frame, .1f);
  SAFE_FREE(output);
  for (int step = 0; step < 6; step++) {
    output = digital_rain_apply(b, frame, 1.0f / 60);
    SAFE_FREE(output);
  }
  for (int i = 0; i < 16; i++)
    cr_assert_float_eq(a->previous_brightness[i], b->previous_brightness[i], .005f);
  digital_rain_destroy(a);
  digital_rain_destroy(b);
}

Test(digital_rain, mutation_preserves_shading_spaces_and_unicode) {
  const char *input = ".:-/xoa# \n\xe2\x96\x88";
  digital_rain_t *rain = digital_rain_init(10, 2);
  char previous[128], current[128];
  char *output = digital_rain_apply(rain, input, 0);
  glyphs(output, previous);
  SAFE_FREE(output);
  bool changed = false;
  for (int frame = 0; frame < 120; frame++) {
    output = digital_rain_apply(rain, input, 1.0f / 60);
    glyphs(output, current);
    SAFE_FREE(output);
    for (int col = 0; col < 8; col++)
      cr_assert(digital_rain_glyph_group(input[col]) == digital_rain_glyph_group(current[col]));
    cr_assert_str_eq(current + 8, input + 8);
    changed |= strcmp(previous, current) != 0;
  }
  cr_assert(changed);
  output = digital_rain_apply(rain, input, 0);
  glyphs(output, previous);
  SAFE_FREE(output);
  cr_assert_str_eq(previous, current);
  digital_rain_destroy(rain);
}

Test(digital_rain, source_luminance_and_colored_heads) {
  digital_rain_t *rain = digital_rain_init(1, 1);
  rain->columns[0].time_offset = 0;
  rain->columns[0].phase_offset = 0;
  char *light = digital_rain_apply(rain, "\033[38;2;255;255;255mX", 0);
  char *dark = digital_rain_apply(rain, "\033[38;2;8;8;8mX", 0);
  int lr, lg, lb, dr, dg, db;
  cr_assert(sscanf(light, "\033[38;2;%d;%d;%dm", &lr, &lg, &lb) == 3);
  cr_assert(sscanf(dark, "\033[38;2;%d;%d;%dm", &dr, &dg, &db) == 3);
  // Heads remain bright while retaining visibly green saturation.
  cr_assert(lg > 240 && lr > 60 && lr < lg * 0.45f && lb < lg * 0.65f);
  cr_assert(lg > dg * 1.1f);
  rain->cursor_brightness = 8.0f;
  char *boosted = digital_rain_apply(rain, "\033[38;2;255;255;255mX", 0);
  cr_assert(sscanf(boosted, "\033[38;2;%d;%d;%dm", &lr, &lg, &lb) == 3);
  cr_assert(lr < lg * 0.45f && lb < lg * 0.65f);
  SAFE_FREE(boosted);
  SAFE_FREE(light);
  SAFE_FREE(dark);
  digital_rain_destroy(rain);
}

#ifdef __EMSCRIPTEN__
int main(void) {
  test_ansi_encoding_does_not_change_smoothing();
  test_smoothing_tracks_elapsed_time();
  test_mutation_preserves_shading_spaces_and_unicode();
  test_source_luminance_and_colored_heads();
  return 0;
}
#endif

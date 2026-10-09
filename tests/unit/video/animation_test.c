#include <criterion/criterion.h>
#include <ascii-chat/video/anim/controller.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/common.h>
#include <math.h>
#include <string.h>

static animation_config_t config(animation_type_t type) {
  return (animation_config_t){.type = type, .fps = 10, .speed = 1, .hidden_policy = ANIMATION_HIDDEN_PAUSE};
}

Test(animation, clock_pause_visibility_speed_and_reset) {
  animation_t a;
  animation_sample_t s;
  cr_assert_eq(animation_init(&a, "test_animation", config(ANIMATION_SPLASH_RAINBOW)), ASCIICHAT_OK);
  cr_assert_eq(animation_update(&a, 1000000000, &s), ASCIICHAT_OK);
  cr_assert(s.changed);
  cr_assert_eq(s.next_frame_ns, 1100000000);
  animation_update(&a, 1050000000, &s);
  cr_assert_not(s.changed);
  animation_update(&a, 1200000000, &s);
  cr_assert_eq(s.frame, 2);
  animation_set_paused(&a, true);
  animation_update(&a, 2200000000, &s);
  cr_assert_eq(s.frame, 2);
  cr_assert_eq(s.next_frame_ns, UINT64_MAX);
  animation_set_paused(&a, false);
  animation_set_visible(&a, false);
  animation_update(&a, 3200000000, &s);
  cr_assert_eq(s.frame, 2);
  animation_set_visible(&a, true);
  animation_set_speed(&a, 2);
  animation_update(&a, 3700000000, &s);
  cr_assert_float_eq(s.seconds, 1.2, 1e-8);
  cr_assert_neq(animation_update(&a, 1, &s), ASCIICHAT_OK);
  animation_reset(&a);
  cr_assert_eq(animation_update(&a, 1, &s), ASCIICHAT_OK);
  cr_assert_eq(s.frame, 0);
  animation_destroy(&a);
}

Test(animation, independent_clocks_and_hidden_continue) {
  animation_t a, b;
  animation_sample_t sa, sb;
  animation_config_t c = config(ANIMATION_RAINBOW_FILTER);
  c.hidden_policy = ANIMATION_HIDDEN_CONTINUE;
  animation_init(&a, "test_rainbow_a", c);
  animation_init(&b, "test_rainbow_b", c);
  animation_update(&a, 0, &sa);
  animation_update(&b, 0, &sb);
  animation_set_visible(&a, false);
  animation_set_paused(&b, true);
  animation_update(&a, 1000000000, &sa);
  animation_update(&b, 1000000000, &sb);
  cr_assert_eq(sa.frame, 10);
  cr_assert_eq(sb.frame, 0);
  animation_destroy(&a);
  animation_destroy(&b);
}

Test(animation, deterministic_color_and_invalid_targets) {
  animation_sample_t s;
  rgb_pixel_t first, later;
  animation_target_t t = {.type = ANIMATION_TARGET_COLOR, .color = {.out = &first}};
  for (int type = ANIMATION_SPLASH_RAINBOW; type <= ANIMATION_RAINBOW_FILTER; type += 2) {
    animation_sample_at(config(type), 0, &s);
    cr_assert_eq(animation_apply(&s, &t), ASCIICHAT_OK);
    cr_assert_eq(first.r, 255);
    cr_assert_gt(first.r, first.g);
    t.color.out = &later;
    animation_sample_at(config(type), 1, &s);
    cr_assert_eq(animation_apply(&s, &t), ASCIICHAT_OK);
    cr_assert(memcmp(&first, &later, sizeof(first)) != 0);
    t.color.out = &first;
  }
  s.type = ANIMATION_TEST_PATTERN;
  cr_assert_neq(animation_apply(&s, &t), ASCIICHAT_OK);
  cr_assert_neq(animation_sample_at(config(ANIMATION_RAINBOW_FILTER), NAN, &s), ASCIICHAT_OK);
}

Test(animation, test_pattern_frames_match_legacy_and_change) {
  image_t *a = image_new(80, 60), *b = image_new(80, 60);
  unsigned phase = 0;
  animation_target_t t = {.type = ANIMATION_TARGET_IMAGE, .image = a};
  for (uint64_t frame = 0; frame < 4; ++frame) {
    animation_sample_t s = {.type = ANIMATION_TEST_PATTERN, .frame = frame};
    cr_assert_eq(animation_apply(&s, &t), ASCIICHAT_OK);
    if (frame)
      cr_assert(memcmp(a->pixels, b->pixels, 80 * 60 * sizeof(rgb_pixel_t)) != 0);
    image_render_test_pattern(b, &phase);
    cr_assert_eq(memcmp(a->pixels, b->pixels, 80 * 60 * sizeof(rgb_pixel_t)), 0);
  }
  image_destroy(a);
  image_destroy(b);
}

Test(animation, rain_clock_reset_and_rainbow_composition) {
  digital_rain_t *rain = digital_rain_init(4, 1);
  cr_assert_not_null(rain);
  const char *input = "\033[38;2;255;0;0mABCD\033[0m";
  char *a = digital_rain_apply(rain, input, 0.25f);
  cr_assert_not_null(a);
  cr_assert_float_eq(rain->time, .25, 1e-6);
  digital_rain_reset(rain);
  char *b = digital_rain_apply(rain, input, 0.25f);
  cr_assert_str_eq(a, b);
  animation_sample_t s;
  animation_sample_at(config(ANIMATION_RAINBOW_FILTER), 1, &s);
  char *combined = NULL;
  animation_target_t t = {.type = ANIMATION_TARGET_ANSI, .ansi = {.input = b, .out = &combined}};
  cr_assert_eq(animation_apply(&s, &t), ASCIICHAT_OK);
  cr_assert_not_null(combined);
  cr_assert_str_neq(b, combined);
  SAFE_FREE(a);
  SAFE_FREE(b);
  SAFE_FREE(combined);
  digital_rain_destroy(rain);
}

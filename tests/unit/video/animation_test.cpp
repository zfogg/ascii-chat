#include <ascii-chat/video/anim/test_pattern.h>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/video/anim/controller.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/common.h>
#include <math.h>
#include <string.h>

static animation_config_t config(animation_type_t type) {
  return (animation_config_t){.type = type, .fps = 10, .speed = 1, .hidden_policy = ANIMATION_HIDDEN_PAUSE};
}

TEST_CASE("animation/clock_pause_visibility_speed_and_reset", "[animation]") {
  animation_t a;
  animation_sample_t s;
  ASCIICHAT_REQUIRE_EQ(animation_init(&a, "test_animation", config(ANIMATION_SPLASH_RAINBOW)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(animation_update(&a, 1000000000, &s), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(s.changed);
  ASCIICHAT_REQUIRE_EQ(s.next_frame_ns, 1100000000);
  animation_update(&a, 1050000000, &s);
  ASCIICHAT_REQUIRE_NOT(s.changed);
  animation_update(&a, 1200000000, &s);
  ASCIICHAT_REQUIRE_EQ(s.frame, 2);
  animation_set_paused(&a, true);
  animation_update(&a, 2200000000, &s);
  ASCIICHAT_REQUIRE_EQ(s.frame, 2);
  ASCIICHAT_REQUIRE_EQ(s.next_frame_ns, UINT64_MAX);
  animation_set_paused(&a, false);
  animation_set_visible(&a, false);
  animation_update(&a, 3200000000, &s);
  ASCIICHAT_REQUIRE_EQ(s.frame, 2);
  animation_set_visible(&a, true);
  animation_set_speed(&a, 2);
  animation_update(&a, 3700000000, &s);
  ASCIICHAT_REQUIRE_FLOAT_EQ(s.seconds, 1.2, 1e-8);
  ASCIICHAT_REQUIRE_NE(animation_update(&a, 1, &s), ASCIICHAT_OK);
  animation_reset(&a);
  ASCIICHAT_REQUIRE_EQ(animation_update(&a, 1, &s), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(s.frame, 0);
  animation_destroy(&a);
}

TEST_CASE("animation/independent_clocks_and_hidden_continue", "[animation]") {
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
  ASCIICHAT_REQUIRE_EQ(sa.frame, 10);
  ASCIICHAT_REQUIRE_EQ(sb.frame, 0);
  animation_destroy(&a);
  animation_destroy(&b);
}

TEST_CASE("animation/deterministic_color_and_invalid_targets", "[animation]") {
  animation_sample_t s;
  rgb_pixel_t first, later;
  animation_target_t t = {.type = ANIMATION_TARGET_COLOR, .color = {.out = &first}};
  for (int type = ANIMATION_SPLASH_RAINBOW; type <= ANIMATION_RAINBOW_FILTER; type += 2) {
    animation_sample_at(config(static_cast<animation_type_t>(type)), 0, &s);
    ASCIICHAT_REQUIRE_EQ(animation_apply(&s, &t), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(first.r, 255);
    ASCIICHAT_REQUIRE_GT(first.r, first.g);
    t.color.out = &later;
    animation_sample_at(config(static_cast<animation_type_t>(type)), 1, &s);
    ASCIICHAT_REQUIRE_EQ(animation_apply(&s, &t), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(memcmp(&first, &later, sizeof(first)) != 0);
    t.color.out = &first;
  }
  s.type = ANIMATION_TEST_PATTERN;
  ASCIICHAT_REQUIRE_NE(animation_apply(&s, &t), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(animation_sample_at(config(ANIMATION_RAINBOW_FILTER), NAN, &s), ASCIICHAT_OK);
}

TEST_CASE("animation/shared_test_patterns_are_deterministic_and_animated", "[animation]") {
  test_pattern_t *pattern = NULL;
  ASCIICHAT_REQUIRE_EQ(test_pattern_create(80, 60, &pattern), ASCIICHAT_OK);
  rgb_pixel_t previous[80 * 60], repeat[80 * 60];
  animation_target_t target = {.type = ANIMATION_TARGET_TEST_PATTERN, .test_pattern = {.context = pattern}};
  for (int index = 0; index < 2; ++index) {
    target.test_pattern.index = index;
    animation_sample_t sample = {.type = ANIMATION_TEST_PATTERN};
    ASCIICHAT_REQUIRE_EQ(animation_apply(&sample, &target), ASCIICHAT_OK);
    memcpy(previous, test_pattern_image(pattern)->pixels, sizeof(previous));
    sample.seconds = 1;
    ASCIICHAT_REQUIRE_EQ(animation_apply(&sample, &target), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_NE(memcmp(previous, test_pattern_image(pattern)->pixels, sizeof(previous)), 0);
    memcpy(repeat, test_pattern_image(pattern)->pixels, sizeof(repeat));
    ASCIICHAT_REQUIRE_EQ(animation_apply(&sample, &target), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(memcmp(repeat, test_pattern_image(pattern)->pixels, sizeof(repeat)), 0);
  }
  test_pattern_destroy(pattern);
}

TEST_CASE("animation/rain_clock_reset_and_rainbow_composition", "[animation]") {
  digital_rain_t *rain = digital_rain_init(4, 1);
  ASCIICHAT_REQUIRE_NOT_NULL(rain);
  const char *input = "\033[38;2;255;0;0mABCD\033[0m";
  char *a = digital_rain_apply(rain, input, 0.25f);
  ASCIICHAT_REQUIRE_NOT_NULL(a);
  ASCIICHAT_REQUIRE_FLOAT_EQ(rain->time, .25, 1e-6);
  digital_rain_reset(rain);
  char *b = digital_rain_apply(rain, input, 0.25f);
  ASCIICHAT_REQUIRE_STR_EQ(a, b);
  animation_sample_t s;
  animation_sample_at(config(ANIMATION_RAINBOW_FILTER), 1, &s);
  char *combined = NULL;
  animation_target_t t = {.type = ANIMATION_TARGET_ANSI, .ansi = {.input = b, .out = &combined}};
  ASCIICHAT_REQUIRE_EQ(animation_apply(&s, &t), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT_NULL(combined);
  ASCIICHAT_REQUIRE_STR_NE(b, combined);
  SAFE_FREE(a);
  SAFE_FREE(b);
  SAFE_FREE(combined);
  digital_rain_destroy(rain);
}

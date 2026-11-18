#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/video/anim/test_pattern.h>
#include <ascii-chat/video/anim/controller.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/common.h>
#include <math.h>
#include <string.h>

static animation_config_t config(animation_type_t type) {
  return (animation_config_t){.type = type, .fps = 10, .speed = 1, .hidden_policy = ANIMATION_HIDDEN_PAUSE};
}

TEST_CASE("animation/clock_pause_visibility_speed_and_reset", "[animation] [unit]") {
  animation_t a;
  animation_sample_t s;
  REQUIRE((animation_init(&a, "test_animation", config(ANIMATION_SPLASH_RAINBOW))) == (ASCIICHAT_OK));
  REQUIRE((animation_update(&a, 1000000000, &s)) == (ASCIICHAT_OK));
  REQUIRE(s.changed);
  REQUIRE((s.next_frame_ns) == (1100000000));
  animation_update(&a, 1050000000, &s);
  REQUIRE(!(s.changed));
  animation_update(&a, 1200000000, &s);
  REQUIRE((s.frame) == (2));
  animation_set_paused(&a, true);
  animation_update(&a, 2200000000, &s);
  REQUIRE((s.frame) == (2));
  REQUIRE((s.next_frame_ns) == (UINT64_MAX));
  animation_set_paused(&a, false);
  animation_set_visible(&a, false);
  animation_update(&a, 3200000000, &s);
  REQUIRE((s.frame) == (2));
  animation_set_visible(&a, true);
  animation_set_speed(&a, 2);
  animation_update(&a, 3700000000, &s);
  REQUIRE((s.seconds) == Catch::Approx(1.2));
  REQUIRE((animation_update(&a, 1, &s)) != (ASCIICHAT_OK));
  animation_reset(&a);
  REQUIRE((animation_update(&a, 1, &s)) == (ASCIICHAT_OK));
  REQUIRE((s.frame) == (0));
  animation_destroy(&a);
}

TEST_CASE("animation/independent_clocks_and_hidden_continue", "[animation] [unit]") {
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
  REQUIRE((sa.frame) == (10));
  REQUIRE((sb.frame) == (0));
  animation_destroy(&a);
  animation_destroy(&b);
}

TEST_CASE("animation/deterministic_color_and_invalid_targets", "[animation] [unit]") {
  animation_sample_t s;
  rgb_pixel_t first, later;
  animation_target_t t = {.type = ANIMATION_TARGET_COLOR, .color = {.out = &first}};
  for (int type = ANIMATION_SPLASH_RAINBOW; type <= ANIMATION_RAINBOW_FILTER; type += 2) {
    animation_sample_at(config(static_cast<animation_type_t>(type)), 0, &s);
    REQUIRE((animation_apply(&s, &t)) == (ASCIICHAT_OK));
    REQUIRE((first.r) == (255));
    REQUIRE((first.r) > (first.g));
    t.color.out = &later;
    animation_sample_at(config(static_cast<animation_type_t>(type)), 1, &s);
    REQUIRE((animation_apply(&s, &t)) == (ASCIICHAT_OK));
    REQUIRE(memcmp(&first, &later, sizeof(first)) != 0);
    t.color.out = &first;
  }
  s.type = ANIMATION_TEST_PATTERN;
  REQUIRE((animation_apply(&s, &t)) != (ASCIICHAT_OK));
  REQUIRE((animation_sample_at(config(ANIMATION_RAINBOW_FILTER), NAN, &s)) != (ASCIICHAT_OK));
}

TEST_CASE("animation/shared_test_patterns_are_deterministic_and_animated", "[animation] [unit]") {
  test_pattern_t *pattern = NULL;
  REQUIRE((test_pattern_create(80, 60, &pattern)) == (ASCIICHAT_OK));
  rgb_pixel_t previous[80 * 60], repeat[80 * 60];
  animation_target_t target = {.type = ANIMATION_TARGET_TEST_PATTERN, .test_pattern = {.context = pattern}};
  for (int index = 0; index < 2; ++index) {
    target.test_pattern.index = index;
    animation_sample_t sample = {.type = ANIMATION_TEST_PATTERN};
    REQUIRE((animation_apply(&sample, &target)) == (ASCIICHAT_OK));
    memcpy(previous, test_pattern_image(pattern)->pixels, sizeof(previous));
    sample.seconds = 1;
    REQUIRE((animation_apply(&sample, &target)) == (ASCIICHAT_OK));
    REQUIRE((memcmp(previous, test_pattern_image(pattern)->pixels, sizeof(previous))) != (0));
    memcpy(repeat, test_pattern_image(pattern)->pixels, sizeof(repeat));
    REQUIRE((animation_apply(&sample, &target)) == (ASCIICHAT_OK));
    REQUIRE((memcmp(repeat, test_pattern_image(pattern)->pixels, sizeof(repeat))) == (0));
  }
  test_pattern_destroy(pattern);
}

TEST_CASE("animation/rain_clock_reset_and_rainbow_composition", "[animation] [unit]") {
  digital_rain_t *rain = digital_rain_init(4, 1);
  REQUIRE((rain) != nullptr);
  const char *input = "\033[38;2;255;0;0mABCD\033[0m";
  char *a = digital_rain_apply(rain, input, 0.25f);
  REQUIRE((a) != nullptr);
  REQUIRE((rain->time) == Catch::Approx(.25));
  digital_rain_reset(rain);
  char *b = digital_rain_apply(rain, input, 0.25f);
  REQUIRE(std::strcmp((a), (b)) == 0);
  animation_sample_t s;
  animation_sample_at(config(ANIMATION_RAINBOW_FILTER), 1, &s);
  char *combined = NULL;
  animation_target_t t = {.type = ANIMATION_TARGET_ANSI, .ansi = {.input = b, .out = &combined}};
  REQUIRE((animation_apply(&s, &t)) == (ASCIICHAT_OK));
  REQUIRE((combined) != nullptr);
  REQUIRE(std::strcmp((b), (combined)) != 0);
  SAFE_FREE(a);
  SAFE_FREE(b);
  SAFE_FREE(combined);
  digital_rain_destroy(rain);
}

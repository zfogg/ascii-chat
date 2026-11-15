#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/util/time.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdio.h>
#include <string.h>

TEST_CASE("time_util/relative_duration_thresholds_and_direction", "[time_util]") {
  const struct {
    int64_t seconds;
    const char *phrase;
  } cases[] = {
      {0, "a moment"},         {2, "a moment"},      {3, "a few seconds"}, {9, "a few seconds"},  {10, "10 seconds"},   {44, "44 seconds"},   {45, "a minute"},
      {89, "a minute"},        {120, "2 minutes"},   {2640, "44 minutes"}, {2700, "an hour"},     {5340, "an hour"},    {7200, "2 hours"},    {75600, "21 hours"},
      {79200, "a day"},        {126000, "a day"},    {172800, "2 days"},   {2073600, "24 days"},  {2160000, "a month"}, {3801600, "a month"}, {5184000, "2 months"},
      {25920000, "10 months"}, {28512000, "a year"}, {44064000, "a year"}, {62208000, "2 years"},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    int64_t ns = cases[i].seconds * (int64_t)NS_PER_SEC_INT;
    char expected[64], actual[64];
    snprintf(expected, sizeof(expected), "%s ago", cases[i].phrase);
    REQUIRE((time_human_readable_unsigned((uint64_t)ns, actual, sizeof(actual))) == ((int)strlen(expected)));
    REQUIRE(std::strcmp((actual), (expected)) == 0);
    REQUIRE((time_human_readable(ns, actual, sizeof(actual))) == ((int)strlen(expected)));
    REQUIRE(std::strcmp((actual), (expected)) == 0);
    if (ns != 0) {
      snprintf(expected, sizeof(expected), "%s from now", cases[i].phrase);
      REQUIRE((time_human_readable(-ns, actual, sizeof(actual))) == ((int)strlen(expected)));
      REQUIRE(std::strcmp((actual), (expected)) == 0);
    }
  }
}

TEST_CASE("time_util/relative_duration_requires_room_for_terminator", "[time_util]") {
  char text[13]; // "a moment ago" plus the terminator.
  REQUIRE((time_human_readable(0, text, sizeof(text))) == (12));
  REQUIRE(std::strcmp((text), ("a moment ago")) == 0);
  REQUIRE((time_human_readable(0, text, sizeof(text) - 1)) == (-1));
  REQUIRE((time_human_readable_unsigned(0, text, sizeof(text) - 1)) == (-1));
  REQUIRE((time_human_readable(0, NULL, sizeof(text))) == (-1));
  REQUIRE((time_human_readable_unsigned(0, NULL, sizeof(text))) == (-1));
  REQUIRE((time_human_readable(0, text, 0)) == (-1));
  REQUIRE((time_human_readable_unsigned(0, text, 0)) == (-1));
}

TEST_CASE("time_util/adaptive_pacing_drains_backlog_and_recovers_gradually", "[time_util]") {
  adaptive_sleep_config_t config = {1000, 1.0, 4.0, 0.5, 0.5};
  adaptive_sleep_state_t state;
  adaptive_sleep_init(&state, &config);
  config.baseline_sleep_ns = 9999; // The state owns its configuration copy.
  REQUIRE((state.last_sleep_ns) == (1000));
  REQUIRE((adaptive_sleep_calculate(&state, 4, 4)) == (1000));
  REQUIRE((adaptive_sleep_calculate(&state, 100, 4)) == (400));
  REQUIRE((state.current_speed_multiplier) == Catch::Approx(2.5));
  REQUIRE((adaptive_sleep_calculate(&state, 100, 4)) == (307));
  REQUIRE((adaptive_sleep_calculate(&state, 0, 4)) == (470));
  REQUIRE((state.current_speed_multiplier) == Catch::Approx(2.125));
  REQUIRE((state.last_sleep_ns) == (470));
  for (int i = 0; i < 64; i++) {
    uint64_t sleep = adaptive_sleep_calculate(&state, 0, 0);
    REQUIRE((sleep) >= (470));
    REQUIRE((sleep) <= (1000));
  }
  REQUIRE((state.current_speed_multiplier) == Catch::Approx(1.0));
}

TEST_CASE("time_util/adaptive_pacing_respects_ranges_excluding_normal_speed", "[time_util]") {
  adaptive_sleep_state_t state;
  adaptive_sleep_config_t fast = {1200, 2.0, 4.0, 1.0, 1.0};
  adaptive_sleep_init(&state, &fast);
  REQUIRE((state.last_sleep_ns) == (600));
  REQUIRE((adaptive_sleep_calculate(&state, 0, 0)) == (600));
  REQUIRE((adaptive_sleep_calculate(&state, 1, 0)) == (300));
  REQUIRE((adaptive_sleep_calculate(&state, 0, 0)) == (600));
  adaptive_sleep_config_t slow = {1200, 0.25, 0.5, 1.0, 1.0};
  adaptive_sleep_init(&state, &slow);
  REQUIRE((state.last_sleep_ns) == (2400));
  REQUIRE((adaptive_sleep_calculate(&state, 0, 0)) == (2400));
  REQUIRE((adaptive_sleep_calculate(NULL, 0, 0)) == (0));
  adaptive_sleep_init(NULL, &slow);
  adaptive_sleep_init(&state, NULL);
  REQUIRE((state.last_sleep_ns) == (2400));
  adaptive_sleep_do(NULL, 0, 0);
}

TEST_CASE("time_util/formatting_rejects_incomplete_specifiers_and_invalid_buffers", "[time_util]") {
  const char *invalid[] = {"%", "%-", "%04", "%.", "%.3", "%E", "%O", "%Q"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    REQUIRE(!(time_format_is_valid_strftime(invalid[i])));
  }
  REQUIRE(time_format_is_valid_strftime("%Y-%m-%d %% %H:%M:%S"));
  char text[128];
  REQUIRE((time_format_safe("literal %% timestamp", text, sizeof(text))) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((text), ("literal % timestamp")) == 0);
  REQUIRE((time_format_safe(NULL, text, sizeof(text))) == (ERROR_INVALID_STATE));
  REQUIRE((time_format_safe("%Y", NULL, sizeof(text))) == (ERROR_INVALID_STATE));
  REQUIRE((time_format_safe("%Y", text, 63)) == (ERROR_INVALID_STATE));
  REQUIRE((time_format_safe("%Q", text, sizeof(text))) == (ERROR_INVALID_STATE));
  REQUIRE((time_format_safe("", text, sizeof(text))) == (ERROR_INVALID_STATE));
  REQUIRE((time_format_now("four", text, 5)) == (0));
}

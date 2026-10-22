#include <criterion/criterion.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdio.h>
#include <string.h>

Test(time_util, relative_duration_thresholds_and_direction) {
  const struct {
    int64_t seconds;
    const char *phrase;
  } cases[] = {
      {0, "a moment"},       {2, "a moment"},         {3, "a few seconds"}, {9, "a few seconds"}, {10, "10 seconds"},
      {44, "44 seconds"},    {45, "a minute"},        {89, "a minute"},     {120, "2 minutes"},   {2640, "44 minutes"},
      {2700, "an hour"},     {5340, "an hour"},       {7200, "2 hours"},    {75600, "21 hours"},  {79200, "a day"},
      {126000, "a day"},     {172800, "2 days"},      {2073600, "24 days"}, {2160000, "a month"}, {3801600, "a month"},
      {5184000, "2 months"}, {25920000, "10 months"}, {28512000, "a year"}, {44064000, "a year"}, {62208000, "2 years"},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    int64_t ns = cases[i].seconds * (int64_t)NS_PER_SEC_INT;
    char expected[64], actual[64];
    snprintf(expected, sizeof(expected), "%s ago", cases[i].phrase);
    cr_assert_eq(time_human_readable_unsigned((uint64_t)ns, actual, sizeof(actual)), (int)strlen(expected));
    cr_assert_str_eq(actual, expected, "past duration: %lld seconds", (long long)cases[i].seconds);
    cr_assert_eq(time_human_readable(ns, actual, sizeof(actual)), (int)strlen(expected));
    cr_assert_str_eq(actual, expected);
    if (ns != 0) {
      snprintf(expected, sizeof(expected), "%s from now", cases[i].phrase);
      cr_assert_eq(time_human_readable(-ns, actual, sizeof(actual)), (int)strlen(expected));
      cr_assert_str_eq(actual, expected, "future duration: %lld seconds", (long long)cases[i].seconds);
    }
  }
}

Test(time_util, relative_duration_requires_room_for_terminator) {
  char text[13]; // "a moment ago" plus the terminator.
  cr_assert_eq(time_human_readable(0, text, sizeof(text)), 12);
  cr_assert_str_eq(text, "a moment ago");
  cr_assert_eq(time_human_readable(0, text, sizeof(text) - 1), -1);
  cr_assert_eq(time_human_readable_unsigned(0, text, sizeof(text) - 1), -1);
  cr_assert_eq(time_human_readable(0, NULL, sizeof(text)), -1);
  cr_assert_eq(time_human_readable_unsigned(0, NULL, sizeof(text)), -1);
  cr_assert_eq(time_human_readable(0, text, 0), -1);
  cr_assert_eq(time_human_readable_unsigned(0, text, 0), -1);
}

Test(time_util, adaptive_pacing_drains_backlog_and_recovers_gradually) {
  adaptive_sleep_config_t config = {1000, 1.0, 4.0, 0.5, 0.5};
  adaptive_sleep_state_t state;
  adaptive_sleep_init(&state, &config);
  config.baseline_sleep_ns = 9999; // The state owns its configuration copy.
  cr_assert_eq(state.last_sleep_ns, 1000);
  cr_assert_eq(adaptive_sleep_calculate(&state, 4, 4), 1000);
  cr_assert_eq(adaptive_sleep_calculate(&state, 100, 4), 400);
  cr_assert_float_eq(state.current_speed_multiplier, 2.5, 1e-9);
  cr_assert_eq(adaptive_sleep_calculate(&state, 100, 4), 307);
  cr_assert_eq(adaptive_sleep_calculate(&state, 0, 4), 470);
  cr_assert_float_eq(state.current_speed_multiplier, 2.125, 1e-9);
  cr_assert_eq(state.last_sleep_ns, 470);
  for (int i = 0; i < 64; i++) {
    uint64_t sleep = adaptive_sleep_calculate(&state, 0, 0);
    cr_assert_geq(sleep, 470);
    cr_assert_leq(sleep, 1000);
  }
  cr_assert_float_eq(state.current_speed_multiplier, 1.0, 1e-9);
}

Test(time_util, adaptive_pacing_respects_ranges_excluding_normal_speed) {
  adaptive_sleep_state_t state;
  adaptive_sleep_config_t fast = {1200, 2.0, 4.0, 1.0, 1.0};
  adaptive_sleep_init(&state, &fast);
  cr_assert_eq(state.last_sleep_ns, 600);
  cr_assert_eq(adaptive_sleep_calculate(&state, 0, 0), 600);
  cr_assert_eq(adaptive_sleep_calculate(&state, 1, 0), 300);
  cr_assert_eq(adaptive_sleep_calculate(&state, 0, 0), 600);
  adaptive_sleep_config_t slow = {1200, 0.25, 0.5, 1.0, 1.0};
  adaptive_sleep_init(&state, &slow);
  cr_assert_eq(state.last_sleep_ns, 2400);
  cr_assert_eq(adaptive_sleep_calculate(&state, 0, 0), 2400);
  cr_assert_eq(adaptive_sleep_calculate(NULL, 0, 0), 0);
  adaptive_sleep_init(NULL, &slow);
  adaptive_sleep_init(&state, NULL);
  cr_assert_eq(state.last_sleep_ns, 2400);
  adaptive_sleep_do(NULL, 0, 0);
}

Test(time_util, formatting_rejects_incomplete_specifiers_and_invalid_buffers) {
  const char *invalid[] = {"%", "%-", "%04", "%.", "%.3", "%E", "%O", "%Q"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    cr_assert_not(time_format_is_valid_strftime(invalid[i]), "must reject %s", invalid[i]);
  }
  cr_assert(time_format_is_valid_strftime("%Y-%m-%d %% %H:%M:%S"));
  char text[128];
  cr_assert_eq(time_format_safe("literal %% timestamp", text, sizeof(text)), ASCIICHAT_OK);
  cr_assert_str_eq(text, "literal % timestamp");
  cr_assert_eq(time_format_safe(NULL, text, sizeof(text)), ERROR_INVALID_STATE);
  cr_assert_eq(time_format_safe("%Y", NULL, sizeof(text)), ERROR_INVALID_STATE);
  cr_assert_eq(time_format_safe("%Y", text, 63), ERROR_INVALID_STATE);
  cr_assert_eq(time_format_safe("%Q", text, sizeof(text)), ERROR_INVALID_STATE);
  cr_assert_eq(time_format_safe("", text, sizeof(text)), ERROR_INVALID_STATE);
  cr_assert_eq(time_format_now("four", text, 5), 0);
}

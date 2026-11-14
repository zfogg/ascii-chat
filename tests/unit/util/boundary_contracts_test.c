#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/options/levenshtein.h>
#include <ascii-chat/network/websocket/callback_timing.h>
#include <ascii-chat/video/rgba/image.h>
#include <limits.h>

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
}
static void cleanup(void) {
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(boundary_contracts, .init = setup, .fini = cleanup);

Test(boundary_contracts, image_size_arithmetic_rejects_overflow_without_mutating_output) {
  size_t size = 777;
  cr_assert_eq(image_calc_pixel_count(0, 1, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_pixel_count(1, 0, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_pixel_count(SIZE_MAX, 2, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_pixel_count(1, 1, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_pixel_buffer_size(0, 3, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_pixel_buffer_size(1, 0, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_pixel_buffer_size(SIZE_MAX, 3, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_pixel_buffer_size(1, 3, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_rgb_size(0, 2, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_rgb_size(1, 2, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_total_allocation(0, 2, 8, 3, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_total_allocation(SIZE_MAX, 1, 8, 3, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_total_allocation(1, 1, SIZE_MAX, 1, &size), ERROR_INVALID_PARAM);
  cr_assert_eq(image_calc_total_allocation(1, 1, 8, 3, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(size, 777);
  cr_assert_eq(image_calc_total_allocation(7, 5, 16, 3, &size), ASCIICHAT_OK);
  cr_assert_eq(size, 121);
  cr_assert_eq(image_calc_rgb_size(7, 5, &size), ASCIICHAT_OK);
  cr_assert_eq(size, 105);
  cr_assert_eq(image_validate_dimensions(0, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(image_validate_dimensions(1, 0), ERROR_INVALID_PARAM);
  cr_assert_eq(image_validate_dimensions((size_t)IMAGE_MAX_WIDTH + 1, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(image_validate_dimensions(1, (size_t)IMAGE_MAX_HEIGHT + 1), ERROR_INVALID_PARAM);
  cr_assert_eq(image_validate_dimensions(IMAGE_MAX_WIDTH, IMAGE_MAX_HEIGHT), ASCIICHAT_OK);
  cr_assert_eq(image_validate_buffer_size(IMAGE_MAX_PIXELS_SIZE), ASCIICHAT_OK);
  cr_assert_eq(image_validate_buffer_size((size_t)IMAGE_MAX_PIXELS_SIZE + 1), ERROR_INVALID_PARAM);
}

Test(boundary_contracts, numeric_parsers_preserve_output_on_rejected_input) {
  const char *invalid[] = {NULL, "", "abc", "5x", "-1", "11", "99999999999999999999999999999999999"};
  for (size_t i = 0; i < ARRAY_SIZE(invalid); ++i) {
    long signed_value = 777;
    unsigned long unsigned_value = 777;
    unsigned long long wide_value = 777;
    cr_assert_eq(parse_long(invalid[i], &signed_value, 0, 10), ERROR_INVALID_PARAM);
    cr_assert_eq(parse_ulong(invalid[i], &unsigned_value, 0, 10), ERROR_INVALID_PARAM);
    cr_assert_eq(parse_ulonglong(invalid[i], &wide_value, 0, 10), ERROR_INVALID_PARAM);
    cr_assert_eq(signed_value, 777);
    cr_assert_eq(unsigned_value, 777);
    cr_assert_eq(wide_value, 777);
  }
  cr_assert_eq(parse_long("5", NULL, 0, 10), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_ulong("5", NULL, 0, 10), ERROR_INVALID_PARAM);
  cr_assert_eq(parse_ulonglong("5", NULL, 0, 10), ERROR_INVALID_PARAM);
  long signed_value;
  unsigned long unsigned_value;
  unsigned long long wide_value;
  cr_assert_eq(parse_long("-5", &signed_value, -5, 5), ASCIICHAT_OK);
  cr_assert_eq(signed_value, -5);
  cr_assert_eq(parse_ulong("5", &unsigned_value, 5, 5), ASCIICHAT_OK);
  cr_assert_eq(unsigned_value, 5);
  cr_assert_eq(parse_ulonglong("18446744073709551615", &wide_value, 0, ULLONG_MAX), ASCIICHAT_OK);
  cr_assert_eq(wide_value, ULLONG_MAX);
}

Test(boundary_contracts, wire_text_parsers_reject_invalid_dimensions_and_sample_counts) {
  unsigned int width = 77, height = 88, count = 99;
  const char *sizes[] = {NULL,
                         "OTHER:1,2",
                         "SIZE:",
                         "SIZE:0,1",
                         "SIZE:1,0",
                         "SIZE:1",
                         "SIZE:1:2",
                         "SIZE:1,2x",
                         "SIZE:65536,1",
                         "SIZE:1,65536",
                         "SIZE:4294967296,1",
                         "SIZE:1,4294967296",
                         "SIZE:9999999999999999999999,1",
                         "SIZE:1,9999999999999999999999"};
  for (size_t i = 0; i < ARRAY_SIZE(sizes); ++i) {
    cr_assert_eq(safe_parse_size_message(sizes[i], &width, &height), ERROR_INVALID_PARAM);
    cr_assert_eq(width, 77);
    cr_assert_eq(height, 88);
  }
  cr_assert_eq(safe_parse_size_message("SIZE:1,2", NULL, &height), ERROR_INVALID_PARAM);
  cr_assert_eq(safe_parse_size_message("SIZE:1,2", &width, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(safe_parse_size_message("SIZE:65535,1\n", &width, &height), ASCIICHAT_OK);
  cr_assert_eq(width, 65535);
  cr_assert_eq(height, 1);
  const char *audio[] = {
      NULL, "OTHER:1", "AUDIO:", "AUDIO:0", "AUDIO:2x", "AUDIO:4294967296", "AUDIO:9999999999999999999999999"};
  for (size_t i = 0; i < ARRAY_SIZE(audio); ++i) {
    cr_assert_eq(safe_parse_audio_message(audio[i], &count), ERROR_INVALID_PARAM);
    cr_assert_eq(count, 99);
  }
  cr_assert_eq(safe_parse_audio_message("AUDIO:1", NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(safe_parse_audio_message("AUDIO:960\n", &count), ASCIICHAT_OK);
  cr_assert_eq(count, 960);
}

Test(boundary_contracts, enum_views_are_owned_null_terminated_and_match_the_registry) {
  size_t count = 99;
  cr_assert_null(options_get_enum_entries(NULL, &count));
  cr_assert_eq(count, 0);
  cr_assert_null(options_get_enum_entries("color-mode", NULL));
  cr_assert_null(options_get_enum_values(NULL, &count));
  cr_assert_eq(count, 0);
  cr_assert_null(options_get_enum_values("color-mode", NULL));
  cr_assert_not(options_is_enum_option(NULL));
  cr_assert_not(options_is_enum_option("does-not-exist"));
  cr_assert_null(options_get_enum_entries("does-not-exist", &count));
  cr_assert_null(options_get_enum_values("does-not-exist", &count));
  cr_assert(options_is_enum_option("color-mode"));
  const enum_to_string_entry_t *entries = options_get_enum_entries("color-mode", &count);
  cr_assert_not_null(entries);
  cr_assert_gt(count, 0);
  size_t values_count;
  const char **values = options_get_enum_values("color-mode", &values_count);
  cr_assert_not_null(values);
  cr_assert_eq(values_count, count);
  for (size_t i = 0; i < count; ++i)
    cr_assert_str_eq(entries[i].string, values[i]);
  cr_assert_eq(entries[count].enum_value, -1);
  cr_assert_null(values[count]);
  SAFE_FREE(entries);
  SAFE_FREE(values);
}

Test(boundary_contracts, fuzzy_suggestions_use_unicode_characters_and_respect_thresholds) {
  cr_assert_eq(levenshtein("kitten", "sitting"), 3);
  cr_assert_eq(levenshtein("café", "cafe"), 1);
  cr_assert_eq(levenshtein("界", "界界"), 1);
  cr_assert_eq(levenshtein("", "界界"), 2);
  cr_assert_eq(levenshtein("界界", ""), 2);
  cr_assert_eq(levenshtein(NULL, "x"), SIZE_MAX);
  cr_assert_eq(levenshtein("x", NULL), SIZE_MAX);
  const char *word = "same";
  cr_assert_eq(levenshtein_n(word, 4, word, 4), 0);
  cr_assert_eq(levenshtein_n("", 0, "abc", 3), 3);
  cr_assert_eq(levenshtein_n("abc", 3, "", 0), 3);
  cr_assert_eq(levenshtein_n("kitten", 6, "sitting", 7), 3);
  const char *candidates[] = {"width", "height", "port", NULL}, *empty[] = {NULL};
  cr_assert_str_eq(levenshtein_find_similar("widht", candidates), "width");
  cr_assert_null(levenshtein_find_similar("unrelated-spelling", candidates));
  cr_assert_null(levenshtein_find_similar("x", empty));
  cr_assert_null(levenshtein_find_similar(NULL, candidates));
  cr_assert_null(levenshtein_find_similar("x", NULL));
}

Test(boundary_contracts, callback_statistics_track_intervals_and_clamp_backwards_timestamps) {
  websocket_callback_timing_reset();
  websocket_callback_timing_log_stats();
  websocket_callback_stats_t *stats[] = {&g_ws_callback_timing.protocol_init, &g_ws_callback_timing.protocol_destroy,
                                         &g_ws_callback_timing.server_writeable, &g_ws_callback_timing.receive};
  for (size_t i = 0; i < ARRAY_SIZE(stats); ++i) {
    websocket_callback_timing_record(stats[i], 90, 100);
    websocket_callback_timing_record(stats[i], 130, 150);
    websocket_callback_timing_record(stats[i], 200, 180);
    websocket_callback_timing_record(stats[i], 170, 180);
    websocket_callback_timing_record(stats[i], 80, 90);
    cr_assert_eq(atomic_load_u64(&stats[i]->count), 5);
    cr_assert_eq(atomic_load_u64(&stats[i]->total_duration_ns), 50);
    cr_assert_eq(atomic_load_u64(&stats[i]->min_interval_ns), 30);
    cr_assert_eq(atomic_load_u64(&stats[i]->max_interval_ns), 50);
    cr_assert_eq(atomic_load_u64(&stats[i]->last_ns), 90);
  }
  websocket_callback_timing_record(NULL, 0, 1);
  websocket_callback_timing_log_stats();
  websocket_callback_timing_reset();
  cr_assert_eq(atomic_load_u64(&g_ws_callback_timing.receive.count), 0);
  cr_assert_eq(atomic_load_u64(&g_ws_callback_timing.receive.min_interval_ns), UINT64_MAX);
}

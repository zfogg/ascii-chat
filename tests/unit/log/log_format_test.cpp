/**
 * @file tests/unit/log_template_test.c
 * @brief Unit tests for log format parser and time formatting utilities
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/format.h>
#include <ascii-chat/log/colorize.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>

/* ============================================================================
 * Test Setup/Teardown
 * ============================================================================ */

void setup_test_logging(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  log_set_level(LOG_FATAL);
}

void teardown_test_logging(void) {
  log_set_terminal_output(true);
  log_set_level(LOG_DEBUG);
}
TEST_CASE("log_format/nested_color_fields_respect_the_callers_policy", "[log_format]") {
  bool saved_passed = g_color_flag_passed, saved_value = g_color_flag_value;
  g_color_flag_passed = g_color_flag_value = true;
  log_template_t *format = log_template_parse("%color(INFO,%message)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(format);
  char output[512];
  ASCIICHAT_REQUIRE_GT(log_template_apply(format, output, sizeof(output), LOG_INFO, "00:00:00", "test.c", 1, "test", 1,
                                  "value 123 ms", false, 0),
               0);
  ASCIICHAT_REQUIRE_STR_EQ(output, "value 123 ms", "Plain file formatting must bypass global terminal color detection");
  ASCIICHAT_REQUIRE_GT(log_template_apply(format, output, sizeof(output), LOG_INFO, "00:00:00", "test.c", 1, "test", 1,
                                  "value 123 ms", true, 0),
               0);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(output, "\033["));
  log_template_free(format);
  g_color_flag_passed = saved_passed;
  g_color_flag_value = saved_value;
}

TEST_CASE("log_format/semantic_colorization_accepts_an_explicit_policy_without_a_tty", "[log_format]") {
  const char *message = "value 123 ms";
  ASCIICHAT_REQUIRE_EQ(colorize_log_message_with_color(message, false), message);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(colorize_log_message_with_color(message, true), "\033["));
  ASCIICHAT_REQUIRE_NULL(colorize_log_message_with_color(NULL, true));
}

TEST_CASE("log_format/explicit_monochrome_and_palette_modes_survive_caller_color_policy", "[log_format]") {
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = *options_get();
  options->color_mode = TERM_COLOR_NONE;
  ASCIICHAT_REQUIRE_EQ(options_state_set(options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(log_level_color_for_output(LOG_COLOR_INFO, true), "");
  options->color_mode = TERM_COLOR_256;
  ASCIICHAT_REQUIRE_EQ(options_state_set(options), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(log_level_color_for_output(LOG_COLOR_INFO, true), "38;5;"));
  ASCIICHAT_REQUIRE_STR_EQ(log_level_color_for_output(LOG_COLOR_INFO, false), "");
  SAFE_FREE(options);
}

/* ============================================================================
 * Time Format Validation Tests
 * ============================================================================ */

TEST_CASE("log_format/time_format_valid_basic", "[log_format]") {
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%H:%M:%S"));
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%Y-%m-%d"));
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%F %T"));
}

TEST_CASE("log_format/time_format_valid_all_specifiers", "[log_format]") {
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%Y")); /* Year 4-digit */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%m")); /* Month */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%d")); /* Day */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%H")); /* Hour 24 */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%M")); /* Minute */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%S")); /* Second */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%a")); /* Abbrev weekday */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%A")); /* Full weekday */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%b")); /* Abbrev month */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%B")); /* Full month */
}

TEST_CASE("log_format/time_format_invalid_null", "[log_format]") {
  ASCIICHAT_REQUIRE_NOT(time_format_is_valid_strftime(NULL));
}

TEST_CASE("log_format/time_format_invalid_specifier", "[log_format]") {
  ASCIICHAT_REQUIRE_NOT(time_format_is_valid_strftime("%Q")); /* Invalid */
  ASCIICHAT_REQUIRE_NOT(time_format_is_valid_strftime("%@")); /* Invalid */
}

TEST_CASE("log_format/time_format_invalid_unterminated", "[log_format]") {
  ASCIICHAT_REQUIRE_NOT(time_format_is_valid_strftime("Test %")); /* Unterminated % */
}

TEST_CASE("log_format/time_format_escaped_percent", "[log_format]") {
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%%"));    /* Escaped % */
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("100%%")); /* Percent literal */
}

TEST_CASE("log_format/time_format_with_width", "[log_format]") {
  ASCIICHAT_REQUIRE(time_format_is_valid_strftime("%10Y")); /* Width specifier */
}

/* ============================================================================
 * Time Formatting Tests
 * ============================================================================ */

TEST_CASE("log_format/time_format_now_basic", "[log_format]") {
  char buf[64] = {0};
  int len = time_format_now("%H:%M:%S", buf, sizeof(buf));
  ASCIICHAT_REQUIRE_GT(len, 0, "time_format_now should return > 0");
  /* Check format: HH:MM:SS (without automatic microseconds appending) */
  ASCIICHAT_REQUIRE(strchr(buf, ':') != NULL, "Should contain colons");
  ASCIICHAT_REQUIRE_EQ(len, 8, "Should be exactly HH:MM:SS (8 chars)");
}

TEST_CASE("log_format/time_format_now_with_microseconds", "[log_format]") {
  char buf[64] = {0};
  int len = time_format_now("%H:%M:%S", buf, sizeof(buf));
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Format: HH:MM:SS (8 chars - microseconds are now separate via %ms specifier) */
  ASCIICHAT_REQUIRE_EQ(len, 8, "Should be HH:MM:SS without automatic microseconds");
  ASCIICHAT_REQUIRE_NULL(strchr(buf, '.'), "Should NOT have decimal point (use %ms specifier instead)");
}

TEST_CASE("log_format/time_format_now_date", "[log_format]") {
  char buf[64] = {0};
  int len = time_format_now("%Y-%m-%d", buf, sizeof(buf));
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Check format: YYYY-MM-DD */
  ASCIICHAT_REQUIRE_EQ(strlen(buf), 10, "Date should be YYYY-MM-DD");
  ASCIICHAT_REQUIRE_EQ(buf[4], '-', "Year should be followed by dash");
  ASCIICHAT_REQUIRE_EQ(buf[7], '-', "Month should be followed by dash");
}

TEST_CASE("log_format/time_format_now_buffer_small", "[log_format]") {
  char buf[4] = {0};
  int len = time_format_now("%Y-%m-%d", buf, sizeof(buf));
  ASCIICHAT_REQUIRE_EQ(len, 0, "Should fail with small buffer");
}

TEST_CASE("log_format/time_format_now_null_format", "[log_format]") {
  char buf[64] = {0};
  int len = time_format_now(NULL, buf, sizeof(buf));
  ASCIICHAT_REQUIRE_EQ(len, 0, "Should fail with NULL format");
}

TEST_CASE("log_format/time_format_now_null_buf", "[log_format]") {
  int len = time_format_now("%H:%M:%S", NULL, 64);
  ASCIICHAT_REQUIRE_EQ(len, 0, "Should fail with NULL buffer");
}

TEST_CASE("log_format/time_format_safe_valid", "[log_format]") {
  char buf[64] = {0};
  asciichat_error_t err = time_format_safe("%H:%M:%S", buf, sizeof(buf));
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(strchr(buf, ':') != NULL, "Should be formatted time with colons");
  ASCIICHAT_REQUIRE_NULL(strchr(buf, '.'), "Should NOT have decimal point (use %ms for microseconds)");
}

TEST_CASE("log_format/time_format_safe_invalid_format", "[log_format]") {
  char buf[64] = {0};
  asciichat_error_t err = time_format_safe("%Q", buf, sizeof(buf));
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Should fail for invalid specifier");
}

TEST_CASE("log_format/time_format_safe_buffer_too_small", "[log_format]") {
  char buf[10] = {0};
  asciichat_error_t err = time_format_safe("%Y-%m-%d", buf, sizeof(buf));
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Should fail for buffer < 64 bytes");
}

/* ============================================================================
 * Format Parser - Basic Parsing Tests
 * ============================================================================ */

TEST_CASE("log_format/parse_literal_only", "[log_format]") {
  log_template_t *fmt = log_template_parse("Hello World", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->spec_count, 1);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  ASCIICHAT_REQUIRE_STR_EQ(fmt->specs[0].literal, "Hello World");
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_level_specifier", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->spec_count, 3); /* [ + level + ] */
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[1].type, LOG_FORMAT_LEVEL);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[2].type, LOG_FORMAT_LITERAL);
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_level_aligned", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level_aligned]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[1].type, LOG_FORMAT_LEVEL_ALIGNED);
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_time_specifier", "[log_format]") {
  log_template_t *fmt = log_template_parse("%time(%H:%M:%S)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->spec_count, 1);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_TIME);
  ASCIICHAT_REQUIRE_STR_EQ(fmt->specs[0].literal, "%H:%M:%S");
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_file_line_func", "[log_format]") {
  log_template_t *fmt = log_template_parse("%file:%line in %func()", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_FILE);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[2].type, LOG_FORMAT_LINE);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[4].type, LOG_FORMAT_FUNC);
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_message", "[log_format]") {
  log_template_t *fmt = log_template_parse("%message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->spec_count, 1);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_MESSAGE);
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_tid", "[log_format]") {
  log_template_t *fmt = log_template_parse("[tid:%tid]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[1].type, LOG_FORMAT_TID);
  log_template_free(fmt);
}

/* ============================================================================
 * Format Parser - Escaping Tests
 * ============================================================================ */

TEST_CASE("log_format/parse_escaped_percent", "[log_format]") {
  log_template_t *fmt = log_template_parse("100%%", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->spec_count, 2); /* "100" + "%" */
  ASCIICHAT_REQUIRE_EQ(fmt->specs[1].type, LOG_FORMAT_LITERAL);
  ASCIICHAT_REQUIRE_STR_EQ(fmt->specs[1].literal, "%");
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_escaped_backslash", "[log_format]") {
  log_template_t *fmt = log_template_parse("path\\\\file", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  /* Should have: "path" + "\" + "file" */
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  ASCIICHAT_REQUIRE_STR_EQ(fmt->specs[0].literal, "path");
  ASCIICHAT_REQUIRE_EQ(fmt->specs[1].type, LOG_FORMAT_LITERAL);
  ASCIICHAT_REQUIRE_STR_EQ(fmt->specs[1].literal, "\\");
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_newline", "[log_format]") {
  log_template_t *fmt = log_template_parse("line1\\nline2", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[1].type, LOG_FORMAT_NEWLINE);
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_complex_with_escapes", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level] %message\\n(100%% complete)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_GT(fmt->spec_count, 3);
  log_template_free(fmt);
}

/* ============================================================================
 * Format Parser - UTF-8 Support Tests
 * ============================================================================ */

TEST_CASE("log_format/parse_utf8_literals", "[log_format]") {
  /* UTF-8 format string with non-ASCII characters */
  log_template_t *fmt = log_template_parse("[时间:%time(%H:%M:%S)] [%level] %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_GT(fmt->spec_count, 0);
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_invalid_utf8", "[log_format]") {
  /* Invalid UTF-8 sequence */
  const unsigned char invalid_utf8[] = "[%level] \xFF\xFE message";
  log_template_t *fmt = log_template_parse((const char *)invalid_utf8, false);
  ASCIICHAT_REQUIRE_NULL(fmt, "Should reject invalid UTF-8");
}

/* ============================================================================
 * Format Parser - Error Cases
 * ============================================================================ */

TEST_CASE("log_format/parse_null_format", "[log_format]") {
  log_template_t *fmt = log_template_parse(NULL, false);
  ASCIICHAT_REQUIRE_NULL(fmt);
}

TEST_CASE("log_format/parse_unknown_specifier", "[log_format]") {
  /* Unknown specifiers are now treated as strftime format codes
     and validation is deferred to strftime at format time */
  log_template_t *fmt = log_template_parse("%unknown", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt, "Should accept unknown specifier as strftime code");
  ASCIICHAT_REQUIRE_EQ(fmt->spec_count, 1);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_STRFTIME_CODE);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_strftime_codes", "[log_format]") {
  /* Test that strftime codes work end-to-end */
  log_template_t *fmt = log_template_parse("[%H:%M:%S] %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt, "Should parse strftime codes");

  char buf[256];
  uint64_t now = time_get_realtime_ns();
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "", NULL, 0, NULL, 0, "test", false, now);

  ASCIICHAT_REQUIRE_GT(len, 0, "Should format successfully with strftime codes");
  ASCIICHAT_REQUIRE(strstr(buf, "test") != NULL, "Should contain message");
  /* Should contain formatted time with colons like HH:MM:SS */
  ASCIICHAT_REQUIRE(strchr(buf, ':') != NULL, "Should contain time separator");

  log_template_free(fmt);
}

TEST_CASE("log_format/parse_unterminated_time_format", "[log_format]") {
  log_template_t *fmt = log_template_parse("%time(%H:%M:%S", false);
  ASCIICHAT_REQUIRE_NULL(fmt, "Should reject unterminated time format");
}

TEST_CASE("log_format/parse_console_only_flag", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level] %message", true);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->console_only, true);
  log_template_free(fmt);
}

/* ============================================================================
 * Format Application Tests
 * ============================================================================ */

TEST_CASE("log_format/apply_literal_only", "[log_format]") {
  log_template_t *fmt = log_template_parse("Static text", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "Static text");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_level", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "[INFO]");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_level_aligned", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level_aligned]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  log_template_apply(fmt, buf, sizeof(buf), LOG_WARN, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                     45296123456000);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "[WARN ]");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_file_and_line", "[log_format]") {
  log_template_t *fmt = log_template_parse("%file:%line", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "test.c:42");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_func", "[log_format]") {
  log_template_t *fmt = log_template_parse("in %func()", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "in main()");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_tid", "[log_format]") {
  log_template_t *fmt = log_template_parse("tid:%tid", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 5678, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "tid:5678");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_message", "[log_format]") {
  log_template_t *fmt = log_template_parse("Message: %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "Hello", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "Message: Hello");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_complex_format", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level_aligned] %file:%line - %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_ERROR, "12:34:56", "error.c", 99, "error_func", 1234,
                               "Critical error", false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "[ERROR] error.c:99 - Critical error");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_with_utf8_message", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                               "Processing café", false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "[INFO] Processing café");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_null_optionals", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%file] [%func]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", NULL, 0, NULL, 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "[] []"); /* Empty optional fields */
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_buffer_overflow", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[10] = {0}; /* Too small */
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                               "Very long message", false, 45296123456000);
  ASCIICHAT_REQUIRE_EQ(len, -1, "Should fail on buffer overflow");
  log_template_free(fmt);
}

/* ============================================================================
 * File Relative Path Tests
 * ============================================================================ */

TEST_CASE("log_format/parse_file_relative", "[log_format]") {
  log_template_t *fmt = log_template_parse("%file_relative", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->spec_count, 1);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_FILE_RELATIVE);
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_file_relative_in_context", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%file_relative:%line]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_LITERAL); /* [ */
  ASCIICHAT_REQUIRE_EQ(fmt->specs[1].type, LOG_FORMAT_FILE_RELATIVE);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[3].type, LOG_FORMAT_LINE);
  log_template_free(fmt);
}

TEST_CASE("log_format/parse_file_relative_before_file", "[log_format]") {
  /* %file_relative should be checked before %file since it's longer */
  log_template_t *fmt = log_template_parse("%file_relative:%file", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[0].type, LOG_FORMAT_FILE_RELATIVE);
  ASCIICHAT_REQUIRE_EQ(fmt->specs[2].type, LOG_FORMAT_FILE);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_file_relative", "[log_format]") {
  log_template_t *fmt = log_template_parse("%file_relative:%line", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "src/main.c", 42, "main", 1234, "msg",
                               false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Should contain the relative path (extracted by extract_project_relative_path) */
  ASCIICHAT_REQUIRE(strchr(buf, ':') != NULL, "Should contain colon separator");
  log_template_free(fmt);
}

/* ============================================================================
 * Strftime Format Tests - Various Time Formats
 * ============================================================================ */

TEST_CASE("log_format/apply_time_format_iso8601", "[log_format]") {
  log_template_t *fmt = log_template_parse("%time(%Y-%m-%d %H:%M:%S)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Should have format like: YYYY-MM-DD HH:MM:SS (no automatic microseconds) */
  ASCIICHAT_REQUIRE(strchr(buf, '-') != NULL && strchr(buf, ' ') != NULL);
  ASCIICHAT_REQUIRE_NULL(strchr(buf, '.'), "Should NOT have decimal point (use %ms for microseconds)");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_time_format_with_weekday", "[log_format]") {
  log_template_t *fmt = log_template_parse("%time(%A, %B %d, %Y)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Should contain comma and have reasonable length for full weekday + month name */
  ASCIICHAT_REQUIRE_GT(strlen(buf), 15);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_time_format_short_date", "[log_format]") {
  log_template_t *fmt = log_template_parse("%time(%a %b %d %H:%M)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Should be shorter than full format */
  ASCIICHAT_REQUIRE_LT(strlen(buf), 30);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_time_format_with_percent", "[log_format]") {
  log_template_t *fmt = log_template_parse("%time(%%Y-%%m-%%d)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Should contain percent signs from escaped %% */
  ASCIICHAT_REQUIRE(strchr(buf, '%') != NULL);
  log_template_free(fmt);
}

/* ============================================================================
 * Newline and Multi-Line Output Tests
 * ============================================================================ */

TEST_CASE("log_format/apply_newline_in_format", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level]\\n%message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "Hello", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE(strchr(buf, '\n') != NULL, "Should contain newline");
  ASCIICHAT_REQUIRE_STR_EQ(buf, "[INFO]\nHello");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_multiple_newlines", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level]\\n%file:%line\\n%message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[512] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_WARN, "12:34:56", "src.c", 10, "main", 1234, "Test", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Count newlines */
  int newline_count = 0;
  for (const char *p = buf; *p; p++) {
    if (*p == '\n')
      newline_count++;
  }
  ASCIICHAT_REQUIRE_EQ(newline_count, 2);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_message_first_then_newline_then_header", "[log_format]") {
  log_template_t *fmt = log_template_parse("%message\\n[%level_aligned] %file:%line in %func", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[512] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_ERROR, "12:34:56", "error.c", 99, "process", 1234,
                               "Error occurred", false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE(strchr(buf, '\n') != NULL);

  /* First part should be the message */
  ASCIICHAT_REQUIRE_EQ(strncmp(buf, "Error occurred", 14), 0, "Message should come first");
  /* After newline should be the header */
  const char *after_newline = strchr(buf, '\n') + 1;
  ASCIICHAT_REQUIRE(strstr(after_newline, "ERROR") != NULL);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_newline_at_end", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level] %message\\n", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_EQ(buf[strlen(buf) - 1], '\n', "Should end with newline");
  log_template_free(fmt);
}

/* ============================================================================
 * Different Specifier Order Tests
 * ============================================================================ */

TEST_CASE("log_format/apply_specifiers_reverse_order", "[log_format]") {
  log_template_t *fmt = log_template_parse("%message - %func() at %file:%line", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "app.c", 50, "process", 1234, "Starting",
                               false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Message should come first */
  ASCIICHAT_REQUIRE(strncmp(buf, "Starting", 8) == 0);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_message_in_middle", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%time(%H:%M:%S)] %message [%level_aligned]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_WARN, "14:30:00", "test.c", 42, "main", 1234, "Warning!",
                               false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Time should come first, message in middle, level at end */
  const char *msg_pos = strstr(buf, "Warning!");
  const char *level_pos = strstr(buf, "WARN");
  ASCIICHAT_REQUIRE(msg_pos < level_pos, "Message should come before level");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_duplicate_specifiers", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level] [%level] %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_FATAL, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Should have FATAL twice */
  int count = 0;
  for (const char *p = buf; (p = strstr(p, "FATAL")) != NULL; p++, count++)
    ;
  ASCIICHAT_REQUIRE_EQ(count, 2);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_all_specifiers_together", "[log_format]") {
  log_template_t *fmt = log_template_parse(
      "[%time(%H:%M:%S)] [%level_aligned] [%file_relative:%line] {%func} <tid:%tid> %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[512] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "lib/core.c", 42, "initialize", 5678,
                               "Initializing system", false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* All parts should be present */
  ASCIICHAT_REQUIRE(strchr(buf, '[') != NULL);
  ASCIICHAT_REQUIRE(strchr(buf, '{') != NULL);
  ASCIICHAT_REQUIRE(strchr(buf, '<') != NULL);
  ASCIICHAT_REQUIRE(strstr(buf, "Initializing system") != NULL);
  log_template_free(fmt);
}

/* ============================================================================
 * Edge Cases and Robustness Tests
 * ============================================================================ */

TEST_CASE("log_format/apply_empty_message", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE_STR_EQ(buf, "[INFO] ");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_very_long_message", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char long_msg[500];
  memset(long_msg, 'A', sizeof(long_msg) - 1);
  long_msg[sizeof(long_msg) - 1] = '\0';

  char buf[1024] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, long_msg, false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE(strstr(buf, long_msg) != NULL);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_special_characters_in_message", "[log_format]") {
  log_template_t *fmt = log_template_parse("%message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                               "Test %% %% backslash \\ newline \\n", false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Message should be copied as-is (these are in the message, not format specifiers) */
  ASCIICHAT_REQUIRE_STR_EQ(buf, "Test %% %% backslash \\ newline \\n");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_large_thread_id", "[log_format]") {
  log_template_t *fmt = log_template_parse("tid=%tid", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  uint64_t large_tid = 18446744073709551615ULL; /* Max uint64_t */
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", large_tid, "msg",
                               false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  ASCIICHAT_REQUIRE(strstr(buf, "tid=") != NULL);
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_all_log_levels", "[log_format]") {
  log_template_t *fmt = log_template_parse("[%level_aligned]", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  const log_level_t levels[] = {LOG_DEV, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL};
  const char *expected[] = {"[DEV  ]", "[DEBUG]", "[INFO ]", "[WARN ]", "[ERROR]", "[FATAL]"};

  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    char buf[256] = {0};
    int len = log_template_apply(fmt, buf, sizeof(buf), levels[i], "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                                 45296123456000);
    ASCIICHAT_REQUIRE_GT(len, 0);
    ASCIICHAT_REQUIRE_STR_EQ(buf, expected[i], "Level %lu should format correctly", i);
  }

  log_template_free(fmt);
}

TEST_CASE("log_format/apply_zero_line_number", "[log_format]") {
  log_template_t *fmt = log_template_parse("line=%line", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 0, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Zero line should not be printed (line > 0 check in format.c) */
  ASCIICHAT_REQUIRE_STR_EQ(buf, "line=");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_negative_line_number", "[log_format]") {
  log_template_t *fmt = log_template_parse("line=%line", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", -1, "main", 1234, "msg", false,
                               45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);
  /* Negative line should not be printed (line > 0 check) */
  ASCIICHAT_REQUIRE_STR_EQ(buf, "line=");
  log_template_free(fmt);
}

/* ============================================================================
 * Format Consistency Tests
 * ============================================================================ */

TEST_CASE("log_format/apply_consistent_output", "[log_format]") {
  /* Use format without %time since time changes between calls */
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf1[256] = {0};
  char buf2[256] = {0};

  int len1 = log_template_apply(fmt, buf1, sizeof(buf1), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                                45296123456000);
  int len2 = log_template_apply(fmt, buf2, sizeof(buf2), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                                45296123456000);

  ASCIICHAT_REQUIRE_EQ(len1, len2, "Same inputs should produce same length output");
  ASCIICHAT_REQUIRE_STR_EQ(buf1, buf2, "Same inputs should produce identical output");
  log_template_free(fmt);
}

TEST_CASE("log_format/apply_time_consistency_multiple_calls", "[log_format]") {
  /* Test that time formatting works consistently across multiple calls */
  log_template_t *fmt = log_template_parse("%time(%Y-%m-%d)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf1[256] = {0};
  char buf2[256] = {0};

  int len1 = log_template_apply(fmt, buf1, sizeof(buf1), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                                45296123456000);
  int len2 = log_template_apply(fmt, buf2, sizeof(buf2), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                                45296123456000);

  ASCIICHAT_REQUIRE_GT(len1, 0);
  ASCIICHAT_REQUIRE_GT(len2, 0);
  /* Both should have the date format (YYYY-MM-DD) = 10 chars */
  ASCIICHAT_REQUIRE_EQ(len1, 10, "Date format should be exactly 10 chars");
  ASCIICHAT_REQUIRE_EQ(len2, 10, "Date format should be exactly 10 chars");
  /* Both should have same date (not guaranteed to be identical if test spans midnight, but very unlikely) */
  ASCIICHAT_REQUIRE_STR_EQ(buf1, buf2, "Date should be same on consecutive calls");
  log_template_free(fmt);
}

/* ============================================================================
 * Color Format Tests (Validate ANSI codes are actually present)
 * ============================================================================ */

TEST_CASE("log_format/color_with_use_colors_true", "[log_format]") {
  /* Test that %color() produces ANSI codes with correct color from enum when use_colors=true */
  log_template_t *fmt = log_template_parse("%color(INFO, %message)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  /* Force enable colors for testing */
  bool saved_passed = g_color_flag_passed;
  bool saved_value = g_color_flag_value;
  g_color_flag_passed = true;
  g_color_flag_value = true;

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "test message",
                               true, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0, "Should produce output with colors");

  /* Get the actual INFO color code from the enum */
  const char *info_color = log_level_color(LOG_COLOR_INFO);
  const char *reset_code = log_level_color(LOG_COLOR_RESET);

  /* Should contain the specific INFO color code */
  if (info_color && strlen(info_color) > 0) {
    ASCIICHAT_REQUIRE(strstr(buf, info_color) != NULL, "Should contain INFO color code from enum in colored output");
  }
  /* Should contain the message text */
  ASCIICHAT_REQUIRE(strstr(buf, "test message") != NULL, "Should contain message text");
  /* Should contain reset code */
  if (reset_code && strlen(reset_code) > 0) {
    ASCIICHAT_REQUIRE(strstr(buf, reset_code) != NULL, "Should contain reset code from enum");
  }

  /* Restore saved state */
  g_color_flag_passed = saved_passed;
  g_color_flag_value = saved_value;
  log_template_free(fmt);
}

TEST_CASE("log_format/color_with_use_colors_false", "[log_format]") {
  /* Test that %color() produces plain text when use_colors=false */
  log_template_t *fmt = log_template_parse("%color(INFO, %message)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "test message",
                               false, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0);

  /* Get the INFO color code - should NOT appear when use_colors=false */
  const char *info_color = log_level_color(LOG_COLOR_INFO);

  /* When use_colors=false, INFO color code should NOT be in the output */
  if (info_color && strlen(info_color) > 0) {
    ASCIICHAT_REQUIRE(strstr(buf, info_color) == NULL, "Should NOT contain color codes when use_colors=false");
  }
  /* Should contain the message text */
  ASCIICHAT_REQUIRE(strstr(buf, "test message") != NULL, "Should contain message text");
  log_template_free(fmt);
}

TEST_CASE("log_format/colored_message_with_colorize", "[log_format]") {
  /* Test that %colored_message applies colorize_log_message() rendering
   * Note: In test environment, colorize_log_message might not apply colors due to TTY detection,
   * but we still verify the format specifier is correctly rendered */
  log_template_t *fmt = log_template_parse("%colored_message", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  char buf[512] = {0};
  /* Use message with numbers and hex that would be colorized in a TTY */
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                               "Buffer size: 256 bytes (0x100)", true, 45296123456000);
  ASCIICHAT_REQUIRE_GT(len, 0, "Should render colored_message format");

  /* Should contain the message text */
  ASCIICHAT_REQUIRE(strstr(buf, "Buffer size") != NULL, "Should contain message text");
  ASCIICHAT_REQUIRE(strstr(buf, "256") != NULL, "Should contain number in message");
  ASCIICHAT_REQUIRE(strstr(buf, "0x100") != NULL, "Should contain hex value");

  log_template_free(fmt);
}

TEST_CASE("log_format/color_different_levels", "[log_format]") {
  /* Test that %color(*,...) uses different color codes for different log levels */
  log_template_t *fmt = log_template_parse("%color(*, %message)", false);
  ASCIICHAT_REQUIRE_NOT_NULL(fmt);

  /* Force enable colors for testing */
  bool saved_passed = g_color_flag_passed;
  bool saved_value = g_color_flag_value;
  g_color_flag_passed = true;
  g_color_flag_value = true;

  char buf_info[256] = {0};
  char buf_error[256] = {0};

  log_template_apply(fmt, buf_info, sizeof(buf_info), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "message", true,
                     45296123456000);
  log_template_apply(fmt, buf_error, sizeof(buf_error), LOG_ERROR, "12:34:56", "test.c", 42, "main", 1234, "message",
                     true, 45296123456000);

  /* Get the actual color codes from the enum */
  const char *info_color = log_level_color(LOG_COLOR_INFO);
  const char *error_color = log_level_color(LOG_COLOR_ERROR);

  /* INFO message should contain INFO color code */
  if (info_color && strlen(info_color) > 0) {
    ASCIICHAT_REQUIRE(strstr(buf_info, info_color) != NULL, "INFO message should have INFO color from enum");
  }
  /* ERROR message should contain ERROR color code */
  if (error_color && strlen(error_color) > 0) {
    ASCIICHAT_REQUIRE(strstr(buf_error, error_color) != NULL, "ERROR message should have ERROR color from enum");
  }
  /* Verify they actually use different colors if the codes are different */
  if (info_color && error_color && strcmp(info_color, error_color) != 0) {
    ASCIICHAT_REQUIRE(strstr(buf_info, error_color) == NULL, "INFO message should NOT have ERROR color");
    ASCIICHAT_REQUIRE(strstr(buf_error, info_color) == NULL, "ERROR message should NOT have INFO color");
  }

  /* Restore saved state */
  g_color_flag_passed = saved_passed;
  g_color_flag_value = saved_value;
  log_template_free(fmt);
}

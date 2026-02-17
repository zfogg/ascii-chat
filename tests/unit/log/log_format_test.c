/**
 * @file tests/unit/log_format_test.c
 * @brief Unit tests for log format parser and time formatting utilities
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/log/format.h>
#include <ascii-chat/util/time.h>

/* ============================================================================
 * Test Setup/Teardown
 * ============================================================================ */

void setup_test_logging(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  log_set_level(LOG_FATAL);
  test_logging_disable(true, true);
}

void teardown_test_logging(void) {
  log_set_terminal_output(true);
  test_logging_restore();
  log_set_level(LOG_DEBUG);
}

TestSuite(log_format, .init = setup_test_logging, .fini = teardown_test_logging);

/* ============================================================================
 * Time Format Validation Tests
 * ============================================================================ */

Test(log_format, time_format_valid_basic) {
  cr_assert(time_format_is_valid_strftime("%H:%M:%S"));
  cr_assert(time_format_is_valid_strftime("%Y-%m-%d"));
  cr_assert(time_format_is_valid_strftime("%F %T"));
}

Test(log_format, time_format_valid_all_specifiers) {
  cr_assert(time_format_is_valid_strftime("%Y")); /* Year 4-digit */
  cr_assert(time_format_is_valid_strftime("%m")); /* Month */
  cr_assert(time_format_is_valid_strftime("%d")); /* Day */
  cr_assert(time_format_is_valid_strftime("%H")); /* Hour 24 */
  cr_assert(time_format_is_valid_strftime("%M")); /* Minute */
  cr_assert(time_format_is_valid_strftime("%S")); /* Second */
  cr_assert(time_format_is_valid_strftime("%a")); /* Abbrev weekday */
  cr_assert(time_format_is_valid_strftime("%A")); /* Full weekday */
  cr_assert(time_format_is_valid_strftime("%b")); /* Abbrev month */
  cr_assert(time_format_is_valid_strftime("%B")); /* Full month */
}

Test(log_format, time_format_invalid_null) {
  cr_assert_not(time_format_is_valid_strftime(NULL));
}

Test(log_format, time_format_invalid_specifier) {
  cr_assert_not(time_format_is_valid_strftime("%Q")); /* Invalid */
  cr_assert_not(time_format_is_valid_strftime("%@")); /* Invalid */
}

Test(log_format, time_format_invalid_unterminated) {
  cr_assert_not(time_format_is_valid_strftime("Test %")); /* Unterminated % */
}

Test(log_format, time_format_escaped_percent) {
  cr_assert(time_format_is_valid_strftime("%%"));    /* Escaped % */
  cr_assert(time_format_is_valid_strftime("100%%")); /* Percent literal */
}

Test(log_format, time_format_with_width) {
  cr_assert(time_format_is_valid_strftime("%10Y")); /* Width specifier */
}

/* ============================================================================
 * Time Formatting Tests
 * ============================================================================ */

Test(log_format, time_format_now_basic) {
  char buf[64] = {0};
  int len = time_format_now("%H:%M:%S", buf, sizeof(buf));
  cr_assert_gt(len, 0, "time_format_now should return > 0");
  /* Check format: HH:MM:SS.NNNNNN */
  cr_assert(strchr(buf, ':') != NULL, "Should contain colons");
  cr_assert(strchr(buf, '.') != NULL, "Should contain decimal point for microseconds");
}

Test(log_format, time_format_now_with_microseconds) {
  char buf[64] = {0};
  int len = time_format_now("%H:%M:%S", buf, sizeof(buf));
  cr_assert_gt(len, 0);
  /* Format: HH:MM:SS.NNNNNN (15 chars total: 8 + 7) */
  cr_assert_eq(len, 15, "Should include microseconds appended to timestamp");
  const char *dot = strchr(buf, '.');
  cr_assert_not_null(dot, "Should have decimal point");
  cr_assert_eq(strlen(dot), 7, "Should have exactly .NNNNNN"); /* .NNNNNN = 7 chars */
}

Test(log_format, time_format_now_date) {
  char buf[64] = {0};
  int len = time_format_now("%Y-%m-%d", buf, sizeof(buf));
  cr_assert_gt(len, 0);
  /* Check format: YYYY-MM-DD */
  cr_assert_eq(strlen(buf), 10, "Date should be YYYY-MM-DD");
  cr_assert_eq(buf[4], '-', "Year should be followed by dash");
  cr_assert_eq(buf[7], '-', "Month should be followed by dash");
}

Test(log_format, time_format_now_buffer_small) {
  char buf[4] = {0};
  int len = time_format_now("%Y-%m-%d", buf, sizeof(buf));
  cr_assert_eq(len, 0, "Should fail with small buffer");
}

Test(log_format, time_format_now_null_format) {
  char buf[64] = {0};
  int len = time_format_now(NULL, buf, sizeof(buf));
  cr_assert_eq(len, 0, "Should fail with NULL format");
}

Test(log_format, time_format_now_null_buf) {
  int len = time_format_now("%H:%M:%S", NULL, 64);
  cr_assert_eq(len, 0, "Should fail with NULL buffer");
}

Test(log_format, time_format_safe_valid) {
  char buf[64] = {0};
  asciichat_error_t err = time_format_safe("%H:%M:%S", buf, sizeof(buf));
  cr_assert_eq(err, ASCIICHAT_OK);
  cr_assert(strchr(buf, ':') != NULL && strchr(buf, '.') != NULL, "Should be formatted time");
}

Test(log_format, time_format_safe_invalid_format) {
  char buf[64] = {0};
  asciichat_error_t err = time_format_safe("%Q", buf, sizeof(buf));
  cr_assert_neq(err, ASCIICHAT_OK, "Should fail for invalid specifier");
}

Test(log_format, time_format_safe_buffer_too_small) {
  char buf[10] = {0};
  asciichat_error_t err = time_format_safe("%Y-%m-%d", buf, sizeof(buf));
  cr_assert_neq(err, ASCIICHAT_OK, "Should fail for buffer < 64 bytes");
}

/* ============================================================================
 * Format Parser - Basic Parsing Tests
 * ============================================================================ */

Test(log_format, parse_literal_only) {
  log_format_t *fmt = log_format_parse("Hello World", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[0].literal, "Hello World");
  log_format_free(fmt);
}

Test(log_format, parse_level_specifier) {
  log_format_t *fmt = log_format_parse("[%level]", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 3); /* [ + level + ] */
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LEVEL);
  cr_assert_eq(fmt->specs[2].type, LOG_FORMAT_LITERAL);
  log_format_free(fmt);
}

Test(log_format, parse_level_aligned) {
  log_format_t *fmt = log_format_parse("[%level_aligned]", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LEVEL_ALIGNED);
  log_format_free(fmt);
}

Test(log_format, parse_time_specifier) {
  log_format_t *fmt = log_format_parse("%time(%H:%M:%S)", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_TIME);
  cr_assert_str_eq(fmt->specs[0].literal, "%H:%M:%S");
  log_format_free(fmt);
}

Test(log_format, parse_file_line_func) {
  log_format_t *fmt = log_format_parse("%file:%line in %func()", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_FILE);
  cr_assert_eq(fmt->specs[2].type, LOG_FORMAT_LINE);
  cr_assert_eq(fmt->specs[4].type, LOG_FORMAT_FUNC);
  log_format_free(fmt);
}

Test(log_format, parse_message) {
  log_format_t *fmt = log_format_parse("%message", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_MESSAGE);
  log_format_free(fmt);
}

Test(log_format, parse_tid) {
  log_format_t *fmt = log_format_parse("[tid:%tid]", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_TID);
  log_format_free(fmt);
}

/* ============================================================================
 * Format Parser - Escaping Tests
 * ============================================================================ */

Test(log_format, parse_escaped_percent) {
  log_format_t *fmt = log_format_parse("100%%", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 2); /* "100" + "%" */
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[1].literal, "%");
  log_format_free(fmt);
}

Test(log_format, parse_escaped_backslash) {
  log_format_t *fmt = log_format_parse("path\\\\file", false);
  cr_assert_not_null(fmt);
  /* Should have: "path" + "\" + "file" */
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[0].literal, "path");
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[1].literal, "\\");
  log_format_free(fmt);
}

Test(log_format, parse_newline) {
  log_format_t *fmt = log_format_parse("line1\\nline2", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_NEWLINE);
  log_format_free(fmt);
}

Test(log_format, parse_complex_with_escapes) {
  log_format_t *fmt = log_format_parse("[%level] %message\\n(100%% complete)", false);
  cr_assert_not_null(fmt);
  cr_assert_gt(fmt->spec_count, 3);
  log_format_free(fmt);
}

/* ============================================================================
 * Format Parser - UTF-8 Support Tests
 * ============================================================================ */

Test(log_format, parse_utf8_literals) {
  /* UTF-8 format string with non-ASCII characters */
  log_format_t *fmt = log_format_parse("[时间:%time(%H:%M:%S)] [%level] %message", false);
  cr_assert_not_null(fmt);
  cr_assert_gt(fmt->spec_count, 0);
  log_format_free(fmt);
}

Test(log_format, parse_invalid_utf8) {
  /* Invalid UTF-8 sequence */
  const unsigned char invalid_utf8[] = "[%level] \xFF\xFE message";
  log_format_t *fmt = log_format_parse((const char *)invalid_utf8, false);
  cr_assert_null(fmt, "Should reject invalid UTF-8");
}

/* ============================================================================
 * Format Parser - Error Cases
 * ============================================================================ */

Test(log_format, parse_null_format) {
  log_format_t *fmt = log_format_parse(NULL, false);
  cr_assert_null(fmt);
}

Test(log_format, parse_unknown_specifier) {
  log_format_t *fmt = log_format_parse("%unknown", false);
  cr_assert_null(fmt, "Should reject unknown specifier");
}

Test(log_format, parse_unterminated_time_format) {
  log_format_t *fmt = log_format_parse("%time(%H:%M:%S", false);
  cr_assert_null(fmt, "Should reject unterminated time format");
}

Test(log_format, parse_console_only_flag) {
  log_format_t *fmt = log_format_parse("[%level] %message", true);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->console_only, true);
  log_format_free(fmt);
}

/* ============================================================================
 * Format Application Tests
 * ============================================================================ */

Test(log_format, apply_literal_only) {
  log_format_t *fmt = log_format_parse("Static text", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "Static text");
  log_format_free(fmt);
}

Test(log_format, apply_level) {
  log_format_t *fmt = log_format_parse("[%level]", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[INFO]");
  log_format_free(fmt);
}

Test(log_format, apply_level_aligned) {
  log_format_t *fmt = log_format_parse("[%level_aligned]", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  log_format_apply(fmt, buf, sizeof(buf), LOG_WARN, "12:34:56", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_str_eq(buf, "[WARN ]");
  log_format_free(fmt);
}

Test(log_format, apply_file_and_line) {
  log_format_t *fmt = log_format_parse("%file:%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "test.c:42");
  log_format_free(fmt);
}

Test(log_format, apply_func) {
  log_format_t *fmt = log_format_parse("in %func()", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "in main()");
  log_format_free(fmt);
}

Test(log_format, apply_tid) {
  log_format_t *fmt = log_format_parse("tid:%tid", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 5678, "msg", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "tid:5678");
  log_format_free(fmt);
}

Test(log_format, apply_message) {
  log_format_t *fmt = log_format_parse("Message: %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "Hello", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "Message: Hello");
  log_format_free(fmt);
}

Test(log_format, apply_complex_format) {
  log_format_t *fmt = log_format_parse("[%level_aligned] %file:%line - %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_ERROR, "12:34:56", "error.c", 99, "error_func", 1234,
                             "Critical error", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[ERROR] error.c:99 - Critical error");
  log_format_free(fmt);
}

Test(log_format, apply_with_utf8_message) {
  log_format_t *fmt = log_format_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "Processing café",
                             false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[INFO] Processing café");
  log_format_free(fmt);
}

Test(log_format, apply_null_optionals) {
  log_format_t *fmt = log_format_parse("[%file] [%func]", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", NULL, 0, NULL, 1234, "msg", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[] []"); /* Empty optional fields */
  log_format_free(fmt);
}

Test(log_format, apply_buffer_overflow) {
  log_format_t *fmt = log_format_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char buf[10] = {0}; /* Too small */
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                             "Very long message", false);
  cr_assert_eq(len, -1, "Should fail on buffer overflow");
  log_format_free(fmt);
}

/* ============================================================================
 * Format Default Tests
 * ============================================================================ */

Test(log_format, default_format) {
  const char *def = log_format_default();
  cr_assert_not_null(def);
  cr_assert_str_eq(def, "[%time(%H:%M:%S)] [%level_aligned] %message");
}

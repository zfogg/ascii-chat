/**
 * @file tests/unit/log_template_test.c
 * @brief Unit tests for log format parser and time formatting utilities
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
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
}

void teardown_test_logging(void) {
  log_set_terminal_output(true);
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
  /* Check format: HH:MM:SS (without automatic microseconds appending) */
  cr_assert(strchr(buf, ':') != NULL, "Should contain colons");
  cr_assert_eq(len, 8, "Should be exactly HH:MM:SS (8 chars)");
}

Test(log_format, time_format_now_with_microseconds) {
  char buf[64] = {0};
  int len = time_format_now("%H:%M:%S", buf, sizeof(buf));
  cr_assert_gt(len, 0);
  /* Format: HH:MM:SS (8 chars - microseconds are now separate via %ms specifier) */
  cr_assert_eq(len, 8, "Should be HH:MM:SS without automatic microseconds");
  cr_assert_null(strchr(buf, '.'), "Should NOT have decimal point (use %ms specifier instead)");
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
  cr_assert(strchr(buf, ':') != NULL, "Should be formatted time with colons");
  cr_assert_null(strchr(buf, '.'), "Should NOT have decimal point (use %ms for microseconds)");
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
  log_template_t *fmt = log_template_parse("Hello World", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[0].literal, "Hello World");
  log_template_free(fmt);
}

Test(log_format, parse_level_specifier) {
  log_template_t *fmt = log_template_parse("[%level]", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 3); /* [ + level + ] */
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LEVEL);
  cr_assert_eq(fmt->specs[2].type, LOG_FORMAT_LITERAL);
  log_template_free(fmt);
}

Test(log_format, parse_level_aligned) {
  log_template_t *fmt = log_template_parse("[%level_aligned]", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LEVEL_ALIGNED);
  log_template_free(fmt);
}

Test(log_format, parse_time_specifier) {
  log_template_t *fmt = log_template_parse("%time(%H:%M:%S)", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_TIME);
  cr_assert_str_eq(fmt->specs[0].literal, "%H:%M:%S");
  log_template_free(fmt);
}

Test(log_format, parse_file_line_func) {
  log_template_t *fmt = log_template_parse("%file:%line in %func()", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_FILE);
  cr_assert_eq(fmt->specs[2].type, LOG_FORMAT_LINE);
  cr_assert_eq(fmt->specs[4].type, LOG_FORMAT_FUNC);
  log_template_free(fmt);
}

Test(log_format, parse_message) {
  log_template_t *fmt = log_template_parse("%message", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_MESSAGE);
  log_template_free(fmt);
}

Test(log_format, parse_tid) {
  log_template_t *fmt = log_template_parse("[tid:%tid]", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_TID);
  log_template_free(fmt);
}

/* ============================================================================
 * Format Parser - Escaping Tests
 * ============================================================================ */

Test(log_format, parse_escaped_percent) {
  log_template_t *fmt = log_template_parse("100%%", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 2); /* "100" + "%" */
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[1].literal, "%");
  log_template_free(fmt);
}

Test(log_format, parse_escaped_backslash) {
  log_template_t *fmt = log_template_parse("path\\\\file", false);
  cr_assert_not_null(fmt);
  /* Should have: "path" + "\" + "file" */
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[0].literal, "path");
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[1].literal, "\\");
  log_template_free(fmt);
}

Test(log_format, parse_newline) {
  log_template_t *fmt = log_template_parse("line1\\nline2", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_NEWLINE);
  log_template_free(fmt);
}

Test(log_format, parse_complex_with_escapes) {
  log_template_t *fmt = log_template_parse("[%level] %message\\n(100%% complete)", false);
  cr_assert_not_null(fmt);
  cr_assert_gt(fmt->spec_count, 3);
  log_template_free(fmt);
}

/* ============================================================================
 * Format Parser - UTF-8 Support Tests
 * ============================================================================ */

Test(log_format, parse_utf8_literals) {
  /* UTF-8 format string with non-ASCII characters */
  log_template_t *fmt = log_template_parse("[时间:%time(%H:%M:%S)] [%level] %message", false);
  cr_assert_not_null(fmt);
  cr_assert_gt(fmt->spec_count, 0);
  log_template_free(fmt);
}

Test(log_format, parse_invalid_utf8) {
  /* Invalid UTF-8 sequence */
  const unsigned char invalid_utf8[] = "[%level] \xFF\xFE message";
  log_template_t *fmt = log_template_parse((const char *)invalid_utf8, false);
  cr_assert_null(fmt, "Should reject invalid UTF-8");
}

/* ============================================================================
 * Format Parser - Error Cases
 * ============================================================================ */

Test(log_format, parse_null_format) {
  log_template_t *fmt = log_template_parse(NULL, false);
  cr_assert_null(fmt);
}

Test(log_format, parse_unknown_specifier) {
  /* Unknown specifiers are now treated as strftime format codes
     and validation is deferred to strftime at format time */
  log_template_t *fmt = log_template_parse("%unknown", false);
  cr_assert_not_null(fmt, "Should accept unknown specifier as strftime code");
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_STRFTIME_CODE);
  log_template_free(fmt);
}

Test(log_format, apply_strftime_codes) {
  /* Test that strftime codes work end-to-end */
  log_template_t *fmt = log_template_parse("[%H:%M:%S] %message", false);
  cr_assert_not_null(fmt, "Should parse strftime codes");

  char buf[256];
  uint64_t now = time_get_realtime_ns();
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "", NULL, 0, NULL, 0, "test", false, now);

  cr_assert_gt(len, 0, "Should format successfully with strftime codes");
  cr_assert(strstr(buf, "test") != NULL, "Should contain message");
  /* Should contain formatted time with colons like HH:MM:SS */
  cr_assert(strchr(buf, ':') != NULL, "Should contain time separator");

  log_template_free(fmt);
}

Test(log_format, parse_unterminated_time_format) {
  log_template_t *fmt = log_template_parse("%time(%H:%M:%S", false);
  cr_assert_null(fmt, "Should reject unterminated time format");
}

Test(log_format, parse_console_only_flag) {
  log_template_t *fmt = log_template_parse("[%level] %message", true);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->console_only, true);
  log_template_free(fmt);
}

/* ============================================================================
 * Format Application Tests
 * ============================================================================ */

Test(log_format, apply_literal_only) {
  log_template_t *fmt = log_template_parse("Static text", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "Static text");
  log_template_free(fmt);
}

Test(log_format, apply_level) {
  log_template_t *fmt = log_template_parse("[%level]", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[INFO]");
  log_template_free(fmt);
}

Test(log_format, apply_level_aligned) {
  log_template_t *fmt = log_template_parse("[%level_aligned]", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  log_template_apply(fmt, buf, sizeof(buf), LOG_WARN, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                     45296123456000);
  cr_assert_str_eq(buf, "[WARN ]");
  log_template_free(fmt);
}

Test(log_format, apply_file_and_line) {
  log_template_t *fmt = log_template_parse("%file:%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "test.c:42");
  log_template_free(fmt);
}

Test(log_format, apply_func) {
  log_template_t *fmt = log_template_parse("in %func()", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "in main()");
  log_template_free(fmt);
}

Test(log_format, apply_tid) {
  log_template_t *fmt = log_template_parse("tid:%tid", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "test.c", 42, "main", 5678, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "tid:5678");
  log_template_free(fmt);
}

Test(log_format, apply_message) {
  log_template_t *fmt = log_template_parse("Message: %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "Hello", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "Message: Hello");
  log_template_free(fmt);
}

Test(log_format, apply_complex_format) {
  log_template_t *fmt = log_template_parse("[%level_aligned] %file:%line - %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_ERROR, "12:34:56", "error.c", 99, "error_func", 1234,
                               "Critical error", false, 45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[ERROR] error.c:99 - Critical error");
  log_template_free(fmt);
}

Test(log_format, apply_with_utf8_message) {
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                               "Processing café", false, 45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[INFO] Processing café");
  log_template_free(fmt);
}

Test(log_format, apply_null_optionals) {
  log_template_t *fmt = log_template_parse("[%file] [%func]", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", NULL, 0, NULL, 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[] []"); /* Empty optional fields */
  log_template_free(fmt);
}

Test(log_format, apply_buffer_overflow) {
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char buf[10] = {0}; /* Too small */
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                               "Very long message", false, 45296123456000);
  cr_assert_eq(len, -1, "Should fail on buffer overflow");
  log_template_free(fmt);
}

/* ============================================================================
 * File Relative Path Tests
 * ============================================================================ */

Test(log_format, parse_file_relative) {
  log_template_t *fmt = log_template_parse("%file_relative", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_FILE_RELATIVE);
  log_template_free(fmt);
}

Test(log_format, parse_file_relative_in_context) {
  log_template_t *fmt = log_template_parse("[%file_relative:%line]", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL); /* [ */
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_FILE_RELATIVE);
  cr_assert_eq(fmt->specs[3].type, LOG_FORMAT_LINE);
  log_template_free(fmt);
}

Test(log_format, parse_file_relative_before_file) {
  /* %file_relative should be checked before %file since it's longer */
  log_template_t *fmt = log_template_parse("%file_relative:%file", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_FILE_RELATIVE);
  cr_assert_eq(fmt->specs[2].type, LOG_FORMAT_FILE);
  log_template_free(fmt);
}

Test(log_format, apply_file_relative) {
  log_template_t *fmt = log_template_parse("%file_relative:%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "src/main.c", 42, "main", 1234, "msg",
                               false, 45296123456000);
  cr_assert_gt(len, 0);
  /* Should contain the relative path (extracted by extract_project_relative_path) */
  cr_assert(strchr(buf, ':') != NULL, "Should contain colon separator");
  log_template_free(fmt);
}

/* ============================================================================
 * Strftime Format Tests - Various Time Formats
 * ============================================================================ */

Test(log_format, apply_time_format_iso8601) {
  log_template_t *fmt = log_template_parse("%time(%Y-%m-%d %H:%M:%S)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  /* Should have format like: YYYY-MM-DD HH:MM:SS (no automatic microseconds) */
  cr_assert(strchr(buf, '-') != NULL && strchr(buf, ' ') != NULL);
  cr_assert_null(strchr(buf, '.'), "Should NOT have decimal point (use %ms for microseconds)");
  log_template_free(fmt);
}

Test(log_format, apply_time_format_with_weekday) {
  log_template_t *fmt = log_template_parse("%time(%A, %B %d, %Y)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  /* Should contain comma and have reasonable length for full weekday + month name */
  cr_assert_gt(strlen(buf), 15);
  log_template_free(fmt);
}

Test(log_format, apply_time_format_short_date) {
  log_template_t *fmt = log_template_parse("%time(%a %b %d %H:%M)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  /* Should be shorter than full format */
  cr_assert_lt(strlen(buf), 30);
  log_template_free(fmt);
}

Test(log_format, apply_time_format_with_percent) {
  log_template_t *fmt = log_template_parse("%time(%%Y-%%m-%%d)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  /* Should contain percent signs from escaped %% */
  cr_assert(strchr(buf, '%') != NULL);
  log_template_free(fmt);
}

/* ============================================================================
 * Newline and Multi-Line Output Tests
 * ============================================================================ */

Test(log_format, apply_newline_in_format) {
  log_template_t *fmt = log_template_parse("[%level]\\n%message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "Hello", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert(strchr(buf, '\n') != NULL, "Should contain newline");
  cr_assert_str_eq(buf, "[INFO]\nHello");
  log_template_free(fmt);
}

Test(log_format, apply_multiple_newlines) {
  log_template_t *fmt = log_template_parse("[%level]\\n%file:%line\\n%message", false);
  cr_assert_not_null(fmt);

  char buf[512] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_WARN, "12:34:56", "src.c", 10, "main", 1234, "Test", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  /* Count newlines */
  int newline_count = 0;
  for (const char *p = buf; *p; p++) {
    if (*p == '\n')
      newline_count++;
  }
  cr_assert_eq(newline_count, 2);
  log_template_free(fmt);
}

Test(log_format, apply_message_first_then_newline_then_header) {
  log_template_t *fmt = log_template_parse("%message\\n[%level_aligned] %file:%line in %func", false);
  cr_assert_not_null(fmt);

  char buf[512] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_ERROR, "12:34:56", "error.c", 99, "process", 1234,
                               "Error occurred", false, 45296123456000);
  cr_assert_gt(len, 0);
  cr_assert(strchr(buf, '\n') != NULL);

  /* First part should be the message */
  cr_assert_eq(strncmp(buf, "Error occurred", 14), 0, "Message should come first");
  /* After newline should be the header */
  const char *after_newline = strchr(buf, '\n') + 1;
  cr_assert(strstr(after_newline, "ERROR") != NULL);
  log_template_free(fmt);
}

Test(log_format, apply_newline_at_end) {
  log_template_t *fmt = log_template_parse("[%level] %message\\n", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_eq(buf[strlen(buf) - 1], '\n', "Should end with newline");
  log_template_free(fmt);
}

/* ============================================================================
 * Different Specifier Order Tests
 * ============================================================================ */

Test(log_format, apply_specifiers_reverse_order) {
  log_template_t *fmt = log_template_parse("%message - %func() at %file:%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "app.c", 50, "process", 1234, "Starting",
                               false, 45296123456000);
  cr_assert_gt(len, 0);
  /* Message should come first */
  cr_assert(strncmp(buf, "Starting", 8) == 0);
  log_template_free(fmt);
}

Test(log_format, apply_message_in_middle) {
  log_template_t *fmt = log_template_parse("[%time(%H:%M:%S)] %message [%level_aligned]", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_WARN, "14:30:00", "test.c", 42, "main", 1234, "Warning!",
                               false, 45296123456000);
  cr_assert_gt(len, 0);
  /* Time should come first, message in middle, level at end */
  const char *msg_pos = strstr(buf, "Warning!");
  const char *level_pos = strstr(buf, "WARN");
  cr_assert(msg_pos < level_pos, "Message should come before level");
  log_template_free(fmt);
}

Test(log_format, apply_duplicate_specifiers) {
  log_template_t *fmt = log_template_parse("[%level] [%level] %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_FATAL, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  /* Should have FATAL twice */
  int count = 0;
  for (const char *p = buf; (p = strstr(p, "FATAL")) != NULL; p++, count++)
    ;
  cr_assert_eq(count, 2);
  log_template_free(fmt);
}

Test(log_format, apply_all_specifiers_together) {
  log_template_t *fmt = log_template_parse(
      "[%time(%H:%M:%S)] [%level_aligned] [%file_relative:%line] {%func} <tid:%tid> %message", false);
  cr_assert_not_null(fmt);

  char buf[512] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "lib/core.c", 42, "initialize", 5678,
                               "Initializing system", false, 45296123456000);
  cr_assert_gt(len, 0);
  /* All parts should be present */
  cr_assert(strchr(buf, '[') != NULL);
  cr_assert(strchr(buf, '{') != NULL);
  cr_assert(strchr(buf, '<') != NULL);
  cr_assert(strstr(buf, "Initializing system") != NULL);
  log_template_free(fmt);
}

/* ============================================================================
 * Edge Cases and Robustness Tests
 * ============================================================================ */

Test(log_format, apply_empty_message) {
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[INFO] ");
  log_template_free(fmt);
}

Test(log_format, apply_very_long_message) {
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char long_msg[500];
  memset(long_msg, 'A', sizeof(long_msg) - 1);
  long_msg[sizeof(long_msg) - 1] = '\0';

  char buf[1024] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, long_msg, false,
                               45296123456000);
  cr_assert_gt(len, 0);
  cr_assert(strstr(buf, long_msg) != NULL);
  log_template_free(fmt);
}

Test(log_format, apply_special_characters_in_message) {
  log_template_t *fmt = log_template_parse("%message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                               "Test %% %% backslash \\ newline \\n", false, 45296123456000);
  cr_assert_gt(len, 0);
  /* Message should be copied as-is (these are in the message, not format specifiers) */
  cr_assert_str_eq(buf, "Test %% %% backslash \\ newline \\n");
  log_template_free(fmt);
}

Test(log_format, apply_large_thread_id) {
  log_template_t *fmt = log_template_parse("tid=%tid", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  uint64_t large_tid = 18446744073709551615ULL; /* Max uint64_t */
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", large_tid, "msg",
                               false, 45296123456000);
  cr_assert_gt(len, 0);
  cr_assert(strstr(buf, "tid=") != NULL);
  log_template_free(fmt);
}

Test(log_format, apply_all_log_levels) {
  log_template_t *fmt = log_template_parse("[%level_aligned]", false);
  cr_assert_not_null(fmt);

  const log_level_t levels[] = {LOG_DEV, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL};
  const char *expected[] = {"[DEV  ]", "[DEBUG]", "[INFO ]", "[WARN ]", "[ERROR]", "[FATAL]"};

  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    char buf[256] = {0};
    int len = log_template_apply(fmt, buf, sizeof(buf), levels[i], "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                                 45296123456000);
    cr_assert_gt(len, 0);
    cr_assert_str_eq(buf, expected[i], "Level %lu should format correctly", i);
  }

  log_template_free(fmt);
}

Test(log_format, apply_zero_line_number) {
  log_template_t *fmt = log_template_parse("line=%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 0, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  /* Zero line should not be printed (line > 0 check in format.c) */
  cr_assert_str_eq(buf, "line=");
  log_template_free(fmt);
}

Test(log_format, apply_negative_line_number) {
  log_template_t *fmt = log_template_parse("line=%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", -1, "main", 1234, "msg", false,
                               45296123456000);
  cr_assert_gt(len, 0);
  /* Negative line should not be printed (line > 0 check) */
  cr_assert_str_eq(buf, "line=");
  log_template_free(fmt);
}

/* ============================================================================
 * Format Consistency Tests
 * ============================================================================ */

Test(log_format, apply_consistent_output) {
  /* Use format without %time since time changes between calls */
  log_template_t *fmt = log_template_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char buf1[256] = {0};
  char buf2[256] = {0};

  int len1 = log_template_apply(fmt, buf1, sizeof(buf1), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                                45296123456000);
  int len2 = log_template_apply(fmt, buf2, sizeof(buf2), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false,
                                45296123456000);

  cr_assert_eq(len1, len2, "Same inputs should produce same length output");
  cr_assert_str_eq(buf1, buf2, "Same inputs should produce identical output");
  log_template_free(fmt);
}

Test(log_format, apply_time_consistency_multiple_calls) {
  /* Test that time formatting works consistently across multiple calls */
  log_template_t *fmt = log_template_parse("%time(%Y-%m-%d)", false);
  cr_assert_not_null(fmt);

  char buf1[256] = {0};
  char buf2[256] = {0};

  int len1 = log_template_apply(fmt, buf1, sizeof(buf1), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                                45296123456000);
  int len2 = log_template_apply(fmt, buf2, sizeof(buf2), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false,
                                45296123456000);

  cr_assert_gt(len1, 0);
  cr_assert_gt(len2, 0);
  /* Both should have the date format (YYYY-MM-DD) = 10 chars */
  cr_assert_eq(len1, 10, "Date format should be exactly 10 chars");
  cr_assert_eq(len2, 10, "Date format should be exactly 10 chars");
  /* Both should have same date (not guaranteed to be identical if test spans midnight, but very unlikely) */
  cr_assert_str_eq(buf1, buf2, "Date should be same on consecutive calls");
  log_template_free(fmt);
}

/* ============================================================================
 * Color Format Tests (Validate ANSI codes are actually present)
 * ============================================================================ */

Test(log_format, color_with_use_colors_true) {
  /* Test that %color() produces ANSI codes with correct color from enum when use_colors=true */
  log_template_t *fmt = log_template_parse("%color(INFO, %message)", false);
  cr_assert_not_null(fmt);

  /* Force enable colors for testing */
  extern bool g_color_flag_passed;
  extern bool g_color_flag_value;
  bool saved_passed = g_color_flag_passed;
  bool saved_value = g_color_flag_value;
  g_color_flag_passed = true;
  g_color_flag_value = true;

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "test message",
                               true, 45296123456000);
  cr_assert_gt(len, 0, "Should produce output with colors");

  /* Get the actual INFO color code from the enum */
  extern const char *log_level_color(log_color_t color);
  const char *info_color = log_level_color(LOG_COLOR_INFO);
  const char *reset_code = log_level_color(LOG_COLOR_RESET);

  /* Should contain the specific INFO color code */
  if (info_color && strlen(info_color) > 0) {
    cr_assert(strstr(buf, info_color) != NULL, "Should contain INFO color code from enum in colored output");
  }
  /* Should contain the message text */
  cr_assert(strstr(buf, "test message") != NULL, "Should contain message text");
  /* Should contain reset code */
  if (reset_code && strlen(reset_code) > 0) {
    cr_assert(strstr(buf, reset_code) != NULL, "Should contain reset code from enum");
  }

  /* Restore saved state */
  g_color_flag_passed = saved_passed;
  g_color_flag_value = saved_value;
  log_template_free(fmt);
}

Test(log_format, color_with_use_colors_false) {
  /* Test that %color() produces plain text when use_colors=false */
  log_template_t *fmt = log_template_parse("%color(INFO, %message)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "test message",
                               false, 45296123456000);
  cr_assert_gt(len, 0);

  /* Get the INFO color code - should NOT appear when use_colors=false */
  extern const char *log_level_color(log_color_t color);
  const char *info_color = log_level_color(LOG_COLOR_INFO);

  /* When use_colors=false, INFO color code should NOT be in the output */
  if (info_color && strlen(info_color) > 0) {
    cr_assert(strstr(buf, info_color) == NULL, "Should NOT contain color codes when use_colors=false");
  }
  /* Should contain the message text */
  cr_assert(strstr(buf, "test message") != NULL, "Should contain message text");
  log_template_free(fmt);
}

Test(log_format, colored_message_with_colorize) {
  /* Test that %colored_message applies colorize_log_message() rendering
   * Note: In test environment, colorize_log_message might not apply colors due to TTY detection,
   * but we still verify the format specifier is correctly rendered */
  log_template_t *fmt = log_template_parse("%colored_message", false);
  cr_assert_not_null(fmt);

  char buf[512] = {0};
  /* Use message with numbers and hex that would be colorized in a TTY */
  int len = log_template_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                               "Buffer size: 256 bytes (0x100)", true, 45296123456000);
  cr_assert_gt(len, 0, "Should render colored_message format");

  /* Should contain the message text */
  cr_assert(strstr(buf, "Buffer size") != NULL, "Should contain message text");
  cr_assert(strstr(buf, "256") != NULL, "Should contain number in message");
  cr_assert(strstr(buf, "0x100") != NULL, "Should contain hex value");

  log_template_free(fmt);
}

Test(log_format, color_different_levels) {
  /* Test that %color(*,...) uses different color codes for different log levels */
  log_template_t *fmt = log_template_parse("%color(*, %message)", false);
  cr_assert_not_null(fmt);

  /* Force enable colors for testing */
  extern bool g_color_flag_passed;
  extern bool g_color_flag_value;
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
  extern const char *log_level_color(log_color_t color);
  const char *info_color = log_level_color(LOG_COLOR_INFO);
  const char *error_color = log_level_color(LOG_COLOR_ERROR);

  /* INFO message should contain INFO color code */
  if (info_color && strlen(info_color) > 0) {
    cr_assert(strstr(buf_info, info_color) != NULL, "INFO message should have INFO color from enum");
  }
  /* ERROR message should contain ERROR color code */
  if (error_color && strlen(error_color) > 0) {
    cr_assert(strstr(buf_error, error_color) != NULL, "ERROR message should have ERROR color from enum");
  }
  /* Verify they actually use different colors if the codes are different */
  if (info_color && error_color && strcmp(info_color, error_color) != 0) {
    cr_assert(strstr(buf_info, error_color) == NULL, "INFO message should NOT have ERROR color");
    cr_assert(strstr(buf_error, info_color) == NULL, "ERROR message should NOT have INFO color");
  }

  /* Restore saved state */
  g_color_flag_passed = saved_passed;
  g_color_flag_value = saved_value;
  log_template_free(fmt);
}

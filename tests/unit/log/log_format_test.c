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
  /* Default format depends on build mode (debug vs release) */
#ifdef NDEBUG
  cr_assert_str_eq(def, "[%time(%H:%M:%S)] [%level_aligned] %message");
#else
  cr_assert_str_eq(def, "[%time(%H:%M:%S)] [%level_aligned] [tid:%tid] %file_relative:%line in %func(): %message");
#endif
}

/* ============================================================================
 * File Relative Path Tests
 * ============================================================================ */

Test(log_format, parse_file_relative) {
  log_format_t *fmt = log_format_parse("%file_relative", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_FILE_RELATIVE);
  log_format_free(fmt);
}

Test(log_format, parse_file_relative_in_context) {
  log_format_t *fmt = log_format_parse("[%file_relative:%line]", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL); /* [ */
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_FILE_RELATIVE);
  cr_assert_eq(fmt->specs[3].type, LOG_FORMAT_LINE);
  log_format_free(fmt);
}

Test(log_format, parse_file_relative_before_file) {
  /* %file_relative should be checked before %file since it's longer */
  log_format_t *fmt = log_format_parse("%file_relative:%file", false);
  cr_assert_not_null(fmt);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_FILE_RELATIVE);
  cr_assert_eq(fmt->specs[2].type, LOG_FORMAT_FILE);
  log_format_free(fmt);
}

Test(log_format, apply_file_relative) {
  log_format_t *fmt = log_format_parse("%file_relative:%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len =
      log_format_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "src/main.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  /* Should contain the relative path (extracted by extract_project_relative_path) */
  cr_assert(strchr(buf, ':') != NULL, "Should contain colon separator");
  log_format_free(fmt);
}

/* ============================================================================
 * Strftime Format Tests - Various Time Formats
 * ============================================================================ */

Test(log_format, apply_time_format_iso8601) {
  log_format_t *fmt = log_format_parse("%time(%Y-%m-%d %H:%M:%S)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  /* Should have format like: YYYY-MM-DD HH:MM:SS.NNNNNN */
  cr_assert(strchr(buf, '-') != NULL && strchr(buf, ' ') != NULL && strchr(buf, '.') != NULL);
  log_format_free(fmt);
}

Test(log_format, apply_time_format_with_weekday) {
  log_format_t *fmt = log_format_parse("%time(%A, %B %d, %Y)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  /* Should contain comma and have reasonable length for full weekday + month name */
  cr_assert_gt(strlen(buf), 15);
  log_format_free(fmt);
}

Test(log_format, apply_time_format_short_date) {
  log_format_t *fmt = log_format_parse("%time(%a %b %d %H:%M)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  /* Should be shorter than full format */
  cr_assert_lt(strlen(buf), 30);
  log_format_free(fmt);
}

Test(log_format, apply_time_format_with_percent) {
  log_format_t *fmt = log_format_parse("%time(%%Y-%%m-%%d)", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  /* Should contain percent signs from escaped %% */
  cr_assert(strchr(buf, '%') != NULL);
  log_format_free(fmt);
}

/* ============================================================================
 * Newline and Multi-Line Output Tests
 * ============================================================================ */

Test(log_format, apply_newline_in_format) {
  log_format_t *fmt = log_format_parse("[%level]\\n%message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "Hello", false);
  cr_assert_gt(len, 0);
  cr_assert(strchr(buf, '\n') != NULL, "Should contain newline");
  cr_assert_str_eq(buf, "[INFO]\nHello");
  log_format_free(fmt);
}

Test(log_format, apply_multiple_newlines) {
  log_format_t *fmt = log_format_parse("[%level]\\n%file:%line\\n%message", false);
  cr_assert_not_null(fmt);

  char buf[512] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_WARN, "12:34:56", "src.c", 10, "main", 1234, "Test", false);
  cr_assert_gt(len, 0);
  /* Count newlines */
  int newline_count = 0;
  for (const char *p = buf; *p; p++) {
    if (*p == '\n')
      newline_count++;
  }
  cr_assert_eq(newline_count, 2);
  log_format_free(fmt);
}

Test(log_format, apply_message_first_then_newline_then_header) {
  log_format_t *fmt = log_format_parse("%message\\n[%level_aligned] %file:%line in %func", false);
  cr_assert_not_null(fmt);

  char buf[512] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_ERROR, "12:34:56", "error.c", 99, "process", 1234,
                             "Error occurred", false);
  cr_assert_gt(len, 0);
  cr_assert(strchr(buf, '\n') != NULL);

  /* First part should be the message */
  cr_assert_eq(strncmp(buf, "Error occurred", 14), 0, "Message should come first");
  /* After newline should be the header */
  const char *after_newline = strchr(buf, '\n') + 1;
  cr_assert(strstr(after_newline, "ERROR") != NULL);
  log_format_free(fmt);
}

Test(log_format, apply_newline_at_end) {
  log_format_t *fmt = log_format_parse("[%level] %message\\n", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  cr_assert_eq(buf[strlen(buf) - 1], '\n', "Should end with newline");
  log_format_free(fmt);
}

/* ============================================================================
 * Different Specifier Order Tests
 * ============================================================================ */

Test(log_format, apply_specifiers_reverse_order) {
  log_format_t *fmt = log_format_parse("%message - %func() at %file:%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len =
      log_format_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "app.c", 50, "process", 1234, "Starting", false);
  cr_assert_gt(len, 0);
  /* Message should come first */
  cr_assert(strncmp(buf, "Starting", 8) == 0);
  log_format_free(fmt);
}

Test(log_format, apply_message_in_middle) {
  log_format_t *fmt = log_format_parse("[%time(%H:%M:%S)] %message [%level_aligned]", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len =
      log_format_apply(fmt, buf, sizeof(buf), LOG_WARN, "14:30:00", "test.c", 42, "main", 1234, "Warning!", false);
  cr_assert_gt(len, 0);
  /* Time should come first, message in middle, level at end */
  const char *msg_pos = strstr(buf, "Warning!");
  const char *level_pos = strstr(buf, "WARN");
  cr_assert(msg_pos < level_pos, "Message should come before level");
  log_format_free(fmt);
}

Test(log_format, apply_duplicate_specifiers) {
  log_format_t *fmt = log_format_parse("[%level] [%level] %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_FATAL, "12:34:56", "test.c", 42, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  /* Should have FATAL twice */
  int count = 0;
  for (const char *p = buf; (p = strstr(p, "FATAL")) != NULL; p++, count++)
    ;
  cr_assert_eq(count, 2);
  log_format_free(fmt);
}

Test(log_format, apply_all_specifiers_together) {
  log_format_t *fmt =
      log_format_parse("[%time(%H:%M:%S)] [%level_aligned] [%file_relative:%line] {%func} <tid:%tid> %message", false);
  cr_assert_not_null(fmt);

  char buf[512] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_DEBUG, "12:34:56", "lib/core.c", 42, "initialize", 5678,
                             "Initializing system", false);
  cr_assert_gt(len, 0);
  /* All parts should be present */
  cr_assert(strchr(buf, '[') != NULL);
  cr_assert(strchr(buf, '{') != NULL);
  cr_assert(strchr(buf, '<') != NULL);
  cr_assert(strstr(buf, "Initializing system") != NULL);
  log_format_free(fmt);
}

/* ============================================================================
 * Edge Cases and Robustness Tests
 * ============================================================================ */

Test(log_format, apply_empty_message) {
  log_format_t *fmt = log_format_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "", false);
  cr_assert_gt(len, 0);
  cr_assert_str_eq(buf, "[INFO] ");
  log_format_free(fmt);
}

Test(log_format, apply_very_long_message) {
  log_format_t *fmt = log_format_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char long_msg[500];
  memset(long_msg, 'A', sizeof(long_msg) - 1);
  long_msg[sizeof(long_msg) - 1] = '\0';

  char buf[1024] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, long_msg, false);
  cr_assert_gt(len, 0);
  cr_assert(strstr(buf, long_msg) != NULL);
  log_format_free(fmt);
}

Test(log_format, apply_special_characters_in_message) {
  log_format_t *fmt = log_format_parse("%message", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234,
                             "Test %% %% backslash \\ newline \\n", false);
  cr_assert_gt(len, 0);
  /* Message should be copied as-is (these are in the message, not format specifiers) */
  cr_assert_str_eq(buf, "Test %% %% backslash \\ newline \\n");
  log_format_free(fmt);
}

Test(log_format, apply_large_thread_id) {
  log_format_t *fmt = log_format_parse("tid=%tid", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  uint64_t large_tid = 18446744073709551615ULL; /* Max uint64_t */
  int len =
      log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 42, "main", large_tid, "msg", false);
  cr_assert_gt(len, 0);
  cr_assert(strstr(buf, "tid=") != NULL);
  log_format_free(fmt);
}

Test(log_format, apply_all_log_levels) {
  log_format_t *fmt = log_format_parse("[%level_aligned]", false);
  cr_assert_not_null(fmt);

  const log_level_t levels[] = {LOG_DEV, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL};
  const char *expected[] = {"[DEV  ]", "[DEBUG]", "[INFO ]", "[WARN ]", "[ERROR]", "[FATAL]"};

  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    char buf[256] = {0};
    int len = log_format_apply(fmt, buf, sizeof(buf), levels[i], "12:34:56", "test.c", 42, "main", 1234, "msg", false);
    cr_assert_gt(len, 0);
    cr_assert_str_eq(buf, expected[i], "Level %d should format correctly", i);
  }

  log_format_free(fmt);
}

Test(log_format, apply_zero_line_number) {
  log_format_t *fmt = log_format_parse("line=%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", 0, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  /* Zero line should not be printed (line > 0 check in format.c) */
  cr_assert_str_eq(buf, "line=");
  log_format_free(fmt);
}

Test(log_format, apply_negative_line_number) {
  log_format_t *fmt = log_format_parse("line=%line", false);
  cr_assert_not_null(fmt);

  char buf[256] = {0};
  int len = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO, "12:34:56", "test.c", -1, "main", 1234, "msg", false);
  cr_assert_gt(len, 0);
  /* Negative line should not be printed (line > 0 check) */
  cr_assert_str_eq(buf, "line=");
  log_format_free(fmt);
}

/* ============================================================================
 * Format Consistency Tests
 * ============================================================================ */

Test(log_format, apply_consistent_output) {
  /* Use format without %time since time changes between calls */
  log_format_t *fmt = log_format_parse("[%level] %message", false);
  cr_assert_not_null(fmt);

  char buf1[256] = {0};
  char buf2[256] = {0};

  int len1 = log_format_apply(fmt, buf1, sizeof(buf1), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false);
  int len2 = log_format_apply(fmt, buf2, sizeof(buf2), LOG_INFO, "12:34:56", "test.c", 42, "main", 1234, "msg", false);

  cr_assert_eq(len1, len2, "Same inputs should produce same length output");
  cr_assert_str_eq(buf1, buf2, "Same inputs should produce identical output");
  log_format_free(fmt);
}

Test(log_format, apply_time_consistency_multiple_calls) {
  /* Test that time formatting works consistently across multiple calls */
  log_format_t *fmt = log_format_parse("%time(%Y-%m-%d)", false);
  cr_assert_not_null(fmt);

  char buf1[256] = {0};
  char buf2[256] = {0};

  int len1 = log_format_apply(fmt, buf1, sizeof(buf1), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false);
  int len2 = log_format_apply(fmt, buf2, sizeof(buf2), LOG_INFO, "dummy", "test.c", 42, "main", 1234, "msg", false);

  cr_assert_gt(len1, 0);
  cr_assert_gt(len2, 0);
  /* Both should have the date format (YYYY-MM-DD) = 10 chars */
  cr_assert_eq(len1, 10, "Date format should be exactly 10 chars");
  cr_assert_eq(len2, 10, "Date format should be exactly 10 chars");
  /* Both should have same date (not guaranteed to be identical if test spans midnight, but very unlikely) */
  cr_assert_str_eq(buf1, buf2, "Date should be same on consecutive calls");
  log_format_free(fmt);
}

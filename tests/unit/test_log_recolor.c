#include <criterion/criterion.h>
#include <criterion/assert.h>
#include <string.h>
#include <stdio.h>

// Include the headers we're testing
#include "ascii-chat/log/logging.h"
#include "ascii-chat/logging/file_parser.h"
#include "ascii-chat/session/session_log_buffer.h"

/* ============================================================================
 * Basic Recoloring Tests
 * ============================================================================ */

Test(log_recolor, valid_debug_format) {
  const char *plain =
      "[2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c:42 in test_func(): This is a test message";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));

  cr_assert_gt(len, 0, "Should successfully recolor a valid debug log");
  cr_assert_neq(strcmp(colored, plain), 0, "Colored output should contain ANSI codes");
  cr_assert(strstr(colored, "test message") != NULL, "Original message should be preserved");
}

Test(log_recolor, valid_error_format) {
  const char *plain =
      "[2026-02-08 12:34:56.789] [ERROR] [tid:67890] lib/network/socket.c:123 in send_packet(): Connection failed";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));

  cr_assert_gt(len, 0);
  cr_assert(strstr(colored, "Connection failed") != NULL);
}

Test(log_recolor, valid_info_format) {
  const char *plain = "[2026-02-08 12:34:56.789] [INFO ] [tid:11111] src/main.c:1 in main(): Application started";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));

  cr_assert_gt(len, 0);
}

/* ============================================================================
 * Edge Cases and Malformed Input Tests
 * ============================================================================ */

Test(log_recolor, null_pointer) {
  char colored[512];

  size_t len = log_recolor_plain_entry(NULL, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should return 0 for NULL input");
}

Test(log_recolor, null_buffer) {
  const char *plain = "[2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c:42 in test_func(): msg";

  size_t len = log_recolor_plain_entry(plain, NULL, 512);
  cr_assert_eq(len, 0, "Should return 0 for NULL buffer");
}

Test(log_recolor, buffer_too_small) {
  const char *plain =
      "[2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c:42 in test_func(): This is a test message";
  char colored[64];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should return 0 if buffer too small");
}

Test(log_recolor, missing_opening_bracket) {
  const char *plain = "2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c:42 in test_func(): msg";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should reject format without opening bracket");
}

Test(log_recolor, missing_closing_bracket_timestamp) {
  const char *plain = "[2026-02-08 12:34:56.789 [DEBUG] [tid:12345] src/test.c:42 in test_func(): msg";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should reject malformed timestamp");
}

Test(log_recolor, missing_level_bracket) {
  const char *plain = "[2026-02-08 12:34:56.789] DEBUG] [tid:12345] src/test.c:42 in test_func(): msg";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should reject missing level opening bracket");
}

Test(log_recolor, missing_tid_bracket) {
  const char *plain = "[2026-02-08 12:34:56.789] [DEBUG] tid:12345] src/test.c:42 in test_func(): msg";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should reject malformed tid");
}

Test(log_recolor, missing_colon_in_file_line) {
  const char *plain = "[2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c42 in test_func(): msg";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should reject missing colon in file:line");
}

Test(log_recolor, missing_in_keyword) {
  const char *plain = "[2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c:42 test_func(): msg";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should reject missing 'in' keyword");
}

Test(log_recolor, missing_function_parens) {
  const char *plain = "[2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c:42 in test_func: msg";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_eq(len, 0, "Should reject missing function parentheses");
}

Test(log_recolor, empty_string) {
  char colored[512];

  size_t len = log_recolor_plain_entry("", colored, sizeof(colored));
  cr_assert_eq(len, 0);
}

Test(log_recolor, all_log_levels) {
  const char *levels[] = {"DEV", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

  for (size_t i = 0; i < 6; i++) {
    char plain[512];
    snprintf(plain, sizeof(plain),
             "[2026-02-08 12:34:56.789] [%5s] [tid:12345] src/test.c:42 in test_func(): Test level %s", levels[i],
             levels[i]);

    char colored[512];
    size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
    cr_assert_gt(len, 0, "Should handle level %s", levels[i]);
  }
}

Test(log_recolor, timestamp_preserved) {
  const char *plain = "[2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c:42 in test_func(): msg";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_gt(len, 0);
  cr_assert(strstr(colored, "2026-02-08") != NULL);
}

Test(log_recolor, message_with_colons) {
  const char *plain = "[2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/test.c:42 in test_func(): Error: Invalid "
                      "state: timeout occurred";
  char colored[512];

  size_t len = log_recolor_plain_entry(plain, colored, sizeof(colored));
  cr_assert_gt(len, 0);
  cr_assert(strstr(colored, "Invalid state: timeout occurred") != NULL);
}

/* ============================================================================
 * Merge and Dedupe Tests
 * ============================================================================ */

Test(merge_dedupe, empty_inputs) {
  session_log_entry_t *merged = NULL;
  size_t count = log_file_parser_merge_and_dedupe(NULL, 0, NULL, 0, &merged);

  cr_assert_eq(count, 0);
  cr_assert_null(merged);
}

Test(merge_dedupe, only_buffer_entries) {
  session_log_entry_t buffer[2];
  strncpy(buffer[0].message, "[2026-02-08 12:34:56.001] [INFO ] test1", SESSION_LOG_LINE_MAX - 1);
  buffer[0].message[SESSION_LOG_LINE_MAX - 1] = '\0';
  buffer[0].sequence = 1;
  strncpy(buffer[1].message, "[2026-02-08 12:34:56.002] [INFO ] test2", SESSION_LOG_LINE_MAX - 1);
  buffer[1].message[SESSION_LOG_LINE_MAX - 1] = '\0';
  buffer[1].sequence = 2;

  session_log_entry_t *merged = NULL;
  size_t count = log_file_parser_merge_and_dedupe(buffer, 2, NULL, 0, &merged);

  cr_assert_eq(count, 2);
  cr_assert_not_null(merged);
  SAFE_FREE(merged);
}

Test(merge_dedupe, dedup_identical_timestamps) {
  session_log_entry_t buffer[1];
  strncpy(buffer[0].message, "[2026-02-08 12:34:56.789] [INFO ] duplicated message", SESSION_LOG_LINE_MAX - 1);
  buffer[0].message[SESSION_LOG_LINE_MAX - 1] = '\0';
  buffer[0].sequence = 1;

  session_log_entry_t file[1];
  strncpy(file[0].message, "[2026-02-08 12:34:56.789] [INFO ] duplicated message", SESSION_LOG_LINE_MAX - 1);
  file[0].message[SESSION_LOG_LINE_MAX - 1] = '\0';
  file[0].sequence = 0;

  session_log_entry_t *merged = NULL;
  size_t count = log_file_parser_merge_and_dedupe(buffer, 1, file, 1, &merged);

  cr_assert_eq(count, 1, "Should deduplicate entries with same timestamp");
  SAFE_FREE(merged);
}

Test(merge_dedupe, chronological_order) {
  session_log_entry_t buffer[2];
  strncpy(buffer[0].message, "msg3", SESSION_LOG_LINE_MAX - 1);
  buffer[0].message[SESSION_LOG_LINE_MAX - 1] = '\0';
  buffer[0].sequence = 3;
  strncpy(buffer[1].message, "msg1", SESSION_LOG_LINE_MAX - 1);
  buffer[1].message[SESSION_LOG_LINE_MAX - 1] = '\0';
  buffer[1].sequence = 1;

  session_log_entry_t file[1];
  strncpy(file[0].message, "msg2", SESSION_LOG_LINE_MAX - 1);
  file[0].message[SESSION_LOG_LINE_MAX - 1] = '\0';
  file[0].sequence = 2;

  session_log_entry_t *merged = NULL;
  size_t count = log_file_parser_merge_and_dedupe(buffer, 2, file, 1, &merged);

  cr_assert_eq(count, 3);
  cr_assert(merged[0].sequence < merged[1].sequence);
  cr_assert(merged[1].sequence < merged[2].sequence);
  SAFE_FREE(merged);
}

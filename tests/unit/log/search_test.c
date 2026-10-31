#include <criterion/criterion.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <ascii-chat/util/string.h>

static void search_setup(void) {
  grep_clear_patterns();
  cr_assert_eq(log_search_init(), ASCIICHAT_OK);
}
static void search_cleanup(void) {
  log_search_destroy();
  grep_destroy();
}
TestSuite(log_search_behavior, .init = search_setup, .fini = search_cleanup);

static void type_pattern(const char *pattern) {
  log_search_enter_mode();
  for (const char *p = pattern; *p; ++p)
    cr_assert_eq(log_search_handle_key((keyboard_key_t)*p), ASCIICHAT_OK);
  cr_assert_eq(log_search_get_input_len(), strlen(pattern));
  cr_assert_eq(log_search_get_cursor_position(), strlen(pattern));
  cr_assert_str_eq(log_search_get_input_buffer(), pattern);
  log_search_exit_mode(true);
  cr_assert(log_search_is_active());
}

Test(log_search_behavior, editing_cancel_and_signal_state) {
  cr_assert_not(log_search_is_entering());
  cr_assert_not(log_search_is_active());
  cr_assert(log_search_should_handle('/'));
  cr_assert_eq(log_search_handle_key('/'), ASCIICHAT_OK);
  cr_assert(log_search_is_entering_atomic());
  log_search_handle_key('a');
  log_search_handle_key('c');
  log_search_handle_key(KEY_LEFT);
  log_search_handle_key('b');
  cr_assert_str_eq(log_search_get_input_buffer(), "abc");
  cr_assert(log_search_needs_rerender());
  cr_assert_not(log_search_needs_rerender());
  log_search_signal_cancel();
  cr_assert(log_search_check_signal_cancel());
  cr_assert_not(log_search_check_signal_cancel());
  log_search_handle_key(KEY_ESCAPE);
  cr_assert_not(log_search_is_entering());
  cr_assert_not(log_search_is_active());
}

Test(log_search_behavior, regex_filters_ansi_messages_and_reports_match_offsets) {
  session_log_buffer_t *buffer = session_log_buffer_create();
  cr_assert_not_null(buffer);
  session_log_buffer_append(buffer, "ready");
  session_log_buffer_append(buffer, "\033[31merror 42\033[0m");
  session_log_buffer_append(buffer, "error 7");
  type_pattern("error [0-9]+");
  session_log_entry_t *entries = NULL;
  size_t count = 0, start = 0, length = 0;
  cr_assert_eq(log_search_filter_buffer(buffer, &entries, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 2);
  cr_assert(log_search_get_match_info("prefix error 42 suffix", &start, &length));
  cr_assert_eq(start, 7);
  cr_assert_eq(length, 8);
  cr_assert_not(log_search_get_match_info("ready", &start, &length));
  cr_assert_not(log_search_get_match_info(NULL, &start, &length));
  SAFE_FREE(entries);
  log_search_exit_mode(false);
  cr_assert_eq(log_search_filter_buffer(buffer, &entries, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 3);
  SAFE_FREE(entries);
  session_log_buffer_destroy(buffer);
}

Test(log_search_behavior, fixed_case_insensitive_unicode_and_inversion) {
  session_log_buffer_t *buffer = session_log_buffer_create();
  cr_assert_not_null(buffer);
  session_log_buffer_append(buffer, "prefix NEEDLE suffix");
  session_log_buffer_append(buffer, "other");
  type_pattern("/needle/Fi");
  cr_assert(log_search_get_case_insensitive());
  session_log_entry_t *entries = NULL;
  size_t count = 0, start = 0, length = 0;
  cr_assert_eq(log_search_filter_buffer(buffer, &entries, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 1);
  SAFE_FREE(entries);
  cr_assert(log_search_get_match_info("é NEEDLE", &start, &length));
  cr_assert_eq(start, 2);
  cr_assert_eq(length, 6);
  log_search_exit_mode(false);
  type_pattern("/needle/FiI");
  cr_assert_eq(log_search_filter_buffer(buffer, &entries, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 1);
  cr_assert_str_eq(entries[0].message, "other");
  SAFE_FREE(entries);
  session_log_buffer_destroy(buffer);
}

static unsigned background_count(const char *text) {
  unsigned count = 0;
  for (const char *p = text; (p = strstr(p, "\033[48;2;")) != NULL; ++p)
    ++count;
  return count;
}
Test(log_search_behavior, interactive_global_highlighting_preserves_colored_unicode_text) {
  const char *patterns[] = {"/needle/ig", "/needle/igF"};
  const char *plain = "é NEEDLE middle needle end";
  const char *colored = "\033[31mé NEEDLE\033[0m middle \033[34mneedle end\033[0m";
  for (size_t i = 0; i < 2; ++i) {
    type_pattern(patterns[i]);
    cr_assert(log_search_get_global_highlight());
    size_t start = 0, length = 0;
    cr_assert(log_search_get_match_info(plain, &start, &length));
    char *result = grep_highlight_colored_copy(colored, plain, start, length);
    cr_assert_not_null(result);
    cr_assert_geq(background_count(result), 2, "pattern %s", patterns[i]);
    char stripped[256];
    strip_ansi_codes(result, stripped, sizeof(stripped));
    cr_assert_str_eq(stripped, plain);
    cr_assert_not_null(strstr(result, "\033[31m"));
    cr_assert_not_null(strstr(result, "\033[34m"));
    SAFE_FREE(result);
    log_search_exit_mode(false);
  }
}
Test(log_search_behavior, single_highlight_survives_embedded_reset_and_zero_width_matches_terminate) {
  const char *plain = "before needle after";
  const char *colored = "before ne\033[0med\033[00mle after";
  char *result = grep_highlight_colored_copy(colored, plain, 7, 6);
  cr_assert_not_null(result);
  cr_assert_eq(background_count(result), 3);
  char stripped[256];
  strip_ansi_codes(result, stripped, sizeof(stripped));
  cr_assert_str_eq(stripped, plain);
  SAFE_FREE(result);
  cr_assert_eq(grep_init("/(?=x)/g"), ASCIICHAT_OK);
  result = grep_highlight_colored_copy("x x", "x x", 0, 1);
  cr_assert_not_null(result);
  strip_ansi_codes(result, stripped, sizeof(stripped));
  cr_assert_str_eq(stripped, "x x");
  SAFE_FREE(result);
}

Test(log_search_behavior, unicode_typing_paste_and_word_deletion) {
  cr_assert_eq(log_search_insert_text("x"), ERROR_INVALID_STATE);
  cr_assert_eq(log_search_insert_text(NULL), ERROR_INVALID_PARAM);
  log_search_enter_mode();
  log_search_handle_key((keyboard_key_t)(KEY_UNICODE_BASE + 0x754c));
  log_search_handle_key((keyboard_key_t)(KEY_UNICODE_BASE + 0x1f600));
  cr_assert_str_eq(log_search_get_input_buffer(), "界😀");
  log_search_handle_key(KEY_LEFT);
  cr_assert_eq(log_search_get_cursor_position(), 3);
  cr_assert_eq(log_search_insert_text("é\n?=o"), ASCIICHAT_OK);
  cr_assert_str_eq(log_search_get_input_buffer(), "界é ?=o😀");
  cr_assert(log_search_is_entering());
  cr_assert_eq(log_search_insert_text("\xc0\xaf"), ERROR_INVALID_PARAM);
  cr_assert_str_eq(log_search_get_input_buffer(), "界é ?=o😀");
  char too_long[256];
  memset(too_long, 'x', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = 0;
  cr_assert_eq(log_search_insert_text(too_long), ERROR_BUFFER_OVERFLOW);
  cr_assert_str_eq(log_search_get_input_buffer(), "界é ?=o😀");
  log_search_handle_key(KEY_END);
  log_search_handle_key(23);
  cr_assert_str_eq(log_search_get_input_buffer(), "界é ");
}

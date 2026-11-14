#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/log/search.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <ascii-chat/util/string.h>

static void search_setup(void) {
  grep_clear_patterns();
  ASCIICHAT_REQUIRE_EQ(log_search_init(), ASCIICHAT_OK);
}
static void search_cleanup(void) {
  log_search_destroy();
  grep_destroy();
}
static void type_pattern(const char *pattern) {
  log_search_enter_mode();
  for (const char *p = pattern; *p; ++p)
    ASCIICHAT_REQUIRE_EQ(log_search_handle_key((keyboard_key_t)*p), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(log_search_get_input_len(), strlen(pattern));
  ASCIICHAT_REQUIRE_EQ(log_search_get_cursor_position(), strlen(pattern));
  ASCIICHAT_REQUIRE_STR_EQ(log_search_get_input_buffer(), pattern);
  log_search_exit_mode(true);
  ASCIICHAT_REQUIRE(log_search_is_active());
}

TEST_CASE("log_search_behavior/editing_cancel_and_signal_state", "[log_search_behavior]") {
  ASCIICHAT_REQUIRE_NOT(log_search_is_entering());
  ASCIICHAT_REQUIRE_NOT(log_search_is_active());
  ASCIICHAT_REQUIRE(log_search_should_handle('/'));
  ASCIICHAT_REQUIRE_EQ(log_search_handle_key((keyboard_key_t)'/'), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(log_search_is_entering_atomic());
  log_search_handle_key((keyboard_key_t)'a');
  log_search_handle_key((keyboard_key_t)'c');
  log_search_handle_key(KEY_LEFT);
  log_search_handle_key((keyboard_key_t)'b');
  ASCIICHAT_REQUIRE_STR_EQ(log_search_get_input_buffer(), "abc");
  ASCIICHAT_REQUIRE(log_search_needs_rerender());
  ASCIICHAT_REQUIRE_NOT(log_search_needs_rerender());
  log_search_signal_cancel();
  ASCIICHAT_REQUIRE(log_search_check_signal_cancel());
  ASCIICHAT_REQUIRE_NOT(log_search_check_signal_cancel());
  log_search_handle_key(KEY_ESCAPE);
  ASCIICHAT_REQUIRE_NOT(log_search_is_entering());
  ASCIICHAT_REQUIRE_NOT(log_search_is_active());
}

TEST_CASE("log_search_behavior/regex_filters_ansi_messages_and_reports_match_offsets", "[log_search_behavior]") {
  session_log_buffer_t *buffer = session_log_buffer_create();
  ASCIICHAT_REQUIRE_NOT_NULL(buffer);
  session_log_buffer_append(buffer, "ready");
  session_log_buffer_append(buffer, "\033[31merror 42\033[0m");
  session_log_buffer_append(buffer, "error 7");
  type_pattern("error [0-9]+");
  session_log_entry_t *entries = NULL;
  size_t count = 0, start = 0, length = 0;
  ASCIICHAT_REQUIRE_EQ(log_search_filter_buffer(buffer, &entries, &count), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(count, 2);
  ASCIICHAT_REQUIRE(log_search_get_match_info("prefix error 42 suffix", &start, &length));
  ASCIICHAT_REQUIRE_EQ(start, 7);
  ASCIICHAT_REQUIRE_EQ(length, 8);
  ASCIICHAT_REQUIRE_NOT(log_search_get_match_info("ready", &start, &length));
  ASCIICHAT_REQUIRE_NOT(log_search_get_match_info(NULL, &start, &length));
  SAFE_FREE(entries);
  log_search_exit_mode(false);
  ASCIICHAT_REQUIRE_EQ(log_search_filter_buffer(buffer, &entries, &count), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(count, 3);
  SAFE_FREE(entries);
  session_log_buffer_destroy(buffer);
}

TEST_CASE("log_search_behavior/fixed_case_insensitive_unicode_and_inversion", "[log_search_behavior]") {
  session_log_buffer_t *buffer = session_log_buffer_create();
  ASCIICHAT_REQUIRE_NOT_NULL(buffer);
  session_log_buffer_append(buffer, "prefix NEEDLE suffix");
  session_log_buffer_append(buffer, "other");
  type_pattern("/needle/Fi");
  ASCIICHAT_REQUIRE(log_search_get_case_insensitive());
  session_log_entry_t *entries = NULL;
  size_t count = 0, start = 0, length = 0;
  ASCIICHAT_REQUIRE_EQ(log_search_filter_buffer(buffer, &entries, &count), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(count, 1);
  SAFE_FREE(entries);
  ASCIICHAT_REQUIRE(log_search_get_match_info("é NEEDLE", &start, &length));
  ASCIICHAT_REQUIRE_EQ(start, 2);
  ASCIICHAT_REQUIRE_EQ(length, 6);
  log_search_exit_mode(false);
  type_pattern("/needle/FiI");
  ASCIICHAT_REQUIRE_EQ(log_search_filter_buffer(buffer, &entries, &count), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(count, 1);
  ASCIICHAT_REQUIRE_STR_EQ(entries[0].message, "other");
  SAFE_FREE(entries);
  session_log_buffer_destroy(buffer);
}

static unsigned background_count(const char *text) {
  unsigned count = 0;
  for (const char *p = text; (p = strstr(p, "\033[48;2;")) != NULL; ++p)
    ++count;
  return count;
}
TEST_CASE("log_search_behavior/interactive_global_highlighting_preserves_colored_unicode_text", "[log_search_behavior]") {
  const char *patterns[] = {"/needle/ig", "/needle/igF"};
  const char *plain = "é NEEDLE middle needle end";
  const char *colored = "\033[31mé NEEDLE\033[0m middle \033[34mneedle end\033[0m";
  for (size_t i = 0; i < 2; ++i) {
    type_pattern(patterns[i]);
    ASCIICHAT_REQUIRE(log_search_get_global_highlight());
    size_t start = 0, length = 0;
    ASCIICHAT_REQUIRE(log_search_get_match_info(plain, &start, &length));
    char *result = grep_highlight_colored_copy(colored, plain, start, length);
    ASCIICHAT_REQUIRE_NOT_NULL(result);
    ASCIICHAT_REQUIRE_GE(background_count(result), 2, "pattern %s", patterns[i]);
    char stripped[256];
    strip_ansi_codes(result, stripped, sizeof(stripped));
    ASCIICHAT_REQUIRE_STR_EQ(stripped, plain);
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(result, "\033[31m"));
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(result, "\033[34m"));
    SAFE_FREE(result);
    log_search_exit_mode(false);
  }
}
TEST_CASE("log_search_behavior/single_highlight_survives_embedded_reset_and_zero_width_matches_terminate", "[log_search_behavior]") {
  const char *plain = "before needle after";
  const char *colored = "before ne\033[0med\033[00mle after";
  char *result = grep_highlight_colored_copy(colored, plain, 7, 6);
  ASCIICHAT_REQUIRE_NOT_NULL(result);
  ASCIICHAT_REQUIRE_EQ(background_count(result), 3);
  char stripped[256];
  strip_ansi_codes(result, stripped, sizeof(stripped));
  ASCIICHAT_REQUIRE_STR_EQ(stripped, plain);
  SAFE_FREE(result);
  ASCIICHAT_REQUIRE_EQ(grep_init("/(?=x)/g"), ASCIICHAT_OK);
  result = grep_highlight_colored_copy("x x", "x x", 0, 1);
  ASCIICHAT_REQUIRE_NOT_NULL(result);
  strip_ansi_codes(result, stripped, sizeof(stripped));
  ASCIICHAT_REQUIRE_STR_EQ(stripped, "x x");
  SAFE_FREE(result);
}

TEST_CASE("log_search_behavior/unicode_typing_paste_and_word_deletion", "[log_search_behavior]") {
  ASCIICHAT_REQUIRE_EQ(log_search_insert_text("x"), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(log_search_insert_text(NULL), ERROR_INVALID_PARAM);
  log_search_enter_mode();
  log_search_handle_key((keyboard_key_t)(0x754c));
  log_search_handle_key((keyboard_key_t)(0x1f600));
  ASCIICHAT_REQUIRE_STR_EQ(log_search_get_input_buffer(), "界😀");
  log_search_handle_key(KEY_LEFT);
  ASCIICHAT_REQUIRE_EQ(log_search_get_cursor_position(), 3);
  ASCIICHAT_REQUIRE_EQ(log_search_insert_text("é\n?=o"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(log_search_get_input_buffer(), "界é ?=o😀");
  ASCIICHAT_REQUIRE(log_search_is_entering());
  ASCIICHAT_REQUIRE_EQ(log_search_insert_text("\xc0\xaf"), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_STR_EQ(log_search_get_input_buffer(), "界é ?=o😀");
  char too_long[256];
  memset(too_long, 'x', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = 0;
  ASCIICHAT_REQUIRE_EQ(log_search_insert_text(too_long), ERROR_BUFFER_OVERFLOW);
  ASCIICHAT_REQUIRE_STR_EQ(log_search_get_input_buffer(), "界é ?=o😀");
  log_search_handle_key(KEY_END);
  log_search_handle_key((keyboard_key_t)23);
  ASCIICHAT_REQUIRE_STR_EQ(log_search_get_input_buffer(), "界é ");
}

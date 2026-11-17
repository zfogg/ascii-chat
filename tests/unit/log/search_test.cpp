#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/log/search.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <ascii-chat/util/string.h>

static void search_setup(void) {
  grep_clear_patterns();
  REQUIRE((log_search_init()) == (ASCIICHAT_OK));
}
static void search_cleanup(void) {
  log_search_destroy();
  grep_destroy();
}
static void type_pattern(const char *pattern) {
  log_search_enter_mode();
  for (const char *p = pattern; *p; ++p)
    REQUIRE((log_search_handle_key((keyboard_key_t)*p)) == (ASCIICHAT_OK));
  REQUIRE((log_search_get_input_len()) == (strlen(pattern)));
  REQUIRE((log_search_get_cursor_position()) == (strlen(pattern)));
  REQUIRE(std::strcmp((log_search_get_input_buffer()), (pattern)) == 0);
  log_search_exit_mode(true);
  REQUIRE(log_search_is_active());
}

TEST_CASE("log_search_behavior/editing_cancel_and_signal_state", "[log_search_behavior] [unit]") {
  REQUIRE(!(log_search_is_entering()));
  REQUIRE(!(log_search_is_active()));
  REQUIRE(log_search_should_handle('/'));
  REQUIRE((log_search_handle_key((keyboard_key_t)'/')) == (ASCIICHAT_OK));
  REQUIRE(log_search_is_entering_atomic());
  log_search_handle_key((keyboard_key_t)'a');
  log_search_handle_key((keyboard_key_t)'c');
  log_search_handle_key(KEY_LEFT);
  log_search_handle_key((keyboard_key_t)'b');
  REQUIRE(std::strcmp((log_search_get_input_buffer()), ("abc")) == 0);
  REQUIRE(log_search_needs_rerender());
  REQUIRE(!(log_search_needs_rerender()));
  log_search_signal_cancel();
  REQUIRE(log_search_check_signal_cancel());
  REQUIRE(!(log_search_check_signal_cancel()));
  log_search_handle_key(KEY_ESCAPE);
  REQUIRE(!(log_search_is_entering()));
  REQUIRE(!(log_search_is_active()));
}

TEST_CASE("log_search_behavior/regex_filters_ansi_messages_and_reports_match_offsets", "[log_search_behavior] [unit]") {
  session_log_buffer_t *buffer = session_log_buffer_create();
  REQUIRE((buffer) != nullptr);
  session_log_buffer_append(buffer, "ready");
  session_log_buffer_append(buffer, "\033[31merror 42\033[0m");
  session_log_buffer_append(buffer, "error 7");
  type_pattern("error [0-9]+");
  session_log_entry_t *entries = NULL;
  size_t count = 0, start = 0, length = 0;
  REQUIRE((log_search_filter_buffer(buffer, &entries, &count)) == (ASCIICHAT_OK));
  REQUIRE((count) == (2));
  REQUIRE(log_search_get_match_info("prefix error 42 suffix", &start, &length));
  REQUIRE((start) == (7));
  REQUIRE((length) == (8));
  REQUIRE(!(log_search_get_match_info("ready", &start, &length)));
  REQUIRE(!(log_search_get_match_info(NULL, &start, &length)));
  SAFE_FREE(entries);
  log_search_exit_mode(false);
  REQUIRE((log_search_filter_buffer(buffer, &entries, &count)) == (ASCIICHAT_OK));
  REQUIRE((count) == (3));
  SAFE_FREE(entries);
  session_log_buffer_destroy(buffer);
}

TEST_CASE("log_search_behavior/fixed_case_insensitive_unicode_and_inversion", "[log_search_behavior] [unit]") {
  session_log_buffer_t *buffer = session_log_buffer_create();
  REQUIRE((buffer) != nullptr);
  session_log_buffer_append(buffer, "prefix NEEDLE suffix");
  session_log_buffer_append(buffer, "other");
  type_pattern("/needle/Fi");
  REQUIRE(log_search_get_case_insensitive());
  session_log_entry_t *entries = NULL;
  size_t count = 0, start = 0, length = 0;
  REQUIRE((log_search_filter_buffer(buffer, &entries, &count)) == (ASCIICHAT_OK));
  REQUIRE((count) == (1));
  SAFE_FREE(entries);
  REQUIRE(log_search_get_match_info("é NEEDLE", &start, &length));
  REQUIRE((start) == (2));
  REQUIRE((length) == (6));
  log_search_exit_mode(false);
  type_pattern("/needle/FiI");
  REQUIRE((log_search_filter_buffer(buffer, &entries, &count)) == (ASCIICHAT_OK));
  REQUIRE((count) == (1));
  REQUIRE(std::strcmp((entries[0].message), ("other")) == 0);
  SAFE_FREE(entries);
  session_log_buffer_destroy(buffer);
}

static unsigned background_count(const char *text) {
  unsigned count = 0;
  for (const char *p = text; (p = strstr(p, "\033[48;2;")) != NULL; ++p)
    ++count;
  return count;
}
TEST_CASE("log_search_behavior/interactive_global_highlighting_preserves_colored_unicode_text", "[log_search_behavior] [unit]") {
  const char *patterns[] = {"/needle/ig", "/needle/igF"};
  const char *plain = "é NEEDLE middle needle end";
  const char *colored = "\033[31mé NEEDLE\033[0m middle \033[34mneedle end\033[0m";
  for (size_t i = 0; i < 2; ++i) {
    type_pattern(patterns[i]);
    REQUIRE(log_search_get_global_highlight());
    size_t start = 0, length = 0;
    REQUIRE(log_search_get_match_info(plain, &start, &length));
    char *result = grep_highlight_colored_copy(colored, plain, start, length);
    REQUIRE((result) != nullptr);
    REQUIRE((background_count(result)) >= (2));
    char stripped[256];
    strip_ansi_codes(result, stripped, sizeof(stripped));
    REQUIRE(std::strcmp((stripped), (plain)) == 0);
    REQUIRE((strstr(result, "\033[31m")) != nullptr);
    REQUIRE((strstr(result, "\033[34m")) != nullptr);
    SAFE_FREE(result);
    log_search_exit_mode(false);
  }
}
TEST_CASE("log_search_behavior/single_highlight_survives_embedded_reset_and_zero_width_matches_terminate", "[log_search_behavior] [unit]") {
  const char *plain = "before needle after";
  const char *colored = "before ne\033[0med\033[00mle after";
  char *result = grep_highlight_colored_copy(colored, plain, 7, 6);
  REQUIRE((result) != nullptr);
  REQUIRE((background_count(result)) == (3));
  char stripped[256];
  strip_ansi_codes(result, stripped, sizeof(stripped));
  REQUIRE(std::strcmp((stripped), (plain)) == 0);
  SAFE_FREE(result);
  REQUIRE((grep_init("/(?=x)/g")) == (ASCIICHAT_OK));
  result = grep_highlight_colored_copy("x x", "x x", 0, 1);
  REQUIRE((result) != nullptr);
  strip_ansi_codes(result, stripped, sizeof(stripped));
  REQUIRE(std::strcmp((stripped), ("x x")) == 0);
  SAFE_FREE(result);
}

TEST_CASE("log_search_behavior/unicode_typing_paste_and_word_deletion", "[log_search_behavior] [unit]") {
  REQUIRE((log_search_insert_text("x")) == (ERROR_INVALID_STATE));
  REQUIRE((log_search_insert_text(NULL)) == (ERROR_INVALID_PARAM));
  log_search_enter_mode();
  log_search_handle_key((keyboard_key_t)(0x754c));
  log_search_handle_key((keyboard_key_t)(0x1f600));
  REQUIRE(std::strcmp((log_search_get_input_buffer()), ("界😀")) == 0);
  log_search_handle_key(KEY_LEFT);
  REQUIRE((log_search_get_cursor_position()) == (3));
  REQUIRE((log_search_insert_text("é\n?=o")) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((log_search_get_input_buffer()), ("界é ?=o😀")) == 0);
  REQUIRE(log_search_is_entering());
  REQUIRE((log_search_insert_text("\xc0\xaf")) == (ERROR_INVALID_PARAM));
  REQUIRE(std::strcmp((log_search_get_input_buffer()), ("界é ?=o😀")) == 0);
  char too_long[256];
  memset(too_long, 'x', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = 0;
  REQUIRE((log_search_insert_text(too_long)) == (ERROR_BUFFER_OVERFLOW));
  REQUIRE(std::strcmp((log_search_get_input_buffer()), ("界é ?=o😀")) == 0);
  log_search_handle_key(KEY_END);
  log_search_handle_key((keyboard_key_t)23);
  REQUIRE(std::strcmp((log_search_get_input_buffer()), ("界é ")) == 0);
}

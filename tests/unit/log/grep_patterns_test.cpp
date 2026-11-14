#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <ascii-chat/util/string.h>
TEST_CASE("grep_patterns/context_and_inverse_patterns_select_expected_lines", "[grep_patterns]") {
  ASCIICHAT_REQUIRE_EQ(grep_init("/error/iA1"), ASCIICHAT_OK);
  size_t start = 99, length = 99;
  ASCIICHAT_REQUIRE_NOT(grep_should_output("ordinary line", &start, &length));
  ASCIICHAT_REQUIRE(grep_should_output("an ERROR occurred", &start, &length));
  ASCIICHAT_REQUIRE_EQ(start, 3);
  ASCIICHAT_REQUIRE_EQ(length, 5);
  ASCIICHAT_REQUIRE(grep_should_output("after context", &start, &length));
  ASCIICHAT_REQUIRE_NOT(grep_should_output("outside context", &start, &length));
  grep_clear_patterns();
  ASCIICHAT_REQUIRE_EQ(grep_init("/secret/I"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(grep_should_output("secret", &start, &length));
  ASCIICHAT_REQUIRE(grep_should_output("public", &start, &length));
}

TEST_CASE("grep_patterns/saved_pattern_set_restores_case_and_fixed_string_semantics", "[grep_patterns]") {
  ASCIICHAT_REQUIRE_EQ(grep_init("/[a.b]/iF"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_init("second"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_get_pattern_count(), 2);
  ASCIICHAT_REQUIRE_EQ(grep_save_patterns(), ASCIICHAT_OK);
  grep_clear_patterns();
  ASCIICHAT_REQUIRE_EQ(grep_init("replacement"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_restore_patterns(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_get_pattern_count(), 2);
  size_t start, length;
  ASCIICHAT_REQUIRE(grep_should_output("literal [A.B] suffix", &start, &length));
  ASCIICHAT_REQUIRE_EQ(start, 8);
  ASCIICHAT_REQUIRE_EQ(length, 5);
  ASCIICHAT_REQUIRE_NOT(grep_should_output("aXb", &start, &length));
  ASCIICHAT_REQUIRE(grep_should_output("second", &start, &length));
}

TEST_CASE("grep_patterns/global_highlighting_preserves_text_and_existing_foreground_colors", "[grep_patterns]") {
  const char *patterns[] = {"/foo/ig", "/foo/igF"};
  for (size_t i = 0; i < 2; ++i) {
    grep_clear_patterns();
    ASCIICHAT_REQUIRE_EQ(grep_init(patterns[i]), ASCIICHAT_OK);
    const char *plain = "foo FOO suffix";
    const char *colored = "\x1b[31mfoo FOO suffix\x1b[0m";
    char *highlighted = grep_highlight_colored_copy(colored, plain, 0, 3);
    ASCIICHAT_REQUIRE_NOT_NULL(highlighted);
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(highlighted, "\x1b[31m"));
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(highlighted, "foo"));
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(highlighted, "FOO"));
    const char *first = strstr(highlighted, "\x1b[48;2;");
    ASCIICHAT_REQUIRE_NOT_NULL(first);
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(first + 1, "\x1b[48;2;"));
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(highlighted, " suffix\x1b[0m"));
    SAFE_FREE(highlighted);
  }
}

TEST_CASE("grep_patterns/flag_parser_and_invalid_regex_fail_without_losing_existing_filters", "[grep_patterns]") {
  grep_parse_result_t parsed = grep_parse_pattern("/one.*two/imsxgIC3B2A4");
  ASCIICHAT_REQUIRE(parsed.valid);
  ASCIICHAT_REQUIRE(parsed.case_insensitive);
  ASCIICHAT_REQUIRE(parsed.invert);
  ASCIICHAT_REQUIRE(parsed.global_flag);
  ASCIICHAT_REQUIRE_EQ(parsed.context_before, 2);
  ASCIICHAT_REQUIRE_EQ(parsed.context_after, 4);
  ASCIICHAT_REQUIRE_EQ(grep_init("valid"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_init("["), ERROR_INVALID_PARAM);
  size_t start, length;
  ASCIICHAT_REQUIRE(grep_should_output("valid", &start, &length));
  ASCIICHAT_REQUIRE_EQ(grep_get_pattern_count(), 1);
}

TEST_CASE("grep_patterns/dense_global_fixed_matches_preserve_long_lines_without_overflow", "[grep_patterns]") {
  char line[6001];
  memset(line, 'X', sizeof(line) - 1);
  line[sizeof(line) - 1] = 0;
  ASCIICHAT_REQUIRE_EQ(grep_init("/X/gF"), ASCIICHAT_OK);
  char *highlighted = grep_highlight_colored_copy(line, line, 0, 1);
  ASCIICHAT_REQUIRE_NOT_NULL(highlighted);
  ASCIICHAT_REQUIRE_STR_EQ(highlighted, line);
  SAFE_FREE(highlighted);
}

TEST_CASE("grep_patterns/plain_global_and_single_highlights_preserve_all_original_text", "[grep_patterns]") {
  const char *line = "prefix foo middle FOO suffix";
  ASCIICHAT_REQUIRE_EQ(grep_init("/foo/ig"), ASCIICHAT_OK);
  const char *highlighted = grep_highlight(line, 7, 3);
  ASCIICHAT_REQUIRE_NOT_NULL(highlighted);
  const char *first = strstr(highlighted, "\033[48;2;");
  ASCIICHAT_REQUIRE_NOT_NULL(first);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(first + 1, "\033[48;2;"));
  char plain[256];
  strip_ansi_codes(highlighted, plain, sizeof(plain));
  ASCIICHAT_REQUIRE_STR_EQ(plain, line);
  grep_destroy();
  highlighted = grep_highlight(line, 7, 3);
  ASCIICHAT_REQUIRE_NOT_NULL(highlighted);
  strip_ansi_codes(highlighted, plain, sizeof(plain));
  ASCIICHAT_REQUIRE_STR_EQ(plain, line);
  ASCIICHAT_REQUIRE_EQ(grep_highlight(line, strlen(line), 1), line);
  ASCIICHAT_REQUIRE_EQ(grep_highlight(line, 0, 0), line);
  ASCIICHAT_REQUIRE_NULL(grep_highlight(NULL, 0, 1));
  ASCIICHAT_REQUIRE_EQ(grep_init("/(?=o)/g"), ASCIICHAT_OK);
  highlighted = grep_highlight("foo", 1, 1);
  strip_ansi_codes(highlighted, plain, sizeof(plain));
  ASCIICHAT_REQUIRE_STR_EQ(plain, "foo");
}
TEST_CASE("grep_patterns/saving_again_replaces_previous_snapshot_and_empty_restore_disables_filtering", "[grep_patterns]") {
  ASCIICHAT_REQUIRE_EQ(grep_init("first"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_save_patterns(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_init("/second/iF"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_save_patterns(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_init("third"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_restore_patterns(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_get_pattern_count(), 2);
  ASCIICHAT_REQUIRE_STR_EQ(grep_get_last_pattern(), "/second/iF");
  size_t start, length;
  ASCIICHAT_REQUIRE_NOT(grep_should_output("third", &start, &length));
  ASCIICHAT_REQUIRE(grep_should_output("SECOND", &start, &length));
  grep_clear_patterns();
  ASCIICHAT_REQUIRE_NULL(grep_get_last_pattern());
  ASCIICHAT_REQUIRE_EQ(grep_save_patterns(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_init("temporary"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_restore_patterns(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(grep_get_pattern_count(), 0);
  ASCIICHAT_REQUIRE(grep_should_output("unfiltered", &start, &length));
}

TEST_CASE("grep_patterns/single_plain_match_resets_background_before_suffix", "[grep_patterns]") {
  const char *result = grep_highlight("[WARN] warning details", 1, 4);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(result, "WARN\033[49m] warning details"));
}

TEST_CASE("grep_patterns/colored_utf8_match_uses_plain_byte_offsets", "[grep_patterns]") {
  const char *plain = "é [WARN] detail";
  const char *colored = "\033[33mé [WARN] detail\033[0m";
  const char *result = grep_highlight_colored(colored, plain, strlen("é ["), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(result, "WARN\033[49m] detail"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(result, "\033[33m"));
}

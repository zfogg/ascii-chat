#include <criterion/criterion.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <ascii-chat/util/string.h>

TestSuite(grep_patterns, .fini = grep_destroy);

Test(grep_patterns, context_and_inverse_patterns_select_expected_lines) {
  cr_assert_eq(grep_init("/error/iA1"), ASCIICHAT_OK);
  size_t start = 99, length = 99;
  cr_assert_not(grep_should_output("ordinary line", &start, &length));
  cr_assert(grep_should_output("an ERROR occurred", &start, &length));
  cr_assert_eq(start, 3);
  cr_assert_eq(length, 5);
  cr_assert(grep_should_output("after context", &start, &length));
  cr_assert_not(grep_should_output("outside context", &start, &length));
  grep_clear_patterns();
  cr_assert_eq(grep_init("/secret/I"), ASCIICHAT_OK);
  cr_assert_not(grep_should_output("secret", &start, &length));
  cr_assert(grep_should_output("public", &start, &length));
}

Test(grep_patterns, saved_pattern_set_restores_case_and_fixed_string_semantics) {
  cr_assert_eq(grep_init("/[a.b]/iF"), ASCIICHAT_OK);
  cr_assert_eq(grep_init("second"), ASCIICHAT_OK);
  cr_assert_eq(grep_get_pattern_count(), 2);
  cr_assert_eq(grep_save_patterns(), ASCIICHAT_OK);
  grep_clear_patterns();
  cr_assert_eq(grep_init("replacement"), ASCIICHAT_OK);
  cr_assert_eq(grep_restore_patterns(), ASCIICHAT_OK);
  cr_assert_eq(grep_get_pattern_count(), 2);
  size_t start, length;
  cr_assert(grep_should_output("literal [A.B] suffix", &start, &length));
  cr_assert_eq(start, 8);
  cr_assert_eq(length, 5);
  cr_assert_not(grep_should_output("aXb", &start, &length));
  cr_assert(grep_should_output("second", &start, &length));
}

Test(grep_patterns, global_highlighting_preserves_text_and_existing_foreground_colors) {
  const char *patterns[] = {"/foo/ig", "/foo/igF"};
  for (size_t i = 0; i < 2; ++i) {
    grep_clear_patterns();
    cr_assert_eq(grep_init(patterns[i]), ASCIICHAT_OK);
    const char *plain = "foo FOO suffix";
    const char *colored = "\x1b[31mfoo FOO suffix\x1b[0m";
    char *highlighted = grep_highlight_colored_copy(colored, plain, 0, 3);
    cr_assert_not_null(highlighted);
    cr_assert_not_null(strstr(highlighted, "\x1b[31m"));
    cr_assert_not_null(strstr(highlighted, "foo"));
    cr_assert_not_null(strstr(highlighted, "FOO"));
    const char *first = strstr(highlighted, "\x1b[48;2;");
    cr_assert_not_null(first);
    cr_assert_not_null(strstr(first + 1, "\x1b[48;2;"));
    cr_assert_not_null(strstr(highlighted, " suffix\x1b[0m"));
    SAFE_FREE(highlighted);
  }
}

Test(grep_patterns, flag_parser_and_invalid_regex_fail_without_losing_existing_filters) {
  grep_parse_result_t parsed = grep_parse_pattern("/one.*two/imsxgIC3B2A4");
  cr_assert(parsed.valid);
  cr_assert(parsed.case_insensitive);
  cr_assert(parsed.invert);
  cr_assert(parsed.global_flag);
  cr_assert_eq(parsed.context_before, 2);
  cr_assert_eq(parsed.context_after, 4);
  cr_assert_eq(grep_init("valid"), ASCIICHAT_OK);
  cr_assert_eq(grep_init("["), ERROR_INVALID_PARAM);
  size_t start, length;
  cr_assert(grep_should_output("valid", &start, &length));
  cr_assert_eq(grep_get_pattern_count(), 1);
}

Test(grep_patterns, dense_global_fixed_matches_preserve_long_lines_without_overflow) {
  char line[6001];
  memset(line, 'X', sizeof(line) - 1);
  line[sizeof(line) - 1] = 0;
  cr_assert_eq(grep_init("/X/gF"), ASCIICHAT_OK);
  char *highlighted = grep_highlight_colored_copy(line, line, 0, 1);
  cr_assert_not_null(highlighted);
  cr_assert_str_eq(highlighted, line);
  SAFE_FREE(highlighted);
}

Test(grep_patterns, plain_global_and_single_highlights_preserve_all_original_text) {
  const char *line = "prefix foo middle FOO suffix";
  cr_assert_eq(grep_init("/foo/ig"), ASCIICHAT_OK);
  const char *highlighted = grep_highlight(line, 7, 3);
  cr_assert_not_null(highlighted);
  const char *first = strstr(highlighted, "\033[48;2;");
  cr_assert_not_null(first);
  cr_assert_not_null(strstr(first + 1, "\033[48;2;"));
  char plain[256];
  strip_ansi_codes(highlighted, plain, sizeof(plain));
  cr_assert_str_eq(plain, line);
  grep_destroy();
  highlighted = grep_highlight(line, 7, 3);
  cr_assert_not_null(highlighted);
  strip_ansi_codes(highlighted, plain, sizeof(plain));
  cr_assert_str_eq(plain, line);
  cr_assert_eq(grep_highlight(line, strlen(line), 1), line);
  cr_assert_eq(grep_highlight(line, 0, 0), line);
  cr_assert_null(grep_highlight(NULL, 0, 1));
  cr_assert_eq(grep_init("/(?=o)/g"), ASCIICHAT_OK);
  highlighted = grep_highlight("foo", 1, 1);
  strip_ansi_codes(highlighted, plain, sizeof(plain));
  cr_assert_str_eq(plain, "foo");
}
Test(grep_patterns, saving_again_replaces_previous_snapshot_and_empty_restore_disables_filtering) {
  cr_assert_eq(grep_init("first"), ASCIICHAT_OK);
  cr_assert_eq(grep_save_patterns(), ASCIICHAT_OK);
  cr_assert_eq(grep_init("/second/iF"), ASCIICHAT_OK);
  cr_assert_eq(grep_save_patterns(), ASCIICHAT_OK);
  cr_assert_eq(grep_init("third"), ASCIICHAT_OK);
  cr_assert_eq(grep_restore_patterns(), ASCIICHAT_OK);
  cr_assert_eq(grep_get_pattern_count(), 2);
  cr_assert_str_eq(grep_get_last_pattern(), "/second/iF");
  size_t start, length;
  cr_assert_not(grep_should_output("third", &start, &length));
  cr_assert(grep_should_output("SECOND", &start, &length));
  grep_clear_patterns();
  cr_assert_null(grep_get_last_pattern());
  cr_assert_eq(grep_save_patterns(), ASCIICHAT_OK);
  cr_assert_eq(grep_init("temporary"), ASCIICHAT_OK);
  cr_assert_eq(grep_restore_patterns(), ASCIICHAT_OK);
  cr_assert_eq(grep_get_pattern_count(), 0);
  cr_assert(grep_should_output("unfiltered", &start, &length));
}

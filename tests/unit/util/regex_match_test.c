#include <criterion/criterion.h>
#include <ascii-chat/util/pcre2.h>

static pcre2_code *pattern;
static pcre2_match_data *matches;

static void setup(void) {
  int error = 0;
  PCRE2_SIZE offset = 0;
  pattern = pcre2_compile((PCRE2_SPTR) "(tone)", PCRE2_ZERO_TERMINATED, 0, &error, &offset, NULL);
  cr_assert_not_null(pattern);
  int result = pcre2_jit_compile(pattern, PCRE2_JIT_COMPLETE);
  cr_assert(result == 0 || result == PCRE2_ERROR_JIT_BADOPTION);
  matches = pcre2_match_data_create_from_pattern(pattern, NULL);
  cr_assert_not_null(matches);
}

static void teardown(void) {
  pcre2_match_data_free(matches);
  matches = NULL;
  pcre2_code_free(pattern);
  pattern = NULL;
}

TestSuite(regex_match, .init = setup, .fini = teardown);

Test(regex_match, successful_match_retains_capture_count_and_offsets) {
  cr_assert_eq(asciichat_pcre2_match(pattern, (PCRE2_SPTR) "tone", 4, 0, 0, matches, NULL), 2);
  PCRE2_SIZE *offsets = pcre2_get_ovector_pointer(matches);
  cr_assert_eq(offsets[0], 0);
  cr_assert_eq(offsets[1], 4);
  cr_assert_eq(offsets[2], 0);
  cr_assert_eq(offsets[3], 4);
}

Test(regex_match, no_match_retains_native_error) {
  cr_assert_eq(asciichat_pcre2_match(pattern, (PCRE2_SPTR) "silence", 7, 0, 0, matches, NULL), PCRE2_ERROR_NOMATCH);
}

Test(regex_match, partial_mode_falls_back_when_not_jit_compiled) {
  cr_assert_eq(asciichat_pcre2_match(pattern, (PCRE2_SPTR) "to", 2, 0, PCRE2_PARTIAL_HARD, matches, NULL),
               PCRE2_ERROR_PARTIAL);
}

Test(regex_match, nonzero_start_offset_is_forwarded) {
  cr_assert_eq(asciichat_pcre2_match(pattern, (PCRE2_SPTR) "tone tone", 9, 5, 0, matches, NULL), 2);
  PCRE2_SIZE *offsets = pcre2_get_ovector_pointer(matches);
  cr_assert_eq(offsets[0], 5);
  cr_assert_eq(offsets[1], 9);
}

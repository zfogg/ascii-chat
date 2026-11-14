#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/util/pcre2.h>

static pcre2_code *pattern;
static pcre2_match_data *matches;

static void setup(void) {
  int error = 0;
  PCRE2_SIZE offset = 0;
  pattern = pcre2_compile((PCRE2_SPTR) "(tone)", PCRE2_ZERO_TERMINATED, 0, &error, &offset, NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(pattern);
  int result = pcre2_jit_compile(pattern, PCRE2_JIT_COMPLETE);
  ASCIICHAT_REQUIRE(result == 0 || result == PCRE2_ERROR_JIT_BADOPTION);
  matches = pcre2_match_data_create_from_pattern(pattern, NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(matches);
}

static void teardown(void) {
  pcre2_match_data_free(matches);
  matches = NULL;
  pcre2_code_free(pattern);
  pattern = NULL;
}
TEST_CASE("regex_match/successful_match_retains_capture_count_and_offsets", "[regex_match]") {
  ASCIICHAT_REQUIRE_EQ(asciichat_pcre2_match(pattern, (PCRE2_SPTR) "tone", 4, 0, 0, matches, NULL), 2);
  PCRE2_SIZE *offsets = pcre2_get_ovector_pointer(matches);
  ASCIICHAT_REQUIRE_EQ(offsets[0], 0);
  ASCIICHAT_REQUIRE_EQ(offsets[1], 4);
  ASCIICHAT_REQUIRE_EQ(offsets[2], 0);
  ASCIICHAT_REQUIRE_EQ(offsets[3], 4);
}

TEST_CASE("regex_match/no_match_retains_native_error", "[regex_match]") {
  ASCIICHAT_REQUIRE_EQ(asciichat_pcre2_match(pattern, (PCRE2_SPTR) "silence", 7, 0, 0, matches, NULL), PCRE2_ERROR_NOMATCH);
}

TEST_CASE("regex_match/partial_mode_falls_back_when_not_jit_compiled", "[regex_match]") {
  ASCIICHAT_REQUIRE_EQ(asciichat_pcre2_match(pattern, (PCRE2_SPTR) "to", 2, 0, PCRE2_PARTIAL_HARD, matches, NULL),
               PCRE2_ERROR_PARTIAL);
}

TEST_CASE("regex_match/nonzero_start_offset_is_forwarded", "[regex_match]") {
  ASCIICHAT_REQUIRE_EQ(asciichat_pcre2_match(pattern, (PCRE2_SPTR) "tone tone", 9, 5, 0, matches, NULL), 2);
  PCRE2_SIZE *offsets = pcre2_get_ovector_pointer(matches);
  ASCIICHAT_REQUIRE_EQ(offsets[0], 5);
  ASCIICHAT_REQUIRE_EQ(offsets[1], 9);
}

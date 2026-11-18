#include <catch2/generators/catch_generators_all.hpp>
#include <iterator>
#include <catch2/catch_test_macros.hpp>
/**
 * @file log_filter_test.c
 * @brief Comprehensive tests for --grep log filtering functionality
 *
 * Tests all flag combinations, multiple patterns, context lines, and edge cases.
 */

#include <string.h>
#include <stdbool.h>

#include <ascii-chat/log/grep.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>

/* ============================================================================
 * Test Suite Setup
 * ============================================================================ */

static void setup_filter_tests(void) {
  // Ensure clean state
  grep_destroy();
}

static void teardown_filter_tests(void) {
  // Clean up after tests
  grep_destroy();
}

// IMPORTANT: These tests MUST run serially (.jobs = 1) because log_filter uses
// global state (g_filter_state). Running in parallel causes race conditions where
// multiple tests try to init/destroy the same global state simultaneously.
// The alternative would be to add mutex locks to all filter operations, but that
// would add overhead to production code just for testing.
/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Check if a pattern is valid
 * Note: Doesn't clean up - relies on test teardown to call grep_destroy()
 */
static bool is_valid_pattern(const char *pattern) {
  // Destroy any previous pattern first
  grep_destroy();

  asciichat_error_t result = grep_init(pattern);
  return (result == ASCIICHAT_OK);
}

/**
 * @brief Test if a line matches the filter
 */
static bool line_matches(const char *line) {
  size_t match_start = 0, match_len = 0;
  return grep_should_output(line, &match_start, &match_len);
}

/**
 * @brief Test if a line matches and extract match position
 */
static bool line_matches_pos(const char *line, size_t *match_start, size_t *match_len) {
  return grep_should_output(line, match_start, match_len);
}

/* ============================================================================
 * Basic Pattern Format Tests
 * ============================================================================ */
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("log_filter/pattern_format_valid", "[log_filter] [unit]") {
  // Valid formats
  REQUIRE(is_valid_pattern("/test/"));
  REQUIRE(is_valid_pattern("/test/i"));
  REQUIRE(is_valid_pattern("/test/ig"));
  REQUIRE(is_valid_pattern("/test/F"));
  REQUIRE(is_valid_pattern("/test/A5"));
  REQUIRE(is_valid_pattern("/test/B3"));
  REQUIRE(is_valid_pattern("/test/C10"));
}

TEST_CASE("log_filter/pattern_format_invalid", "[log_filter] [unit]") {
  // Invalid formats (slash format with missing closing slash)
  REQUIRE(!(is_valid_pattern("/test")));

  // Empty patterns
  REQUIRE(!(is_valid_pattern("//")));
  REQUIRE(!(is_valid_pattern("")));
}

TEST_CASE("log_filter/pattern_format_edge_cases", "[log_filter] [unit]") {
  // Edge case: "test/" is valid as plain pattern (matches literal "test/")
  REQUIRE(is_valid_pattern("test/"));
  asciichat_error_t result = grep_init("test/");
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(line_matches("api/test/endpoint"));
  REQUIRE(!(line_matches("test endpoint")));
}

TEST_CASE("log_filter/pattern_format_plain_regex", "[log_filter] [unit]") {
  // Plain regex patterns (without slashes) should now be valid
  REQUIRE(is_valid_pattern("test"));
  REQUIRE(is_valid_pattern("error"));
  REQUIRE(is_valid_pattern("error|warn"));
  REQUIRE(is_valid_pattern("^ERROR"));
  REQUIRE(is_valid_pattern("\\d+"));
  REQUIRE(is_valid_pattern("test.*end"));
}

/* ============================================================================
 * Plain Regex Format Tests (without slashes)
 * ============================================================================ */

typedef struct {
  char pattern[256];
  char test_line[256];
  bool should_match;
  char description[64];
} plain_regex_test_t;

static plain_regex_test_t plain_regex_cases[] = {
    {"error", "This is an error message", true, "Simple word match"},
    {"error", "This is a warning message", false, "No match"},
    {"ERROR", "This is an error message", false, "Case sensitive (no i flag)"},
    {"^error", "error at start", true, "Anchor at start"},
    {"^error", "not error at start", false, "Anchor at start fails"},
    {"error$", "message ends with error", true, "Anchor at end"},
    {"error$", "error not at end", false, "Anchor at end fails"},
    {"(warn|error)", "This is a warning", true, "Alternation matches first"},
    {"(warn|error)", "This is an error", true, "Alternation matches second"},
    {"(warn|error)", "This is info", false, "Alternation no match"},
    {"\\d+", "Port 8080 opened", true, "Digit pattern matches"},
    {"\\d+", "No numbers here", false, "Digit pattern no match"},
    {"test.*end", "test something end", true, "Dot-star matches"},
    {"test.*end", "test something", false, "Dot-star no match without end"},
};

TEST_CASE("log_filter/plain_regex_format", "[log_filter] [unit]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(plain_regex_cases), std::end(plain_regex_cases)));
  auto *tc = &generated_case;
  asciichat_error_t result = grep_init(tc->pattern);
  REQUIRE((result) == (ASCIICHAT_OK));

  bool matches = line_matches(tc->test_line);
  REQUIRE((matches) == (tc->should_match));
}

TEST_CASE("log_filter/plain_regex_no_flags", "[log_filter] [unit]") {
  // Verify plain regex has NO flags (case-sensitive by default)
  asciichat_error_t result = grep_init("test");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("test message"));
  REQUIRE(!(line_matches("TEST message")));
  REQUIRE(!(line_matches("Test message")));
}

TEST_CASE("log_filter/plain_regex_complex_patterns", "[log_filter] [unit]") {
  // Test complex regex patterns without slashes
  asciichat_error_t result;

  // IPv4 pattern
  result = grep_init("\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}");
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(line_matches("Server IP: 192.168.1.1"));

  grep_destroy();

  // Email pattern
  result = grep_init("[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}");
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(line_matches("Contact: user@example.com"));

  grep_destroy();

  // URL pattern
  result = grep_init("https?://[^\\s]+");
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(line_matches("Visit https://example.com"));
}

/* ============================================================================
 * Basic Regex Matching Tests
 * ============================================================================ */

typedef struct {
  char pattern[256];
  char test_line[256];
  bool should_match;
  char description[64];
} basic_match_test_t;

static basic_match_test_t basic_match_cases[] = {
    {"/error/", "This is an error message", true, "Simple word match"},
    {"/error/", "This is a warning message", false, "No match"},
    {"/ERROR/", "This is an error message", false, "Case sensitive mismatch"},
    {"/error/i", "This is an ERROR message", true, "Case insensitive match"},
    {"/^error/", "error at start", true, "Anchor at start"},
    {"/^error/", "not error at start", false, "Anchor at start fails"},
    {"/error$/", "message ends with error", true, "Anchor at end"},
    {"/error$/", "error not at end", false, "Anchor at end fails"},
    {"/(warn|error)/", "This is a warning", true, "Alternation matches first"},
    {"/(warn|error)/", "This is an error", true, "Alternation matches second"},
    {"/(warn|error)/", "This is info", false, "Alternation no match"},
    {"/\\d+/", "Port 8080 opened", true, "Digit pattern matches"},
    {"/\\d+/", "No numbers here", false, "Digit pattern no match"},
};

TEST_CASE("log_filter/basic_regex_matching", "[log_filter] [unit]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(basic_match_cases), std::end(basic_match_cases)));
  auto *tc = &generated_case;
  asciichat_error_t result = grep_init(tc->pattern);
  REQUIRE((result) == (ASCIICHAT_OK));

  bool matches = line_matches(tc->test_line);
  REQUIRE((matches) == (tc->should_match));
}

/* ============================================================================
 * Fixed String (F flag) Tests
 * ============================================================================ */

typedef struct {
  char pattern[256];
  char test_line[256];
  bool should_match;
  char description[64];
} fixed_string_test_t;

static fixed_string_test_t fixed_string_cases[] = {
    {"/test/F", "This is a test message", true, "Simple fixed string match"},
    {"/test/F", "No match here", false, "Fixed string no match"},
    {"/test.*/F", "test.* should be literal", true, "Regex metachar as literal"},
    {"/(warn|error)/F", "Looking for (warn|error) pattern", true, "Parens as literal"},
    {"/[abc]/F", "String with [abc] brackets", true, "Brackets as literal"},
    {"/$/F", "Dollar sign $ here", true, "Dollar sign as literal"},
    {"/^/F", "Caret ^ symbol", true, "Caret as literal"},
    {"/.*/F", "Match .* literally", true, "Dot-star as literal"},
    {"/test/iF", "TEST in caps", true, "Fixed string with case-insensitive"},
    {"/test/Fi", "TEST in caps", true, "Flags order doesn't matter"},
};

TEST_CASE("log_filter/fixed_string_matching", "[log_filter] [unit]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(fixed_string_cases), std::end(fixed_string_cases)));
  auto *tc = &generated_case;
  asciichat_error_t result = grep_init(tc->pattern);
  REQUIRE((result) == (ASCIICHAT_OK));

  bool matches = line_matches(tc->test_line);
  REQUIRE((matches) == (tc->should_match));
}

/* ============================================================================
 * Invert Match (I flag) Tests
 * ============================================================================ */

TEST_CASE("log_filter/invert_match_basic", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/error/I");
  REQUIRE((result) == (ASCIICHAT_OK));

  // Lines WITHOUT "error" should match
  REQUIRE(line_matches("This is a warning"));
  REQUIRE(line_matches("Info message"));

  // Lines WITH "error" should NOT match
  REQUIRE(!(line_matches("This is an error")));
}

TEST_CASE("log_filter/invert_match_with_flags", "[log_filter] [unit]") {
  // Invert + case-insensitive
  asciichat_error_t result = grep_init("/error/Ii");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("This is a warning"));
  REQUIRE(!(line_matches("This is an ERROR")));
}

TEST_CASE("log_filter/invert_match_fixed_string", "[log_filter] [unit]") {
  // Invert + fixed string
  asciichat_error_t result = grep_init("/test.*/IF");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("This is a message"));
  REQUIRE(!(line_matches("Looking for test.* pattern")));
}

/* ============================================================================
 * Global Flag (g) Tests
 * ============================================================================ */

TEST_CASE("log_filter/global_flag_multiple_matches", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/the/g");
  REQUIRE((result) == (ASCIICHAT_OK));

  size_t match_start = 0, match_len = 0;
  const char *line = "the quick brown fox jumps over the lazy dog";

  bool matches = line_matches_pos(line, &match_start, &match_len);
  REQUIRE(matches);
  REQUIRE((match_start) == (0));
  REQUIRE((match_len) == (3));

  // Note: The highlight function handles multiple matches, but should_output
  // only returns the first match position. The /g flag affects highlighting behavior.
}

/* ============================================================================
 * Context Line Tests - After (A flag)
 * ============================================================================ */

TEST_CASE("log_filter/context_after_lines", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/ERROR/A2");
  REQUIRE((result) == (ASCIICHAT_OK));

  // First line matches
  REQUIRE(line_matches("ERROR: Something failed"));

  // Next 2 lines should pass (context-after)
  REQUIRE(line_matches("Next line 1"));
  REQUIRE(line_matches("Next line 2"));

  // Third line after match should NOT pass (only A2)
  REQUIRE(!(line_matches("This line is outside context")));
}

TEST_CASE("log_filter/context_after_multiple_values", "[log_filter] [unit]") {
  // Test A0, A1, A5, A10
  const char *patterns[] = {"/test/A0", "/test/A1", "/test/A5", "/test/A10"};
  int expected_context[] = {0, 1, 5, 10};

  for (int i = 0; i < 4; i++) {
    grep_destroy();
    asciichat_error_t result = grep_init(patterns[i]);
    REQUIRE((result) == (ASCIICHAT_OK));

    // Trigger match
    REQUIRE(line_matches("test message"));

    // Check context lines
    for (int j = 0; j < expected_context[i]; j++) {
      bool should_match = line_matches("context line");
      REQUIRE(should_match);
    }

    // Line after context should not match
    if (expected_context[i] > 0) {
      REQUIRE(!(line_matches("outside context")));
    }
  }
}

/* ============================================================================
 * Context Line Tests - Before (B flag)
 * ============================================================================ */

TEST_CASE("log_filter/context_before_lines", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/ERROR/B2");
  REQUIRE((result) == (ASCIICHAT_OK));

  // Feed lines before match (these get buffered)
  line_matches("Line before 2");
  line_matches("Line before 1");

  // When match occurs, buffered lines should be output
  // (This is implementation-specific behavior - the circular buffer stores them)
  REQUIRE(line_matches("ERROR: Match!"));
}

/* ============================================================================
 * Context Line Tests - Both (C flag)
 * ============================================================================ */

TEST_CASE("log_filter/context_both_lines", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/ERROR/C3");
  REQUIRE((result) == (ASCIICHAT_OK));

  // Feed lines before match
  line_matches("Line before 3");
  line_matches("Line before 2");
  line_matches("Line before 1");

  // Match line
  REQUIRE(line_matches("ERROR: Match!"));

  // Lines after match (3 lines due to C3)
  REQUIRE(line_matches("Line after 1"));
  REQUIRE(line_matches("Line after 2"));
  REQUIRE(line_matches("Line after 3"));

  // Line outside context
  REQUIRE(!(line_matches("Outside context")));
}

/* ============================================================================
 * Multiple Pattern (OR Logic) Tests
 * ============================================================================ */

TEST_CASE("log_filter/multiple_patterns_or_logic", "[log_filter] [unit]") {
  // Add two patterns
  asciichat_error_t result1 = grep_init("/ERROR/");
  REQUIRE((result1) == (ASCIICHAT_OK));

  asciichat_error_t result2 = grep_init("/WARN/");
  REQUIRE((result2) == (ASCIICHAT_OK));

  // Lines matching either pattern should pass
  REQUIRE(line_matches("ERROR: Failed"));
  REQUIRE(line_matches("WARN: Check this"));
  REQUIRE(line_matches("Both ERROR and WARN"));

  // Line matching neither should fail
  REQUIRE(!(line_matches("INFO: Normal operation")));
}

TEST_CASE("log_filter/multiple_patterns_three", "[log_filter] [unit]") {
  // Add three patterns
  grep_init("/ERROR/");
  grep_init("/WARN/");
  grep_init("/FATAL/");

  REQUIRE(line_matches("ERROR message"));
  REQUIRE(line_matches("WARN message"));
  REQUIRE(line_matches("FATAL message"));
  REQUIRE(!(line_matches("DEBUG message")));
}

TEST_CASE("log_filter/multiple_patterns_mixed_flags", "[log_filter] [unit]") {
  // Mix of different flags
  grep_init("/error/i");    // Case-insensitive
  grep_init("/critical/F"); // Fixed string
  grep_init("/timeout/I");  // Inverted

  REQUIRE(line_matches("ERROR in caps"));
  REQUIRE(line_matches("critical failure"));
  REQUIRE(line_matches("normal message"));
  REQUIRE(!(line_matches("timeout detected")));
}

TEST_CASE("log_filter/multiple_patterns_mixed_formats", "[log_filter] [unit]") {
  // Mix of slash format and plain format
  grep_init("/error/i"); // Slash format with flag
  grep_init("warn");     // Plain format
  grep_init("/FATAL/");  // Slash format no flag
  grep_init("\\d{4}");   // Plain format with regex

  REQUIRE(line_matches("ERROR: Failed"));
  REQUIRE(line_matches("warn: Check this"));
  REQUIRE(line_matches("FATAL error"));
  REQUIRE(line_matches("Code: 1234"));
  REQUIRE(!(line_matches("INFO: Normal")));
}

/* ============================================================================
 * Flag Combination Tests
 * ============================================================================ */

typedef struct {
  char pattern[256];
  char test_line[256];
  bool should_match;
  char description[128];
} flag_combo_test_t;

static flag_combo_test_t flag_combo_cases[] = {
    // Case-insensitive combinations
    {"/test/i", "TEST", true, "Case-insensitive basic"},
    {"/test/im", "TEST on new line", true, "Case-insensitive + multiline"},
    {"/test/is", "TEST", true, "Case-insensitive + dotall"},
    {"/test/ix", "TEST", true, "Case-insensitive + extended"},
    {"/test/ig", "TEST multiple TEST", true, "Case-insensitive + global"},

    // Fixed string combinations
    {"/test/Fi", "TEST", true, "Fixed + case-insensitive"},
    {"/test/Fg", "test multiple test", true, "Fixed + global"},
    {"/test/FA3", "test", true, "Fixed + context-after"},
    {"/test/FB2", "test", true, "Fixed + context-before"},
    {"/test/FC1", "test", true, "Fixed + context-both"},

    // Invert combinations
    {"/test/Ii", "no match", true, "Invert + case-insensitive (no match)"},
    {"/test/IF", "no match", true, "Invert + fixed string (no match)"},
    {"/test/Ig", "no match", true, "Invert + global (no match)"},

    // Context combinations
    {"/test/A2B2", "test", true, "Context-after + context-before"},
    {"/test/A2B2C5", "test", true, "Context-after + context-before + context"},
    {"/test/C5g", "test", true, "Context-both + global"},
    {"/test/C3i", "TEST", true, "Context-both + case-insensitive"},

    // All flags (order shouldn't matter)
    {"/test/imsxgIFA3B2", "no match", true, "All flags (invert allows non-match)"},
    {"/test/FA3B2Iimsxg", "no match", true, "All flags different order"},
};

TEST_CASE("log_filter/flag_combinations", "[log_filter] [unit]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(flag_combo_cases), std::end(flag_combo_cases)));
  auto *tc = &generated_case;
  asciichat_error_t result = grep_init(tc->pattern);
  REQUIRE((result) == (ASCIICHAT_OK));

  bool matches = line_matches(tc->test_line);
  REQUIRE((matches) == (tc->should_match));
}

/* ============================================================================
 * Invalid Flag Tests
 * ============================================================================ */

TEST_CASE("log_filter/invalid_flags_rejected", "[log_filter] [unit]") {
  // Invalid flags should cause pattern to fail validation
  REQUIRE(!(is_valid_pattern("/test/z")));
  REQUIRE(!(is_valid_pattern("/test/Z")));
  REQUIRE(!(is_valid_pattern("/test/123")));
  REQUIRE(!(is_valid_pattern("/test/iX")));
}

TEST_CASE("log_filter/invalid_flags_with_fixed_string", "[log_filter] [unit]") {
  // With F flag, invalid flags should be ignored (not cause error)
  REQUIRE(is_valid_pattern("/test/Fz"));
  REQUIRE(is_valid_pattern("/test/FzZ123"));

  // Verify the pattern still works
  asciichat_error_t result = grep_init("/test/Fz");
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(line_matches("test message"));
}

/* ============================================================================
 * Edge Case Tests
 * ============================================================================ */

TEST_CASE("log_filter/empty_pattern", "[log_filter] [unit]") {
  REQUIRE(!(is_valid_pattern("//")));
  REQUIRE(!(is_valid_pattern("//i")));
}

TEST_CASE("log_filter/special_characters_in_pattern", "[log_filter] [unit]") {
  // Special regex characters
  REQUIRE(is_valid_pattern("/\\[\\]/"));
  REQUIRE(is_valid_pattern("/\\(\\)/"));
  REQUIRE(is_valid_pattern("/\\*/"));
  REQUIRE(is_valid_pattern("/\\+/"));
  REQUIRE(is_valid_pattern("/\\?/"));
  REQUIRE(is_valid_pattern("/\\./"));
  REQUIRE(is_valid_pattern("/\\^/"));
  REQUIRE(is_valid_pattern("/\\$/"));
}

TEST_CASE("log_filter/unicode_in_pattern", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/café/");
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(line_matches("I went to a café"));
  REQUIRE(!(line_matches("I went to a cafe")));
}

TEST_CASE("log_filter/very_long_pattern", "[log_filter] [unit]") {
  // Create a long but valid pattern
  char long_pattern[4096];
  strcpy(long_pattern, "/");
  for (int i = 0; i < 100; i++) {
    strcat(long_pattern, "test");
  }
  strcat(long_pattern, "/");

  REQUIRE(is_valid_pattern(long_pattern));
}

TEST_CASE("log_filter/pattern_with_newlines", "[log_filter] [unit]") {
  // Patterns should not contain newlines
  asciichat_error_t result = grep_init("/test\nline/");
  // This might be valid for multiline matching, depending on implementation
  // Just verify it doesn't crash
  (void)result;
}

TEST_CASE("log_filter/null_pattern", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init(NULL);
  // NULL pattern should be handled gracefully (either error or disable filtering)
  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("log_filter/null_line", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/test/");
  REQUIRE((result) == (ASCIICHAT_OK));

  // NULL line should return false without crashing
  size_t match_start = 0, match_len = 0;
  bool matches = grep_should_output(NULL, &match_start, &match_len);
  REQUIRE(!(matches));
}

/* ============================================================================
 * Regex Mode Flag Tests
 * ============================================================================ */

TEST_CASE("log_filter/multiline_mode", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/^test/m");
  REQUIRE((result) == (ASCIICHAT_OK));

  // In multiline mode, ^ matches after newlines too
  // This behavior depends on how the logging system feeds lines
  // (usually one line at a time, so 'm' flag has limited effect)
}

TEST_CASE("log_filter/dotall_mode", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/test.end/s");
  REQUIRE((result) == (ASCIICHAT_OK));

  // With 's' flag, . matches newlines
  // Again, limited effect when processing line-by-line
}

TEST_CASE("log_filter/extended_mode", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/test # comment/x");
  REQUIRE((result) == (ASCIICHAT_OK));

  // With 'x' flag, whitespace and comments are ignored
  REQUIRE(line_matches("test"));
}

/* ============================================================================
 * Performance and Stress Tests
 * ============================================================================ */

TEST_CASE("log_filter/many_patterns", "[log_filter] [unit]") {
  // Add 50 patterns
  for (int i = 0; i < 50; i++) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "/pattern%d/", i);
    asciichat_error_t result = grep_init(pattern);
    REQUIRE((result) == (ASCIICHAT_OK));
  }

  // Test matching
  REQUIRE(line_matches("pattern0 here"));
  REQUIRE(line_matches("pattern25 here"));
  REQUIRE(line_matches("pattern49 here"));
  REQUIRE(!(line_matches("no match")));
}

TEST_CASE("log_filter/rapid_matching", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/test/");
  REQUIRE((result) == (ASCIICHAT_OK));

  // Match many times rapidly
  for (int i = 0; i < 10000; i++) {
    bool matches = line_matches("test message");
    REQUIRE(matches);
  }
}

/* ============================================================================
 * Highlight Position Tests
 * ============================================================================ */

TEST_CASE("log_filter/match_position_simple", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/error/");
  REQUIRE((result) == (ASCIICHAT_OK));

  size_t match_start = 0, match_len = 0;
  bool matches = line_matches_pos("This is an error message", &match_start, &match_len);

  REQUIRE(matches);
  REQUIRE((match_start) == (11));
  REQUIRE((match_len) == (5));
}

TEST_CASE("log_filter/match_position_start", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/^error/");
  REQUIRE((result) == (ASCIICHAT_OK));

  size_t match_start = 0, match_len = 0;
  bool matches = line_matches_pos("error at start", &match_start, &match_len);

  REQUIRE(matches);
  REQUIRE((match_start) == (0));
  REQUIRE((match_len) == (5));
}

TEST_CASE("log_filter/match_position_end", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/error$/");
  REQUIRE((result) == (ASCIICHAT_OK));

  size_t match_start = 0, match_len = 0;
  const char *line = "message ends with error";
  bool matches = line_matches_pos(line, &match_start, &match_len);

  REQUIRE(matches);
  REQUIRE((match_start) == (strlen(line) - 5));
  REQUIRE((match_len) == (5));
}

/* ============================================================================
 * Functional Context Line Tests (A/B/C flags)
 * ============================================================================ */

TEST_CASE("log_filter/context_after_functional", "[log_filter] [unit]") {
  // Test that A3 actually outputs 3 lines after match
  asciichat_error_t result = grep_init("/MATCH/A3");
  REQUIRE((result) == (ASCIICHAT_OK));

  // Feed lines before match (these should NOT output)
  REQUIRE(!(line_matches("Before 1")));
  REQUIRE(!(line_matches("Before 2")));

  // Match line
  REQUIRE(line_matches("MATCH found here"));

  // Next 3 lines should output (context-after)
  REQUIRE(line_matches("After 1"));
  REQUIRE(line_matches("After 2"));
  REQUIRE(line_matches("After 3"));

  // Line 4 after match should NOT output
  REQUIRE(!(line_matches("After 4")));

  // Continue with non-matching lines
  REQUIRE(!(line_matches("Normal log line")));
}

TEST_CASE("log_filter/context_before_functional", "[log_filter] [unit]") {
  // Test that B2 actually outputs 2 lines before match
  // NOTE: This test verifies the circular buffer behavior
  asciichat_error_t result = grep_init("/MATCH/B2");
  REQUIRE((result) == (ASCIICHAT_OK));

  // Feed lines before match (these get buffered)
  // The implementation stores these in a circular buffer
  line_matches("Before 1"); // Buffered
  line_matches("Before 2"); // Buffered
  line_matches("Before 3"); // Buffered (overwrites Before 1)

  // When match occurs, the 2 most recent lines (Before 2, Before 3) should be output
  // along with the match line
  REQUIRE(line_matches("MATCH found here"));

  // After the match, non-matching lines should not output
  REQUIRE(!(line_matches("After match")));
}

TEST_CASE("log_filter/context_both_functional", "[log_filter] [unit]") {
  // Test that C2 outputs 2 lines before AND 2 lines after match
  asciichat_error_t result = grep_init("/MATCH/C2");
  REQUIRE((result) == (ASCIICHAT_OK));

  // Feed lines before match (buffered for context-before)
  line_matches("Before 1");
  line_matches("Before 2");

  // Match line (outputs buffered lines + match)
  REQUIRE(line_matches("MATCH found here"));

  // Next 2 lines should output (context-after)
  REQUIRE(line_matches("After 1"));
  REQUIRE(line_matches("After 2"));

  // Line 3 after match should NOT output
  REQUIRE(!(line_matches("After 3")));
}

TEST_CASE("log_filter/context_separate_matches", "[log_filter] [unit]") {
  // Test that separate matches each get their own context windows
  asciichat_error_t result = grep_init("/MATCH/A2");
  REQUIRE((result) == (ASCIICHAT_OK));

  // First match
  REQUIRE(line_matches("MATCH 1"));
  REQUIRE(line_matches("After 1-1"));
  REQUIRE(line_matches("After 1-2"));

  // Non-matching line
  REQUIRE(!(line_matches("Between")));

  // Second match (separate from first)
  REQUIRE(line_matches("MATCH 2"));
  REQUIRE(line_matches("After 2-1"));
  REQUIRE(line_matches("After 2-2"));

  // Now outside any context
  REQUIRE(!(line_matches("After all")));
}

/* ============================================================================
 * UTF-8 Fixed String Tests (Case-Sensitive)
 * ============================================================================ */

TEST_CASE("log_filter/utf8_fixed_string_ascii", "[log_filter] [unit]") {
  // Basic ASCII fixed string
  asciichat_error_t result = grep_init("/test/F");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("This is a test message"));
  REQUIRE(!(line_matches("This is a TEST message")));
}

TEST_CASE("log_filter/utf8_fixed_string_accented", "[log_filter] [unit]") {
  // French accented characters
  asciichat_error_t result = grep_init("/café/F");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("J'aime le café français"));
  REQUIRE(!(line_matches("J'aime le cafe français")));
  REQUIRE(!(line_matches("J'aime le CAFÉ français")));
}

TEST_CASE("log_filter/utf8_fixed_string_greek", "[log_filter] [unit]") {
  // Greek characters
  asciichat_error_t result = grep_init("/ελληνικά/F");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Μιλάω ελληνικά"));
  REQUIRE(!(line_matches("Μιλάω ΕΛΛΗΝΙΚΆ")));
}

TEST_CASE("log_filter/utf8_fixed_string_cyrillic", "[log_filter] [unit]") {
  // Cyrillic characters
  asciichat_error_t result = grep_init("/русский/F");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Я говорю по-русский"));
  REQUIRE(!(line_matches("Я говорю по-РУССКИЙ")));
}

TEST_CASE("log_filter/utf8_fixed_string_cjk", "[log_filter] [unit]") {
  // Chinese characters
  asciichat_error_t result = grep_init("/中文/F");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("我说中文"));
  REQUIRE(!(line_matches("我说英文")));
}

TEST_CASE("log_filter/utf8_fixed_string_emoji", "[log_filter] [unit]") {
  // Emoji (4-byte UTF-8)
  asciichat_error_t result = grep_init("/🎉/F");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Celebration 🎉 time!"));
  REQUIRE(!(line_matches("Celebration 🎊 time!")));
}

/* ============================================================================
 * UTF-8 Fixed String Tests (Case-Insensitive)
 * ============================================================================ */

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_ascii", "[log_filter] [unit]") {
  // ASCII case-insensitive
  asciichat_error_t result = grep_init("/test/iF");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("This is a test message"));
  REQUIRE(line_matches("This is a TEST message"));
  REQUIRE(line_matches("This is a TeSt message"));
}

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_accented", "[log_filter] [unit]") {
  // French with case-insensitive
  asciichat_error_t result = grep_init("/café/iF");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("J'aime le café"));
  REQUIRE(line_matches("J'aime le CAFÉ"));
  REQUIRE(line_matches("J'aime le Café"));
  REQUIRE(!(line_matches("J'aime le cafe")));
}

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_greek", "[log_filter] [unit]") {
  // Greek case-insensitive
  asciichat_error_t result = grep_init("/ελληνικά/iF");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Μιλάω ελληνικά"));
  REQUIRE(line_matches("Μιλάω ΕΛΛΗΝΙΚΆ"));
}

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_cyrillic", "[log_filter] [unit]") {
  // Cyrillic case-insensitive
  asciichat_error_t result = grep_init("/русский/iF");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Я говорю по-русский"));
  REQUIRE(line_matches("Я говорю по-РУССКИЙ"));
  REQUIRE(line_matches("Я говорю по-Русский"));
}

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_mixed", "[log_filter] [unit]") {
  // Mixed scripts with case-insensitive
  asciichat_error_t result = grep_init("/Café Μπαρ/iF");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Welcome to Café Μπαρ"));
  REQUIRE(line_matches("Welcome to CAFÉ ΜΠΑΡ"));
  REQUIRE(line_matches("Welcome to café μπαρ"));
}

/* ============================================================================
 * UTF-8 Regex Tests
 * ============================================================================ */

TEST_CASE("log_filter/utf8_regex_ascii", "[log_filter] [unit]") {
  // ASCII regex patterns
  asciichat_error_t result = grep_init("/test[0-9]+/");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("test123 passed"));
  REQUIRE(!(line_matches("test passed")));
}

TEST_CASE("log_filter/utf8_regex_unicode_class", "[log_filter] [unit]") {
  // Unicode character class (any letter)
  asciichat_error_t result = grep_init("/café.*français/");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Le café est français"));
  REQUIRE(line_matches("Un café très français"));
  REQUIRE(!(line_matches("Le cafe est francais")));
}

TEST_CASE("log_filter/utf8_regex_case_insensitive", "[log_filter] [unit]") {
  // Regex with case-insensitive flag
  asciichat_error_t result = grep_init("/café|thé/i");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("J'aime le café"));
  REQUIRE(line_matches("J'aime le CAFÉ"));
  REQUIRE(line_matches("J'aime le thé"));
  REQUIRE(line_matches("J'aime le THÉ"));
}

TEST_CASE("log_filter/utf8_regex_greek_pattern", "[log_filter] [unit]") {
  // Greek word boundary
  asciichat_error_t result = grep_init("/\\bελληνικά\\b/");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Μιλάω ελληνικά καλά"));
  REQUIRE(!(line_matches("Μιλάω ελληνικάς καλά")));
}

TEST_CASE("log_filter/utf8_regex_cyrillic_alternation", "[log_filter] [unit]") {
  // Cyrillic alternation pattern
  asciichat_error_t result = grep_init("/(русский|английский)/");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Я говорю по-русский"));
  REQUIRE(line_matches("Я говорю по-английский"));
  REQUIRE(!(line_matches("Я говорю по-французский")));
}

TEST_CASE("log_filter/utf8_regex_mixed_scripts", "[log_filter] [unit]") {
  // Pattern with multiple Unicode scripts
  asciichat_error_t result = grep_init("/Hello.*你好.*Привет/");
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(line_matches("Hello world 你好 世界 Привет мир"));
  REQUIRE(!(line_matches("Hello world 你好")));
}

/* ============================================================================
 * Cleanup Tests
 * ============================================================================ */

TEST_CASE("log_filter/destroy_idempotent", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/test/");
  REQUIRE((result) == (ASCIICHAT_OK));

  // Multiple destroys should not crash
  grep_destroy();
  grep_destroy();
  grep_destroy();
}

TEST_CASE("log_filter/reinitialize_after_destroy", "[log_filter] [unit]") {
  asciichat_error_t result = grep_init("/test/");
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(line_matches("test message"));

  grep_destroy();

  result = grep_init("/other/");
  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE(line_matches("other message"));
  REQUIRE(!(line_matches("test message")));
}

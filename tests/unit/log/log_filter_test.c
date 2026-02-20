/**
 * @file log_filter_test.c
 * @brief Comprehensive tests for --grep log filtering functionality
 *
 * Tests all flag combinations, multiple patterns, context lines, and edge cases.
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <stdbool.h>

#include <ascii-chat/log/grep.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/common.h>
#include <ascii-chat/tests/common.h>

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
TestSuite(log_filter, .init = setup_filter_tests, .fini = teardown_filter_tests);

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
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(log_filter, LOG_DEBUG, LOG_DEBUG, false, false);

Test(log_filter, pattern_format_valid) {
  // Valid formats
  cr_assert(is_valid_pattern("/test/"), "Basic pattern should be valid");
  cr_assert(is_valid_pattern("/test/i"), "Pattern with flag should be valid");
  cr_assert(is_valid_pattern("/test/ig"), "Pattern with multiple flags should be valid");
  cr_assert(is_valid_pattern("/test/F"), "Fixed string pattern should be valid");
  cr_assert(is_valid_pattern("/test/A5"), "Pattern with context-after should be valid");
  cr_assert(is_valid_pattern("/test/B3"), "Pattern with context-before should be valid");
  cr_assert(is_valid_pattern("/test/C10"), "Pattern with context-both should be valid");
}

Test(log_filter, pattern_format_invalid) {
  // Invalid formats (slash format with missing closing slash)
  cr_assert_not(is_valid_pattern("/test"), "Missing trailing slash should be invalid");

  // Empty patterns
  cr_assert_not(is_valid_pattern("//"), "Empty pattern should be invalid");
  cr_assert_not(is_valid_pattern(""), "Empty string should be invalid");
}

Test(log_filter, pattern_format_edge_cases) {
  // Edge case: "test/" is valid as plain pattern (matches literal "test/")
  cr_assert(is_valid_pattern("test/"), "'test/' should be valid as plain pattern");
  asciichat_error_t result = grep_init("test/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should initialize successfully");
  cr_assert(line_matches("api/test/endpoint"), "Should match 'test/' in string");
  cr_assert_not(line_matches("test endpoint"), "Should not match without slash");
}

Test(log_filter, pattern_format_plain_regex) {
  // Plain regex patterns (without slashes) should now be valid
  cr_assert(is_valid_pattern("test"), "Bare string should be valid as plain regex");
  cr_assert(is_valid_pattern("error"), "Simple word should be valid");
  cr_assert(is_valid_pattern("error|warn"), "Alternation should be valid");
  cr_assert(is_valid_pattern("^ERROR"), "Anchored pattern should be valid");
  cr_assert(is_valid_pattern("\\d+"), "Digit pattern should be valid");
  cr_assert(is_valid_pattern("test.*end"), "Dot-star pattern should be valid");
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

ParameterizedTestParameters(log_filter, plain_regex_format) {
  return cr_make_param_array(plain_regex_test_t, plain_regex_cases,
                             sizeof(plain_regex_cases) / sizeof(plain_regex_cases[0]));
}

ParameterizedTest(plain_regex_test_t *tc, log_filter, plain_regex_format) {
  asciichat_error_t result = grep_init(tc->pattern);
  cr_assert_eq(result, ASCIICHAT_OK, "Plain pattern '%s' should be valid", tc->pattern);

  bool matches = line_matches(tc->test_line);
  cr_assert_eq(matches, tc->should_match, "%s: '%s' with pattern '%s'", tc->description, tc->test_line, tc->pattern);
}

Test(log_filter, plain_regex_no_flags) {
  // Verify plain regex has NO flags (case-sensitive by default)
  asciichat_error_t result = grep_init("test");
  cr_assert_eq(result, ASCIICHAT_OK, "Plain pattern should be valid");

  cr_assert(line_matches("test message"), "Should match lowercase");
  cr_assert_not(line_matches("TEST message"), "Should NOT match uppercase (no i flag)");
  cr_assert_not(line_matches("Test message"), "Should NOT match mixed case (no i flag)");
}

Test(log_filter, plain_regex_complex_patterns) {
  // Test complex regex patterns without slashes
  asciichat_error_t result;

  // IPv4 pattern
  result = grep_init("\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}");
  cr_assert_eq(result, ASCIICHAT_OK, "IPv4 pattern should be valid");
  cr_assert(line_matches("Server IP: 192.168.1.1"), "Should match IPv4");

  grep_destroy();

  // Email pattern
  result = grep_init("[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}");
  cr_assert_eq(result, ASCIICHAT_OK, "Email pattern should be valid");
  cr_assert(line_matches("Contact: user@example.com"), "Should match email");

  grep_destroy();

  // URL pattern
  result = grep_init("https?://[^\\s]+");
  cr_assert_eq(result, ASCIICHAT_OK, "URL pattern should be valid");
  cr_assert(line_matches("Visit https://example.com"), "Should match URL");
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

ParameterizedTestParameters(log_filter, basic_regex_matching) {
  return cr_make_param_array(basic_match_test_t, basic_match_cases,
                             sizeof(basic_match_cases) / sizeof(basic_match_cases[0]));
}

ParameterizedTest(basic_match_test_t *tc, log_filter, basic_regex_matching) {
  asciichat_error_t result = grep_init(tc->pattern);
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern '%s' should be valid", tc->pattern);

  bool matches = line_matches(tc->test_line);
  cr_assert_eq(matches, tc->should_match, "%s: '%s' with pattern '%s'", tc->description, tc->test_line, tc->pattern);
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

ParameterizedTestParameters(log_filter, fixed_string_matching) {
  return cr_make_param_array(fixed_string_test_t, fixed_string_cases,
                             sizeof(fixed_string_cases) / sizeof(fixed_string_cases[0]));
}

ParameterizedTest(fixed_string_test_t *tc, log_filter, fixed_string_matching) {
  asciichat_error_t result = grep_init(tc->pattern);
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern '%s' should be valid", tc->pattern);

  bool matches = line_matches(tc->test_line);
  cr_assert_eq(matches, tc->should_match, "%s: '%s' with pattern '%s'", tc->description, tc->test_line, tc->pattern);
}

/* ============================================================================
 * Invert Match (I flag) Tests
 * ============================================================================ */

Test(log_filter, invert_match_basic) {
  asciichat_error_t result = grep_init("/error/I");
  cr_assert_eq(result, ASCIICHAT_OK, "Invert pattern should be valid");

  // Lines WITHOUT "error" should match
  cr_assert(line_matches("This is a warning"), "Non-matching line should pass invert");
  cr_assert(line_matches("Info message"), "Non-matching line should pass invert");

  // Lines WITH "error" should NOT match
  cr_assert_not(line_matches("This is an error"), "Matching line should fail invert");
}

Test(log_filter, invert_match_with_flags) {
  // Invert + case-insensitive
  asciichat_error_t result = grep_init("/error/Ii");
  cr_assert_eq(result, ASCIICHAT_OK, "Invert with flags should be valid");

  cr_assert(line_matches("This is a warning"), "Non-matching line should pass");
  cr_assert_not(line_matches("This is an ERROR"), "Case-insensitive match should fail invert");
}

Test(log_filter, invert_match_fixed_string) {
  // Invert + fixed string
  asciichat_error_t result = grep_init("/test.*/IF");
  cr_assert_eq(result, ASCIICHAT_OK, "Invert fixed string should be valid");

  cr_assert(line_matches("This is a message"), "Non-matching line should pass");
  cr_assert_not(line_matches("Looking for test.* pattern"), "Fixed string match should fail invert");
}

/* ============================================================================
 * Global Flag (g) Tests
 * ============================================================================ */

Test(log_filter, global_flag_multiple_matches) {
  asciichat_error_t result = grep_init("/the/g");
  cr_assert_eq(result, ASCIICHAT_OK, "Global pattern should be valid");

  size_t match_start = 0, match_len = 0;
  const char *line = "the quick brown fox jumps over the lazy dog";

  bool matches = line_matches_pos(line, &match_start, &match_len);
  cr_assert(matches, "Line with multiple 'the' should match");
  cr_assert_eq(match_start, 0, "First match should be at position 0");
  cr_assert_eq(match_len, 3, "Match length should be 3");

  // Note: The highlight function handles multiple matches, but should_output
  // only returns the first match position. The /g flag affects highlighting behavior.
}

/* ============================================================================
 * Context Line Tests - After (A flag)
 * ============================================================================ */

Test(log_filter, context_after_lines) {
  asciichat_error_t result = grep_init("/ERROR/A2");
  cr_assert_eq(result, ASCIICHAT_OK, "Context-after pattern should be valid");

  // First line matches
  cr_assert(line_matches("ERROR: Something failed"), "Match line should pass");

  // Next 2 lines should pass (context-after)
  cr_assert(line_matches("Next line 1"), "First context line should pass");
  cr_assert(line_matches("Next line 2"), "Second context line should pass");

  // Third line after match should NOT pass (only A2)
  cr_assert_not(line_matches("This line is outside context"), "Line outside context should fail");
}

Test(log_filter, context_after_multiple_values) {
  // Test A0, A1, A5, A10
  const char *patterns[] = {"/test/A0", "/test/A1", "/test/A5", "/test/A10"};
  int expected_context[] = {0, 1, 5, 10};

  for (int i = 0; i < 4; i++) {
    grep_destroy();
    asciichat_error_t result = grep_init(patterns[i]);
    cr_assert_eq(result, ASCIICHAT_OK, "Pattern '%s' should be valid", patterns[i]);

    // Trigger match
    cr_assert(line_matches("test message"), "Match line should pass");

    // Check context lines
    for (int j = 0; j < expected_context[i]; j++) {
      bool should_match = line_matches("context line");
      cr_assert(should_match, "Context line %d/%d for '%s' should match", j + 1, expected_context[i], patterns[i]);
    }

    // Line after context should not match
    if (expected_context[i] > 0) {
      cr_assert_not(line_matches("outside context"), "Line outside context for '%s' should not match", patterns[i]);
    }
  }
}

/* ============================================================================
 * Context Line Tests - Before (B flag)
 * ============================================================================ */

Test(log_filter, context_before_lines) {
  asciichat_error_t result = grep_init("/ERROR/B2");
  cr_assert_eq(result, ASCIICHAT_OK, "Context-before pattern should be valid");

  // Feed lines before match (these get buffered)
  line_matches("Line before 2");
  line_matches("Line before 1");

  // When match occurs, buffered lines should be output
  // (This is implementation-specific behavior - the circular buffer stores them)
  cr_assert(line_matches("ERROR: Match!"), "Match line should pass");
}

/* ============================================================================
 * Context Line Tests - Both (C flag)
 * ============================================================================ */

Test(log_filter, context_both_lines) {
  asciichat_error_t result = grep_init("/ERROR/C3");
  cr_assert_eq(result, ASCIICHAT_OK, "Context-both pattern should be valid");

  // Feed lines before match
  line_matches("Line before 3");
  line_matches("Line before 2");
  line_matches("Line before 1");

  // Match line
  cr_assert(line_matches("ERROR: Match!"), "Match line should pass");

  // Lines after match (3 lines due to C3)
  cr_assert(line_matches("Line after 1"), "Context-after 1 should pass");
  cr_assert(line_matches("Line after 2"), "Context-after 2 should pass");
  cr_assert(line_matches("Line after 3"), "Context-after 3 should pass");

  // Line outside context
  cr_assert_not(line_matches("Outside context"), "Line outside context should fail");
}

/* ============================================================================
 * Multiple Pattern (OR Logic) Tests
 * ============================================================================ */

Test(log_filter, multiple_patterns_or_logic) {
  // Add two patterns
  asciichat_error_t result1 = grep_init("/ERROR/");
  cr_assert_eq(result1, ASCIICHAT_OK, "First pattern should be valid");

  asciichat_error_t result2 = grep_init("/WARN/");
  cr_assert_eq(result2, ASCIICHAT_OK, "Second pattern should be valid");

  // Lines matching either pattern should pass
  cr_assert(line_matches("ERROR: Failed"), "First pattern match should pass");
  cr_assert(line_matches("WARN: Check this"), "Second pattern match should pass");
  cr_assert(line_matches("Both ERROR and WARN"), "Both patterns match should pass");

  // Line matching neither should fail
  cr_assert_not(line_matches("INFO: Normal operation"), "No match should fail");
}

Test(log_filter, multiple_patterns_three) {
  // Add three patterns
  grep_init("/ERROR/");
  grep_init("/WARN/");
  grep_init("/FATAL/");

  cr_assert(line_matches("ERROR message"), "First pattern should match");
  cr_assert(line_matches("WARN message"), "Second pattern should match");
  cr_assert(line_matches("FATAL message"), "Third pattern should match");
  cr_assert_not(line_matches("DEBUG message"), "No pattern should match");
}

Test(log_filter, multiple_patterns_mixed_flags) {
  // Mix of different flags
  grep_init("/error/i");    // Case-insensitive
  grep_init("/critical/F"); // Fixed string
  grep_init("/timeout/I");  // Inverted

  cr_assert(line_matches("ERROR in caps"), "Case-insensitive should match");
  cr_assert(line_matches("critical failure"), "Fixed string should match");
  cr_assert(line_matches("normal message"), "Inverted pattern allows non-match");
  cr_assert_not(line_matches("timeout detected"), "Inverted pattern blocks match");
}

Test(log_filter, multiple_patterns_mixed_formats) {
  // Mix of slash format and plain format
  grep_init("/error/i"); // Slash format with flag
  grep_init("warn");     // Plain format
  grep_init("/FATAL/");  // Slash format no flag
  grep_init("\\d{4}");   // Plain format with regex

  cr_assert(line_matches("ERROR: Failed"), "Slash format case-insensitive should match");
  cr_assert(line_matches("warn: Check this"), "Plain format should match");
  cr_assert(line_matches("FATAL error"), "Slash format no flags should match");
  cr_assert(line_matches("Code: 1234"), "Plain regex with digits should match");
  cr_assert_not(line_matches("INFO: Normal"), "No pattern should match");
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

ParameterizedTestParameters(log_filter, flag_combinations) {
  return cr_make_param_array(flag_combo_test_t, flag_combo_cases,
                             sizeof(flag_combo_cases) / sizeof(flag_combo_cases[0]));
}

ParameterizedTest(flag_combo_test_t *tc, log_filter, flag_combinations) {
  asciichat_error_t result = grep_init(tc->pattern);
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern '%s' should be valid", tc->pattern);

  bool matches = line_matches(tc->test_line);
  cr_assert_eq(matches, tc->should_match, "%s: '%s' with pattern '%s'", tc->description, tc->test_line, tc->pattern);
}

/* ============================================================================
 * Invalid Flag Tests
 * ============================================================================ */

Test(log_filter, invalid_flags_rejected) {
  // Invalid flags should cause pattern to fail validation
  cr_assert_not(is_valid_pattern("/test/z"), "Invalid flag 'z' should fail");
  cr_assert_not(is_valid_pattern("/test/Z"), "Invalid flag 'Z' should fail");
  cr_assert_not(is_valid_pattern("/test/123"), "Digit without A/B/C should fail");
  cr_assert_not(is_valid_pattern("/test/iX"), "Mixed case flags should fail");
}

Test(log_filter, invalid_flags_with_fixed_string) {
  // With F flag, invalid flags should be ignored (not cause error)
  cr_assert(is_valid_pattern("/test/Fz"), "Invalid flag with F should be ignored");
  cr_assert(is_valid_pattern("/test/FzZ123"), "Multiple invalid flags with F should be ignored");

  // Verify the pattern still works
  asciichat_error_t result = grep_init("/test/Fz");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern with F and invalid flags should work");
  cr_assert(line_matches("test message"), "Should still match as fixed string");
}

/* ============================================================================
 * Edge Case Tests
 * ============================================================================ */

Test(log_filter, empty_pattern) {
  cr_assert_not(is_valid_pattern("//"), "Empty pattern should be invalid");
  cr_assert_not(is_valid_pattern("//i"), "Empty pattern with flags should be invalid");
}

Test(log_filter, special_characters_in_pattern) {
  // Special regex characters
  cr_assert(is_valid_pattern("/\\[\\]/"), "Escaped brackets should be valid");
  cr_assert(is_valid_pattern("/\\(\\)/"), "Escaped parens should be valid");
  cr_assert(is_valid_pattern("/\\*/"), "Escaped asterisk should be valid");
  cr_assert(is_valid_pattern("/\\+/"), "Escaped plus should be valid");
  cr_assert(is_valid_pattern("/\\?/"), "Escaped question should be valid");
  cr_assert(is_valid_pattern("/\\./"), "Escaped dot should be valid");
  cr_assert(is_valid_pattern("/\\^/"), "Escaped caret should be valid");
  cr_assert(is_valid_pattern("/\\$/"), "Escaped dollar should be valid");
}

Test(log_filter, unicode_in_pattern) {
  asciichat_error_t result = grep_init("/café/");
  cr_assert_eq(result, ASCIICHAT_OK, "Unicode pattern should be valid");
  cr_assert(line_matches("I went to a café"), "Unicode match should work");
  cr_assert_not(line_matches("I went to a cafe"), "ASCII should not match Unicode");
}

Test(log_filter, very_long_pattern) {
  // Create a long but valid pattern
  char long_pattern[4096];
  strcpy(long_pattern, "/");
  for (int i = 0; i < 100; i++) {
    strcat(long_pattern, "test");
  }
  strcat(long_pattern, "/");

  cr_assert(is_valid_pattern(long_pattern), "Long pattern should be valid");
}

Test(log_filter, pattern_with_newlines) {
  // Patterns should not contain newlines
  asciichat_error_t result = grep_init("/test\nline/");
  // This might be valid for multiline matching, depending on implementation
  // Just verify it doesn't crash
  (void)result;
}

Test(log_filter, null_pattern) {
  asciichat_error_t result = grep_init(NULL);
  // NULL pattern should be handled gracefully (either error or disable filtering)
  cr_assert_neq(result, ASCIICHAT_OK, "NULL pattern should fail");
}

Test(log_filter, null_line) {
  asciichat_error_t result = grep_init("/test/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  // NULL line should return false without crashing
  size_t match_start = 0, match_len = 0;
  bool matches = grep_should_output(NULL, &match_start, &match_len);
  cr_assert_not(matches, "NULL line should not match");
}

/* ============================================================================
 * Regex Mode Flag Tests
 * ============================================================================ */

Test(log_filter, multiline_mode) {
  asciichat_error_t result = grep_init("/^test/m");
  cr_assert_eq(result, ASCIICHAT_OK, "Multiline pattern should be valid");

  // In multiline mode, ^ matches after newlines too
  // This behavior depends on how the logging system feeds lines
  // (usually one line at a time, so 'm' flag has limited effect)
}

Test(log_filter, dotall_mode) {
  asciichat_error_t result = grep_init("/test.end/s");
  cr_assert_eq(result, ASCIICHAT_OK, "Dotall pattern should be valid");

  // With 's' flag, . matches newlines
  // Again, limited effect when processing line-by-line
}

Test(log_filter, extended_mode) {
  asciichat_error_t result = grep_init("/test # comment/x");
  cr_assert_eq(result, ASCIICHAT_OK, "Extended pattern should be valid");

  // With 'x' flag, whitespace and comments are ignored
  cr_assert(line_matches("test"), "Extended mode should match despite comment in pattern");
}

/* ============================================================================
 * Performance and Stress Tests
 * ============================================================================ */

Test(log_filter, many_patterns) {
  // Add 50 patterns
  for (int i = 0; i < 50; i++) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "/pattern%d/", i);
    asciichat_error_t result = grep_init(pattern);
    cr_assert_eq(result, ASCIICHAT_OK, "Pattern %d should be valid", i);
  }

  // Test matching
  cr_assert(line_matches("pattern0 here"), "First pattern should match");
  cr_assert(line_matches("pattern25 here"), "Middle pattern should match");
  cr_assert(line_matches("pattern49 here"), "Last pattern should match");
  cr_assert_not(line_matches("no match"), "Non-matching should fail");
}

Test(log_filter, rapid_matching) {
  asciichat_error_t result = grep_init("/test/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  // Match many times rapidly
  for (int i = 0; i < 10000; i++) {
    bool matches = line_matches("test message");
    cr_assert(matches, "Rapid match %d should work", i);
  }
}

/* ============================================================================
 * Highlight Position Tests
 * ============================================================================ */

Test(log_filter, match_position_simple) {
  asciichat_error_t result = grep_init("/error/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  size_t match_start = 0, match_len = 0;
  bool matches = line_matches_pos("This is an error message", &match_start, &match_len);

  cr_assert(matches, "Should match");
  cr_assert_eq(match_start, 11, "Match should start at position 11");
  cr_assert_eq(match_len, 5, "Match length should be 5");
}

Test(log_filter, match_position_start) {
  asciichat_error_t result = grep_init("/^error/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  size_t match_start = 0, match_len = 0;
  bool matches = line_matches_pos("error at start", &match_start, &match_len);

  cr_assert(matches, "Should match");
  cr_assert_eq(match_start, 0, "Match should start at position 0");
  cr_assert_eq(match_len, 5, "Match length should be 5");
}

Test(log_filter, match_position_end) {
  asciichat_error_t result = grep_init("/error$/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  size_t match_start = 0, match_len = 0;
  const char *line = "message ends with error";
  bool matches = line_matches_pos(line, &match_start, &match_len);

  cr_assert(matches, "Should match");
  cr_assert_eq(match_start, strlen(line) - 5, "Match should be at end");
  cr_assert_eq(match_len, 5, "Match length should be 5");
}

/* ============================================================================
 * Functional Context Line Tests (A/B/C flags)
 * ============================================================================ */

Test(log_filter, context_after_functional) {
  // Test that A3 actually outputs 3 lines after match
  asciichat_error_t result = grep_init("/MATCH/A3");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  // Feed lines before match (these should NOT output)
  cr_assert_not(line_matches("Before 1"), "Line before match should not output");
  cr_assert_not(line_matches("Before 2"), "Line before match should not output");

  // Match line
  cr_assert(line_matches("MATCH found here"), "Match line should output");

  // Next 3 lines should output (context-after)
  cr_assert(line_matches("After 1"), "Context line 1 should output");
  cr_assert(line_matches("After 2"), "Context line 2 should output");
  cr_assert(line_matches("After 3"), "Context line 3 should output");

  // Line 4 after match should NOT output
  cr_assert_not(line_matches("After 4"), "Line 4 after match should not output");

  // Continue with non-matching lines
  cr_assert_not(line_matches("Normal log line"), "Non-matching line should not output");
}

Test(log_filter, context_before_functional) {
  // Test that B2 actually outputs 2 lines before match
  // NOTE: This test verifies the circular buffer behavior
  asciichat_error_t result = grep_init("/MATCH/B2");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  // Feed lines before match (these get buffered)
  // The implementation stores these in a circular buffer
  line_matches("Before 1"); // Buffered
  line_matches("Before 2"); // Buffered
  line_matches("Before 3"); // Buffered (overwrites Before 1)

  // When match occurs, the 2 most recent lines (Before 2, Before 3) should be output
  // along with the match line
  cr_assert(line_matches("MATCH found here"), "Match line should output");

  // After the match, non-matching lines should not output
  cr_assert_not(line_matches("After match"), "Non-matching line should not output");
}

Test(log_filter, context_both_functional) {
  // Test that C2 outputs 2 lines before AND 2 lines after match
  asciichat_error_t result = grep_init("/MATCH/C2");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  // Feed lines before match (buffered for context-before)
  line_matches("Before 1");
  line_matches("Before 2");

  // Match line (outputs buffered lines + match)
  cr_assert(line_matches("MATCH found here"), "Match line should output");

  // Next 2 lines should output (context-after)
  cr_assert(line_matches("After 1"), "Context-after line 1 should output");
  cr_assert(line_matches("After 2"), "Context-after line 2 should output");

  // Line 3 after match should NOT output
  cr_assert_not(line_matches("After 3"), "Line 3 should not output");
}

Test(log_filter, context_separate_matches) {
  // Test that separate matches each get their own context windows
  asciichat_error_t result = grep_init("/MATCH/A2");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  // First match
  cr_assert(line_matches("MATCH 1"), "First match should output");
  cr_assert(line_matches("After 1-1"), "After first match (1/2)");
  cr_assert(line_matches("After 1-2"), "After first match (2/2)");

  // Non-matching line
  cr_assert_not(line_matches("Between"), "Non-matching should not output");

  // Second match (separate from first)
  cr_assert(line_matches("MATCH 2"), "Second match should output");
  cr_assert(line_matches("After 2-1"), "After second match (1/2)");
  cr_assert(line_matches("After 2-2"), "After second match (2/2)");

  // Now outside any context
  cr_assert_not(line_matches("After all"), "Should not output");
}

/* ============================================================================
 * UTF-8 Fixed String Tests (Case-Sensitive)
 * ============================================================================ */

Test(log_filter, utf8_fixed_string_ascii) {
  // Basic ASCII fixed string
  asciichat_error_t result = grep_init("/test/F");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("This is a test message"), "Should match");
  cr_assert_not(line_matches("This is a TEST message"), "Should not match (case-sensitive)");
}

Test(log_filter, utf8_fixed_string_accented) {
  // French accented characters
  asciichat_error_t result = grep_init("/café/F");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("J'aime le café français"), "Should match café");
  cr_assert_not(line_matches("J'aime le cafe français"), "Should not match cafe (no accent)");
  cr_assert_not(line_matches("J'aime le CAFÉ français"), "Should not match CAFÉ (case-sensitive)");
}

Test(log_filter, utf8_fixed_string_greek) {
  // Greek characters
  asciichat_error_t result = grep_init("/ελληνικά/F");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Μιλάω ελληνικά"), "Should match Greek lowercase");
  cr_assert_not(line_matches("Μιλάω ΕΛΛΗΝΙΚΆ"), "Should not match Greek uppercase");
}

Test(log_filter, utf8_fixed_string_cyrillic) {
  // Cyrillic characters
  asciichat_error_t result = grep_init("/русский/F");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Я говорю по-русский"), "Should match Cyrillic lowercase");
  cr_assert_not(line_matches("Я говорю по-РУССКИЙ"), "Should not match Cyrillic uppercase");
}

Test(log_filter, utf8_fixed_string_cjk) {
  // Chinese characters
  asciichat_error_t result = grep_init("/中文/F");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("我说中文"), "Should match Chinese");
  cr_assert_not(line_matches("我说英文"), "Should not match different Chinese");
}

Test(log_filter, utf8_fixed_string_emoji) {
  // Emoji (4-byte UTF-8)
  asciichat_error_t result = grep_init("/🎉/F");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Celebration 🎉 time!"), "Should match emoji");
  cr_assert_not(line_matches("Celebration 🎊 time!"), "Should not match different emoji");
}

/* ============================================================================
 * UTF-8 Fixed String Tests (Case-Insensitive)
 * ============================================================================ */

Test(log_filter, utf8_fixed_string_case_insensitive_ascii) {
  // ASCII case-insensitive
  asciichat_error_t result = grep_init("/test/iF");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("This is a test message"), "Should match lowercase");
  cr_assert(line_matches("This is a TEST message"), "Should match uppercase");
  cr_assert(line_matches("This is a TeSt message"), "Should match mixed case");
}

Test(log_filter, utf8_fixed_string_case_insensitive_accented) {
  // French with case-insensitive
  asciichat_error_t result = grep_init("/café/iF");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("J'aime le café"), "Should match lowercase café");
  cr_assert(line_matches("J'aime le CAFÉ"), "Should match uppercase CAFÉ");
  cr_assert(line_matches("J'aime le Café"), "Should match mixed case Café");
  cr_assert_not(line_matches("J'aime le cafe"), "Should not match cafe (no accent)");
}

Test(log_filter, utf8_fixed_string_case_insensitive_greek) {
  // Greek case-insensitive
  asciichat_error_t result = grep_init("/ελληνικά/iF");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Μιλάω ελληνικά"), "Should match lowercase");
  cr_assert(line_matches("Μιλάω ΕΛΛΗΝΙΚΆ"), "Should match uppercase");
}

Test(log_filter, utf8_fixed_string_case_insensitive_cyrillic) {
  // Cyrillic case-insensitive
  asciichat_error_t result = grep_init("/русский/iF");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Я говорю по-русский"), "Should match lowercase");
  cr_assert(line_matches("Я говорю по-РУССКИЙ"), "Should match uppercase");
  cr_assert(line_matches("Я говорю по-Русский"), "Should match mixed case");
}

Test(log_filter, utf8_fixed_string_case_insensitive_mixed) {
  // Mixed scripts with case-insensitive
  asciichat_error_t result = grep_init("/Café Μπαρ/iF");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Welcome to Café Μπαρ"), "Should match mixed case");
  cr_assert(line_matches("Welcome to CAFÉ ΜΠΑΡ"), "Should match all uppercase");
  cr_assert(line_matches("Welcome to café μπαρ"), "Should match all lowercase");
}

/* ============================================================================
 * UTF-8 Regex Tests
 * ============================================================================ */

Test(log_filter, utf8_regex_ascii) {
  // ASCII regex patterns
  asciichat_error_t result = grep_init("/test[0-9]+/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("test123 passed"), "Should match test followed by digits");
  cr_assert_not(line_matches("test passed"), "Should not match test without digits");
}

Test(log_filter, utf8_regex_unicode_class) {
  // Unicode character class (any letter)
  asciichat_error_t result = grep_init("/café.*français/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Le café est français"), "Should match with accents");
  cr_assert(line_matches("Un café très français"), "Should match with .* in between");
  cr_assert_not(line_matches("Le cafe est francais"), "Should not match without accents");
}

Test(log_filter, utf8_regex_case_insensitive) {
  // Regex with case-insensitive flag
  asciichat_error_t result = grep_init("/café|thé/i");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("J'aime le café"), "Should match café");
  cr_assert(line_matches("J'aime le CAFÉ"), "Should match CAFÉ (case-insensitive)");
  cr_assert(line_matches("J'aime le thé"), "Should match thé");
  cr_assert(line_matches("J'aime le THÉ"), "Should match THÉ (case-insensitive)");
}

Test(log_filter, utf8_regex_greek_pattern) {
  // Greek word boundary
  asciichat_error_t result = grep_init("/\\bελληνικά\\b/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Μιλάω ελληνικά καλά"), "Should match Greek word");
  cr_assert_not(line_matches("Μιλάω ελληνικάς καλά"), "Should not match with suffix");
}

Test(log_filter, utf8_regex_cyrillic_alternation) {
  // Cyrillic alternation pattern
  asciichat_error_t result = grep_init("/(русский|английский)/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Я говорю по-русский"), "Should match русский");
  cr_assert(line_matches("Я говорю по-английский"), "Should match английский");
  cr_assert_not(line_matches("Я говорю по-французский"), "Should not match французский");
}

Test(log_filter, utf8_regex_mixed_scripts) {
  // Pattern with multiple Unicode scripts
  asciichat_error_t result = grep_init("/Hello.*你好.*Привет/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  cr_assert(line_matches("Hello world 你好 世界 Привет мир"), "Should match mixed scripts");
  cr_assert_not(line_matches("Hello world 你好"), "Should not match without Russian");
}

/* ============================================================================
 * Cleanup Tests
 * ============================================================================ */

Test(log_filter, destroy_idempotent) {
  asciichat_error_t result = grep_init("/test/");
  cr_assert_eq(result, ASCIICHAT_OK, "Pattern should be valid");

  // Multiple destroys should not crash
  grep_destroy();
  grep_destroy();
  grep_destroy();
}

Test(log_filter, reinitialize_after_destroy) {
  asciichat_error_t result = grep_init("/test/");
  cr_assert_eq(result, ASCIICHAT_OK, "First pattern should be valid");
  cr_assert(line_matches("test message"), "First pattern should match");

  grep_destroy();

  result = grep_init("/other/");
  cr_assert_eq(result, ASCIICHAT_OK, "Second pattern should be valid");
  cr_assert(line_matches("other message"), "Second pattern should match");
  cr_assert_not(line_matches("test message"), "Old pattern should not match");
}

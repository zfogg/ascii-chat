/**
 * @file log_filter_test.c
 * @brief Comprehensive tests for --grep log filtering functionality
 *
 * Tests all flag combinations, multiple patterns, context lines, and edge cases.
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
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

TEST_CASE("log_filter/pattern_format_valid", "[log_filter]") {
  // Valid formats
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/"), "Basic pattern should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/i"), "Pattern with flag should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/ig"), "Pattern with multiple flags should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/F"), "Fixed string pattern should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/A5"), "Pattern with context-after should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/B3"), "Pattern with context-before should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/C10"), "Pattern with context-both should be valid");
}

TEST_CASE("log_filter/pattern_format_invalid", "[log_filter]") {
  // Invalid formats (slash format with missing closing slash)
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern("/test"), "Missing trailing slash should be invalid");

  // Empty patterns
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern("//"), "Empty pattern should be invalid");
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern(""), "Empty string should be invalid");
}

TEST_CASE("log_filter/pattern_format_edge_cases", "[log_filter]") {
  // Edge case: "test/" is valid as plain pattern (matches literal "test/")
  ASCIICHAT_REQUIRE(is_valid_pattern("test/"), "'test/' should be valid as plain pattern");
  asciichat_error_t result = grep_init("test/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should initialize successfully");
  ASCIICHAT_REQUIRE(line_matches("api/test/endpoint"), "Should match 'test/' in string");
  ASCIICHAT_REQUIRE_NOT(line_matches("test endpoint"), "Should not match without slash");
}

TEST_CASE("log_filter/pattern_format_plain_regex", "[log_filter]") {
  // Plain regex patterns (without slashes) should now be valid
  ASCIICHAT_REQUIRE(is_valid_pattern("test"), "Bare string should be valid as plain regex");
  ASCIICHAT_REQUIRE(is_valid_pattern("error"), "Simple word should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("error|warn"), "Alternation should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("^ERROR"), "Anchored pattern should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("\\d+"), "Digit pattern should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("test.*end"), "Dot-star pattern should be valid");
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



TEST_CASE("log_filter/plain_regex_format", "[log_filter]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(plain_regex_cases)); auto *tc = &generated_case;
  asciichat_error_t result = grep_init(tc->pattern);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Plain pattern '%s' should be valid", tc->pattern);

  bool matches = line_matches(tc->test_line);
  ASCIICHAT_REQUIRE_EQ(matches, tc->should_match, "%s: '%s' with pattern '%s'", tc->description, tc->test_line, tc->pattern);
}

TEST_CASE("log_filter/plain_regex_no_flags", "[log_filter]") {
  // Verify plain regex has NO flags (case-sensitive by default)
  asciichat_error_t result = grep_init("test");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Plain pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("test message"), "Should match lowercase");
  ASCIICHAT_REQUIRE_NOT(line_matches("TEST message"), "Should NOT match uppercase (no i flag)");
  ASCIICHAT_REQUIRE_NOT(line_matches("Test message"), "Should NOT match mixed case (no i flag)");
}

TEST_CASE("log_filter/plain_regex_complex_patterns", "[log_filter]") {
  // Test complex regex patterns without slashes
  asciichat_error_t result;

  // IPv4 pattern
  result = grep_init("\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "IPv4 pattern should be valid");
  ASCIICHAT_REQUIRE(line_matches("Server IP: 192.168.1.1"), "Should match IPv4");

  grep_destroy();

  // Email pattern
  result = grep_init("[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Email pattern should be valid");
  ASCIICHAT_REQUIRE(line_matches("Contact: user@example.com"), "Should match email");

  grep_destroy();

  // URL pattern
  result = grep_init("https?://[^\\s]+");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "URL pattern should be valid");
  ASCIICHAT_REQUIRE(line_matches("Visit https://example.com"), "Should match URL");
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



TEST_CASE("log_filter/basic_regex_matching", "[log_filter]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(basic_match_cases)); auto *tc = &generated_case;
  asciichat_error_t result = grep_init(tc->pattern);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern '%s' should be valid", tc->pattern);

  bool matches = line_matches(tc->test_line);
  ASCIICHAT_REQUIRE_EQ(matches, tc->should_match, "%s: '%s' with pattern '%s'", tc->description, tc->test_line, tc->pattern);
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



TEST_CASE("log_filter/fixed_string_matching", "[log_filter]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(fixed_string_cases)); auto *tc = &generated_case;
  asciichat_error_t result = grep_init(tc->pattern);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern '%s' should be valid", tc->pattern);

  bool matches = line_matches(tc->test_line);
  ASCIICHAT_REQUIRE_EQ(matches, tc->should_match, "%s: '%s' with pattern '%s'", tc->description, tc->test_line, tc->pattern);
}

/* ============================================================================
 * Invert Match (I flag) Tests
 * ============================================================================ */

TEST_CASE("log_filter/invert_match_basic", "[log_filter]") {
  asciichat_error_t result = grep_init("/error/I");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Invert pattern should be valid");

  // Lines WITHOUT "error" should match
  ASCIICHAT_REQUIRE(line_matches("This is a warning"), "Non-matching line should pass invert");
  ASCIICHAT_REQUIRE(line_matches("Info message"), "Non-matching line should pass invert");

  // Lines WITH "error" should NOT match
  ASCIICHAT_REQUIRE_NOT(line_matches("This is an error"), "Matching line should fail invert");
}

TEST_CASE("log_filter/invert_match_with_flags", "[log_filter]") {
  // Invert + case-insensitive
  asciichat_error_t result = grep_init("/error/Ii");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Invert with flags should be valid");

  ASCIICHAT_REQUIRE(line_matches("This is a warning"), "Non-matching line should pass");
  ASCIICHAT_REQUIRE_NOT(line_matches("This is an ERROR"), "Case-insensitive match should fail invert");
}

TEST_CASE("log_filter/invert_match_fixed_string", "[log_filter]") {
  // Invert + fixed string
  asciichat_error_t result = grep_init("/test.*/IF");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Invert fixed string should be valid");

  ASCIICHAT_REQUIRE(line_matches("This is a message"), "Non-matching line should pass");
  ASCIICHAT_REQUIRE_NOT(line_matches("Looking for test.* pattern"), "Fixed string match should fail invert");
}

/* ============================================================================
 * Global Flag (g) Tests
 * ============================================================================ */

TEST_CASE("log_filter/global_flag_multiple_matches", "[log_filter]") {
  asciichat_error_t result = grep_init("/the/g");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Global pattern should be valid");

  size_t match_start = 0, match_len = 0;
  const char *line = "the quick brown fox jumps over the lazy dog";

  bool matches = line_matches_pos(line, &match_start, &match_len);
  ASCIICHAT_REQUIRE(matches, "Line with multiple 'the' should match");
  ASCIICHAT_REQUIRE_EQ(match_start, 0, "First match should be at position 0");
  ASCIICHAT_REQUIRE_EQ(match_len, 3, "Match length should be 3");

  // Note: The highlight function handles multiple matches, but should_output
  // only returns the first match position. The /g flag affects highlighting behavior.
}

/* ============================================================================
 * Context Line Tests - After (A flag)
 * ============================================================================ */

TEST_CASE("log_filter/context_after_lines", "[log_filter]") {
  asciichat_error_t result = grep_init("/ERROR/A2");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Context-after pattern should be valid");

  // First line matches
  ASCIICHAT_REQUIRE(line_matches("ERROR: Something failed"), "Match line should pass");

  // Next 2 lines should pass (context-after)
  ASCIICHAT_REQUIRE(line_matches("Next line 1"), "First context line should pass");
  ASCIICHAT_REQUIRE(line_matches("Next line 2"), "Second context line should pass");

  // Third line after match should NOT pass (only A2)
  ASCIICHAT_REQUIRE_NOT(line_matches("This line is outside context"), "Line outside context should fail");
}

TEST_CASE("log_filter/context_after_multiple_values", "[log_filter]") {
  // Test A0, A1, A5, A10
  const char *patterns[] = {"/test/A0", "/test/A1", "/test/A5", "/test/A10"};
  int expected_context[] = {0, 1, 5, 10};

  for (int i = 0; i < 4; i++) {
    grep_destroy();
    asciichat_error_t result = grep_init(patterns[i]);
    ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern '%s' should be valid", patterns[i]);

    // Trigger match
    ASCIICHAT_REQUIRE(line_matches("test message"), "Match line should pass");

    // Check context lines
    for (int j = 0; j < expected_context[i]; j++) {
      bool should_match = line_matches("context line");
      ASCIICHAT_REQUIRE(should_match, "Context line %d/%d for '%s' should match", j + 1, expected_context[i], patterns[i]);
    }

    // Line after context should not match
    if (expected_context[i] > 0) {
      ASCIICHAT_REQUIRE_NOT(line_matches("outside context"), "Line outside context for '%s' should not match", patterns[i]);
    }
  }
}

/* ============================================================================
 * Context Line Tests - Before (B flag)
 * ============================================================================ */

TEST_CASE("log_filter/context_before_lines", "[log_filter]") {
  asciichat_error_t result = grep_init("/ERROR/B2");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Context-before pattern should be valid");

  // Feed lines before match (these get buffered)
  line_matches("Line before 2");
  line_matches("Line before 1");

  // When match occurs, buffered lines should be output
  // (This is implementation-specific behavior - the circular buffer stores them)
  ASCIICHAT_REQUIRE(line_matches("ERROR: Match!"), "Match line should pass");
}

/* ============================================================================
 * Context Line Tests - Both (C flag)
 * ============================================================================ */

TEST_CASE("log_filter/context_both_lines", "[log_filter]") {
  asciichat_error_t result = grep_init("/ERROR/C3");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Context-both pattern should be valid");

  // Feed lines before match
  line_matches("Line before 3");
  line_matches("Line before 2");
  line_matches("Line before 1");

  // Match line
  ASCIICHAT_REQUIRE(line_matches("ERROR: Match!"), "Match line should pass");

  // Lines after match (3 lines due to C3)
  ASCIICHAT_REQUIRE(line_matches("Line after 1"), "Context-after 1 should pass");
  ASCIICHAT_REQUIRE(line_matches("Line after 2"), "Context-after 2 should pass");
  ASCIICHAT_REQUIRE(line_matches("Line after 3"), "Context-after 3 should pass");

  // Line outside context
  ASCIICHAT_REQUIRE_NOT(line_matches("Outside context"), "Line outside context should fail");
}

/* ============================================================================
 * Multiple Pattern (OR Logic) Tests
 * ============================================================================ */

TEST_CASE("log_filter/multiple_patterns_or_logic", "[log_filter]") {
  // Add two patterns
  asciichat_error_t result1 = grep_init("/ERROR/");
  ASCIICHAT_REQUIRE_EQ(result1, ASCIICHAT_OK, "First pattern should be valid");

  asciichat_error_t result2 = grep_init("/WARN/");
  ASCIICHAT_REQUIRE_EQ(result2, ASCIICHAT_OK, "Second pattern should be valid");

  // Lines matching either pattern should pass
  ASCIICHAT_REQUIRE(line_matches("ERROR: Failed"), "First pattern match should pass");
  ASCIICHAT_REQUIRE(line_matches("WARN: Check this"), "Second pattern match should pass");
  ASCIICHAT_REQUIRE(line_matches("Both ERROR and WARN"), "Both patterns match should pass");

  // Line matching neither should fail
  ASCIICHAT_REQUIRE_NOT(line_matches("INFO: Normal operation"), "No match should fail");
}

TEST_CASE("log_filter/multiple_patterns_three", "[log_filter]") {
  // Add three patterns
  grep_init("/ERROR/");
  grep_init("/WARN/");
  grep_init("/FATAL/");

  ASCIICHAT_REQUIRE(line_matches("ERROR message"), "First pattern should match");
  ASCIICHAT_REQUIRE(line_matches("WARN message"), "Second pattern should match");
  ASCIICHAT_REQUIRE(line_matches("FATAL message"), "Third pattern should match");
  ASCIICHAT_REQUIRE_NOT(line_matches("DEBUG message"), "No pattern should match");
}

TEST_CASE("log_filter/multiple_patterns_mixed_flags", "[log_filter]") {
  // Mix of different flags
  grep_init("/error/i");    // Case-insensitive
  grep_init("/critical/F"); // Fixed string
  grep_init("/timeout/I");  // Inverted

  ASCIICHAT_REQUIRE(line_matches("ERROR in caps"), "Case-insensitive should match");
  ASCIICHAT_REQUIRE(line_matches("critical failure"), "Fixed string should match");
  ASCIICHAT_REQUIRE(line_matches("normal message"), "Inverted pattern allows non-match");
  ASCIICHAT_REQUIRE_NOT(line_matches("timeout detected"), "Inverted pattern blocks match");
}

TEST_CASE("log_filter/multiple_patterns_mixed_formats", "[log_filter]") {
  // Mix of slash format and plain format
  grep_init("/error/i"); // Slash format with flag
  grep_init("warn");     // Plain format
  grep_init("/FATAL/");  // Slash format no flag
  grep_init("\\d{4}");   // Plain format with regex

  ASCIICHAT_REQUIRE(line_matches("ERROR: Failed"), "Slash format case-insensitive should match");
  ASCIICHAT_REQUIRE(line_matches("warn: Check this"), "Plain format should match");
  ASCIICHAT_REQUIRE(line_matches("FATAL error"), "Slash format no flags should match");
  ASCIICHAT_REQUIRE(line_matches("Code: 1234"), "Plain regex with digits should match");
  ASCIICHAT_REQUIRE_NOT(line_matches("INFO: Normal"), "No pattern should match");
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



TEST_CASE("log_filter/flag_combinations", "[log_filter]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(flag_combo_cases)); auto *tc = &generated_case;
  asciichat_error_t result = grep_init(tc->pattern);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern '%s' should be valid", tc->pattern);

  bool matches = line_matches(tc->test_line);
  ASCIICHAT_REQUIRE_EQ(matches, tc->should_match, "%s: '%s' with pattern '%s'", tc->description, tc->test_line, tc->pattern);
}

/* ============================================================================
 * Invalid Flag Tests
 * ============================================================================ */

TEST_CASE("log_filter/invalid_flags_rejected", "[log_filter]") {
  // Invalid flags should cause pattern to fail validation
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern("/test/z"), "Invalid flag 'z' should fail");
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern("/test/Z"), "Invalid flag 'Z' should fail");
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern("/test/123"), "Digit without A/B/C should fail");
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern("/test/iX"), "Mixed case flags should fail");
}

TEST_CASE("log_filter/invalid_flags_with_fixed_string", "[log_filter]") {
  // With F flag, invalid flags should be ignored (not cause error)
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/Fz"), "Invalid flag with F should be ignored");
  ASCIICHAT_REQUIRE(is_valid_pattern("/test/FzZ123"), "Multiple invalid flags with F should be ignored");

  // Verify the pattern still works
  asciichat_error_t result = grep_init("/test/Fz");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern with F and invalid flags should work");
  ASCIICHAT_REQUIRE(line_matches("test message"), "Should still match as fixed string");
}

/* ============================================================================
 * Edge Case Tests
 * ============================================================================ */

TEST_CASE("log_filter/empty_pattern", "[log_filter]") {
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern("//"), "Empty pattern should be invalid");
  ASCIICHAT_REQUIRE_NOT(is_valid_pattern("//i"), "Empty pattern with flags should be invalid");
}

TEST_CASE("log_filter/special_characters_in_pattern", "[log_filter]") {
  // Special regex characters
  ASCIICHAT_REQUIRE(is_valid_pattern("/\\[\\]/"), "Escaped brackets should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/\\(\\)/"), "Escaped parens should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/\\*/"), "Escaped asterisk should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/\\+/"), "Escaped plus should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/\\?/"), "Escaped question should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/\\./"), "Escaped dot should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/\\^/"), "Escaped caret should be valid");
  ASCIICHAT_REQUIRE(is_valid_pattern("/\\$/"), "Escaped dollar should be valid");
}

TEST_CASE("log_filter/unicode_in_pattern", "[log_filter]") {
  asciichat_error_t result = grep_init("/café/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Unicode pattern should be valid");
  ASCIICHAT_REQUIRE(line_matches("I went to a café"), "Unicode match should work");
  ASCIICHAT_REQUIRE_NOT(line_matches("I went to a cafe"), "ASCII should not match Unicode");
}

TEST_CASE("log_filter/very_long_pattern", "[log_filter]") {
  // Create a long but valid pattern
  char long_pattern[4096];
  strcpy(long_pattern, "/");
  for (int i = 0; i < 100; i++) {
    strcat(long_pattern, "test");
  }
  strcat(long_pattern, "/");

  ASCIICHAT_REQUIRE(is_valid_pattern(long_pattern), "Long pattern should be valid");
}

TEST_CASE("log_filter/pattern_with_newlines", "[log_filter]") {
  // Patterns should not contain newlines
  asciichat_error_t result = grep_init("/test\nline/");
  // This might be valid for multiline matching, depending on implementation
  // Just verify it doesn't crash
  (void)result;
}

TEST_CASE("log_filter/null_pattern", "[log_filter]") {
  asciichat_error_t result = grep_init(NULL);
  // NULL pattern should be handled gracefully (either error or disable filtering)
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "NULL pattern should fail");
}

TEST_CASE("log_filter/null_line", "[log_filter]") {
  asciichat_error_t result = grep_init("/test/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  // NULL line should return false without crashing
  size_t match_start = 0, match_len = 0;
  bool matches = grep_should_output(NULL, &match_start, &match_len);
  ASCIICHAT_REQUIRE_NOT(matches, "NULL line should not match");
}

/* ============================================================================
 * Regex Mode Flag Tests
 * ============================================================================ */

TEST_CASE("log_filter/multiline_mode", "[log_filter]") {
  asciichat_error_t result = grep_init("/^test/m");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Multiline pattern should be valid");

  // In multiline mode, ^ matches after newlines too
  // This behavior depends on how the logging system feeds lines
  // (usually one line at a time, so 'm' flag has limited effect)
}

TEST_CASE("log_filter/dotall_mode", "[log_filter]") {
  asciichat_error_t result = grep_init("/test.end/s");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Dotall pattern should be valid");

  // With 's' flag, . matches newlines
  // Again, limited effect when processing line-by-line
}

TEST_CASE("log_filter/extended_mode", "[log_filter]") {
  asciichat_error_t result = grep_init("/test # comment/x");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Extended pattern should be valid");

  // With 'x' flag, whitespace and comments are ignored
  ASCIICHAT_REQUIRE(line_matches("test"), "Extended mode should match despite comment in pattern");
}

/* ============================================================================
 * Performance and Stress Tests
 * ============================================================================ */

TEST_CASE("log_filter/many_patterns", "[log_filter]") {
  // Add 50 patterns
  for (int i = 0; i < 50; i++) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "/pattern%d/", i);
    asciichat_error_t result = grep_init(pattern);
    ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern %d should be valid", i);
  }

  // Test matching
  ASCIICHAT_REQUIRE(line_matches("pattern0 here"), "First pattern should match");
  ASCIICHAT_REQUIRE(line_matches("pattern25 here"), "Middle pattern should match");
  ASCIICHAT_REQUIRE(line_matches("pattern49 here"), "Last pattern should match");
  ASCIICHAT_REQUIRE_NOT(line_matches("no match"), "Non-matching should fail");
}

TEST_CASE("log_filter/rapid_matching", "[log_filter]") {
  asciichat_error_t result = grep_init("/test/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  // Match many times rapidly
  for (int i = 0; i < 10000; i++) {
    bool matches = line_matches("test message");
    ASCIICHAT_REQUIRE(matches, "Rapid match %d should work", i);
  }
}

/* ============================================================================
 * Highlight Position Tests
 * ============================================================================ */

TEST_CASE("log_filter/match_position_simple", "[log_filter]") {
  asciichat_error_t result = grep_init("/error/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  size_t match_start = 0, match_len = 0;
  bool matches = line_matches_pos("This is an error message", &match_start, &match_len);

  ASCIICHAT_REQUIRE(matches, "Should match");
  ASCIICHAT_REQUIRE_EQ(match_start, 11, "Match should start at position 11");
  ASCIICHAT_REQUIRE_EQ(match_len, 5, "Match length should be 5");
}

TEST_CASE("log_filter/match_position_start", "[log_filter]") {
  asciichat_error_t result = grep_init("/^error/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  size_t match_start = 0, match_len = 0;
  bool matches = line_matches_pos("error at start", &match_start, &match_len);

  ASCIICHAT_REQUIRE(matches, "Should match");
  ASCIICHAT_REQUIRE_EQ(match_start, 0, "Match should start at position 0");
  ASCIICHAT_REQUIRE_EQ(match_len, 5, "Match length should be 5");
}

TEST_CASE("log_filter/match_position_end", "[log_filter]") {
  asciichat_error_t result = grep_init("/error$/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  size_t match_start = 0, match_len = 0;
  const char *line = "message ends with error";
  bool matches = line_matches_pos(line, &match_start, &match_len);

  ASCIICHAT_REQUIRE(matches, "Should match");
  ASCIICHAT_REQUIRE_EQ(match_start, strlen(line) - 5, "Match should be at end");
  ASCIICHAT_REQUIRE_EQ(match_len, 5, "Match length should be 5");
}

/* ============================================================================
 * Functional Context Line Tests (A/B/C flags)
 * ============================================================================ */

TEST_CASE("log_filter/context_after_functional", "[log_filter]") {
  // Test that A3 actually outputs 3 lines after match
  asciichat_error_t result = grep_init("/MATCH/A3");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  // Feed lines before match (these should NOT output)
  ASCIICHAT_REQUIRE_NOT(line_matches("Before 1"), "Line before match should not output");
  ASCIICHAT_REQUIRE_NOT(line_matches("Before 2"), "Line before match should not output");

  // Match line
  ASCIICHAT_REQUIRE(line_matches("MATCH found here"), "Match line should output");

  // Next 3 lines should output (context-after)
  ASCIICHAT_REQUIRE(line_matches("After 1"), "Context line 1 should output");
  ASCIICHAT_REQUIRE(line_matches("After 2"), "Context line 2 should output");
  ASCIICHAT_REQUIRE(line_matches("After 3"), "Context line 3 should output");

  // Line 4 after match should NOT output
  ASCIICHAT_REQUIRE_NOT(line_matches("After 4"), "Line 4 after match should not output");

  // Continue with non-matching lines
  ASCIICHAT_REQUIRE_NOT(line_matches("Normal log line"), "Non-matching line should not output");
}

TEST_CASE("log_filter/context_before_functional", "[log_filter]") {
  // Test that B2 actually outputs 2 lines before match
  // NOTE: This test verifies the circular buffer behavior
  asciichat_error_t result = grep_init("/MATCH/B2");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  // Feed lines before match (these get buffered)
  // The implementation stores these in a circular buffer
  line_matches("Before 1"); // Buffered
  line_matches("Before 2"); // Buffered
  line_matches("Before 3"); // Buffered (overwrites Before 1)

  // When match occurs, the 2 most recent lines (Before 2, Before 3) should be output
  // along with the match line
  ASCIICHAT_REQUIRE(line_matches("MATCH found here"), "Match line should output");

  // After the match, non-matching lines should not output
  ASCIICHAT_REQUIRE_NOT(line_matches("After match"), "Non-matching line should not output");
}

TEST_CASE("log_filter/context_both_functional", "[log_filter]") {
  // Test that C2 outputs 2 lines before AND 2 lines after match
  asciichat_error_t result = grep_init("/MATCH/C2");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  // Feed lines before match (buffered for context-before)
  line_matches("Before 1");
  line_matches("Before 2");

  // Match line (outputs buffered lines + match)
  ASCIICHAT_REQUIRE(line_matches("MATCH found here"), "Match line should output");

  // Next 2 lines should output (context-after)
  ASCIICHAT_REQUIRE(line_matches("After 1"), "Context-after line 1 should output");
  ASCIICHAT_REQUIRE(line_matches("After 2"), "Context-after line 2 should output");

  // Line 3 after match should NOT output
  ASCIICHAT_REQUIRE_NOT(line_matches("After 3"), "Line 3 should not output");
}

TEST_CASE("log_filter/context_separate_matches", "[log_filter]") {
  // Test that separate matches each get their own context windows
  asciichat_error_t result = grep_init("/MATCH/A2");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  // First match
  ASCIICHAT_REQUIRE(line_matches("MATCH 1"), "First match should output");
  ASCIICHAT_REQUIRE(line_matches("After 1-1"), "After first match (1/2)");
  ASCIICHAT_REQUIRE(line_matches("After 1-2"), "After first match (2/2)");

  // Non-matching line
  ASCIICHAT_REQUIRE_NOT(line_matches("Between"), "Non-matching should not output");

  // Second match (separate from first)
  ASCIICHAT_REQUIRE(line_matches("MATCH 2"), "Second match should output");
  ASCIICHAT_REQUIRE(line_matches("After 2-1"), "After second match (1/2)");
  ASCIICHAT_REQUIRE(line_matches("After 2-2"), "After second match (2/2)");

  // Now outside any context
  ASCIICHAT_REQUIRE_NOT(line_matches("After all"), "Should not output");
}

/* ============================================================================
 * UTF-8 Fixed String Tests (Case-Sensitive)
 * ============================================================================ */

TEST_CASE("log_filter/utf8_fixed_string_ascii", "[log_filter]") {
  // Basic ASCII fixed string
  asciichat_error_t result = grep_init("/test/F");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("This is a test message"), "Should match");
  ASCIICHAT_REQUIRE_NOT(line_matches("This is a TEST message"), "Should not match (case-sensitive)");
}

TEST_CASE("log_filter/utf8_fixed_string_accented", "[log_filter]") {
  // French accented characters
  asciichat_error_t result = grep_init("/café/F");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("J'aime le café français"), "Should match café");
  ASCIICHAT_REQUIRE_NOT(line_matches("J'aime le cafe français"), "Should not match cafe (no accent)");
  ASCIICHAT_REQUIRE_NOT(line_matches("J'aime le CAFÉ français"), "Should not match CAFÉ (case-sensitive)");
}

TEST_CASE("log_filter/utf8_fixed_string_greek", "[log_filter]") {
  // Greek characters
  asciichat_error_t result = grep_init("/ελληνικά/F");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Μιλάω ελληνικά"), "Should match Greek lowercase");
  ASCIICHAT_REQUIRE_NOT(line_matches("Μιλάω ΕΛΛΗΝΙΚΆ"), "Should not match Greek uppercase");
}

TEST_CASE("log_filter/utf8_fixed_string_cyrillic", "[log_filter]") {
  // Cyrillic characters
  asciichat_error_t result = grep_init("/русский/F");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Я говорю по-русский"), "Should match Cyrillic lowercase");
  ASCIICHAT_REQUIRE_NOT(line_matches("Я говорю по-РУССКИЙ"), "Should not match Cyrillic uppercase");
}

TEST_CASE("log_filter/utf8_fixed_string_cjk", "[log_filter]") {
  // Chinese characters
  asciichat_error_t result = grep_init("/中文/F");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("我说中文"), "Should match Chinese");
  ASCIICHAT_REQUIRE_NOT(line_matches("我说英文"), "Should not match different Chinese");
}

TEST_CASE("log_filter/utf8_fixed_string_emoji", "[log_filter]") {
  // Emoji (4-byte UTF-8)
  asciichat_error_t result = grep_init("/🎉/F");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Celebration 🎉 time!"), "Should match emoji");
  ASCIICHAT_REQUIRE_NOT(line_matches("Celebration 🎊 time!"), "Should not match different emoji");
}

/* ============================================================================
 * UTF-8 Fixed String Tests (Case-Insensitive)
 * ============================================================================ */

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_ascii", "[log_filter]") {
  // ASCII case-insensitive
  asciichat_error_t result = grep_init("/test/iF");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("This is a test message"), "Should match lowercase");
  ASCIICHAT_REQUIRE(line_matches("This is a TEST message"), "Should match uppercase");
  ASCIICHAT_REQUIRE(line_matches("This is a TeSt message"), "Should match mixed case");
}

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_accented", "[log_filter]") {
  // French with case-insensitive
  asciichat_error_t result = grep_init("/café/iF");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("J'aime le café"), "Should match lowercase café");
  ASCIICHAT_REQUIRE(line_matches("J'aime le CAFÉ"), "Should match uppercase CAFÉ");
  ASCIICHAT_REQUIRE(line_matches("J'aime le Café"), "Should match mixed case Café");
  ASCIICHAT_REQUIRE_NOT(line_matches("J'aime le cafe"), "Should not match cafe (no accent)");
}

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_greek", "[log_filter]") {
  // Greek case-insensitive
  asciichat_error_t result = grep_init("/ελληνικά/iF");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Μιλάω ελληνικά"), "Should match lowercase");
  ASCIICHAT_REQUIRE(line_matches("Μιλάω ΕΛΛΗΝΙΚΆ"), "Should match uppercase");
}

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_cyrillic", "[log_filter]") {
  // Cyrillic case-insensitive
  asciichat_error_t result = grep_init("/русский/iF");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Я говорю по-русский"), "Should match lowercase");
  ASCIICHAT_REQUIRE(line_matches("Я говорю по-РУССКИЙ"), "Should match uppercase");
  ASCIICHAT_REQUIRE(line_matches("Я говорю по-Русский"), "Should match mixed case");
}

TEST_CASE("log_filter/utf8_fixed_string_case_insensitive_mixed", "[log_filter]") {
  // Mixed scripts with case-insensitive
  asciichat_error_t result = grep_init("/Café Μπαρ/iF");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Welcome to Café Μπαρ"), "Should match mixed case");
  ASCIICHAT_REQUIRE(line_matches("Welcome to CAFÉ ΜΠΑΡ"), "Should match all uppercase");
  ASCIICHAT_REQUIRE(line_matches("Welcome to café μπαρ"), "Should match all lowercase");
}

/* ============================================================================
 * UTF-8 Regex Tests
 * ============================================================================ */

TEST_CASE("log_filter/utf8_regex_ascii", "[log_filter]") {
  // ASCII regex patterns
  asciichat_error_t result = grep_init("/test[0-9]+/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("test123 passed"), "Should match test followed by digits");
  ASCIICHAT_REQUIRE_NOT(line_matches("test passed"), "Should not match test without digits");
}

TEST_CASE("log_filter/utf8_regex_unicode_class", "[log_filter]") {
  // Unicode character class (any letter)
  asciichat_error_t result = grep_init("/café.*français/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Le café est français"), "Should match with accents");
  ASCIICHAT_REQUIRE(line_matches("Un café très français"), "Should match with .* in between");
  ASCIICHAT_REQUIRE_NOT(line_matches("Le cafe est francais"), "Should not match without accents");
}

TEST_CASE("log_filter/utf8_regex_case_insensitive", "[log_filter]") {
  // Regex with case-insensitive flag
  asciichat_error_t result = grep_init("/café|thé/i");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("J'aime le café"), "Should match café");
  ASCIICHAT_REQUIRE(line_matches("J'aime le CAFÉ"), "Should match CAFÉ (case-insensitive)");
  ASCIICHAT_REQUIRE(line_matches("J'aime le thé"), "Should match thé");
  ASCIICHAT_REQUIRE(line_matches("J'aime le THÉ"), "Should match THÉ (case-insensitive)");
}

TEST_CASE("log_filter/utf8_regex_greek_pattern", "[log_filter]") {
  // Greek word boundary
  asciichat_error_t result = grep_init("/\\bελληνικά\\b/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Μιλάω ελληνικά καλά"), "Should match Greek word");
  ASCIICHAT_REQUIRE_NOT(line_matches("Μιλάω ελληνικάς καλά"), "Should not match with suffix");
}

TEST_CASE("log_filter/utf8_regex_cyrillic_alternation", "[log_filter]") {
  // Cyrillic alternation pattern
  asciichat_error_t result = grep_init("/(русский|английский)/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Я говорю по-русский"), "Should match русский");
  ASCIICHAT_REQUIRE(line_matches("Я говорю по-английский"), "Should match английский");
  ASCIICHAT_REQUIRE_NOT(line_matches("Я говорю по-французский"), "Should not match французский");
}

TEST_CASE("log_filter/utf8_regex_mixed_scripts", "[log_filter]") {
  // Pattern with multiple Unicode scripts
  asciichat_error_t result = grep_init("/Hello.*你好.*Привет/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  ASCIICHAT_REQUIRE(line_matches("Hello world 你好 世界 Привет мир"), "Should match mixed scripts");
  ASCIICHAT_REQUIRE_NOT(line_matches("Hello world 你好"), "Should not match without Russian");
}

/* ============================================================================
 * Cleanup Tests
 * ============================================================================ */

TEST_CASE("log_filter/destroy_idempotent", "[log_filter]") {
  asciichat_error_t result = grep_init("/test/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Pattern should be valid");

  // Multiple destroys should not crash
  grep_destroy();
  grep_destroy();
  grep_destroy();
}

TEST_CASE("log_filter/reinitialize_after_destroy", "[log_filter]") {
  asciichat_error_t result = grep_init("/test/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "First pattern should be valid");
  ASCIICHAT_REQUIRE(line_matches("test message"), "First pattern should match");

  grep_destroy();

  result = grep_init("/other/");
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Second pattern should be valid");
  ASCIICHAT_REQUIRE(line_matches("other message"), "Second pattern should match");
  ASCIICHAT_REQUIRE_NOT(line_matches("test message"), "Old pattern should not match");
}

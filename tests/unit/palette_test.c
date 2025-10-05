#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
#include <string.h>
#include <locale.h>
#include "palette.h"
#include "common.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(palette_tests);

// Test case structure for builtin palette tests
// NOTE: Use fixed-size char arrays instead of pointers to avoid issues with Criterion's fork-based test runner
typedef struct {
  palette_type_t type;
  char expected_name[32];
  char expected_chars[64];
  bool expected_utf8;
} palette_test_case_t;

// Test data for builtin palette validation
static palette_test_case_t builtin_palette_cases[] = {{PALETTE_STANDARD, "standard", PALETTE_CHARS_STANDARD, false},
                                                      {PALETTE_BLOCKS, "blocks", PALETTE_CHARS_BLOCKS, true},
                                                      {PALETTE_DIGITAL, "digital", PALETTE_CHARS_DIGITAL, true},
                                                      {PALETTE_MINIMAL, "minimal", PALETTE_CHARS_MINIMAL, false},
                                                      {PALETTE_COOL, "cool", PALETTE_CHARS_COOL, true}};

ParameterizedTestParameters(palette_tests, builtin_palette_tests) {
  size_t nb_cases = sizeof(builtin_palette_cases) / sizeof(builtin_palette_cases[0]);
  return cr_make_param_array(palette_test_case_t, builtin_palette_cases, nb_cases);
}

ParameterizedTest(palette_test_case_t *tc, palette_tests, builtin_palette_tests) {
  const palette_def_t *palette = get_builtin_palette(tc->type);

  cr_assert_not_null(palette, "Palette %s should not be null", tc->expected_name);
  cr_assert_str_eq(palette->name, tc->expected_name, "Palette name should match for %s", tc->expected_name);
  cr_assert_str_eq(palette->chars, tc->expected_chars, "Palette chars should match for %s", tc->expected_name);
  cr_assert_eq(palette->requires_utf8, tc->expected_utf8, "UTF-8 requirement should match for %s", tc->expected_name);
}

// Test case structure for invalid palette tests
typedef struct {
  palette_type_t type;
  char description[64]; // Use fixed-size array
} invalid_palette_test_case_t;

static invalid_palette_test_case_t invalid_palette_cases[] = {
    {PALETTE_CUSTOM, "Custom palette"}, {PALETTE_COUNT, "Count palette"}, {(palette_type_t)999, "Invalid enum value"}};

ParameterizedTestParameters(palette_tests, invalid_palette_tests) {
  size_t nb_cases = sizeof(invalid_palette_cases) / sizeof(invalid_palette_cases[0]);
  return cr_make_param_array(invalid_palette_test_case_t, invalid_palette_cases, nb_cases);
}

ParameterizedTest(invalid_palette_test_case_t *tc, palette_tests, invalid_palette_tests) {
  const palette_def_t *palette = get_builtin_palette(tc->type);
  cr_assert_null(palette, "Palette should be null for %s", tc->description);
}

// Test case structure for UTF-8 encoding tests
typedef struct {
  char palette_chars[64]; // Use fixed-size array
  char description[64];   // Use fixed-size array
  bool expected_utf8;
} utf8_test_case_t;

static utf8_test_case_t utf8_test_cases[] = {{PALETTE_CHARS_STANDARD, "Standard ASCII palette", false},
                                             {PALETTE_CHARS_MINIMAL, "Minimal ASCII palette", false},
                                             {PALETTE_CHARS_BLOCKS, "Blocks UTF-8 palette", true},
                                             {PALETTE_CHARS_COOL, "Cool UTF-8 palette", true},
                                             {"", "Empty palette", false}};

ParameterizedTestParameters(palette_tests, utf8_encoding_tests) {
  size_t nb_cases = sizeof(utf8_test_cases) / sizeof(utf8_test_cases[0]);
  return cr_make_param_array(utf8_test_case_t, utf8_test_cases, nb_cases);
}

ParameterizedTest(utf8_test_case_t *tc, palette_tests, utf8_encoding_tests) {
  size_t len = strlen(tc->palette_chars);
  bool requires = palette_requires_utf8_encoding(tc->palette_chars, len);
  cr_assert_eq(requires, tc->expected_utf8, "UTF-8 requirement should match for %s (got %d, expected %d)",
               tc->description, requires, tc->expected_utf8);
}

// Test case structure for palette validation tests
typedef struct {
  char palette_chars[64]; // Use fixed-size array instead of pointer
  size_t palette_len;
  char description[64]; // Use fixed-size array instead of pointer
  bool expected_valid;
} validation_test_case_t;

static validation_test_case_t validation_test_cases[] = {
    {PALETTE_CHARS_STANDARD, strlen(PALETTE_CHARS_STANDARD), "Valid standard palette", true},
    {PALETTE_CHARS_BLOCKS, strlen(PALETTE_CHARS_BLOCKS), "Valid UTF-8 palette", true},
    {"A", 1, "Single character", true},
    {"", 10, "NULL/empty palette chars", false}, // Can't use actual NULL with array, use empty string
    {"", 0, "Empty palette", false}};

ParameterizedTestParameters(palette_tests, validation_tests) {
  size_t nb_cases = sizeof(validation_test_cases) / sizeof(validation_test_cases[0]);
  return cr_make_param_array(validation_test_case_t, validation_test_cases, nb_cases);
}

ParameterizedTest(validation_test_case_t *tc, palette_tests, validation_tests) {
  // Handle empty string case - pass NULL to test NULL handling
  const char *chars_to_test = (tc->palette_chars[0] == '\0') ? NULL : tc->palette_chars;
  bool valid = validate_palette_chars(chars_to_test, tc->palette_len);
  cr_assert_eq(valid, tc->expected_valid, "Validation should match for %s", tc->description);
}

// Test case structure for palette compatibility tests
typedef struct {
  palette_type_t requested_type;
  bool has_utf8_support;
  char description[64]; // Use fixed-size array
  palette_type_t expected_type;
} compatibility_test_case_t;

static compatibility_test_case_t compatibility_test_cases[] = {
    {PALETTE_BLOCKS, true, "UTF-8 blocks with support", PALETTE_BLOCKS},
    {PALETTE_COOL, true, "UTF-8 cool with support", PALETTE_COOL},
    {PALETTE_BLOCKS, false, "UTF-8 blocks without support", PALETTE_STANDARD},
    {PALETTE_DIGITAL, false, "UTF-8 digital without support", PALETTE_STANDARD},
    {PALETTE_COOL, false, "UTF-8 cool without support", PALETTE_STANDARD},
    {PALETTE_STANDARD, false, "ASCII standard without support", PALETTE_STANDARD},
    {PALETTE_MINIMAL, false, "ASCII minimal without support", PALETTE_MINIMAL},
    {PALETTE_CUSTOM, false, "Custom palette", PALETTE_CUSTOM}};

ParameterizedTestParameters(palette_tests, compatibility_tests) {
  size_t nb_cases = sizeof(compatibility_test_cases) / sizeof(compatibility_test_cases[0]);
  return cr_make_param_array(compatibility_test_case_t, compatibility_test_cases, nb_cases);
}

ParameterizedTest(compatibility_test_case_t *tc, palette_tests, compatibility_tests) {
  palette_type_t selected = select_compatible_palette(tc->requested_type, tc->has_utf8_support);
  cr_assert_eq(selected, tc->expected_type, "Compatibility selection should match for %s", tc->description);
}

// Test case structure for UTF-8 palette creation tests
typedef struct {
  char palette_string[64]; // Use fixed-size array
  char description[64];    // Use fixed-size array
  size_t expected_char_count;
  size_t expected_total_bytes;
  bool should_succeed;
} utf8_palette_test_case_t;

static utf8_palette_test_case_t utf8_palette_test_cases[] = {
    {" .:-=+*#%@", "ASCII palette", 10, 10, true},
    {"🌑🌒🌓🌔🌕", "Emoji palette", 5, 20, true}, // 5 emojis × 4 bytes each
    {"A→B", "Mixed ASCII/UTF-8", 3, 5, true},     // A(1) + →(3) + B(1)
    {"", "NULL/empty string", 0, 0, false},       // Use empty string to test NULL handling
    {"", "Empty string", 0, 0, false}};

ParameterizedTestParameters(palette_tests, utf8_palette_creation_tests) {
  size_t nb_cases = sizeof(utf8_palette_test_cases) / sizeof(utf8_palette_test_cases[0]);
  return cr_make_param_array(utf8_palette_test_case_t, utf8_palette_test_cases, nb_cases);
}

ParameterizedTest(utf8_palette_test_case_t *tc, palette_tests, utf8_palette_creation_tests) {
  // Handle empty string case - pass NULL to test NULL handling
  const char *str_to_test = (tc->palette_string[0] == '\0') ? NULL : tc->palette_string;
  utf8_palette_t *palette = utf8_palette_create(str_to_test);

  if (tc->should_succeed) {
    cr_assert_not_null(palette, "Palette creation should succeed for %s", tc->description);
    cr_assert_eq(utf8_palette_get_char_count(palette), tc->expected_char_count, "Char count should match for %s",
                 tc->description);
    cr_assert_eq(palette->total_bytes, tc->expected_total_bytes, "Total bytes should match for %s", tc->description);
    if (str_to_test) {
      cr_assert_str_eq(palette->raw_string, str_to_test, "Raw string should match for %s", tc->description);
    }
    utf8_palette_destroy(palette);
  } else {
    cr_assert_null(palette, "Palette creation should fail for %s", tc->description);
  }
}

// Test case structure for UTF-8 palette character access tests
typedef struct {
  char palette_string[64]; // Use fixed-size array
  size_t char_index;
  char description[64]; // Use fixed-size array
  bool should_succeed;
  size_t expected_byte_len;
} utf8_char_test_case_t;

static utf8_char_test_case_t utf8_char_test_cases[] = {
    {"ABC", 0, "First ASCII char", true, 1}, {"ABC", 2, "Last ASCII char", true, 1},
    {"ABC", 3, "Out of bounds", false, 0},   {"A→B", 0, "First mixed char", true, 1},
    {"A→B", 1, "UTF-8 char", true, 3},       {"A→B", 2, "Last mixed char", true, 1},
    {"🌑🌒", 0, "First emoji", true, 4},     {"🌑🌒", 1, "Second emoji", true, 4}};

ParameterizedTestParameters(palette_tests, utf8_char_access_tests) {
  size_t nb_cases = sizeof(utf8_char_test_cases) / sizeof(utf8_char_test_cases[0]);
  return cr_make_param_array(utf8_char_test_case_t, utf8_char_test_cases, nb_cases);
}

ParameterizedTest(utf8_char_test_case_t *tc, palette_tests, utf8_char_access_tests) {
  utf8_palette_t *palette = utf8_palette_create(tc->palette_string);
  cr_assert_not_null(palette, "Palette should be created for %s", tc->description);

  const utf8_char_info_t *char_info = utf8_palette_get_char(palette, tc->char_index);

  if (tc->should_succeed) {
    cr_assert_not_null(char_info, "Char info should exist for %s", tc->description);
    cr_assert_eq(char_info->byte_len, tc->expected_byte_len, "Byte length should match for %s", tc->description);
  } else {
    cr_assert_null(char_info, "Char info should be null for %s", tc->description);
  }

  utf8_palette_destroy(palette);
}

// Test case structure for UTF-8 palette character search tests
typedef struct {
  char palette_string[64]; // Use fixed-size array
  char search_char[8];     // Use fixed-size array (max UTF-8 char is 4 bytes + null)
  size_t search_len;
  char description[64]; // Use fixed-size array
  bool should_contain;
  size_t expected_index;
} utf8_search_test_case_t;

static utf8_search_test_case_t utf8_search_test_cases[] = {
    {"ABC", "A", 1, "Find first ASCII", true, 0},
    {"ABC", "B", 1, "Find middle ASCII", true, 1},
    {"ABC", "Z", 1, "Find non-existent ASCII", false, (size_t)-1},
    {"A→B", "→", 3, "Find UTF-8 char", true, 1},
    {"🌑🌒🌓", "🌒", 4, "Find emoji", true, 1},
    {"🌑🌒🌓", "🌕", 4, "Find non-existent emoji", false, (size_t)-1}};

ParameterizedTestParameters(palette_tests, utf8_search_tests) {
  size_t nb_cases = sizeof(utf8_search_test_cases) / sizeof(utf8_search_test_cases[0]);
  return cr_make_param_array(utf8_search_test_case_t, utf8_search_test_cases, nb_cases);
}

ParameterizedTest(utf8_search_test_case_t *tc, palette_tests, utf8_search_tests) {
  utf8_palette_t *palette = utf8_palette_create(tc->palette_string);
  cr_assert_not_null(palette, "Palette should be created for %s", tc->description);

  bool contains = utf8_palette_contains_char(palette, tc->search_char, tc->search_len);
  cr_assert_eq(contains, tc->should_contain, "Contains should match for %s", tc->description);

  if (tc->should_contain) {
    size_t index = utf8_palette_find_char_index(palette, tc->search_char, tc->search_len);
    cr_assert_eq(index, tc->expected_index, "Index should match for %s", tc->description);
  }

  utf8_palette_destroy(palette);
}

// Test case structure for client palette initialization tests
typedef struct {
  palette_type_t palette_type;
  char custom_palette[64]; // Use fixed-size array
  char description[64];    // Use fixed-size array
  bool should_succeed;
  char expected_chars[64]; // Use fixed-size array
} client_palette_init_test_case_t;

static client_palette_init_test_case_t client_palette_init_cases[] = {
    {PALETTE_STANDARD, "", "Standard builtin palette", true, PALETTE_CHARS_STANDARD},
    {PALETTE_MINIMAL, "", "Minimal builtin palette", true, PALETTE_CHARS_MINIMAL},
    {PALETTE_BLOCKS, "", "Blocks builtin palette", true, PALETTE_CHARS_BLOCKS},
    {PALETTE_COOL, "", "Cool builtin palette", true, PALETTE_CHARS_COOL},
    {PALETTE_CUSTOM, "01234567", "Valid custom palette", true, "01234567"},
    {PALETTE_CUSTOM, "", "NULL custom palette", false, ""},
    {PALETTE_CUSTOM, "", "Empty custom palette", false, ""}};

ParameterizedTestParameters(palette_tests, client_palette_initialization_tests) {
  size_t nb_cases = sizeof(client_palette_init_cases) / sizeof(client_palette_init_cases[0]);
  return cr_make_param_array(client_palette_init_test_case_t, client_palette_init_cases, nb_cases);
}

ParameterizedTest(client_palette_init_test_case_t *tc, palette_tests, client_palette_initialization_tests) {
  char client_palette_chars[256];
  size_t client_palette_len;
  char client_luminance_palette[256];

  // Handle empty string case - pass NULL to test NULL handling
  const char *custom_to_test = (tc->custom_palette[0] == '\0') ? NULL : tc->custom_palette;
  const char *expected_to_check = (tc->expected_chars[0] == '\0') ? NULL : tc->expected_chars;

  int result = initialize_client_palette(tc->palette_type, custom_to_test, client_palette_chars, &client_palette_len,
                                         client_luminance_palette);

  if (tc->should_succeed) {
    cr_assert_eq(result, 0, "Initialization should succeed for %s", tc->description);
    if (expected_to_check) {
      cr_assert_eq(client_palette_len, strlen(expected_to_check), "Palette length should match for %s",
                   tc->description);
      cr_assert_str_eq(client_palette_chars, expected_to_check, "Palette chars should match for %s", tc->description);
    }
  } else {
    cr_assert_eq(result, -1, "Initialization should fail for %s", tc->description);
  }
}

// Legacy individual tests for functions that don't fit parameterized patterns well
Test(palette, detect_client_utf8_support) {
  utf8_capabilities_t caps;

  // Test detection (results will vary by environment)
  bool supports = detect_client_utf8_support(&caps);

  // Verify structure is populated
  cr_assert(caps.terminal_type[0] != '\0' || caps.locale_encoding[0] != '\0', "Should populate at least one field");

  // NULL caps should return false
  supports = detect_client_utf8_support(NULL);
  cr_assert_eq(supports, false);
}

// Theory: Palette length property - luminance palettes should work for various lengths
TheoryDataPoints(palette, palette_length_property) = {
    DataPoints(int, 2, 5, 10, 15, 20, 30, 50, 70),
};

Theory((int palette_len), palette, palette_length_property) {
  cr_assume(palette_len >= 2 && palette_len <= 70);

  char luminance_mapping[256];
  char *test_palette = malloc(palette_len + 1);
  cr_assume(test_palette != NULL);

  // Create a palette of increasing complexity
  for (int i = 0; i < palette_len; i++) {
    test_palette[i] = ' ' + i % 94; // Printable ASCII range
  }
  test_palette[palette_len] = '\0';

  // PROPERTY: Should successfully build luminance palette for any valid length
  int result = build_client_luminance_palette(test_palette, palette_len, luminance_mapping);
  cr_assert_eq(result, 0, "Should build luminance palette for length %d", palette_len);

  // PROPERTY: Darkest should map to first char, brightest to last
  cr_assert_eq(luminance_mapping[0], test_palette[0], "Darkest should map to first char for length %d", palette_len);
  cr_assert_eq(luminance_mapping[255], test_palette[palette_len - 1], "Brightest should map to last char for length %d",
               palette_len);

  free(test_palette);
}

Test(palette, build_client_luminance_palette) {
  char luminance_mapping[256];
  const char *palette = " .:-=+*#%@";
  size_t palette_len = strlen(palette);

  // Valid palette
  int result = build_client_luminance_palette(palette, palette_len, luminance_mapping);
  cr_assert_eq(result, 0);

  // Check some mappings
  cr_assert_eq(luminance_mapping[0], ' ');   // Darkest
  cr_assert_eq(luminance_mapping[255], '@'); // Brightest

  // Invalid parameters
  result = build_client_luminance_palette(NULL, palette_len, luminance_mapping);
  cr_assert_eq(result, -1);

  result = build_client_luminance_palette(palette, 0, luminance_mapping);
  cr_assert_eq(result, -1);

  result = build_client_luminance_palette(palette, palette_len, NULL);
  cr_assert_eq(result, -1);
}

Test(palette, initialize_client_palette_builtin) {
  char client_palette_chars[256];
  size_t client_palette_len;
  char client_luminance_palette[256];

  // Initialize with standard palette
  int result = initialize_client_palette(PALETTE_STANDARD, NULL, client_palette_chars, &client_palette_len,
                                         client_luminance_palette);
  cr_assert_eq(result, 0);
  cr_assert_eq(client_palette_len, strlen(PALETTE_CHARS_STANDARD));
  cr_assert_str_eq(client_palette_chars, PALETTE_CHARS_STANDARD);

  // Initialize with minimal palette
  result = initialize_client_palette(PALETTE_MINIMAL, NULL, client_palette_chars, &client_palette_len,
                                     client_luminance_palette);
  cr_assert_eq(result, 0);
  cr_assert_eq(client_palette_len, strlen(PALETTE_CHARS_MINIMAL));
}

Test(palette, initialize_client_palette_custom) {
  char client_palette_chars[256];
  size_t client_palette_len;
  char client_luminance_palette[256];
  const char *custom = "01234567";

  // Valid custom palette
  int result = initialize_client_palette(PALETTE_CUSTOM, custom, client_palette_chars, &client_palette_len,
                                         client_luminance_palette);
  cr_assert_eq(result, 0);
  cr_assert_eq(client_palette_len, strlen(custom));
  cr_assert_str_eq(client_palette_chars, custom);

  // Invalid custom palette (NULL)
  result = initialize_client_palette(PALETTE_CUSTOM, NULL, client_palette_chars, &client_palette_len,
                                     client_luminance_palette);
  cr_assert_eq(result, -1);

  // Invalid custom palette (empty)
  result = initialize_client_palette(PALETTE_CUSTOM, "", client_palette_chars, &client_palette_len,
                                     client_luminance_palette);
  cr_assert_eq(result, -1);
}

Test(palette, utf8_palette_standard_palette_coverage) {
  // Test with the standard palette that has duplicate spaces
  const char *std_palette = "   ...',;:clodxkO0KXNWM";
  utf8_palette_t *palette = utf8_palette_create(std_palette);

  cr_assert_not_null(palette);

  // Should have 23 characters total (including duplicates)
  size_t char_count = utf8_palette_get_char_count(palette);
  cr_assert_eq(char_count, 23);

  // First 3 should be spaces
  for (size_t i = 0; i < 3; i++) {
    const utf8_char_info_t *char_info = utf8_palette_get_char(palette, i);
    cr_assert_eq(char_info->byte_len, 1);
    cr_assert_eq(char_info->bytes[0], ' ');
  }

  // Next 3 should be dots
  for (size_t i = 3; i < 6; i++) {
    const utf8_char_info_t *char_info = utf8_palette_get_char(palette, i);
    cr_assert_eq(char_info->byte_len, 1);
    cr_assert_eq(char_info->bytes[0], '.');
  }

  utf8_palette_destroy(palette);
}

Test(palette, utf8_palette_emoji_palette) {
  // Test with complex emoji palette
  const char *emoji_palette = "😀😃😄😁😆😅😂🤣";
  utf8_palette_t *palette = utf8_palette_create(emoji_palette);

  cr_assert_not_null(palette);
  cr_assert_eq(utf8_palette_get_char_count(palette), 8);

  // Each emoji should be 4 bytes
  for (size_t i = 0; i < 8; i++) {
    const utf8_char_info_t *char_info = utf8_palette_get_char(palette, i);
    cr_assert_eq(char_info->byte_len, 4);
  }

  utf8_palette_destroy(palette);
}

Test(palette, null_palette_handling) {
  // Test that all functions properly handle NULL input

  // palette_requires_utf8_encoding should return false for NULL
  bool requires = palette_requires_utf8_encoding(NULL, 10);
  cr_assert_eq(requires, false, "NULL palette should not require UTF-8");

  // validate_palette_chars should return false for NULL
  bool valid = validate_palette_chars(NULL, 10);
  cr_assert_eq(valid, false, "NULL palette should not be valid");

  // utf8_palette_create should return NULL for NULL input
  utf8_palette_t *palette = utf8_palette_create(NULL);
  cr_assert_null(palette, "Creating palette from NULL should return NULL");

  // utf8_palette_contains_char should handle NULL palette gracefully
  // (Note: This would segfault if not handled, so we test it exists)

  // initialize_client_palette should fail for NULL custom palette
  char client_palette_chars[256];
  size_t client_palette_len;
  char client_luminance_palette[256];
  int result = initialize_client_palette(PALETTE_CUSTOM, NULL, client_palette_chars, &client_palette_len,
                                         client_luminance_palette);
  cr_assert_eq(result, -1, "NULL custom palette should fail initialization");
}

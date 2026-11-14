#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <locale.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>

// Use the enhanced macro to create complete test suite with basic quiet logging
// Test case structure for builtin palette tests
// NOTE: Use fixed-size char arrays instead of pointers to avoid issues with Catch2's fork-based test runner
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



TEST_CASE("palette_tests/builtin_palette_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(builtin_palette_cases)); auto *tc = &generated_case;
  const palette_def_t *palette = get_builtin_palette(tc->type);

  ASCIICHAT_REQUIRE_NOT_NULL(palette, "Palette %s should not be null", tc->expected_name);
  ASCIICHAT_REQUIRE_STR_EQ(palette->name, tc->expected_name, "Palette name should match for %s", tc->expected_name);
  ASCIICHAT_REQUIRE_STR_EQ(palette->chars, tc->expected_chars, "Palette chars should match for %s", tc->expected_name);
  ASCIICHAT_REQUIRE_EQ(palette->requires_utf8, tc->expected_utf8, "UTF-8 requirement should match for %s", tc->expected_name);
}

// Test case structure for invalid palette tests
typedef struct {
  palette_type_t type;
  char description[64]; // Use fixed-size array
} invalid_palette_test_case_t;

static invalid_palette_test_case_t invalid_palette_cases[] = {
    {PALETTE_CUSTOM, "Custom palette"}, {PALETTE_COUNT, "Count palette"}, {(palette_type_t)999, "Invalid enum value"}};



TEST_CASE("palette_tests/invalid_palette_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(invalid_palette_cases)); auto *tc = &generated_case;
  const palette_def_t *palette = get_builtin_palette(tc->type);
  ASCIICHAT_REQUIRE_NULL(palette, "Palette should be null for %s", tc->description);
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



TEST_CASE("palette_tests/utf8_encoding_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(utf8_test_cases)); auto *tc = &generated_case;
  size_t len = strlen(tc->palette_chars);
  bool requires_utf8 = palette_requires_utf8_encoding(tc->palette_chars, len);
  ASCIICHAT_REQUIRE_EQ(requires_utf8, tc->expected_utf8, "UTF-8 requirement should match for %s (got %d, expected %d)",
               tc->description, requires_utf8, tc->expected_utf8);
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



TEST_CASE("palette_tests/validation_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(validation_test_cases)); auto *tc = &generated_case;
  // Handle empty string case - pass NULL to test NULL handling
  const char *chars_to_test = (tc->palette_chars[0] == '\0') ? NULL : tc->palette_chars;
  bool valid = validate_palette_chars(chars_to_test, tc->palette_len);
  ASCIICHAT_REQUIRE_EQ(valid, tc->expected_valid, "Validation should match for %s", tc->description);
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



TEST_CASE("palette_tests/compatibility_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(compatibility_test_cases)); auto *tc = &generated_case;
  palette_type_t selected = select_compatible_palette(tc->requested_type, tc->has_utf8_support);
  ASCIICHAT_REQUIRE_EQ(selected, tc->expected_type, "Compatibility selection should match for %s", tc->description);
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



TEST_CASE("palette_tests/utf8_palette_creation_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(utf8_palette_test_cases)); auto *tc = &generated_case;
  // Handle empty string case - pass NULL to test NULL handling
  const char *str_to_test = (tc->palette_string[0] == '\0') ? NULL : tc->palette_string;
  utf8_palette_t *palette = utf8_palette_create(str_to_test);

  if (tc->should_succeed) {
    ASCIICHAT_REQUIRE_NOT_NULL(palette, "Palette creation should succeed for %s", tc->description);
    ASCIICHAT_REQUIRE_EQ(utf8_palette_get_char_count(palette), tc->expected_char_count, "Char count should match for %s",
                 tc->description);
    ASCIICHAT_REQUIRE_EQ(palette->total_bytes, tc->expected_total_bytes, "Total bytes should match for %s", tc->description);
    if (str_to_test) {
      ASCIICHAT_REQUIRE_STR_EQ(palette->raw_string, str_to_test, "Raw string should match for %s", tc->description);
    }
    utf8_palette_destroy(palette);
  } else {
    ASCIICHAT_REQUIRE_NULL(palette, "Palette creation should fail for %s", tc->description);
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



TEST_CASE("palette_tests/utf8_char_access_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(utf8_char_test_cases)); auto *tc = &generated_case;
  utf8_palette_t *palette = utf8_palette_create(tc->palette_string);
  ASCIICHAT_REQUIRE_NOT_NULL(palette, "Palette should be created for %s", tc->description);

  const utf8_char_info_t *char_info = utf8_palette_get_char(palette, tc->char_index);

  if (tc->should_succeed) {
    ASCIICHAT_REQUIRE_NOT_NULL(char_info, "Char info should exist for %s", tc->description);
    ASCIICHAT_REQUIRE_EQ(char_info->byte_len, tc->expected_byte_len, "Byte length should match for %s", tc->description);
  } else {
    ASCIICHAT_REQUIRE_NULL(char_info, "Char info should be null for %s", tc->description);
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



TEST_CASE("palette_tests/utf8_search_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(utf8_search_test_cases)); auto *tc = &generated_case;
  utf8_palette_t *palette = utf8_palette_create(tc->palette_string);
  ASCIICHAT_REQUIRE_NOT_NULL(palette, "Palette should be created for %s", tc->description);

  bool contains = utf8_palette_contains_char(palette, tc->search_char, tc->search_len);
  ASCIICHAT_REQUIRE_EQ(contains, tc->should_contain, "Contains should match for %s", tc->description);

  if (tc->should_contain) {
    size_t index = utf8_palette_find_char_index(palette, tc->search_char, tc->search_len);
    ASCIICHAT_REQUIRE_EQ(index, tc->expected_index, "Index should match for %s", tc->description);
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



TEST_CASE("palette_tests/client_palette_initialization_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(client_palette_init_cases)); auto *tc = &generated_case;
  char client_palette_chars[256];
  size_t client_palette_len;
  char client_luminance_palette[256];

  // Handle empty string case - pass NULL to test NULL handling
  const char *custom_to_test = (tc->custom_palette[0] == '\0') ? NULL : tc->custom_palette;
  const char *expected_to_check = (tc->expected_chars[0] == '\0') ? NULL : tc->expected_chars;

  int result = initialize_client_palette(tc->palette_type, custom_to_test, client_palette_chars, &client_palette_len,
                                         client_luminance_palette);

  if (tc->should_succeed) {
    ASCIICHAT_REQUIRE_EQ(result, 0, "Initialization should succeed for %s", tc->description);
    if (expected_to_check) {
      ASCIICHAT_REQUIRE_EQ(client_palette_len, strlen(expected_to_check), "Palette length should match for %s",
                   tc->description);
      ASCIICHAT_REQUIRE_STR_EQ(client_palette_chars, expected_to_check, "Palette chars should match for %s", tc->description);
    }
  } else {
    ASCIICHAT_REQUIRE_EQ(result, -1, "Initialization should fail for %s", tc->description);
  }
}

// Legacy individual tests for functions that don't fit parameterized patterns well
TEST_CASE("palette/detect_client_utf8_support", "[palette]") {
  utf8_capabilities_t caps;

  // Test detection (results will vary by environment)
  (void)detect_client_utf8_support(&caps);

  // Verify structure is populated
  ASCIICHAT_REQUIRE(caps.terminal_type[0] != '\0' || caps.locale_encoding[0] != '\0', "Should populate at least one field");

  // NULL caps should return false
  bool supports = detect_client_utf8_support(NULL);
  ASCIICHAT_REQUIRE_EQ(supports, false);
}

// Theory: Palette length property - luminance palettes should work for various lengths
static constexpr int palette_length_cases[] = {2, 5, 10, 15, 20, 30, 50, 70};

TEST_CASE("palette/palette_length_property", "[palette]") {
  const int palette_len = GENERATE(Catch::Generators::from_range(palette_length_cases));
  ASCIICHAT_ASSUME(palette_len >= 2 && palette_len <= 70);

  char luminance_mapping[256];
  char *test_palette = SAFE_MALLOC(palette_len + 1, char *);
  ASCIICHAT_ASSUME(test_palette != NULL);

  // Create a palette of increasing complexity
  for (int i = 0; i < palette_len; i++) {
    test_palette[i] = ' ' + i % 94; // Printable ASCII range
  }
  test_palette[palette_len] = '\0';

  // PROPERTY: Should successfully build luminance palette for any valid length
  int result = build_client_luminance_palette(test_palette, palette_len, luminance_mapping);
  ASCIICHAT_REQUIRE_EQ(result, 0, "Should build luminance palette for length %d", palette_len);

  // PROPERTY: Darkest should map to first char, brightest to last
  ASCIICHAT_REQUIRE_EQ(luminance_mapping[0], test_palette[0], "Darkest should map to first char for length %d", palette_len);
  ASCIICHAT_REQUIRE_EQ(luminance_mapping[255], test_palette[palette_len - 1], "Brightest should map to last char for length %d",
               palette_len);

  SAFE_FREE(test_palette);
}

// Test case structure for luminance palette error handling
typedef struct {
  char palette_chars[64];
  size_t palette_len;
  bool pass_null_output;
  char description[64];
  int expected_result;
} luminance_error_test_case_t;

static luminance_error_test_case_t luminance_error_cases[] = {{" .:-=+*#%@", 10, false, "Valid palette", 0},
                                                              {"", 10, false, "NULL palette chars", -1},
                                                              {" .:-=+*#%@", 0, false, "Zero length", -1},
                                                              {" .:-=+*#%@", 10, true, "NULL output buffer", -1}};



TEST_CASE("palette_tests/luminance_palette_error_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(luminance_error_cases)); auto *tc = &generated_case;
  char luminance_mapping[256] = {0};
  const char *palette_to_test = (tc->palette_chars[0] == '\0') ? NULL : tc->palette_chars;
  char *output_buffer = tc->pass_null_output ? NULL : luminance_mapping;

  int result = build_client_luminance_palette(palette_to_test, tc->palette_len, output_buffer);
  ASCIICHAT_REQUIRE_EQ(result, tc->expected_result, "Result should match for %s", tc->description);

  // For valid case, verify the mappings (only when output_buffer was non-NULL)
  if (tc->expected_result == 0 && !tc->pass_null_output) {
    ASCIICHAT_REQUIRE_EQ(luminance_mapping[0], ' ', "Darkest should map to first char for %s", tc->description);
    ASCIICHAT_REQUIRE_EQ(luminance_mapping[255], '@', "Brightest should map to last char for %s", tc->description);
  }
}

// NOTE: initialize_client_palette tests are now handled by parameterized test
// client_palette_initialization_tests (lines 275-302)

// Test case structure for duplicate character handling
typedef struct {
  char palette_string[64];
  char description[64];
  size_t expected_total_count;
  size_t duplicate_start_index;
  size_t duplicate_count;
  char expected_duplicate_char;
} duplicate_test_case_t;

static duplicate_test_case_t duplicate_test_cases[] = {
    {"   ...',;:clodxkO0KXNWM", "Standard palette with duplicate spaces", 23, 0, 3, ' '},
    {"AAA", "Triple ASCII duplicate", 3, 0, 3, 'A'},
    {"ABCAAA", "ASCII with trailing duplicates", 6, 3, 3, 'A'},
    {"🌑🌑🌑", "Triple emoji duplicate", 3, 0, 3, '\0'}, // Special case for emoji
    {"A🌑🌑B", "Mixed with emoji duplicates", 4, 1, 2, '\0'}};



TEST_CASE("palette_tests/duplicate_character_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(duplicate_test_cases)); auto *tc = &generated_case;
  utf8_palette_t *palette = utf8_palette_create(tc->palette_string);
  ASCIICHAT_REQUIRE_NOT_NULL(palette, "Palette should be created for %s", tc->description);

  // Verify total character count
  size_t char_count = utf8_palette_get_char_count(palette);
  ASCIICHAT_REQUIRE_EQ(char_count, tc->expected_total_count, "Total char count should match for %s", tc->description);

  // Verify duplicate characters (skip emoji check if expected_duplicate_char is '\0')
  if (tc->expected_duplicate_char != '\0') {
    for (size_t i = tc->duplicate_start_index; i < tc->duplicate_start_index + tc->duplicate_count; i++) {
      const utf8_char_info_t *char_info = utf8_palette_get_char(palette, i);
      ASCIICHAT_REQUIRE_EQ(char_info->byte_len, 1, "Duplicate char should be 1 byte for %s at index %zu", tc->description, i);
      ASCIICHAT_REQUIRE_EQ(char_info->bytes[0], tc->expected_duplicate_char, "Duplicate char should match for %s at index %zu",
                   tc->description, i);
    }
  }

  utf8_palette_destroy(palette);
}

// Test case structure for emoji palette tests
typedef struct {
  char emoji_palette[128];
  char description[64];
  size_t expected_count;
  size_t expected_bytes_per_char;
} emoji_test_case_t;

static emoji_test_case_t emoji_test_cases[] = {{"😀😃😄😁😆😅😂🤣", "Smiley face emojis (4-byte)", 8, 4},
                                               {"🌑🌒🌓🌔🌕", "Moon phase emojis (4-byte)", 5, 4},
                                               {"🔥💧🌊", "Element emojis (4-byte)", 3, 4}};



TEST_CASE("palette_tests/emoji_palette_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(emoji_test_cases)); auto *tc = &generated_case;
  utf8_palette_t *palette = utf8_palette_create(tc->emoji_palette);

  ASCIICHAT_REQUIRE_NOT_NULL(palette, "Palette should be created for %s", tc->description);
  ASCIICHAT_REQUIRE_EQ(utf8_palette_get_char_count(palette), tc->expected_count, "Emoji count should match for %s",
               tc->description);

  // Each emoji should be expected_bytes_per_char bytes
  for (size_t i = 0; i < tc->expected_count; i++) {
    const utf8_char_info_t *char_info = utf8_palette_get_char(palette, i);
    ASCIICHAT_REQUIRE_EQ(char_info->byte_len, tc->expected_bytes_per_char, "Emoji byte length should match for %s at index %zu",
                 tc->description, i);
  }

  utf8_palette_destroy(palette);
}

// Test case structure for NULL handling tests
typedef enum {
  NULL_TEST_REQUIRES_UTF8,
  NULL_TEST_VALIDATE_CHARS,
  NULL_TEST_CREATE_PALETTE,
  NULL_TEST_INIT_CUSTOM_PALETTE
} null_test_function_t;

typedef struct {
  null_test_function_t function;
  char description[64];
} null_handling_test_case_t;

static null_handling_test_case_t null_handling_cases[] = {
    {NULL_TEST_REQUIRES_UTF8, "palette_requires_utf8_encoding with NULL"},
    {NULL_TEST_VALIDATE_CHARS, "validate_palette_chars with NULL"},
    {NULL_TEST_CREATE_PALETTE, "utf8_palette_create with NULL"},
    {NULL_TEST_INIT_CUSTOM_PALETTE, "initialize_client_palette with NULL custom"}};



TEST_CASE("palette_tests/null_handling_tests", "[palette_tests]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(null_handling_cases)); auto *tc = &generated_case;
  switch (tc->function) {
  case NULL_TEST_REQUIRES_UTF8: {
    bool requires_utf8 = palette_requires_utf8_encoding(NULL, 10);
    ASCIICHAT_REQUIRE_EQ(requires_utf8, false, "%s should return false", tc->description);
    break;
  }
  case NULL_TEST_VALIDATE_CHARS: {
    bool valid = validate_palette_chars(NULL, 10);
    ASCIICHAT_REQUIRE_EQ(valid, false, "%s should return false", tc->description);
    break;
  }
  case NULL_TEST_CREATE_PALETTE: {
    utf8_palette_t *palette = utf8_palette_create(NULL);
    ASCIICHAT_REQUIRE_NULL(palette, "%s should return NULL", tc->description);
    break;
  }
  case NULL_TEST_INIT_CUSTOM_PALETTE: {
    char client_palette_chars[256];
    size_t client_palette_len;
    char client_luminance_palette[256];
    int result = initialize_client_palette(PALETTE_CUSTOM, NULL, client_palette_chars, &client_palette_len,
                                           client_luminance_palette);
    ASCIICHAT_REQUIRE_EQ(result, -1, "%s should return -1", tc->description);
    break;
  }
  }
}

// Theory: UTF-8 character boundary property - all palettes should correctly parse UTF-8 boundaries
static constexpr const char *utf8_boundary_cases[] = {
    "ABC", "→→→", "🌑🌑", "A→B🌑C", " .:-=+*#%@", "   ...',;:clodxkO0KXNWM", "😀😃😄😁"};

TEST_CASE("palette/utf8_boundary_property", "[palette]") {
  const char *palette_string = GENERATE(Catch::Generators::from_range(utf8_boundary_cases));
  ASCIICHAT_ASSUME(palette_string != NULL);

  utf8_palette_t *palette = utf8_palette_create(palette_string);
  ASCIICHAT_ASSUME(palette != NULL);

  size_t char_count = utf8_palette_get_char_count(palette);

  // PROPERTY: Sum of all character byte lengths should equal total bytes
  size_t calculated_total = 0;
  for (size_t i = 0; i < char_count; i++) {
    const utf8_char_info_t *char_info = utf8_palette_get_char(palette, i);
    ASCIICHAT_REQUIRE_NOT_NULL(char_info, "Every character should be accessible at index %zu", i);
    ASCIICHAT_REQUIRE(char_info->byte_len > 0 && char_info->byte_len <= 4,
              "Character byte length should be 1-4 at index %zu (got %zu)", i, char_info->byte_len);
    calculated_total += char_info->byte_len;
  }
  ASCIICHAT_REQUIRE_EQ(calculated_total, palette->total_bytes, "Sum of character bytes should equal total bytes for '%s'",
               palette_string);

  // PROPERTY: Raw string should match input
  ASCIICHAT_REQUIRE_STR_EQ(palette->raw_string, palette_string, "Raw string should be preserved for '%s'", palette_string);

  // PROPERTY: Out-of-bounds access should return NULL
  const utf8_char_info_t *out_of_bounds = utf8_palette_get_char(palette, char_count);
  ASCIICHAT_REQUIRE_NULL(out_of_bounds, "Out-of-bounds access should return NULL for '%s'", palette_string);

  utf8_palette_destroy(palette);
}

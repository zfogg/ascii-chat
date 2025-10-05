#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <locale.h>
#include "palette.h"
#include "common.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(palette);

// Test builtin palette retrieval
Test(palette, get_builtin_palette_valid) {
  const palette_def_t *palette;

  // Test all valid palette types
  palette = get_builtin_palette(PALETTE_STANDARD);
  cr_assert_not_null(palette);
  cr_assert_str_eq(palette->name, "standard");
  cr_assert_str_eq(palette->chars, PALETTE_CHARS_STANDARD);
  cr_assert_eq(palette->requires_utf8, false);

  palette = get_builtin_palette(PALETTE_BLOCKS);
  cr_assert_not_null(palette);
  cr_assert_str_eq(palette->name, "blocks");
  cr_assert_str_eq(palette->chars, PALETTE_CHARS_BLOCKS);
  cr_assert_eq(palette->requires_utf8, true);

  palette = get_builtin_palette(PALETTE_DIGITAL);
  cr_assert_not_null(palette);
  cr_assert_str_eq(palette->name, "digital");
  cr_assert_eq(palette->requires_utf8, true);

  palette = get_builtin_palette(PALETTE_MINIMAL);
  cr_assert_not_null(palette);
  cr_assert_str_eq(palette->name, "minimal");
  cr_assert_eq(palette->requires_utf8, false);

  palette = get_builtin_palette(PALETTE_COOL);
  cr_assert_not_null(palette);
  cr_assert_str_eq(palette->name, "cool");
  cr_assert_eq(palette->requires_utf8, true);
}

Test(palette, get_builtin_palette_invalid) {
  const palette_def_t *palette;

  // Test invalid palette types
  palette = get_builtin_palette(PALETTE_CUSTOM);
  cr_assert_null(palette);

  palette = get_builtin_palette(PALETTE_COUNT);
  cr_assert_null(palette);

  palette = get_builtin_palette((palette_type_t)999);
  cr_assert_null(palette);
}

Test(palette, palette_requires_utf8_encoding) {
  // ASCII palette should not require UTF-8
  bool requires = palette_requires_utf8_encoding(PALETTE_CHARS_STANDARD, strlen(PALETTE_CHARS_STANDARD));
  cr_assert_eq(requires, false);

  // Minimal ASCII palette
  requires = palette_requires_utf8_encoding(PALETTE_CHARS_MINIMAL, strlen(PALETTE_CHARS_MINIMAL));
  cr_assert_eq(requires, false);

  // Blocks palette requires UTF-8
  requires = palette_requires_utf8_encoding(PALETTE_CHARS_BLOCKS, strlen(PALETTE_CHARS_BLOCKS));
  cr_assert_eq(requires, true);

  // Cool palette with box drawing requires UTF-8
  requires = palette_requires_utf8_encoding(PALETTE_CHARS_COOL, strlen(PALETTE_CHARS_COOL));
  cr_assert_eq(requires, true);

  // Empty palette
  requires = palette_requires_utf8_encoding("", 0);
  cr_assert_eq(requires, false);
}

Test(palette, validate_palette_chars_valid) {
  // Valid ASCII palette
  bool valid = validate_palette_chars(PALETTE_CHARS_STANDARD, strlen(PALETTE_CHARS_STANDARD));
  cr_assert_eq(valid, true);

  // Valid UTF-8 palette
  valid = validate_palette_chars(PALETTE_CHARS_BLOCKS, strlen(PALETTE_CHARS_BLOCKS));
  cr_assert_eq(valid, true);

  // Single character
  valid = validate_palette_chars("A", 1);
  cr_assert_eq(valid, true);
}

Test(palette, validate_palette_chars_invalid) {
  // NULL palette
  bool valid = validate_palette_chars(NULL, 10);
  cr_assert_eq(valid, false);

  // Empty palette
  valid = validate_palette_chars("", 0);
  cr_assert_eq(valid, false);

  // Too long palette (>256 chars)
  char long_palette[300];
  memset(long_palette, 'A', 299);
  long_palette[299] = '\0';
  valid = validate_palette_chars(long_palette, 299);
  cr_assert_eq(valid, false);
}

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

Test(palette, select_compatible_palette) {
  // With UTF-8 support, any palette should work
  palette_type_t selected = select_compatible_palette(PALETTE_BLOCKS, true);
  cr_assert_eq(selected, PALETTE_BLOCKS);

  selected = select_compatible_palette(PALETTE_COOL, true);
  cr_assert_eq(selected, PALETTE_COOL);

  // Without UTF-8, should fallback to ASCII
  selected = select_compatible_palette(PALETTE_BLOCKS, false);
  cr_assert_eq(selected, PALETTE_STANDARD);

  selected = select_compatible_palette(PALETTE_DIGITAL, false);
  cr_assert_eq(selected, PALETTE_STANDARD);

  selected = select_compatible_palette(PALETTE_COOL, false);
  cr_assert_eq(selected, PALETTE_STANDARD);

  // ASCII palettes should work without UTF-8
  selected = select_compatible_palette(PALETTE_STANDARD, false);
  cr_assert_eq(selected, PALETTE_STANDARD);

  selected = select_compatible_palette(PALETTE_MINIMAL, false);
  cr_assert_eq(selected, PALETTE_MINIMAL);

  // Custom palette should pass through
  selected = select_compatible_palette(PALETTE_CUSTOM, false);
  cr_assert_eq(selected, PALETTE_CUSTOM);
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

/* UTF-8 Palette Function Tests */

Test(palette, utf8_palette_create_ascii) {
  const char *ascii_palette = " .:-=+*#%@";
  utf8_palette_t *palette = utf8_palette_create(ascii_palette);

  cr_assert_not_null(palette);
  cr_assert_eq(utf8_palette_get_char_count(palette), 10);
  cr_assert_eq(palette->total_bytes, strlen(ascii_palette));
  cr_assert_str_eq(palette->raw_string, ascii_palette);

  // Check individual characters
  for (size_t i = 0; i < 10; i++) {
    const utf8_char_info_t *char_info = utf8_palette_get_char(palette, i);
    cr_assert_not_null(char_info);
    cr_assert_eq(char_info->byte_len, 1);
    cr_assert_eq(char_info->bytes[0], ascii_palette[i]);
  }

  utf8_palette_destroy(palette);
}

Test(palette, utf8_palette_create_utf8) {
  const char *utf8_palette = "🌑🌒🌓🌔🌕"; // 5 moon phase emojis (4 bytes each)
  utf8_palette_t *palette = utf8_palette_create(utf8_palette);

  cr_assert_not_null(palette);
  cr_assert_eq(utf8_palette_get_char_count(palette), 5);
  cr_assert_eq(palette->total_bytes, strlen(utf8_palette));

  // Check that each character is 4 bytes
  for (size_t i = 0; i < 5; i++) {
    const utf8_char_info_t *char_info = utf8_palette_get_char(palette, i);
    cr_assert_not_null(char_info);
    cr_assert_eq(char_info->byte_len, 4);
  }

  utf8_palette_destroy(palette);
}

Test(palette, utf8_palette_create_mixed) {
  const char *mixed = "A→B"; // ASCII + 3-byte arrow + ASCII
  utf8_palette_t *palette = utf8_palette_create(mixed);

  cr_assert_not_null(palette);
  cr_assert_eq(utf8_palette_get_char_count(palette), 3);

  // First character: A (1 byte)
  const utf8_char_info_t *char_info = utf8_palette_get_char(palette, 0);
  cr_assert_eq(char_info->byte_len, 1);
  cr_assert_eq(char_info->bytes[0], 'A');

  // Second character: → (3 bytes)
  char_info = utf8_palette_get_char(palette, 1);
  cr_assert_eq(char_info->byte_len, 3);

  // Third character: B (1 byte)
  char_info = utf8_palette_get_char(palette, 2);
  cr_assert_eq(char_info->byte_len, 1);
  cr_assert_eq(char_info->bytes[0], 'B');

  utf8_palette_destroy(palette);
}

Test(palette, utf8_palette_create_invalid) {
  // NULL string
  utf8_palette_t *palette = utf8_palette_create(NULL);
  cr_assert_null(palette);

  // Empty string
  palette = utf8_palette_create("");
  cr_assert_null(palette);
}

Test(palette, utf8_palette_get_char_bounds) {
  const char *palette_str = "ABC";
  utf8_palette_t *palette = utf8_palette_create(palette_str);

  // Valid indices
  const utf8_char_info_t *char_info = utf8_palette_get_char(palette, 0);
  cr_assert_not_null(char_info);

  char_info = utf8_palette_get_char(palette, 2);
  cr_assert_not_null(char_info);

  // Out of bounds
  char_info = utf8_palette_get_char(palette, 3);
  cr_assert_null(char_info);

  char_info = utf8_palette_get_char(palette, 100);
  cr_assert_null(char_info);

  // NULL palette
  char_info = utf8_palette_get_char(NULL, 0);
  cr_assert_null(char_info);

  utf8_palette_destroy(palette);
}

Test(palette, utf8_palette_contains_char) {
  const char *palette_str = "A→B🌕";
  utf8_palette_t *palette = utf8_palette_create(palette_str);

  // Check ASCII character
  bool contains = utf8_palette_contains_char(palette, "A", 1);
  cr_assert_eq(contains, true);

  contains = utf8_palette_contains_char(palette, "B", 1);
  cr_assert_eq(contains, true);

  // Check 3-byte character (→)
  contains = utf8_palette_contains_char(palette, "→", 3);
  cr_assert_eq(contains, true);

  // Check 4-byte emoji
  contains = utf8_palette_contains_char(palette, "🌕", 4);
  cr_assert_eq(contains, true);

  // Character not in palette
  contains = utf8_palette_contains_char(palette, "Z", 1);
  cr_assert_eq(contains, false);

  // Invalid parameters
  contains = utf8_palette_contains_char(NULL, "A", 1);
  cr_assert_eq(contains, false);

  contains = utf8_palette_contains_char(palette, NULL, 1);
  cr_assert_eq(contains, false);

  contains = utf8_palette_contains_char(palette, "A", 0);
  cr_assert_eq(contains, false);

  contains = utf8_palette_contains_char(palette, "A", 5);
  cr_assert_eq(contains, false);

  utf8_palette_destroy(palette);
}

Test(palette, utf8_palette_find_char_index) {
  const char *palette_str = "A→B🌕C";
  utf8_palette_t *palette = utf8_palette_create(palette_str);

  // Find ASCII characters
  size_t index = utf8_palette_find_char_index(palette, "A", 1);
  cr_assert_eq(index, 0);

  index = utf8_palette_find_char_index(palette, "B", 1);
  cr_assert_eq(index, 2);

  index = utf8_palette_find_char_index(palette, "C", 1);
  cr_assert_eq(index, 4);

  // Find 3-byte character
  index = utf8_palette_find_char_index(palette, "→", 3);
  cr_assert_eq(index, 1);

  // Find 4-byte emoji
  index = utf8_palette_find_char_index(palette, "🌕", 4);
  cr_assert_eq(index, 3);

  // Character not found
  index = utf8_palette_find_char_index(palette, "Z", 1);
  cr_assert_eq(index, (size_t)-1);

  // Invalid parameters
  index = utf8_palette_find_char_index(NULL, "A", 1);
  cr_assert_eq(index, (size_t)-1);

  index = utf8_palette_find_char_index(palette, NULL, 1);
  cr_assert_eq(index, (size_t)-1);

  utf8_palette_destroy(palette);
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

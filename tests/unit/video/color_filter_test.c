/**
 * @file tests/unit/video/color_filter_test.c
 * @brief Unit tests for monochromatic color filter implementation
 * @ingroup test
 */

#include <criterion/criterion.h>
#include <stdint.h>
#include <string.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <ascii-chat/tests/logging.h>

/**
 * @brief Test grayscale conversion accuracy
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TestSuite(color_filter);

Test(color_filter, rgb_to_grayscale_primary_colors) {
  // Pure red: should be ~77
  uint8_t red_gray = rgb_to_grayscale(255, 0, 0);
  cr_assert(red_gray >= 75 && red_gray <= 79, "Red grayscale: expected ~77, got %d", red_gray);

  // Pure green: should be ~150
  uint8_t green_gray = rgb_to_grayscale(0, 255, 0);
  cr_assert(green_gray >= 148 && green_gray <= 152, "Green grayscale: expected ~150, got %d", green_gray);

  // Pure blue: should be ~29
  uint8_t blue_gray = rgb_to_grayscale(0, 0, 255);
  cr_assert(blue_gray >= 27 && blue_gray <= 31, "Blue grayscale: expected ~29, got %d", blue_gray);
}

/**
 * @brief Test grayscale conversion for neutral colors
 */
Test(color_filter, rgb_to_grayscale_neutral) {
  // Pure white: should be 255
  uint8_t white_gray = rgb_to_grayscale(255, 255, 255);
  cr_assert_eq(white_gray, 255, "White grayscale should be 255, got %d", white_gray);

  // Pure black: should be 0
  uint8_t black_gray = rgb_to_grayscale(0, 0, 0);
  cr_assert_eq(black_gray, 0, "Black grayscale should be 0, got %d", black_gray);

  // Mid-gray: should be ~128
  uint8_t gray_gray = rgb_to_grayscale(128, 128, 128);
  cr_assert(gray_gray >= 126 && gray_gray <= 130, "Mid-gray grayscale: expected ~128, got %d", gray_gray);
}

/**
 * @brief Test color filter metadata retrieval
 */
Test(color_filter, get_metadata) {
  // Test valid filter
  const color_filter_def_t *meta = color_filter_get_metadata(COLOR_FILTER_GREEN);
  cr_assert_not_null(meta, "Metadata for COLOR_FILTER_GREEN should not be null");
  cr_assert_str_eq(meta->cli_name, "green", "CLI name should be 'green'");
  cr_assert_eq(meta->r, 0, "Green filter red channel should be 0");
  cr_assert_eq(meta->g, 255, "Green filter green channel should be 255");
  cr_assert_eq(meta->b, 65, "Green filter blue channel should be 65");

  // Test NONE filter (should return NULL)
  meta = color_filter_get_metadata(COLOR_FILTER_NONE);
  cr_assert_null(meta, "Metadata for COLOR_FILTER_NONE should be null");

  // Test invalid filter
  meta = color_filter_get_metadata((color_filter_t)999);
  cr_assert_null(meta, "Metadata for invalid filter should be null");
}

/**
 * @brief Test CLI name to enum conversion
 */
Test(color_filter, from_cli_name) {
  cr_assert_eq(color_filter_from_cli_name("green"), COLOR_FILTER_GREEN,
               "CLI name 'green' should map to COLOR_FILTER_GREEN");
  cr_assert_eq(color_filter_from_cli_name("cyan"), COLOR_FILTER_CYAN,
               "CLI name 'cyan' should map to COLOR_FILTER_CYAN");
  cr_assert_eq(color_filter_from_cli_name("black"), COLOR_FILTER_BLACK,
               "CLI name 'black' should map to COLOR_FILTER_BLACK");
  cr_assert_eq(color_filter_from_cli_name("white"), COLOR_FILTER_WHITE,
               "CLI name 'white' should map to COLOR_FILTER_WHITE");
  cr_assert_eq(color_filter_from_cli_name("none"), COLOR_FILTER_NONE,
               "CLI name 'none' should map to COLOR_FILTER_NONE");

  // Test invalid name
  cr_assert_eq(color_filter_from_cli_name("invalid-filter"), COLOR_FILTER_NONE,
               "Invalid CLI name should map to COLOR_FILTER_NONE");
  cr_assert_eq(color_filter_from_cli_name(NULL), COLOR_FILTER_NONE, "NULL CLI name should map to COLOR_FILTER_NONE");
}

/**
 * @brief Test white-on-color colorization (most filters)
 */
Test(color_filter, colorize_white_on_color) {
  // Create a small test image: 2x2 pixels
  uint8_t pixels[2 * 2 * 3] = {
      // Row 1: black pixel, white pixel
      0,
      0,
      0, // Black (should become dark color)
      255,
      255,
      255, // White (should become bright color)
      // Row 2: mid-gray pixel, dark-gray pixel
      128,
      128,
      128, // Mid-gray (should become medium color)
      64,
      64,
      64, // Dark-gray (should become dim color)
  };

  int result = apply_color_filter(pixels, 2, 2, 6, COLOR_FILTER_CYAN, 0.0f);
  cr_assert_eq(result, 0, "apply_color_filter should return 0");

  // Check that black pixel became dark cyan (scaled down)
  // With white-on-color mode, gray=0 means all RGB are 0
  cr_assert_eq(pixels[0], 0, "Black pixel red should be 0");
  cr_assert_eq(pixels[1], 0, "Black pixel green should be 0");
  cr_assert_eq(pixels[2], 0, "Black pixel blue should be 0");

  // Check that white pixel became bright cyan (scaled up)
  // With white-on-color mode for cyan (0,255,255), gray=255 means full color
  cr_assert_eq(pixels[3], 0, "White pixel red should be 0");
  cr_assert_eq(pixels[4], 255, "White pixel green should be 255");
  cr_assert_eq(pixels[5], 255, "White pixel blue should be 255");
}

/**
 * @brief Test black-on-white colorization
 */
Test(color_filter, colorize_black_on_white) {
  // Create test image: black and white pixels
  uint8_t pixels[2 * 1 * 3] = {
      0,   0,   0,   // Black (should stay dark)
      255, 255, 255, // White (should become white background)
  };

  int result = apply_color_filter(pixels, 2, 1, 6, COLOR_FILTER_BLACK, 0.0f);
  cr_assert_eq(result, 0, "apply_color_filter should return 0");

  // Black pixel should remain dark (all components scale toward black)
  cr_assert(pixels[0] < 50, "Black pixel red should be dark, got %d", pixels[0]);
  cr_assert(pixels[1] < 50, "Black pixel green should be dark, got %d", pixels[1]);
  cr_assert(pixels[2] < 50, "Black pixel blue should be dark, got %d", pixels[2]);

  // White pixel should become white (all components = 255)
  cr_assert_eq(pixels[3], 255, "White pixel red should be 255, got %d", pixels[3]);
  cr_assert_eq(pixels[4], 255, "White pixel green should be 255, got %d", pixels[4]);
  cr_assert_eq(pixels[5], 255, "White pixel blue should be 255, got %d", pixels[5]);
}

/**
 * @brief Test apply_color_filter with NONE filter (should be no-op)
 */
Test(color_filter, apply_none_filter) {
  uint8_t pixels[3 * 3] = {100, 150, 200, 50, 100, 150, 200, 50, 100};
  uint8_t original[3 * 3];
  memcpy(original, pixels, sizeof(pixels));

  int result = apply_color_filter(pixels, 1, 1, 3, COLOR_FILTER_NONE, 0.0f);
  cr_assert_eq(result, 0, "apply_color_filter(NONE) should return 0");
  cr_assert_arr_eq(pixels, original, 9, "NONE filter should not modify pixels");
}

/**
 * @brief Test apply_color_filter with invalid parameters
 */
Test(color_filter, apply_invalid_params) {
  uint8_t pixels[3] = {255, 255, 255};

  // NULL pixels
  int result = apply_color_filter(NULL, 1, 1, 3, COLOR_FILTER_GREEN, 0.0f);
  cr_assert_eq(result, -1, "apply_color_filter(NULL pixels) should return -1");

  // Zero width
  result = apply_color_filter(pixels, 0, 1, 3, COLOR_FILTER_GREEN, 0.0f);
  cr_assert_eq(result, -1, "apply_color_filter(zero width) should return -1");

  // Zero height
  result = apply_color_filter(pixels, 1, 0, 3, COLOR_FILTER_GREEN, 0.0f);
  cr_assert_eq(result, -1, "apply_color_filter(zero height) should return -1");

  // Zero stride
  result = apply_color_filter(pixels, 1, 1, 0, COLOR_FILTER_GREEN, 0.0f);
  cr_assert_eq(result, -1, "apply_color_filter(zero stride) should return -1");

  // Invalid filter
  result = apply_color_filter(pixels, 1, 1, 3, (color_filter_t)999, 0.0f);
  cr_assert_eq(result, -1, "apply_color_filter(invalid filter) should return -1");
}

/**
 * @brief Test all filter color values are correct
 */
Test(color_filter, metadata_colors) {
  // Test each filter's RGB values match expected colors
  struct {
    color_filter_t filter;
    uint8_t r, g, b;
    const char *name;
  } expected[] = {
      {COLOR_FILTER_BLACK, 0, 0, 0, "black"},         {COLOR_FILTER_WHITE, 255, 255, 255, "white"},
      {COLOR_FILTER_GREEN, 0, 255, 65, "green"},      {COLOR_FILTER_MAGENTA, 255, 0, 255, "magenta"},
      {COLOR_FILTER_FUCHSIA, 255, 0, 170, "fuchsia"}, {COLOR_FILTER_ORANGE, 255, 136, 0, "orange"},
      {COLOR_FILTER_TEAL, 0, 221, 221, "teal"},       {COLOR_FILTER_CYAN, 0, 255, 255, "cyan"},
      {COLOR_FILTER_PINK, 255, 182, 193, "pink"},     {COLOR_FILTER_RED, 255, 51, 51, "red"},
      {COLOR_FILTER_YELLOW, 255, 235, 153, "yellow"},
  };

  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
    const color_filter_def_t *meta = color_filter_get_metadata(expected[i].filter);
    cr_assert_not_null(meta, "Metadata for %s should not be null", expected[i].name);
    cr_assert_eq(meta->r, expected[i].r, "%s red should be %d, got %d", expected[i].name, expected[i].r, meta->r);
    cr_assert_eq(meta->g, expected[i].g, "%s green should be %d, got %d", expected[i].name, expected[i].g, meta->g);
    cr_assert_eq(meta->b, expected[i].b, "%s blue should be %d, got %d", expected[i].name, expected[i].b, meta->b);
  }
}

#include <catch2/catch_test_macros.hpp>
/**
 * @file tests/unit/video/color_filter_test.c
 * @brief Unit tests for monochromatic color filter implementation
 * @ingroup test
 */

#include <catch2/catch_all.hpp>
#include <stdint.h>
#include <string.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <ascii-chat/tests/logging.h>
#include "../../../lib/video/anim/backends.h"

/**
 * @brief Test grayscale conversion accuracy
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("color_filter/rgb_to_grayscale_primary_colors", "[color_filter]") {
  // Pure red: should be ~77
  uint8_t red_gray = rgb_to_grayscale(255, 0, 0);
  REQUIRE((red_gray >= 75 && red_gray <= 79));

  // Pure green: should be ~150
  uint8_t green_gray = rgb_to_grayscale(0, 255, 0);
  REQUIRE((green_gray >= 148 && green_gray <= 152));

  // Pure blue: should be ~29
  uint8_t blue_gray = rgb_to_grayscale(0, 0, 255);
  REQUIRE((blue_gray >= 27 && blue_gray <= 31));
}

/**
 * @brief Test grayscale conversion for neutral colors
 */
TEST_CASE("color_filter/rgb_to_grayscale_neutral", "[color_filter]") {
  // Pure white: should be 255
  uint8_t white_gray = rgb_to_grayscale(255, 255, 255);
  REQUIRE((white_gray) == (255));

  // Pure black: should be 0
  uint8_t black_gray = rgb_to_grayscale(0, 0, 0);
  REQUIRE((black_gray) == (0));

  // Mid-gray: should be ~128
  uint8_t gray_gray = rgb_to_grayscale(128, 128, 128);
  REQUIRE((gray_gray >= 126 && gray_gray <= 130));
}

/**
 * @brief Test color filter metadata retrieval
 */
TEST_CASE("color_filter/get_metadata", "[color_filter]") {
  // Test valid filter
  const color_filter_def_t *meta = color_filter_get_metadata(COLOR_FILTER_GREEN);
  REQUIRE((meta) != nullptr);
  REQUIRE(std::strcmp((meta->cli_name), ("green")) == 0);
  REQUIRE((meta->r) == (0));
  REQUIRE((meta->g) == (255));
  REQUIRE((meta->b) == (65));

  // Test NONE filter (should return NULL)
  meta = color_filter_get_metadata(COLOR_FILTER_NONE);
  REQUIRE((meta) == nullptr);

  // Test invalid filter
  meta = color_filter_get_metadata((color_filter_t)999);
  REQUIRE((meta) == nullptr);
}

/**
 * @brief Test CLI name to enum conversion
 */
TEST_CASE("color_filter/from_cli_name", "[color_filter]") {
  REQUIRE((color_filter_from_cli_name("green")) == (COLOR_FILTER_GREEN));
  REQUIRE((color_filter_from_cli_name("cyan")) == (COLOR_FILTER_CYAN));
  REQUIRE((color_filter_from_cli_name("black")) == (COLOR_FILTER_BLACK));
  REQUIRE((color_filter_from_cli_name("white")) == (COLOR_FILTER_WHITE));
  REQUIRE((color_filter_from_cli_name("none")) == (COLOR_FILTER_NONE));

  // Test invalid name
  REQUIRE((color_filter_from_cli_name("invalid-filter")) == (COLOR_FILTER_NONE));
  REQUIRE((color_filter_from_cli_name(NULL)) == (COLOR_FILTER_NONE));
}

/**
 * @brief Test white-on-color colorization (most filters)
 */
TEST_CASE("color_filter/colorize_white_on_color", "[color_filter]") {
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
  REQUIRE((result) == (0));

  // Check that black pixel became dark cyan (scaled down)
  // With white-on-color mode, gray=0 means all RGB are 0
  REQUIRE((pixels[0]) == (0));
  REQUIRE((pixels[1]) == (0));
  REQUIRE((pixels[2]) == (0));

  // Check that white pixel became bright cyan (scaled up)
  // With white-on-color mode for cyan (0,255,255), gray=255 means full color
  REQUIRE((pixels[3]) == (0));
  REQUIRE((pixels[4]) == (255));
  REQUIRE((pixels[5]) == (255));
}

/**
 * @brief Test black-on-white colorization
 */
TEST_CASE("color_filter/colorize_black_on_white", "[color_filter]") {
  // Create test image: black and white pixels
  uint8_t pixels[2 * 1 * 3] = {
      0,   0,   0,   // Black (should stay dark)
      255, 255, 255, // White (should become white background)
  };

  int result = apply_color_filter(pixels, 2, 1, 6, COLOR_FILTER_BLACK, 0.0f);
  REQUIRE((result) == (0));

  // Black pixel should remain dark (all components scale toward black)
  REQUIRE(pixels[0] < 50);
  REQUIRE(pixels[1] < 50);
  REQUIRE(pixels[2] < 50);

  // White pixel should become white (all components = 255)
  REQUIRE((pixels[3]) == (255));
  REQUIRE((pixels[4]) == (255));
  REQUIRE((pixels[5]) == (255));
}

/**
 * @brief Test apply_color_filter with NONE filter (should be no-op)
 */
TEST_CASE("color_filter/apply_none_filter", "[color_filter]") {
  uint8_t pixels[3 * 3] = {100, 150, 200, 50, 100, 150, 200, 50, 100};
  uint8_t original[3 * 3];
  memcpy(original, pixels, sizeof(pixels));

  int result = apply_color_filter(pixels, 1, 1, 3, COLOR_FILTER_NONE, 0.0f);
  REQUIRE((result) == (0));
  REQUIRE(std::memcmp((pixels), (original), (9)) == 0);
}

/**
 * @brief Test apply_color_filter with invalid parameters
 */
TEST_CASE("color_filter/apply_invalid_params", "[color_filter]") {
  uint8_t pixels[3] = {255, 255, 255};

  // NULL pixels
  int result = apply_color_filter(NULL, 1, 1, 3, COLOR_FILTER_GREEN, 0.0f);
  REQUIRE((result) == (-1));

  // Zero width
  result = apply_color_filter(pixels, 0, 1, 3, COLOR_FILTER_GREEN, 0.0f);
  REQUIRE((result) == (-1));

  // Zero height
  result = apply_color_filter(pixels, 1, 0, 3, COLOR_FILTER_GREEN, 0.0f);
  REQUIRE((result) == (-1));

  // Zero stride
  result = apply_color_filter(pixels, 1, 1, 0, COLOR_FILTER_GREEN, 0.0f);
  REQUIRE((result) == (-1));

  // Invalid filter
  result = apply_color_filter(pixels, 1, 1, 3, (color_filter_t)999, 0.0f);
  REQUIRE((result) == (-1));
}

/**
 * @brief Test all filter color values are correct
 */
TEST_CASE("color_filter/metadata_colors", "[color_filter]") {
  // Test each filter's RGB values match expected colors
  struct {
    color_filter_t filter;
    uint8_t r, g, b;
    const char *name;
  } expected[] = {
      {COLOR_FILTER_BLACK, 0, 0, 0, "black"},         {COLOR_FILTER_WHITE, 255, 255, 255, "white"}, {COLOR_FILTER_GREEN, 0, 255, 65, "green"},      {COLOR_FILTER_MAGENTA, 255, 0, 255, "magenta"},
      {COLOR_FILTER_FUCHSIA, 255, 0, 170, "fuchsia"}, {COLOR_FILTER_ORANGE, 255, 136, 0, "orange"}, {COLOR_FILTER_TEAL, 0, 221, 221, "teal"},       {COLOR_FILTER_CYAN, 0, 255, 255, "cyan"},
      {COLOR_FILTER_PINK, 255, 182, 193, "pink"},     {COLOR_FILTER_RED, 255, 51, 51, "red"},       {COLOR_FILTER_YELLOW, 255, 235, 153, "yellow"},
  };

  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
    const color_filter_def_t *meta = color_filter_get_metadata(expected[i].filter);
    REQUIRE((meta) != nullptr);
    REQUIRE((meta->r) == (expected[i].r));
    REQUIRE((meta->g) == (expected[i].g));
    REQUIRE((meta->b) == (expected[i].b));
  }
}

TEST_CASE("color_filter/rainbow_ansi_terminates_and_preserves_malformed_suffix", "[color_filter]") {
  const char *prefix = "\033[38;2;1;2;3m";
  char *frame = rainbow_render_ansi_at(prefix, 0.0f);
  REQUIRE((frame) != nullptr);
  REQUIRE((frame[strlen(frame) - 1]) == ('m'));
  SAFE_FREE(frame);

  frame = rainbow_render_ansi_at("A\033[38;2;1;2;3mB\033[38;2;broken", 0.0f);
  REQUIRE((frame) != nullptr);
  REQUIRE((strstr(frame, "B\033[38;2;broken")) != nullptr);
  REQUIRE((frame[0]) == ('A'));
  SAFE_FREE(frame);
}

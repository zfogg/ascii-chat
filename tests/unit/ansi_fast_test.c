#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "ansi_fast.h"
#include "common.h"
#include "image2ascii/simd/ascii_simd.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(ansi_fast);

/* ============================================================================
 * Initialization Tests
 * ============================================================================ */

Test(ansi_fast, initialization) {
  // Test that initialization functions work without crashing
  ansi_fast_init_256color();
  ansi_fast_init_16color();

  cr_assert(true, "Initialization functions should not crash");
}

Test(ansi_fast, multiple_initialization_calls) {
  // Test that multiple initialization calls are safe
  ansi_fast_init_256color();
  ansi_fast_init_256color(); // Should be safe to call multiple times
  ansi_fast_init_16color();
  ansi_fast_init_16color(); // Should be safe to call multiple times

  cr_assert(true, "Multiple initialization calls should be safe");
}

/* ============================================================================
 * Truecolor ANSI Generation Tests
 * ============================================================================ */

Test(ansi_fast, append_truecolor_fg_basic) {
  char buffer[256];
  char *result;

  // Test basic foreground color generation
  result = append_truecolor_fg(buffer, 255, 128, 64);
  *result = '\0'; // Null terminate for string comparison

  // Should generate: \033[38;2;255;128;64m
  cr_assert_str_eq(buffer, "\033[38;2;255;128;64m", "Basic foreground color should be correct");

  // Test that result pointer is positioned correctly
  cr_assert_eq(result - buffer, 18, "Result pointer should be positioned correctly");
}

// Test case structure for truecolor edge cases
typedef struct {
  uint8_t r, g, b;
  char expected_output[32];
  char description[64];
} truecolor_fg_test_case_t;

static truecolor_fg_test_case_t truecolor_fg_edge_cases[] = {{0, 0, 0, "\033[38;2;0;0;0m", "Black color"},
                                                             {1, 1, 1, "\033[38;2;1;1;1m", "Minimal color"},
                                                             {255, 255, 255, "\033[38;2;255;255;255m", "White color"},
                                                             {255, 0, 0, "\033[38;2;255;0;0m", "Pure red"},
                                                             {0, 255, 0, "\033[38;2;0;255;0m", "Pure green"},
                                                             {0, 0, 255, "\033[38;2;0;0;255m", "Pure blue"},
                                                             {128, 128, 128, "\033[38;2;128;128;128m", "Mid gray"}};

ParameterizedTestParameters(ansi_fast, truecolor_fg_edge_cases_param) {
  size_t nb_cases = sizeof(truecolor_fg_edge_cases) / sizeof(truecolor_fg_edge_cases[0]);
  return cr_make_param_array(truecolor_fg_test_case_t, truecolor_fg_edge_cases, nb_cases);
}

ParameterizedTest(truecolor_fg_test_case_t *tc, ansi_fast, truecolor_fg_edge_cases_param) {
  char buffer[256];
  char *result = append_truecolor_fg(buffer, tc->r, tc->g, tc->b);
  *result = '\0';
  cr_assert_str_eq(buffer, tc->expected_output, "%s should be correct", tc->description);
}

Test(ansi_fast, append_truecolor_bg_basic) {
  char buffer[256];
  char *result;

  // Test basic background color generation
  result = append_truecolor_bg(buffer, 100, 200, 50);
  *result = '\0';

  // Should generate: \033[48;2;100;200;50m
  cr_assert_str_eq(buffer, "\033[48;2;100;200;50m", "Basic background color should be correct");

  cr_assert_eq(result - buffer, 18, "Result pointer should be positioned correctly");
}

// Test case structure for truecolor background edge cases
typedef struct {
  uint8_t r, g, b;
  char expected_output[32];
  char description[64];
} truecolor_bg_test_case_t;

static truecolor_bg_test_case_t truecolor_bg_edge_cases[] = {
    {0, 0, 0, "\033[48;2;0;0;0m", "Black background"},
    {1, 1, 1, "\033[48;2;1;1;1m", "Minimal background"},
    {255, 255, 255, "\033[48;2;255;255;255m", "White background"},
    {255, 0, 0, "\033[48;2;255;0;0m", "Pure red background"},
    {0, 255, 0, "\033[48;2;0;255;0m", "Pure green background"},
    {0, 0, 255, "\033[48;2;0;0;255m", "Pure blue background"},
    {128, 128, 128, "\033[48;2;128;128;128m", "Mid gray background"}};

ParameterizedTestParameters(ansi_fast, truecolor_bg_edge_cases_param) {
  size_t nb_cases = sizeof(truecolor_bg_edge_cases) / sizeof(truecolor_bg_edge_cases[0]);
  return cr_make_param_array(truecolor_bg_test_case_t, truecolor_bg_edge_cases, nb_cases);
}

ParameterizedTest(truecolor_bg_test_case_t *tc, ansi_fast, truecolor_bg_edge_cases_param) {
  char buffer[256];
  char *result = append_truecolor_bg(buffer, tc->r, tc->g, tc->b);
  *result = '\0';
  cr_assert_str_eq(buffer, tc->expected_output, "%s should be correct", tc->description);
}

Test(ansi_fast, append_truecolor_fg_bg_combined) {
  char buffer[256];
  char *result;

  // Test combined foreground and background
  result = append_truecolor_fg_bg(buffer, 255, 0, 0, 0, 0, 255);
  *result = '\0';

  // Should generate: \033[38;2;255;0;0;48;2;0;0;255m
  cr_assert_str_eq(buffer, "\033[38;2;255;0;0;48;2;0;0;255m", "Combined fg/bg should be correct");

  cr_assert_eq(result - buffer, 28, "Result pointer should be positioned correctly");
}

Test(ansi_fast, append_truecolor_fg_bg_edge_cases) {
  char buffer[256];
  char *result;

  // Test various combinations
  result = append_truecolor_fg_bg(buffer, 0, 0, 0, 255, 255, 255);
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[38;2;0;0;0;48;2;255;255;255m", "Black fg, white bg should be correct");

  result = append_truecolor_fg_bg(buffer, 128, 64, 192, 64, 128, 32);
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[38;2;128;64;192;48;2;64;128;32m", "Mixed colors should be correct");
}

/* ============================================================================
 * Run-Length Encoding Tests
 * ============================================================================ */

Test(ansi_fast, ansi_rle_init) {
  char buffer[256];
  ansi_rle_context_t ctx;

  // Test initialization
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  cr_assert_eq(ctx.buffer, buffer, "Buffer should be set correctly");
  cr_assert_eq(ctx.capacity, sizeof(buffer), "Capacity should be set correctly");
  cr_assert_eq(ctx.length, 0, "Length should start at 0");
  cr_assert_eq(ctx.mode, ANSI_MODE_FOREGROUND, "Mode should be set correctly");
  cr_assert_eq(ctx.first_pixel, true, "First pixel should be true");
  cr_assert_eq(ctx.last_r, 0xFF, "Last R should be initialized to impossible value");
  cr_assert_eq(ctx.last_g, 0xFF, "Last G should be initialized to impossible value");
  cr_assert_eq(ctx.last_b, 0xFF, "Last B should be initialized to impossible value");
}

Test(ansi_fast, ansi_rle_add_pixel_first) {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel - should emit SGR sequence
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  cr_assert_gt(ctx.length, 0, "Length should be greater than 0 after adding pixel");
  cr_assert_eq(ctx.last_r, 255, "Last R should be updated");
  cr_assert_eq(ctx.last_g, 128, "Last G should be updated");
  cr_assert_eq(ctx.last_b, 64, "Last B should be updated");
  cr_assert_eq(ctx.first_pixel, false, "First pixel should be false");

  // Check that the character was added
  cr_assert_eq(buffer[ctx.length - 1], 'A', "Character should be added to buffer");
}

Test(ansi_fast, ansi_rle_add_pixel_same_color) {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  size_t length_after_first = ctx.length;

  // Add second pixel with same color - should not emit new SGR
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'B');

  // Length should only increase by 1 (for the character)
  cr_assert_eq(ctx.length, length_after_first + 1, "Length should only increase by 1 for same color");
  cr_assert_eq(buffer[ctx.length - 1], 'B', "Second character should be added");
}

Test(ansi_fast, ansi_rle_add_pixel_different_color) {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  size_t length_after_first = ctx.length;

  // Add second pixel with different color - should emit new SGR
  ansi_rle_add_pixel(&ctx, 100, 200, 50, 'B');

  // Length should increase by more than 1 (SGR + character)
  cr_assert_gt(ctx.length, length_after_first + 1, "Length should increase by more than 1 for different color");
  cr_assert_eq(ctx.last_r, 100, "Last R should be updated");
  cr_assert_eq(ctx.last_g, 200, "Last G should be updated");
  cr_assert_eq(ctx.last_b, 50, "Last B should be updated");
}

Test(ansi_fast, ansi_rle_finish) {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  size_t length_before_finish = ctx.length;
  ansi_rle_finish(&ctx);

  // Should add reset sequence and null terminator
  cr_assert_gt(ctx.length, length_before_finish, "Length should increase after finish");
  cr_assert_eq(buffer[ctx.length], '\0', "Buffer should be null terminated");

  // Check that reset sequence was added
  cr_assert(strstr(buffer, "\033[0m") != NULL, "Reset sequence should be present");
}

Test(ansi_fast, ansi_rle_different_modes) {
  char buffer[256];
  ansi_rle_context_t ctx;

  // Test foreground mode
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  cr_assert(strstr(buffer, "\033[38;2;") != NULL, "Foreground mode should use 38;2");

  // Test background mode
  memset(buffer, 0, sizeof(buffer));
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_BACKGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  cr_assert(strstr(buffer, "\033[48;2;") != NULL, "Background mode should use 48;2");

  // Test combined mode
  memset(buffer, 0, sizeof(buffer));
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND_BACKGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  cr_assert(strstr(buffer, "\033[38;2;") != NULL, "Combined mode should use 38;2");
  cr_assert(strstr(buffer, ";48;2;") != NULL, "Combined mode should use 48;2");
}

/* ============================================================================
 * 256-Color Mode Tests
 * ============================================================================ */

Test(ansi_fast, append_256color_fg_basic) {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  // Test basic 256-color foreground
  result = append_256color_fg(buffer, 123);
  *result = '\0';

  // Should generate: \033[38;5;123m
  cr_assert_str_eq(buffer, "\033[38;5;123m", "256-color foreground should be correct");
}

Test(ansi_fast, append_256color_fg_edge_cases) {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  // Test edge cases
  result = append_256color_fg(buffer, 0);
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[38;5;0m", "Color 0 should be correct");

  result = append_256color_fg(buffer, 255);
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[38;5;255m", "Color 255 should be correct");
}

Test(ansi_fast, rgb_to_256color_basic) {
  uint8_t result;

  // Test basic RGB to 256-color conversion
  result = rgb_to_256color(255, 0, 0); // Red
  cr_assert(result >= 16, "Red should map to color cube (16-231)");
  cr_assert(result <= 231, "Red should map to color cube (16-231)");

  result = rgb_to_256color(0, 255, 0); // Green
  cr_assert(result >= 16, "Green should map to color cube (16-231)");
  cr_assert(result <= 231, "Green should map to color cube (16-231)");

  result = rgb_to_256color(0, 0, 255); // Blue
  cr_assert(result >= 16, "Blue should map to color cube (16-231)");
  cr_assert(result <= 231, "Blue should map to color cube (16-231)");
}

Test(ansi_fast, rgb_to_256color_grayscale) {
  uint8_t result;

  // Test grayscale conversion
  result = rgb_to_256color(0, 0, 0); // Black
  cr_assert(result >= 232, "Black should map to grayscale ramp (232-255)");
  cr_assert(result <= 255, "Black should map to grayscale ramp (232-255)");

  result = rgb_to_256color(255, 255, 255); // White
  cr_assert(result >= 232, "White should map to grayscale ramp (232-255)");
  cr_assert(result <= 255, "White should map to grayscale ramp (232-255)");

  result = rgb_to_256color(128, 128, 128); // Gray
  cr_assert(result >= 232, "Gray should map to grayscale ramp (232-255)");
  cr_assert(result <= 255, "Gray should map to grayscale ramp (232-255)");
}

Test(ansi_fast, rgb_to_256color_edge_cases) {
  uint8_t result;

  // Test edge cases
  result = rgb_to_256color(1, 1, 1); // Near black
  cr_assert(result >= 232, "Near black should map to grayscale");

  result = rgb_to_256color(254, 254, 254); // Near white
  cr_assert(result >= 232, "Near white should map to grayscale");

  result = rgb_to_256color(255, 0, 1); // Not grayscale
  cr_assert(result >= 16, "Non-grayscale should map to color cube");
  cr_assert(result <= 231, "Non-grayscale should map to color cube");
}

/* ============================================================================
 * 16-Color Mode Tests
 * ============================================================================ */

Test(ansi_fast, append_16color_fg_basic) {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test basic 16-color foreground
  result = append_16color_fg(buffer, 1); // Red
  *result = '\0';

  // Should generate: \033[31m
  cr_assert_str_eq(buffer, "\033[31m", "16-color red foreground should be correct");

  result = append_16color_fg(buffer, 2); // Green
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[32m", "16-color green foreground should be correct");
}

Test(ansi_fast, append_16color_fg_edge_cases) {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test edge cases
  result = append_16color_fg(buffer, 0); // Black
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[30m", "Black foreground should be correct");

  result = append_16color_fg(buffer, 7); // Light gray
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[37m", "Light gray foreground should be correct");

  result = append_16color_fg(buffer, 9); // Bright red
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[91m", "Bright red foreground should be correct");

  // Test invalid color index
  result = append_16color_fg(buffer, 99); // Invalid
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[37m", "Invalid color should default to white");
}

Test(ansi_fast, append_16color_bg_basic) {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test basic 16-color background
  result = append_16color_bg(buffer, 1); // Red
  *result = '\0';

  // Should generate: \033[41m
  cr_assert_str_eq(buffer, "\033[41m", "16-color red background should be correct");

  result = append_16color_bg(buffer, 2); // Green
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[42m", "16-color green background should be correct");
}

Test(ansi_fast, append_16color_bg_edge_cases) {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test edge cases
  result = append_16color_bg(buffer, 0); // Black
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[40m", "Black background should be correct");

  result = append_16color_bg(buffer, 7); // Light gray
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[47m", "Light gray background should be correct");

  result = append_16color_bg(buffer, 9); // Bright red
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[101m", "Bright red background should be correct");

  // Test invalid color index
  result = append_16color_bg(buffer, 99); // Invalid
  *result = '\0';
  cr_assert_str_eq(buffer, "\033[40m", "Invalid color should default to black");
}

Test(ansi_fast, rgb_to_16color_basic) {
  uint8_t result;

  // Test basic RGB to 16-color conversion
  result = rgb_to_16color(255, 0, 0); // Red
  cr_assert_eq(result, 9, "Bright red should map to color 9");

  result = rgb_to_16color(0, 255, 0); // Green
  cr_assert_eq(result, 10, "Bright green should map to color 10");

  result = rgb_to_16color(0, 0, 255); // Blue
  cr_assert_eq(result, 12, "Bright blue should map to color 12");

  result = rgb_to_16color(0, 0, 0); // Black
  cr_assert_eq(result, 0, "Black should map to color 0");

  result = rgb_to_16color(255, 255, 255); // White
  cr_assert_eq(result, 15, "White should map to color 15");
}

Test(ansi_fast, rgb_to_16color_approximations) {
  uint8_t result;

  // Test color approximations
  result = rgb_to_16color(128, 0, 0); // Dark red
  cr_assert_eq(result, 1, "Dark red should map to color 1");

  result = rgb_to_16color(0, 128, 0); // Dark green
  cr_assert_eq(result, 2, "Dark green should map to color 2");

  result = rgb_to_16color(0, 0, 128); // Dark blue
  cr_assert_eq(result, 4, "Dark blue should map to color 4");

  result = rgb_to_16color(192, 192, 192); // Light gray
  cr_assert_eq(result, 7, "Light gray should map to color 7");
}

Test(ansi_fast, get_16color_rgb_basic) {
  uint8_t r, g, b;

  // Test getting RGB values for 16-color indices
  get_16color_rgb(0, &r, &g, &b);
  cr_assert_eq(r, 0, "Black R should be 0");
  cr_assert_eq(g, 0, "Black G should be 0");
  cr_assert_eq(b, 0, "Black B should be 0");

  get_16color_rgb(9, &r, &g, &b);
  cr_assert_eq(r, 255, "Bright red R should be 255");
  cr_assert_eq(g, 0, "Bright red G should be 0");
  cr_assert_eq(b, 0, "Bright red B should be 0");

  get_16color_rgb(10, &r, &g, &b);
  cr_assert_eq(r, 0, "Bright green R should be 0");
  cr_assert_eq(g, 255, "Bright green G should be 255");
  cr_assert_eq(b, 0, "Bright green B should be 0");

  get_16color_rgb(15, &r, &g, &b);
  cr_assert_eq(r, 255, "White R should be 255");
  cr_assert_eq(g, 255, "White G should be 255");
  cr_assert_eq(b, 255, "White B should be 255");
}

Test(ansi_fast, get_16color_rgb_edge_cases) {
  uint8_t r, g, b;

  // Test invalid color index
  get_16color_rgb(99, &r, &g, &b);
  cr_assert_eq(r, 192, "Invalid color should default to light gray R");
  cr_assert_eq(g, 192, "Invalid color should default to light gray G");
  cr_assert_eq(b, 192, "Invalid color should default to light gray B");
}

/* ============================================================================
 * Dithering Tests
 * ============================================================================ */

Test(ansi_fast, rgb_to_16color_dithered_basic) {
  rgb_error_t error_buffer[100]; // 10x10 buffer
  uint8_t result;

  // Initialize error buffer
  memset(error_buffer, 0, sizeof(error_buffer));

  // Test basic dithering with a color that will have error
  result = rgb_to_16color_dithered(200, 50, 50, 0, 0, 10, 10, error_buffer);
  cr_assert(result >= 0, "Should return valid color index");
  cr_assert(result <= 15, "Should return valid color index");

  // Check that error was distributed (may be 0 if color maps exactly)
  // Note: Error distribution depends on the actual color mapping
}

Test(ansi_fast, rgb_to_16color_dithered_no_error_buffer) {
  uint8_t result;

  // Test dithering without error buffer
  result = rgb_to_16color_dithered(255, 0, 0, 0, 0, 10, 10, NULL);
  cr_assert_eq(result, 9, "Red should map to bright red without error buffer");
}

Test(ansi_fast, rgb_to_16color_dithered_edge_cases) {
  rgb_error_t error_buffer[100];
  uint8_t result;

  memset(error_buffer, 0, sizeof(error_buffer));

  // Test edge cases (boundary pixels)
  result = rgb_to_16color_dithered(255, 0, 0, 9, 0, 10, 10, error_buffer); // Right edge
  cr_assert_eq(result, 9, "Right edge pixel should work");

  result = rgb_to_16color_dithered(255, 0, 0, 0, 9, 10, 10, error_buffer); // Bottom edge
  cr_assert_eq(result, 9, "Bottom edge pixel should work");

  result = rgb_to_16color_dithered(255, 0, 0, 9, 9, 10, 10, error_buffer); // Corner
  cr_assert_eq(result, 9, "Corner pixel should work");
}

Test(ansi_fast, rgb_to_16color_dithered_clamping) {
  rgb_error_t error_buffer[100];
  uint8_t result;

  memset(error_buffer, 0, sizeof(error_buffer));

  // Test with large error values that would cause overflow
  error_buffer[0].r = 1000;
  error_buffer[0].g = -1000;
  error_buffer[0].b = 500;

  result = rgb_to_16color_dithered(128, 128, 128, 0, 0, 10, 10, error_buffer);

  // Should still return a valid color index
  cr_assert(result >= 0, "Result should be valid color index");
  cr_assert(result <= 15, "Result should be valid color index");
}

/* ============================================================================
 * Mode-Aware Color Function Tests
 * ============================================================================ */

Test(ansi_fast, append_color_fg_for_mode_truecolor) {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_TRUECOLOR);
  *result = '\0';

  cr_assert_str_eq(buffer, "\033[38;2;255;128;64m", "Truecolor mode should generate truecolor sequence");
}

Test(ansi_fast, append_color_fg_for_mode_256color) {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_256_COLOR);
  *result = '\0';

  cr_assert(strstr(buffer, "\033[38;5;") != NULL, "256-color mode should generate 256-color sequence");
}

Test(ansi_fast, append_color_fg_for_mode_16color) {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_16_COLOR);
  *result = '\0';

  cr_assert(strstr(buffer, "\033[") != NULL, "16-color mode should generate ANSI sequence");
}

Test(ansi_fast, append_color_fg_for_mode_mono) {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_MONO);

  // Should return unchanged buffer (no color output)
  cr_assert_eq(result, buffer, "Mono mode should return unchanged buffer");
}

Test(ansi_fast, append_color_fg_for_mode_auto) {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_AUTO);

  // Should return unchanged buffer (no color output)
  cr_assert_eq(result, buffer, "Auto mode should return unchanged buffer");
}

Test(ansi_fast, append_color_fg_for_mode_invalid) {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, (color_mode_t)999);

  // Should return unchanged buffer (no color output)
  cr_assert_eq(result, buffer, "Invalid mode should return unchanged buffer");
}

/* ============================================================================
 * Performance and Stress Tests
 * ============================================================================ */

Test(ansi_fast, performance_truecolor_generation) {
  char buffer[4096];
  char *pos = buffer;

  // Generate many truecolor sequences
  for (int i = 0; i < 100; i++) {
    pos = append_truecolor_fg(pos, i % 256, (i * 2) % 256, (i * 3) % 256);
  }

  cr_assert_lt((size_t)(pos - buffer), sizeof(buffer), "Should not overflow buffer");
  cr_assert_gt(pos - buffer, 0, "Should generate some output");
}

Test(ansi_fast, performance_rle_generation) {
  char buffer[4096];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add many pixels with varying colors
  for (int i = 0; i < 100; i++) {
    ansi_rle_add_pixel(&ctx, i % 256, (i * 2) % 256, (i * 3) % 256, 'A' + (i % 26));
  }

  ansi_rle_finish(&ctx);

  cr_assert_lt(ctx.length, sizeof(buffer), "Should not overflow buffer");
  cr_assert_gt(ctx.length, 0, "Should generate some output");
  cr_assert_eq(buffer[ctx.length], '\0', "Should be null terminated");
}

Test(ansi_fast, performance_color_conversion) {
  uint8_t result;

  // Test many color conversions
  for (int i = 0; i < 1000; i++) {
    result = rgb_to_256color(i % 256, (i * 2) % 256, (i * 3) % 256);
    cr_assert(result >= 0, "256-color result should be valid");
    cr_assert(result <= 255, "256-color result should be valid");

    result = rgb_to_16color(i % 256, (i * 2) % 256, (i * 3) % 256);
    cr_assert(result >= 0, "16-color result should be valid");
    cr_assert(result <= 15, "16-color result should be valid");
  }
}

/* ============================================================================
 * Buffer Overflow Protection Tests
 * ============================================================================ */

Test(ansi_fast, buffer_overflow_protection_rle) {
  char buffer[10]; // Very small buffer
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Try to add many pixels - should not overflow
  for (int i = 0; i < 100; i++) {
    ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  }

  cr_assert(ctx.length <= sizeof(buffer), "Should not overflow small buffer");
}

Test(ansi_fast, buffer_overflow_protection_finish) {
  char buffer[5]; // Very small buffer
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  // Finish should not overflow
  ansi_rle_finish(&ctx);

  cr_assert(ctx.length <= sizeof(buffer), "Finish should not overflow small buffer");
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

Test(ansi_fast, integration_full_workflow) {
  char buffer[1024];
  ansi_rle_context_t ctx;

  // Initialize everything
  ansi_fast_init_256color();
  ansi_fast_init_16color();

  // Test full workflow with RLE
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add pixels with different colors
  ansi_rle_add_pixel(&ctx, 255, 0, 0, 'R'); // Red
  ansi_rle_add_pixel(&ctx, 255, 0, 0, 'E'); // Same red
  ansi_rle_add_pixel(&ctx, 0, 255, 0, 'D'); // Green
  ansi_rle_add_pixel(&ctx, 0, 0, 255, 'B'); // Blue

  ansi_rle_finish(&ctx);

  // Verify output
  cr_assert_gt(ctx.length, 0, "Should generate output");
  cr_assert_eq(buffer[ctx.length], '\0', "Should be null terminated");
  cr_assert(strstr(buffer, "\033[38;2;") != NULL, "Should contain foreground sequences");
  cr_assert(strstr(buffer, "\033[0m") != NULL, "Should contain reset sequence");

  // Check that all expected characters are present (they may be separated by ANSI sequences)
  cr_assert(strstr(buffer, "R") != NULL, "Should contain character R");
  cr_assert(strstr(buffer, "E") != NULL, "Should contain character E");
  cr_assert(strstr(buffer, "D") != NULL, "Should contain character D");
  cr_assert(strstr(buffer, "B") != NULL, "Should contain character B");
}

Test(ansi_fast, integration_color_mode_switching) {
  char buffer[256];
  char *result;

  // Initialize color modes
  ansi_fast_init_256color();
  ansi_fast_init_16color();

  // Test switching between different color modes
  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_TRUECOLOR);
  *result = '\0';
  cr_assert(strstr(buffer, "\033[38;2;") != NULL, "Truecolor should work");

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_256_COLOR);
  *result = '\0';
  cr_assert(strstr(buffer, "\033[38;5;") != NULL, "256-color should work");

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_16_COLOR);
  *result = '\0';
  cr_assert(strstr(buffer, "\033[") != NULL, "16-color should work");
}

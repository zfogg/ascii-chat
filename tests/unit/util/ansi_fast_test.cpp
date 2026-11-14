#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/tests/logging.h>

// Custom init function - must initialize ansi_fast before any tests use append_truecolor_fg/bg
void setup_quiet_test_logging_ansi_fast(void) {
  // Initialize ansi_fast (which initializes dec3 cache) before logging setup
  ansi_fast_init();
}

// Custom fini function
void restore_test_logging_ansi_fast(void) {}

// Use TestSuite with custom init to ensure ansi_fast is initialized
/* ============================================================================
 * Initialization Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/initialization", "[ansi_fast]") {
  // Test that initialization functions work without crashing
  ansi_fast_init_256color();
  ansi_fast_init_16color();

  ASCIICHAT_REQUIRE(true, "Initialization functions should not crash");
}

TEST_CASE("ansi_fast/multiple_initialization_calls", "[ansi_fast]") {
  // Test that multiple initialization calls are safe
  ansi_fast_init_256color();
  ansi_fast_init_256color(); // Should be safe to call multiple times
  ansi_fast_init_16color();
  ansi_fast_init_16color(); // Should be safe to call multiple times

  ASCIICHAT_REQUIRE(true, "Multiple initialization calls should be safe");
}

/* ============================================================================
 * Truecolor ANSI Generation Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/append_truecolor_fg_basic", "[ansi_fast]") {
  char buffer[256];
  char *result;

  // Test basic foreground color generation
  result = append_truecolor_fg(buffer, 255, 128, 64);
  *result = '\0'; // Null terminate for string comparison

  // Should generate: \033[38;2;255;128;64m
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[38;2;255;128;64m", "Basic foreground color should be correct");

  // Test that result pointer is positioned correctly
  ASCIICHAT_REQUIRE_EQ(result - buffer, 18, "Result pointer should be positioned correctly");
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



TEST_CASE("ansi_fast/truecolor_fg_edge_cases_param", "[ansi_fast]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(truecolor_fg_edge_cases)); auto *tc = &generated_case;
  char buffer[256];
  char *result = append_truecolor_fg(buffer, tc->r, tc->g, tc->b);
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, tc->expected_output, "%s should be correct", tc->description);
}

TEST_CASE("ansi_fast/append_truecolor_bg_basic", "[ansi_fast]") {
  char buffer[256];
  char *result;

  // Test basic background color generation
  result = append_truecolor_bg(buffer, 100, 200, 50);
  *result = '\0';

  // Should generate: \033[48;2;100;200;50m
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[48;2;100;200;50m", "Basic background color should be correct");

  ASCIICHAT_REQUIRE_EQ(result - buffer, 18, "Result pointer should be positioned correctly");
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



TEST_CASE("ansi_fast/truecolor_bg_edge_cases_param", "[ansi_fast]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(truecolor_bg_edge_cases)); auto *tc = &generated_case;
  char buffer[256];
  char *result = append_truecolor_bg(buffer, tc->r, tc->g, tc->b);
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, tc->expected_output, "%s should be correct", tc->description);
}

TEST_CASE("ansi_fast/append_truecolor_fg_bg_combined", "[ansi_fast]") {
  char buffer[256];
  char *result;

  // Test combined foreground and background
  result = append_truecolor_fg_bg(buffer, 255, 0, 0, 0, 0, 255);
  *result = '\0';

  // Should generate: \033[38;2;255;0;0;48;2;0;0;255m
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[38;2;255;0;0;48;2;0;0;255m", "Combined fg/bg should be correct");

  ASCIICHAT_REQUIRE_EQ(result - buffer, 28, "Result pointer should be positioned correctly");
}

TEST_CASE("ansi_fast/append_truecolor_fg_bg_edge_cases", "[ansi_fast]") {
  char buffer[256];
  char *result;

  // Test various combinations
  result = append_truecolor_fg_bg(buffer, 0, 0, 0, 255, 255, 255);
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[38;2;0;0;0;48;2;255;255;255m", "Black fg, white bg should be correct");

  result = append_truecolor_fg_bg(buffer, 128, 64, 192, 64, 128, 32);
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[38;2;128;64;192;48;2;64;128;32m", "Mixed colors should be correct");
}

/* ============================================================================
 * Run-Length Encoding Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/ansi_rle_init", "[ansi_fast]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  // Test initialization
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  ASCIICHAT_REQUIRE_EQ(ctx.buffer, buffer, "Buffer should be set correctly");
  ASCIICHAT_REQUIRE_EQ(ctx.capacity, sizeof(buffer), "Capacity should be set correctly");
  ASCIICHAT_REQUIRE_EQ(ctx.length, 0, "Length should start at 0");
  ASCIICHAT_REQUIRE_EQ(ctx.mode, ANSI_MODE_FOREGROUND, "Mode should be set correctly");
  ASCIICHAT_REQUIRE_EQ(ctx.first_pixel, true, "First pixel should be true");
  ASCIICHAT_REQUIRE_EQ(ctx.last_r, 0xFF, "Last R should be initialized to impossible value");
  ASCIICHAT_REQUIRE_EQ(ctx.last_g, 0xFF, "Last G should be initialized to impossible value");
  ASCIICHAT_REQUIRE_EQ(ctx.last_b, 0xFF, "Last B should be initialized to impossible value");
}

TEST_CASE("ansi_fast/ansi_rle_add_pixel_first", "[ansi_fast]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel - should emit SGR sequence
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  ASCIICHAT_REQUIRE_GT(ctx.length, 0, "Length should be greater than 0 after adding pixel");
  ASCIICHAT_REQUIRE_EQ(ctx.last_r, 255, "Last R should be updated");
  ASCIICHAT_REQUIRE_EQ(ctx.last_g, 128, "Last G should be updated");
  ASCIICHAT_REQUIRE_EQ(ctx.last_b, 64, "Last B should be updated");
  ASCIICHAT_REQUIRE_EQ(ctx.first_pixel, false, "First pixel should be false");

  // Check that the character was added
  ASCIICHAT_REQUIRE_EQ(buffer[ctx.length - 1], 'A', "Character should be added to buffer");
}

TEST_CASE("ansi_fast/ansi_rle_add_pixel_same_color", "[ansi_fast]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  size_t length_after_first = ctx.length;

  // Add second pixel with same color - should not emit new SGR
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'B');

  // Length should only increase by 1 (for the character)
  ASCIICHAT_REQUIRE_EQ(ctx.length, length_after_first + 1, "Length should only increase by 1 for same color");
  ASCIICHAT_REQUIRE_EQ(buffer[ctx.length - 1], 'B', "Second character should be added");
}

TEST_CASE("ansi_fast/ansi_rle_add_pixel_different_color", "[ansi_fast]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  size_t length_after_first = ctx.length;

  // Add second pixel with different color - should emit new SGR
  ansi_rle_add_pixel(&ctx, 100, 200, 50, 'B');

  // Length should increase by more than 1 (SGR + character)
  ASCIICHAT_REQUIRE_GT(ctx.length, length_after_first + 1, "Length should increase by more than 1 for different color");
  ASCIICHAT_REQUIRE_EQ(ctx.last_r, 100, "Last R should be updated");
  ASCIICHAT_REQUIRE_EQ(ctx.last_g, 200, "Last G should be updated");
  ASCIICHAT_REQUIRE_EQ(ctx.last_b, 50, "Last B should be updated");
}

TEST_CASE("ansi_fast/ansi_rle_finish", "[ansi_fast]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  size_t length_before_finish = ctx.length;
  ansi_rle_finish(&ctx);

  // Should add reset sequence and null terminator
  ASCIICHAT_REQUIRE_GT(ctx.length, length_before_finish, "Length should increase after finish");
  ASCIICHAT_REQUIRE_EQ(buffer[ctx.length], '\0', "Buffer should be null terminated");

  // Check that reset sequence was added
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[0m") != NULL, "Reset sequence should be present");
}

TEST_CASE("ansi_fast/ansi_rle_different_modes", "[ansi_fast]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  // Test foreground mode
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[38;2;") != NULL, "Foreground mode should use 38;2");

  // Test background mode
  memset(buffer, 0, sizeof(buffer));
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_BACKGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[48;2;") != NULL, "Background mode should use 48;2");

  // Test combined mode
  memset(buffer, 0, sizeof(buffer));
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND_BACKGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[38;2;") != NULL, "Combined mode should use 38;2");
  ASCIICHAT_REQUIRE(strstr(buffer, ";48;2;") != NULL, "Combined mode should use 48;2");
}

/* ============================================================================
 * 256-Color Mode Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/append_256color_fg_basic", "[ansi_fast]") {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  // Test basic 256-color foreground
  result = append_256color_fg(buffer, 123);
  *result = '\0';

  // Should generate: \033[38;5;123m
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[38;5;123m", "256-color foreground should be correct");
}

TEST_CASE("ansi_fast/append_256color_fg_edge_cases", "[ansi_fast]") {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  // Test edge cases
  result = append_256color_fg(buffer, 0);
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[38;5;0m", "Color 0 should be correct");

  result = append_256color_fg(buffer, 255);
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[38;5;255m", "Color 255 should be correct");
}

TEST_CASE("ansi_fast/rgb_to_256color_basic", "[ansi_fast]") {
  uint8_t result;

  // Test basic RGB to 256-color conversion
  result = rgb_to_256color(255, 0, 0); // Red
  ASCIICHAT_REQUIRE(result >= 16, "Red should map to color cube (16-231)");
  ASCIICHAT_REQUIRE(result <= 231, "Red should map to color cube (16-231)");

  result = rgb_to_256color(0, 255, 0); // Green
  ASCIICHAT_REQUIRE(result >= 16, "Green should map to color cube (16-231)");
  ASCIICHAT_REQUIRE(result <= 231, "Green should map to color cube (16-231)");

  result = rgb_to_256color(0, 0, 255); // Blue
  ASCIICHAT_REQUIRE(result >= 16, "Blue should map to color cube (16-231)");
  ASCIICHAT_REQUIRE(result <= 231, "Blue should map to color cube (16-231)");
}

TEST_CASE("ansi_fast/rgb_to_256color_grayscale", "[ansi_fast]") {
  uint8_t result;

  // Test grayscale conversion
  result = rgb_to_256color(0, 0, 0); // Black
  ASCIICHAT_REQUIRE_EQ(result, 16, "Black should use exact cube black");

  result = rgb_to_256color(255, 255, 255); // White
  ASCIICHAT_REQUIRE_EQ(result, 231, "White should use exact cube white");

  result = rgb_to_256color(128, 128, 128); // Gray
  ASCIICHAT_REQUIRE(result >= 232, "Gray should map to grayscale ramp (232-255)");
  ASCIICHAT_REQUIRE(result <= 255, "Gray should map to grayscale ramp (232-255)");
}

TEST_CASE("ansi_fast/rgb_to_256color_edge_cases", "[ansi_fast]") {
  uint8_t result;

  // Test edge cases
  result = rgb_to_256color(1, 1, 1); // Near black
  ASCIICHAT_REQUIRE_EQ(result, 16, "Near black should use the nearest cube endpoint");

  result = rgb_to_256color(254, 254, 254); // Near white
  ASCIICHAT_REQUIRE_EQ(result, 231, "Near white should use the nearest cube endpoint");

  result = rgb_to_256color(255, 0, 1); // Not grayscale
  ASCIICHAT_REQUIRE(result >= 16, "Non-grayscale should map to color cube");
  ASCIICHAT_REQUIRE(result <= 231, "Non-grayscale should map to color cube");
}

/* ============================================================================
 * 16-Color Mode Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/append_16color_fg_basic", "[ansi_fast]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test basic 16-color foreground
  result = append_16color_fg(buffer, 1); // Red
  *result = '\0';

  // Should generate: \033[31m
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[31m", "16-color red foreground should be correct");

  result = append_16color_fg(buffer, 2); // Green
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[32m", "16-color green foreground should be correct");
}

TEST_CASE("ansi_fast/append_16color_fg_edge_cases", "[ansi_fast]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test edge cases
  result = append_16color_fg(buffer, 0); // Black
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[30m", "Black foreground should be correct");

  result = append_16color_fg(buffer, 7); // Light gray
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[37m", "Light gray foreground should be correct");

  result = append_16color_fg(buffer, 9); // Bright red
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[91m", "Bright red foreground should be correct");

  // Test invalid color index
  result = append_16color_fg(buffer, 99); // Invalid
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[37m", "Invalid color should default to white");
}

TEST_CASE("ansi_fast/append_16color_bg_basic", "[ansi_fast]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test basic 16-color background
  result = append_16color_bg(buffer, 1); // Red
  *result = '\0';

  // Should generate: \033[41m
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[41m", "16-color red background should be correct");

  result = append_16color_bg(buffer, 2); // Green
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[42m", "16-color green background should be correct");
}

TEST_CASE("ansi_fast/append_16color_bg_edge_cases", "[ansi_fast]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test edge cases
  result = append_16color_bg(buffer, 0); // Black
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[40m", "Black background should be correct");

  result = append_16color_bg(buffer, 7); // Light gray
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[47m", "Light gray background should be correct");

  result = append_16color_bg(buffer, 9); // Bright red
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[101m", "Bright red background should be correct");

  // Test invalid color index
  result = append_16color_bg(buffer, 99); // Invalid
  *result = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[40m", "Invalid color should default to black");
}

TEST_CASE("ansi_fast/rgb_to_16color_basic", "[ansi_fast]") {
  uint8_t result;

  // Test basic RGB to 16-color conversion
  result = rgb_to_16color(255, 0, 0); // Red
  ASCIICHAT_REQUIRE_EQ(result, 9, "Bright red should map to color 9");

  result = rgb_to_16color(0, 255, 0); // Green
  ASCIICHAT_REQUIRE_EQ(result, 10, "Bright green should map to color 10");

  result = rgb_to_16color(0, 0, 255); // Blue
  ASCIICHAT_REQUIRE_EQ(result, 12, "Bright blue should map to color 12");

  result = rgb_to_16color(0, 0, 0); // Black
  ASCIICHAT_REQUIRE_EQ(result, 0, "Black should map to color 0");

  result = rgb_to_16color(255, 255, 255); // White
  ASCIICHAT_REQUIRE_EQ(result, 15, "White should map to color 15");
}

TEST_CASE("ansi_fast/rgb_to_16color_approximations", "[ansi_fast]") {
  uint8_t result;

  // Test color approximations
  result = rgb_to_16color(128, 0, 0); // Dark red
  ASCIICHAT_REQUIRE_EQ(result, 1, "Dark red should map to color 1");

  result = rgb_to_16color(0, 128, 0); // Dark green
  ASCIICHAT_REQUIRE_EQ(result, 2, "Dark green should map to color 2");

  result = rgb_to_16color(0, 0, 128); // Dark blue
  ASCIICHAT_REQUIRE_EQ(result, 4, "Dark blue should map to color 4");

  result = rgb_to_16color(192, 192, 192); // Light gray
  ASCIICHAT_REQUIRE_EQ(result, 7, "Light gray should map to color 7");
}

TEST_CASE("ansi_fast/get_16color_rgb_basic", "[ansi_fast]") {
  uint8_t r, g, b;

  // Test getting RGB values for 16-color indices
  get_16color_rgb(0, &r, &g, &b);
  ASCIICHAT_REQUIRE_EQ(r, 0, "Black R should be 0");
  ASCIICHAT_REQUIRE_EQ(g, 0, "Black G should be 0");
  ASCIICHAT_REQUIRE_EQ(b, 0, "Black B should be 0");

  get_16color_rgb(9, &r, &g, &b);
  ASCIICHAT_REQUIRE_EQ(r, 255, "Bright red R should be 255");
  ASCIICHAT_REQUIRE_EQ(g, 0, "Bright red G should be 0");
  ASCIICHAT_REQUIRE_EQ(b, 0, "Bright red B should be 0");

  get_16color_rgb(10, &r, &g, &b);
  ASCIICHAT_REQUIRE_EQ(r, 0, "Bright green R should be 0");
  ASCIICHAT_REQUIRE_EQ(g, 255, "Bright green G should be 255");
  ASCIICHAT_REQUIRE_EQ(b, 0, "Bright green B should be 0");

  get_16color_rgb(15, &r, &g, &b);
  ASCIICHAT_REQUIRE_EQ(r, 255, "White R should be 255");
  ASCIICHAT_REQUIRE_EQ(g, 255, "White G should be 255");
  ASCIICHAT_REQUIRE_EQ(b, 255, "White B should be 255");
}

TEST_CASE("ansi_fast/get_16color_rgb_edge_cases", "[ansi_fast]") {
  uint8_t r, g, b;

  // Test invalid color index
  get_16color_rgb(99, &r, &g, &b);
  ASCIICHAT_REQUIRE_EQ(r, 192, "Invalid color should default to light gray R");
  ASCIICHAT_REQUIRE_EQ(g, 192, "Invalid color should default to light gray G");
  ASCIICHAT_REQUIRE_EQ(b, 192, "Invalid color should default to light gray B");
}

/* ============================================================================
 * Dithering Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/rgb_to_16color_dithered_basic", "[ansi_fast]") {
  rgb_error_t error_buffer[100]; // 10x10 buffer
  uint8_t result;

  // Initialize error buffer
  memset(error_buffer, 0, sizeof(error_buffer));

  // Test basic dithering with a color that will have error
  result = rgb_to_16color_dithered(200, 50, 50, 0, 0, 10, 10, error_buffer);
  ASCIICHAT_REQUIRE(result >= 0, "Should return valid color index");
  ASCIICHAT_REQUIRE(result <= 15, "Should return valid color index");

  // Check that error was distributed (may be 0 if color maps exactly)
  // Note: Error distribution depends on the actual color mapping
}

TEST_CASE("ansi_fast/rgb_to_16color_dithered_no_error_buffer", "[ansi_fast]") {
  uint8_t result;

  // Test dithering without error buffer
  result = rgb_to_16color_dithered(255, 0, 0, 0, 0, 10, 10, NULL);
  ASCIICHAT_REQUIRE_EQ(result, 9, "Red should map to bright red without error buffer");
}

TEST_CASE("ansi_fast/rgb_to_16color_dithered_edge_cases", "[ansi_fast]") {
  rgb_error_t error_buffer[100];
  uint8_t result;

  memset(error_buffer, 0, sizeof(error_buffer));

  // Test edge cases (boundary pixels)
  result = rgb_to_16color_dithered(255, 0, 0, 9, 0, 10, 10, error_buffer); // Right edge
  ASCIICHAT_REQUIRE_EQ(result, 9, "Right edge pixel should work");

  result = rgb_to_16color_dithered(255, 0, 0, 0, 9, 10, 10, error_buffer); // Bottom edge
  ASCIICHAT_REQUIRE_EQ(result, 9, "Bottom edge pixel should work");

  result = rgb_to_16color_dithered(255, 0, 0, 9, 9, 10, 10, error_buffer); // Corner
  ASCIICHAT_REQUIRE_EQ(result, 9, "Corner pixel should work");
}

TEST_CASE("ansi_fast/rgb_to_16color_dithered_clamping", "[ansi_fast]") {
  rgb_error_t error_buffer[100];
  uint8_t result;

  memset(error_buffer, 0, sizeof(error_buffer));

  // Test with large error values that would cause overflow
  error_buffer[0].r = 1000;
  error_buffer[0].g = -1000;
  error_buffer[0].b = 500;

  result = rgb_to_16color_dithered(128, 128, 128, 0, 0, 10, 10, error_buffer);

  // Should still return a valid color index
  ASCIICHAT_REQUIRE(result >= 0, "Result should be valid color index");
  ASCIICHAT_REQUIRE(result <= 15, "Result should be valid color index");
}

/* ============================================================================
 * Mode-Aware Color Function Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/append_color_fg_for_mode_truecolor", "[ansi_fast]") {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_TRUECOLOR);
  *result = '\0';

  ASCIICHAT_REQUIRE_STR_EQ(buffer, "\033[38;2;255;128;64m", "Truecolor mode should generate truecolor sequence");
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_256color", "[ansi_fast]") {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_256_COLOR);
  *result = '\0';

  ASCIICHAT_REQUIRE(strstr(buffer, "\033[38;5;") != NULL, "256-color mode should generate 256-color sequence");
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_16color", "[ansi_fast]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_16_COLOR);
  *result = '\0';

  ASCIICHAT_REQUIRE(strstr(buffer, "\033[") != NULL, "16-color mode should generate ANSI sequence");
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_none", "[ansi_fast]") {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_NONE);

  // Should return unchanged buffer (no color output)
  ASCIICHAT_REQUIRE_EQ(result, buffer, "None mode should return unchanged buffer");
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_auto", "[ansi_fast]") {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_AUTO);

  // Should return unchanged buffer (no color output)
  ASCIICHAT_REQUIRE_EQ(result, buffer, "Auto mode should return unchanged buffer");
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_invalid", "[ansi_fast]") {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, (terminal_color_mode_t)999);

  // Should return unchanged buffer (no color output)
  ASCIICHAT_REQUIRE_EQ(result, buffer, "Invalid mode should return unchanged buffer");
}

/* ============================================================================
 * Performance and Stress Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/performance_truecolor_generation", "[ansi_fast]") {
  char buffer[4096];
  char *pos = buffer;

  // Generate many truecolor sequences
  for (int i = 0; i < 100; i++) {
    pos = append_truecolor_fg(pos, i % 256, (i * 2) % 256, (i * 3) % 256);
  }

  ASCIICHAT_REQUIRE_LT((size_t)(pos - buffer), sizeof(buffer), "Should not overflow buffer");
  ASCIICHAT_REQUIRE_GT(pos - buffer, 0, "Should generate some output");
}

TEST_CASE("ansi_fast/performance_rle_generation", "[ansi_fast]") {
  char buffer[4096];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add many pixels with varying colors
  for (int i = 0; i < 100; i++) {
    ansi_rle_add_pixel(&ctx, i % 256, (i * 2) % 256, (i * 3) % 256, 'A' + (i % 26));
  }

  ansi_rle_finish(&ctx);

  ASCIICHAT_REQUIRE_LT(ctx.length, sizeof(buffer), "Should not overflow buffer");
  ASCIICHAT_REQUIRE_GT(ctx.length, 0, "Should generate some output");
  ASCIICHAT_REQUIRE_EQ(buffer[ctx.length], '\0', "Should be null terminated");
}

TEST_CASE("ansi_fast/performance_color_conversion", "[ansi_fast]") {
  uint8_t result;

  // Test many color conversions
  for (int i = 0; i < 1000; i++) {
    result = rgb_to_256color(i % 256, (i * 2) % 256, (i * 3) % 256);
    ASCIICHAT_REQUIRE(result >= 0, "256-color result should be valid");
    ASCIICHAT_REQUIRE(result <= 255, "256-color result should be valid");

    result = rgb_to_16color(i % 256, (i * 2) % 256, (i * 3) % 256);
    ASCIICHAT_REQUIRE(result >= 0, "16-color result should be valid");
    ASCIICHAT_REQUIRE(result <= 15, "16-color result should be valid");
  }
}

/* ============================================================================
 * Buffer Overflow Protection Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/buffer_overflow_protection_rle", "[ansi_fast]") {
  char buffer[10]; // Very small buffer
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Try to add many pixels - should not overflow
  for (int i = 0; i < 100; i++) {
    ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  }

  ASCIICHAT_REQUIRE(ctx.length <= sizeof(buffer), "Should not overflow small buffer");
}

TEST_CASE("ansi_fast/buffer_overflow_protection_finish", "[ansi_fast]") {
  char buffer[5]; // Very small buffer
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  // Finish should not overflow
  ansi_rle_finish(&ctx);

  ASCIICHAT_REQUIRE(ctx.length <= sizeof(buffer), "Finish should not overflow small buffer");
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/integration_full_workflow", "[ansi_fast]") {
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
  ASCIICHAT_REQUIRE_GT(ctx.length, 0, "Should generate output");
  ASCIICHAT_REQUIRE_EQ(buffer[ctx.length], '\0', "Should be null terminated");
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[38;2;") != NULL, "Should contain foreground sequences");
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[0m") != NULL, "Should contain reset sequence");

  // Check that all expected characters are present (they may be separated by ANSI sequences)
  ASCIICHAT_REQUIRE(strstr(buffer, "R") != NULL, "Should contain character R");
  ASCIICHAT_REQUIRE(strstr(buffer, "E") != NULL, "Should contain character E");
  ASCIICHAT_REQUIRE(strstr(buffer, "D") != NULL, "Should contain character D");
  ASCIICHAT_REQUIRE(strstr(buffer, "B") != NULL, "Should contain character B");
}

TEST_CASE("ansi_fast/integration_color_mode_switching", "[ansi_fast]") {
  char buffer[256];
  char *result;

  // Initialize color modes
  ansi_fast_init_256color();
  ansi_fast_init_16color();

  // Test switching between different color modes
  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_TRUECOLOR);
  *result = '\0';
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[38;2;") != NULL, "Truecolor should work");

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_256_COLOR);
  *result = '\0';
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[38;5;") != NULL, "256-color should work");

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_16_COLOR);
  *result = '\0';
  ASCIICHAT_REQUIRE(strstr(buffer, "\033[") != NULL, "16-color should work");
}
// trigger test workflow
// coverage fix

#include <catch2/generators/catch_generators_all.hpp>
#include <iterator>
#include <catch2/catch_test_macros.hpp>
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

TEST_CASE("ansi_fast/initialization", "[ansi_fast] [unit]") {
  // Test that initialization functions work without crashing
  ansi_fast_init_256color();
  ansi_fast_init_16color();

  REQUIRE(true);
}

TEST_CASE("ansi_fast/multiple_initialization_calls", "[ansi_fast] [unit]") {
  // Test that multiple initialization calls are safe
  ansi_fast_init_256color();
  ansi_fast_init_256color(); // Should be safe to call multiple times
  ansi_fast_init_16color();
  ansi_fast_init_16color(); // Should be safe to call multiple times

  REQUIRE(true);
}

/* ============================================================================
 * Truecolor ANSI Generation Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/append_truecolor_fg_basic", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  // Test basic foreground color generation
  result = append_truecolor_fg(buffer, 255, 128, 64);
  *result = '\0'; // Null terminate for string comparison

  // Should generate: \033[38;2;255;128;64m
  REQUIRE(std::strcmp((buffer), ("\033[38;2;255;128;64m")) == 0);

  // Test that result pointer is positioned correctly
  REQUIRE((result - buffer) == (18));
}

// Test case structure for truecolor edge cases
typedef struct {
  uint8_t r, g, b;
  char expected_output[32];
  char description[64];
} truecolor_fg_test_case_t;

static truecolor_fg_test_case_t truecolor_fg_edge_cases[] = {
    {0, 0, 0, "\033[38;2;0;0;0m", "Black color"},         {1, 1, 1, "\033[38;2;1;1;1m", "Minimal color"},  {255, 255, 255, "\033[38;2;255;255;255m", "White color"},
    {255, 0, 0, "\033[38;2;255;0;0m", "Pure red"},        {0, 255, 0, "\033[38;2;0;255;0m", "Pure green"}, {0, 0, 255, "\033[38;2;0;0;255m", "Pure blue"},
    {128, 128, 128, "\033[38;2;128;128;128m", "Mid gray"}};

TEST_CASE("ansi_fast/truecolor_fg_edge_cases_param", "[ansi_fast] [unit]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(truecolor_fg_edge_cases), std::end(truecolor_fg_edge_cases)));
  auto *tc = &generated_case;
  char buffer[256];
  char *result = append_truecolor_fg(buffer, tc->r, tc->g, tc->b);
  *result = '\0';
  REQUIRE(std::strcmp((buffer), (tc->expected_output)) == 0);
}

TEST_CASE("ansi_fast/append_truecolor_bg_basic", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  // Test basic background color generation
  result = append_truecolor_bg(buffer, 100, 200, 50);
  *result = '\0';

  // Should generate: \033[48;2;100;200;50m
  REQUIRE(std::strcmp((buffer), ("\033[48;2;100;200;50m")) == 0);

  REQUIRE((result - buffer) == (18));
}

// Test case structure for truecolor background edge cases
typedef struct {
  uint8_t r, g, b;
  char expected_output[32];
  char description[64];
} truecolor_bg_test_case_t;

static truecolor_bg_test_case_t truecolor_bg_edge_cases[] = {{0, 0, 0, "\033[48;2;0;0;0m", "Black background"},
                                                             {1, 1, 1, "\033[48;2;1;1;1m", "Minimal background"},
                                                             {255, 255, 255, "\033[48;2;255;255;255m", "White background"},
                                                             {255, 0, 0, "\033[48;2;255;0;0m", "Pure red background"},
                                                             {0, 255, 0, "\033[48;2;0;255;0m", "Pure green background"},
                                                             {0, 0, 255, "\033[48;2;0;0;255m", "Pure blue background"},
                                                             {128, 128, 128, "\033[48;2;128;128;128m", "Mid gray background"}};

TEST_CASE("ansi_fast/truecolor_bg_edge_cases_param", "[ansi_fast] [unit]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(truecolor_bg_edge_cases), std::end(truecolor_bg_edge_cases)));
  auto *tc = &generated_case;
  char buffer[256];
  char *result = append_truecolor_bg(buffer, tc->r, tc->g, tc->b);
  *result = '\0';
  REQUIRE(std::strcmp((buffer), (tc->expected_output)) == 0);
}

TEST_CASE("ansi_fast/append_truecolor_fg_bg_combined", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  // Test combined foreground and background
  result = append_truecolor_fg_bg(buffer, 255, 0, 0, 0, 0, 255);
  *result = '\0';

  // Should generate: \033[38;2;255;0;0;48;2;0;0;255m
  REQUIRE(std::strcmp((buffer), ("\033[38;2;255;0;0;48;2;0;0;255m")) == 0);

  REQUIRE((result - buffer) == (28));
}

TEST_CASE("ansi_fast/append_truecolor_fg_bg_edge_cases", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  // Test various combinations
  result = append_truecolor_fg_bg(buffer, 0, 0, 0, 255, 255, 255);
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[38;2;0;0;0;48;2;255;255;255m")) == 0);

  result = append_truecolor_fg_bg(buffer, 128, 64, 192, 64, 128, 32);
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[38;2;128;64;192;48;2;64;128;32m")) == 0);
}

/* ============================================================================
 * Run-Length Encoding Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/ansi_rle_init", "[ansi_fast] [unit]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  // Test initialization
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  REQUIRE((ctx.buffer) == (buffer));
  REQUIRE((ctx.capacity) == (sizeof(buffer)));
  REQUIRE((ctx.length) == (0));
  REQUIRE((ctx.mode) == (ANSI_MODE_FOREGROUND));
  REQUIRE((ctx.first_pixel) == (true));
  REQUIRE((ctx.last_r) == (0xFF));
  REQUIRE((ctx.last_g) == (0xFF));
  REQUIRE((ctx.last_b) == (0xFF));
}

TEST_CASE("ansi_fast/ansi_rle_add_pixel_first", "[ansi_fast] [unit]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel - should emit SGR sequence
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  REQUIRE((ctx.length) > (0));
  REQUIRE((ctx.last_r) == (255));
  REQUIRE((ctx.last_g) == (128));
  REQUIRE((ctx.last_b) == (64));
  REQUIRE((ctx.first_pixel) == (false));

  // Check that the character was added
  REQUIRE((buffer[ctx.length - 1]) == ('A'));
}

TEST_CASE("ansi_fast/ansi_rle_add_pixel_same_color", "[ansi_fast] [unit]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  size_t length_after_first = ctx.length;

  // Add second pixel with same color - should not emit new SGR
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'B');

  // Length should only increase by 1 (for the character)
  REQUIRE((ctx.length) == (length_after_first + 1));
  REQUIRE((buffer[ctx.length - 1]) == ('B'));
}

TEST_CASE("ansi_fast/ansi_rle_add_pixel_different_color", "[ansi_fast] [unit]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add first pixel
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  size_t length_after_first = ctx.length;

  // Add second pixel with different color - should emit new SGR
  ansi_rle_add_pixel(&ctx, 100, 200, 50, 'B');

  // Length should increase by more than 1 (SGR + character)
  REQUIRE((ctx.length) > (length_after_first + 1));
  REQUIRE((ctx.last_r) == (100));
  REQUIRE((ctx.last_g) == (200));
  REQUIRE((ctx.last_b) == (50));
}

TEST_CASE("ansi_fast/ansi_rle_finish", "[ansi_fast] [unit]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  size_t length_before_finish = ctx.length;
  ansi_rle_finish(&ctx);

  // Should add reset sequence and null terminator
  REQUIRE((ctx.length) > (length_before_finish));
  REQUIRE((buffer[ctx.length]) == ('\0'));

  // Check that reset sequence was added
  REQUIRE(strstr(buffer, "\033[0m") != NULL);
}

TEST_CASE("ansi_fast/ansi_rle_different_modes", "[ansi_fast] [unit]") {
  char buffer[256];
  ansi_rle_context_t ctx;

  // Test foreground mode
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  REQUIRE(strstr(buffer, "\033[38;2;") != NULL);

  // Test background mode
  memset(buffer, 0, sizeof(buffer));
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_BACKGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  REQUIRE(strstr(buffer, "\033[48;2;") != NULL);

  // Test combined mode
  memset(buffer, 0, sizeof(buffer));
  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND_BACKGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  ansi_rle_finish(&ctx);
  REQUIRE(strstr(buffer, "\033[38;2;") != NULL);
  REQUIRE(strstr(buffer, ";48;2;") != NULL);
}

/* ============================================================================
 * 256-Color Mode Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/append_256color_fg_basic", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  // Test basic 256-color foreground
  result = append_256color_fg(buffer, 123);
  *result = '\0';

  // Should generate: \033[38;5;123m
  REQUIRE(std::strcmp((buffer), ("\033[38;5;123m")) == 0);
}

TEST_CASE("ansi_fast/append_256color_fg_edge_cases", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  // Test edge cases
  result = append_256color_fg(buffer, 0);
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[38;5;0m")) == 0);

  result = append_256color_fg(buffer, 255);
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[38;5;255m")) == 0);
}

TEST_CASE("ansi_fast/rgb_to_256color_basic", "[ansi_fast] [unit]") {
  uint8_t result;

  // Test basic RGB to 256-color conversion
  result = rgb_to_256color(255, 0, 0); // Red
  REQUIRE(result >= 16);
  REQUIRE(result <= 231);

  result = rgb_to_256color(0, 255, 0); // Green
  REQUIRE(result >= 16);
  REQUIRE(result <= 231);

  result = rgb_to_256color(0, 0, 255); // Blue
  REQUIRE(result >= 16);
  REQUIRE(result <= 231);
}

TEST_CASE("ansi_fast/rgb_to_256color_grayscale", "[ansi_fast] [unit]") {
  uint8_t result;

  // Test grayscale conversion
  result = rgb_to_256color(0, 0, 0); // Black
  REQUIRE((result) == (16));

  result = rgb_to_256color(255, 255, 255); // White
  REQUIRE((result) == (231));

  result = rgb_to_256color(128, 128, 128); // Gray
  REQUIRE(result >= 232);
  REQUIRE(result <= 255);
}

TEST_CASE("ansi_fast/rgb_to_256color_edge_cases", "[ansi_fast] [unit]") {
  uint8_t result;

  // Test edge cases
  result = rgb_to_256color(1, 1, 1); // Near black
  REQUIRE((result) == (16));

  result = rgb_to_256color(254, 254, 254); // Near white
  REQUIRE((result) == (231));

  result = rgb_to_256color(255, 0, 1); // Not grayscale
  REQUIRE(result >= 16);
  REQUIRE(result <= 231);
}

/* ============================================================================
 * 16-Color Mode Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/append_16color_fg_basic", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test basic 16-color foreground
  result = append_16color_fg(buffer, 1); // Red
  *result = '\0';

  // Should generate: \033[31m
  REQUIRE(std::strcmp((buffer), ("\033[31m")) == 0);

  result = append_16color_fg(buffer, 2); // Green
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[32m")) == 0);
}

TEST_CASE("ansi_fast/append_16color_fg_edge_cases", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test edge cases
  result = append_16color_fg(buffer, 0); // Black
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[30m")) == 0);

  result = append_16color_fg(buffer, 7); // Light gray
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[37m")) == 0);

  result = append_16color_fg(buffer, 9); // Bright red
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[91m")) == 0);

  // Test invalid color index
  result = append_16color_fg(buffer, 99); // Invalid
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[37m")) == 0);
}

TEST_CASE("ansi_fast/append_16color_bg_basic", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test basic 16-color background
  result = append_16color_bg(buffer, 1); // Red
  *result = '\0';

  // Should generate: \033[41m
  REQUIRE(std::strcmp((buffer), ("\033[41m")) == 0);

  result = append_16color_bg(buffer, 2); // Green
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[42m")) == 0);
}

TEST_CASE("ansi_fast/append_16color_bg_edge_cases", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  // Test edge cases
  result = append_16color_bg(buffer, 0); // Black
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[40m")) == 0);

  result = append_16color_bg(buffer, 7); // Light gray
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[47m")) == 0);

  result = append_16color_bg(buffer, 9); // Bright red
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[101m")) == 0);

  // Test invalid color index
  result = append_16color_bg(buffer, 99); // Invalid
  *result = '\0';
  REQUIRE(std::strcmp((buffer), ("\033[40m")) == 0);
}

TEST_CASE("ansi_fast/rgb_to_16color_basic", "[ansi_fast] [unit]") {
  uint8_t result;

  // Test basic RGB to 16-color conversion
  result = rgb_to_16color(255, 0, 0); // Red
  REQUIRE((result) == (9));

  result = rgb_to_16color(0, 255, 0); // Green
  REQUIRE((result) == (10));

  result = rgb_to_16color(0, 0, 255); // Blue
  REQUIRE((result) == (12));

  result = rgb_to_16color(0, 0, 0); // Black
  REQUIRE((result) == (0));

  result = rgb_to_16color(255, 255, 255); // White
  REQUIRE((result) == (15));
}

TEST_CASE("ansi_fast/rgb_to_16color_approximations", "[ansi_fast] [unit]") {
  uint8_t result;

  // Test color approximations
  result = rgb_to_16color(128, 0, 0); // Dark red
  REQUIRE((result) == (1));

  result = rgb_to_16color(0, 128, 0); // Dark green
  REQUIRE((result) == (2));

  result = rgb_to_16color(0, 0, 128); // Dark blue
  REQUIRE((result) == (4));

  result = rgb_to_16color(192, 192, 192); // Light gray
  REQUIRE((result) == (7));
}

TEST_CASE("ansi_fast/get_16color_rgb_basic", "[ansi_fast] [unit]") {
  uint8_t r, g, b;

  // Test getting RGB values for 16-color indices
  get_16color_rgb(0, &r, &g, &b);
  REQUIRE((r) == (0));
  REQUIRE((g) == (0));
  REQUIRE((b) == (0));

  get_16color_rgb(9, &r, &g, &b);
  REQUIRE((r) == (255));
  REQUIRE((g) == (0));
  REQUIRE((b) == (0));

  get_16color_rgb(10, &r, &g, &b);
  REQUIRE((r) == (0));
  REQUIRE((g) == (255));
  REQUIRE((b) == (0));

  get_16color_rgb(15, &r, &g, &b);
  REQUIRE((r) == (255));
  REQUIRE((g) == (255));
  REQUIRE((b) == (255));
}

TEST_CASE("ansi_fast/get_16color_rgb_edge_cases", "[ansi_fast] [unit]") {
  uint8_t r, g, b;

  // Test invalid color index
  get_16color_rgb(99, &r, &g, &b);
  REQUIRE((r) == (192));
  REQUIRE((g) == (192));
  REQUIRE((b) == (192));
}

/* ============================================================================
 * Dithering Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/rgb_to_16color_dithered_basic", "[ansi_fast] [unit]") {
  rgb_error_t error_buffer[100]; // 10x10 buffer
  uint8_t result;

  // Initialize error buffer
  memset(error_buffer, 0, sizeof(error_buffer));

  // Test basic dithering with a color that will have error
  result = rgb_to_16color_dithered(200, 50, 50, 0, 0, 10, 10, error_buffer);
  REQUIRE(result >= 0);
  REQUIRE(result <= 15);

  // Check that error was distributed (may be 0 if color maps exactly)
  // Note: Error distribution depends on the actual color mapping
}

TEST_CASE("ansi_fast/rgb_to_16color_dithered_no_error_buffer", "[ansi_fast] [unit]") {
  uint8_t result;

  // Test dithering without error buffer
  result = rgb_to_16color_dithered(255, 0, 0, 0, 0, 10, 10, NULL);
  REQUIRE((result) == (9));
}

TEST_CASE("ansi_fast/rgb_to_16color_dithered_edge_cases", "[ansi_fast] [unit]") {
  rgb_error_t error_buffer[100];
  uint8_t result;

  memset(error_buffer, 0, sizeof(error_buffer));

  // Test edge cases (boundary pixels)
  result = rgb_to_16color_dithered(255, 0, 0, 9, 0, 10, 10, error_buffer); // Right edge
  REQUIRE((result) == (9));

  result = rgb_to_16color_dithered(255, 0, 0, 0, 9, 10, 10, error_buffer); // Bottom edge
  REQUIRE((result) == (9));

  result = rgb_to_16color_dithered(255, 0, 0, 9, 9, 10, 10, error_buffer); // Corner
  REQUIRE((result) == (9));
}

TEST_CASE("ansi_fast/rgb_to_16color_dithered_clamping", "[ansi_fast] [unit]") {
  rgb_error_t error_buffer[100];
  uint8_t result;

  memset(error_buffer, 0, sizeof(error_buffer));

  // Test with large error values that would cause overflow
  error_buffer[0].r = 1000;
  error_buffer[0].g = -1000;
  error_buffer[0].b = 500;

  result = rgb_to_16color_dithered(128, 128, 128, 0, 0, 10, 10, error_buffer);

  // Should still return a valid color index
  REQUIRE(result >= 0);
  REQUIRE(result <= 15);
}

/* ============================================================================
 * Mode-Aware Color Function Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/append_color_fg_for_mode_truecolor", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_TRUECOLOR);
  *result = '\0';

  REQUIRE(std::strcmp((buffer), ("\033[38;2;255;128;64m")) == 0);
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_256color", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  ansi_fast_init_256color();

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_256_COLOR);
  *result = '\0';

  REQUIRE(strstr(buffer, "\033[38;5;") != NULL);
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_16color", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  ansi_fast_init_16color();

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_16_COLOR);
  *result = '\0';

  REQUIRE(strstr(buffer, "\033[") != NULL);
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_none", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_NONE);

  // Should return unchanged buffer (no color output)
  REQUIRE((result) == (buffer));
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_auto", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, COLOR_MODE_AUTO);

  // Should return unchanged buffer (no color output)
  REQUIRE((result) == (buffer));
}

TEST_CASE("ansi_fast/append_color_fg_for_mode_invalid", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  result = append_color_fg_for_mode(buffer, 255, 128, 64, (terminal_color_mode_t)999);

  // Should return unchanged buffer (no color output)
  REQUIRE((result) == (buffer));
}

/* ============================================================================
 * Performance and Stress Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/performance_truecolor_generation", "[ansi_fast] [unit]") {
  char buffer[4096];
  char *pos = buffer;

  // Generate many truecolor sequences
  for (int i = 0; i < 100; i++) {
    pos = append_truecolor_fg(pos, i % 256, (i * 2) % 256, (i * 3) % 256);
  }

  REQUIRE(((size_t)(pos - buffer)) < (sizeof(buffer)));
  REQUIRE((pos - buffer) > (0));
}

TEST_CASE("ansi_fast/performance_rle_generation", "[ansi_fast] [unit]") {
  char buffer[4096];
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Add many pixels with varying colors
  for (int i = 0; i < 100; i++) {
    ansi_rle_add_pixel(&ctx, i % 256, (i * 2) % 256, (i * 3) % 256, 'A' + (i % 26));
  }

  ansi_rle_finish(&ctx);

  REQUIRE((ctx.length) < (sizeof(buffer)));
  REQUIRE((ctx.length) > (0));
  REQUIRE((buffer[ctx.length]) == ('\0'));
}

TEST_CASE("ansi_fast/performance_color_conversion", "[ansi_fast] [unit]") {
  uint8_t result;

  // Test many color conversions
  for (int i = 0; i < 1000; i++) {
    result = rgb_to_256color(i % 256, (i * 2) % 256, (i * 3) % 256);
    REQUIRE(result >= 0);
    REQUIRE(result <= 255);

    result = rgb_to_16color(i % 256, (i * 2) % 256, (i * 3) % 256);
    REQUIRE(result >= 0);
    REQUIRE(result <= 15);
  }
}

/* ============================================================================
 * Buffer Overflow Protection Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/buffer_overflow_protection_rle", "[ansi_fast] [unit]") {
  char buffer[10]; // Very small buffer
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);

  // Try to add many pixels - should not overflow
  for (int i = 0; i < 100; i++) {
    ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');
  }

  REQUIRE(ctx.length <= sizeof(buffer));
}

TEST_CASE("ansi_fast/buffer_overflow_protection_finish", "[ansi_fast] [unit]") {
  char buffer[5]; // Very small buffer
  ansi_rle_context_t ctx;

  ansi_rle_init(&ctx, buffer, sizeof(buffer), ANSI_MODE_FOREGROUND);
  ansi_rle_add_pixel(&ctx, 255, 128, 64, 'A');

  // Finish should not overflow
  ansi_rle_finish(&ctx);

  REQUIRE(ctx.length <= sizeof(buffer));
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST_CASE("ansi_fast/integration_full_workflow", "[ansi_fast] [unit]") {
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
  REQUIRE((ctx.length) > (0));
  REQUIRE((buffer[ctx.length]) == ('\0'));
  REQUIRE(strstr(buffer, "\033[38;2;") != NULL);
  REQUIRE(strstr(buffer, "\033[0m") != NULL);

  // Check that all expected characters are present (they may be separated by ANSI sequences)
  REQUIRE(strstr(buffer, "R") != NULL);
  REQUIRE(strstr(buffer, "E") != NULL);
  REQUIRE(strstr(buffer, "D") != NULL);
  REQUIRE(strstr(buffer, "B") != NULL);
}

TEST_CASE("ansi_fast/integration_color_mode_switching", "[ansi_fast] [unit]") {
  char buffer[256];
  char *result;

  // Initialize color modes
  ansi_fast_init_256color();
  ansi_fast_init_16color();

  // Test switching between different color modes
  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_TRUECOLOR);
  *result = '\0';
  REQUIRE(strstr(buffer, "\033[38;2;") != NULL);

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_256_COLOR);
  *result = '\0';
  REQUIRE(strstr(buffer, "\033[38;5;") != NULL);

  result = append_color_fg_for_mode(buffer, 255, 0, 0, COLOR_MODE_16_COLOR);
  *result = '\0';
  REQUIRE(strstr(buffer, "\033[") != NULL);
}
// trigger test workflow
// coverage fix

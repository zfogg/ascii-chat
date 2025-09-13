#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "image2ascii/image.h"
#include "image2ascii/simd/ascii_simd.h"
#include "image2ascii/simd/common.h"
#include "image2ascii/simd/neon.h"

void setup_neon_quiet_logging(void);
void restore_neon_logging(void);

TestSuite(neon_color_renderers, .init = setup_neon_quiet_logging, .fini = restore_neon_logging);

void setup_neon_quiet_logging(void) {
  log_set_level(LOG_FATAL);
}

void restore_neon_logging(void) {
  log_set_level(LOG_DEBUG);
}

// Test helper: create a simple test image
static image_t *create_test_image(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
  image_t *image;
  SAFE_MALLOC(image, sizeof(image_t), image_t *);
  image->w = width;
  image->h = height;

  size_t pixel_count = (size_t)width * (size_t)height;
  SAFE_MALLOC(image->pixels, pixel_count * sizeof(rgb_pixel_t), rgb_pixel_t *);

  // Fill with solid color
  for (size_t i = 0; i < pixel_count; i++) {
    ((rgb_pixel_t *)image->pixels)[i] = (rgb_pixel_t){r, g, b};
  }

  return image;
}

// Test helper: create gradient test image
static image_t *create_gradient_image(int width, int height) {
  image_t *image;
  SAFE_MALLOC(image, sizeof(image_t), image_t *);
  image->w = width;
  image->h = height;

  size_t pixel_count = (size_t)width * (size_t)height;
  SAFE_MALLOC(image->pixels, pixel_count * sizeof(rgb_pixel_t), rgb_pixel_t *);

  // Create RGB gradient
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t r = (uint8_t)((x * 255) / (width - 1));
      uint8_t g = (uint8_t)((y * 255) / (height - 1));
      uint8_t b = (uint8_t)(((x + y) * 127) / ((width + height) - 2));
      ((rgb_pixel_t *)image->pixels)[y * width + x] = (rgb_pixel_t){r, g, b};
    }
  }

  return image;
}

static void cleanup_image(image_t *image) {
  if (image) {
    if (image->pixels)
      free(image->pixels);
    free(image);
  }
}

#ifdef SIMD_SUPPORT_NEON

Test(neon_color_renderers, test_256color_solid_image) {
  // Test 256-color renderer with solid red image
  image_t *image = create_test_image(32, 16, 255, 0, 0); // Red image
  const char *ascii_chars = " .:-=+*#%@";

  log_debug("=== DEBUG 256-COLOR SOLID RED TEST ===");
  log_debug("Image: %dx%d pixels, all RGB(255,0,0)", image->w, image->h);
  log_debug("ASCII chars: '%s'", ascii_chars);

  char *result = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);

  log_debug("Result pointer: %p", (void *)result);
  log_debug("Result length: %zu", result ? strlen(result) : 0);

  if (result && strlen(result) > 0) {
    log_debug("First 300 chars: '%.300s'", result);
    log_debug("Contains \\033[38;5;: %s", strstr(result, "\033[38;5;") ? "YES" : "NO");
  } else {
    log_error("ERROR: 256-color result is NULL or empty!");
  }

  cr_assert_not_null(result, "256-color renderer should return non-NULL result");
  cr_assert_gt(strlen(result), 0, "256-color renderer should produce non-empty output");

  // Should contain ANSI 256-color escape sequences
  cr_assert_not_null(strstr(result, "\033[38;5;"), "Should contain 256-color FG sequences");

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_256color_gradient_image) {
  // Test 256-color renderer with gradient image
  image_t *image = create_gradient_image(48, 24);
  const char *ascii_chars = " ░▒▓█";

  char *result = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);

  cr_assert_not_null(result, "256-color renderer should return non-NULL result");
  cr_assert_gt(strlen(result), 100, "256-color renderer should produce substantial output for gradient");

  // Should contain multiple different color sequences for gradient
  cr_assert_not_null(strstr(result, "\033[38;5;"), "Should contain 256-color sequences");

  log_debug("256-color gradient result length: %zu", strlen(result));

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_truecolor_solid_image) {
  // Test truecolor renderer with solid green image
  image_t *image = create_test_image(32, 16, 0, 255, 0); // Green image
  const char *ascii_chars = " .:-=+*#%@";

  log_debug("=== DEBUG TRUECOLOR SOLID GREEN TEST ===");
  log_debug("Image: %dx%d pixels, all RGB(0,255,0)", image->w, image->h);
  log_debug("ASCII chars: '%s'", ascii_chars);

  char *result = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);

  log_debug("Result pointer: %p", (void *)result);
  log_debug("Result length: %zu", result ? strlen(result) : 0);

  if (result && strlen(result) > 0) {
    log_debug("First 300 chars: '%.300s'", result);
    log_debug("Contains \\033[38;2;: %s", strstr(result, "\033[38;2;") ? "YES" : "NO");
  } else {
    log_error("ERROR: Truecolor result is NULL or empty!");
  }

  cr_assert_not_null(result, "Truecolor renderer should return non-NULL result");
  cr_assert_gt(strlen(result), 0, "Truecolor renderer should produce non-empty output");

  // Should contain ANSI truecolor escape sequences
  cr_assert_not_null(strstr(result, "\033[38;2;"), "Should contain truecolor FG sequences");

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_truecolor_gradient_image) {
  // Test truecolor renderer with gradient image
  image_t *image = create_gradient_image(48, 24);
  const char *ascii_chars = " ░▒▓██▓▒░";

  char *result = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);

  cr_assert_not_null(result, "Truecolor renderer should return non-NULL result");
  cr_assert_gt(strlen(result), 200, "Truecolor renderer should produce substantial output for gradient");

  // Should contain truecolor sequences
  cr_assert_not_null(strstr(result, "\033[38;2;"), "Should contain truecolor sequences");

  log_debug("Truecolor gradient result length: %zu", strlen(result));

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_background_mode_256color) {
  // Test 256-color renderer in background mode
  image_t *image = create_test_image(16, 8, 0, 0, 255); // Blue image
  const char *ascii_chars = " █";

  char *result = render_ascii_neon_unified_optimized(image, true, true, ascii_chars);

  cr_assert_not_null(result, "\033[0m256-color background renderer should return non-NULL result");

  // Should contain background color sequences
  cr_assert_not_null(strstr(result, "\033[48;5;"), "Should contain 256-color BG sequences");

  log_debug("256-color background result (first 150 chars): %.150s", result);

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_background_mode_truecolor) {
  // Test truecolor renderer in background mode
  image_t *image = create_test_image(16, 8, 255, 255, 0); // Yellow image
  const char *ascii_chars = " █";

  char *result = render_ascii_neon_unified_optimized(image, true, false, ascii_chars);

  cr_assert_not_null(result, "\033[0mTruecolor background renderer should return non-NULL result");

  // Should contain background color sequences
  cr_assert_not_null(strstr(result, "\033[48;2;"), "Should contain truecolor BG sequences");

  log_debug("Truecolor background result (first 150 chars): %.150s", result);

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_unified_dispatcher_256color) {
  // Test unified dispatcher routing to 256-color
  image_t *image = create_test_image(16, 8, 128, 64, 192); // Purple image
  const char *ascii_chars = " .oO@";

  char *result = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);

  cr_assert_not_null(result, "Unified dispatcher should return non-NULL result for 256-color");
  cr_assert_not_null(strstr(result, "\033[38;5;"), "Dispatcher should route to 256-color renderer");

  log_debug("Dispatcher 256-color result length: %zu", strlen(result));

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_unified_dispatcher_truecolor) {
  // Test unified dispatcher routing to truecolor
  image_t *image = create_test_image(16, 8, 192, 128, 64); // Orange image
  const char *ascii_chars = " .oO@";

  char *result = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);

  cr_assert_not_null(result, "Unified dispatcher should return non-NULL result for truecolor");
  cr_assert_not_null(strstr(result, "\033[38;2;"), "Dispatcher should route to truecolor renderer");

  log_debug("Dispatcher truecolor result length: %zu", strlen(result));

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_direct_comparison) {
  // Direct comparison between 256-color and truecolor on same image
  image_t *image = create_test_image(16, 8, 128, 64, 192); // Purple image
  const char *ascii_chars = " .oO@";

  log_debug("=== DIRECT COMPARISON TEST ===");
  log_debug("Same image: %dx%d pixels, all RGB(128,64,192)", image->w, image->h);
  log_debug("Same ASCII chars: '%s'", ascii_chars);

  log_debug("--- 256-COLOR MODE ---");
  char *result_256 = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);
  log_debug("256-color pointer: %p", (void *)result_256);
  log_debug("256-color length: %zu", result_256 ? strlen(result_256) : 0);
  if (result_256) {
    log_debug("256-color first 200 chars: '%.200s'", result_256);
    log_debug("Contains 256-color seq: %s", strstr(result_256, "\033[38;5;") ? "YES" : "NO");
  }

  log_debug("--- TRUECOLOR MODE ---");
  char *result_true = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);
  log_debug("Truecolor pointer: %p", (void *)result_true);
  log_debug("Truecolor length: %zu", result_true ? strlen(result_true) : 0);
  if (result_true) {
    log_debug("Truecolor first 200 chars: '%.200s'", result_true);
    log_debug("Contains truecolor seq: %s", strstr(result_true, "\033[38;2;") ? "YES" : "NO");
  }

  cr_assert_not_null(result_256, "256-color should return non-NULL");
  cr_assert_not_null(result_true, "Truecolor should return non-NULL");

  if (result_256)
    free(result_256);
  if (result_true)
    free(result_true);
  cleanup_image(image);
}

Test(neon_color_renderers, test_utf8_characters) {
  // Test with UTF-8 characters (should work with mixed lengths)
  image_t *image = create_gradient_image(24, 12);
  const char *ascii_chars = " ░▒▓█🌑🌒🌓🌕"; // Mix of 1-byte and 4-byte UTF-8

  log_debug("=== UTF-8 TEST ===");
  log_debug("Gradient image: %dx%d pixels", image->w, image->h);
  log_debug("UTF-8 ASCII chars: '%s'", ascii_chars);

  char *result_256 = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);
  char *result_true = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);

  log_debug("256-color UTF-8 result length: %zu", result_256 ? strlen(result_256) : 0);
  log_debug("Truecolor UTF-8 result length: %zu", result_true ? strlen(result_true) : 0);

  if (result_256 && strlen(result_256) > 0) {
    log_debug("256-color contains moon: %s", strstr(result_256, "🌑") ? "YES" : "NO");
  }
  if (result_true && strlen(result_true) > 0) {
    log_debug("Truecolor contains moon: %s", strstr(result_true, "🌑") ? "YES" : "NO");
  }

  cr_assert_not_null(result_256, "256-color renderer should handle UTF-8 characters");
  cr_assert_not_null(result_true, "Truecolor renderer should handle UTF-8 characters");

  if (result_256)
    free(result_256);
  if (result_true)
    free(result_true);
  cleanup_image(image);
}

#else
// Placeholder tests when NEON is not supported
Test(neon_color_renderers, neon_not_supported) {
  cr_skip("NEON SIMD not supported on this platform");
}
#endif // SIMD_SUPPORT_NEON

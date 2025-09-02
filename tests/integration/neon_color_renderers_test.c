#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "image.h"
#include "ascii_simd.h"
#include "image2ascii/simd/common.h"
#include "image2ascii/simd/neon.h"

TestSuite(neon_color_renderers);

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
    if (image->pixels) free(image->pixels);
    free(image);
  }
}

#ifdef SIMD_SUPPORT_NEON

Test(neon_color_renderers, test_256color_solid_image) {
  // Test 256-color renderer with solid red image
  image_t *image = create_test_image(32, 16, 255, 0, 0);  // Red image
  const char *ascii_chars = " .:-=+*#%@";

  printf("\n=== DEBUG 256-COLOR SOLID RED TEST ===\n");
  printf("Image: %dx%d pixels, all RGB(255,0,0)\n", image->w, image->h);
  printf("ASCII chars: '%s'\n", ascii_chars);

  char *result = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);

  printf("Result pointer: %p\n", (void*)result);
  printf("\033[0mResult length: %zu\n", result ? strlen(result) : 0);

  if (result && strlen(result) > 0) {
    printf("\033[0mFirst 300 chars: '%.300s'\033[0m\n", result);
    printf("\033[0mContains \\033[38;5;: %s\n", strstr(result, "\033[38;5;") ? "YES" : "NO");
  } else {
    printf("ERROR: 256-color result is NULL or empty!\n");
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

  printf("\033[0m256-color gradient result length: %zu\n", strlen(result));

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_truecolor_solid_image) {
  // Test truecolor renderer with solid green image
  image_t *image = create_test_image(32, 16, 0, 255, 0);  // Green image
  const char *ascii_chars = " .:-=+*#%@";

  printf("\n=== DEBUG TRUECOLOR SOLID GREEN TEST ===\n");
  printf("Image: %dx%d pixels, all RGB(0,255,0)\n", image->w, image->h);
  printf("ASCII chars: '%s'\n", ascii_chars);

  char *result = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);

  printf("Result pointer: %p\n", (void*)result);
  printf("Result length: %zu\n", result ? strlen(result) : 0);

  if (result && strlen(result) > 0) {
    printf("First 300 chars: '%.300s'\033[0m\n", result);
    printf("\033[0mContains \\033[38;2;: %s\n", strstr(result, "\033[38;2;") ? "YES" : "NO");
  } else {
    printf("ERROR: Truecolor result is NULL or empty!\n");
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

  printf("\033[0mTruecolor gradient result length: %zu\n", strlen(result));

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_background_mode_256color) {
  // Test 256-color renderer in background mode
  image_t *image = create_test_image(16, 8, 0, 0, 255);  // Blue image
  const char *ascii_chars = " █";

  char *result = render_ascii_neon_unified_optimized(image, true, true, ascii_chars);

  cr_assert_not_null(result, "\033[0m256-color background renderer should return non-NULL result");

  // Should contain background color sequences
  cr_assert_not_null(strstr(result, "\033[48;5;"), "Should contain 256-color BG sequences");

  printf("\033[0m256-color background result (first 150 chars): %.150s\033[0m\n", result);

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_background_mode_truecolor) {
  // Test truecolor renderer in background mode
  image_t *image = create_test_image(16, 8, 255, 255, 0);  // Yellow image
  const char *ascii_chars = " █";

  char *result = render_ascii_neon_unified_optimized(image, true, false, ascii_chars);

  cr_assert_not_null(result, "\033[0mTruecolor background renderer should return non-NULL result");

  // Should contain background color sequences
  cr_assert_not_null(strstr(result, "\033[48;2;"), "Should contain truecolor BG sequences");

  printf("\033[0mTruecolor background result (first 150 chars): %.150s\033[0m\n", result);

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_unified_dispatcher_256color) {
  // Test unified dispatcher routing to 256-color
  image_t *image = create_test_image(16, 8, 128, 64, 192);  // Purple image
  const char *ascii_chars = " .oO@";

  char *result = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);

  cr_assert_not_null(result, "Unified dispatcher should return non-NULL result for 256-color");
  cr_assert_not_null(strstr(result, "\033[38;5;"), "Dispatcher should route to 256-color renderer");

  printf("\033[0mDispatcher 256-color result length: %zu\n", strlen(result));

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_unified_dispatcher_truecolor) {
  // Test unified dispatcher routing to truecolor
  image_t *image = create_test_image(16, 8, 192, 128, 64);  // Orange image
  const char *ascii_chars = " .oO@";

  char *result = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);

  cr_assert_not_null(result, "Unified dispatcher should return non-NULL result for truecolor");
  cr_assert_not_null(strstr(result, "\033[38;2;"), "Dispatcher should route to truecolor renderer");

  printf("\033[0mDispatcher truecolor result length: %zu\n", strlen(result));

  free(result);
  cleanup_image(image);
}

Test(neon_color_renderers, test_direct_comparison) {
  // Direct comparison between 256-color and truecolor on same image
  image_t *image = create_test_image(16, 8, 128, 64, 192);  // Purple image
  const char *ascii_chars = " .oO@";

  printf("\n=== DIRECT COMPARISON TEST ===\n");
  printf("Same image: %dx%d pixels, all RGB(128,64,192)\n", image->w, image->h);
  printf("Same ASCII chars: '%s'\n", ascii_chars);

  printf("\n--- 256-COLOR MODE ---\n");
  char *result_256 = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);
  printf("256-color pointer: %p\n", (void*)result_256);
  printf("\033[0m256-color length: %zu\n", result_256 ? strlen(result_256) : 0);
  if (result_256) {
    printf("256-color first 200 chars: '%.200s'\033[0m\n", result_256);
    printf("\033[0mContains 256-color seq: %s\n", strstr(result_256, "\033[38;5;") ? "YES" : "NO");
  }

  printf("\n--- TRUECOLOR MODE ---\n");
  char *result_true = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);
  printf("Truecolor pointer: %p\n", (void*)result_true);
  printf("Truecolor length: %zu\n", result_true ? strlen(result_true) : 0);
  if (result_true) {
    printf("Truecolor first 200 chars: '%.200s'\033[0m\n", result_true);
    printf("\033[0mContains truecolor seq: %s\n", strstr(result_true, "\033[38;2;") ? "YES" : "NO");
  }

  cr_assert_not_null(result_256, "256-color should return non-NULL");
  cr_assert_not_null(result_true, "Truecolor should return non-NULL");

  if (result_256) free(result_256);
  if (result_true) free(result_true);
  cleanup_image(image);
}

Test(neon_color_renderers, test_utf8_characters) {
  // Test with UTF-8 characters (should work with mixed lengths)
  image_t *image = create_gradient_image(24, 12);
  const char *ascii_chars = " ░▒▓█🌑🌒🌓🌕";  // Mix of 1-byte and 4-byte UTF-8

  printf("\n=== UTF-8 TEST ===\n");
  printf("Gradient image: %dx%d pixels\n", image->w, image->h);
  printf("UTF-8 ASCII chars: '%s'\n", ascii_chars);

  char *result_256 = render_ascii_neon_unified_optimized(image, false, true, ascii_chars);
  char *result_true = render_ascii_neon_unified_optimized(image, false, false, ascii_chars);

  printf("\033[0m256-color UTF-8 result length: %zu\n", result_256 ? strlen(result_256) : 0);
  printf("\033[0mTruecolor UTF-8 result length: %zu\n", result_true ? strlen(result_true) : 0);

  if (result_256 && strlen(result_256) > 0) {
    printf("\033[0m256-color contains moon: %s\n", strstr(result_256, "🌑") ? "YES" : "NO");
  }
  if (result_true && strlen(result_true) > 0) {
    printf("\033[0mTruecolor contains moon: %s\n", strstr(result_true, "🌑") ? "YES" : "NO");
  }

  cr_assert_not_null(result_256, "256-color renderer should handle UTF-8 characters");
  cr_assert_not_null(result_true, "Truecolor renderer should handle UTF-8 characters");

  if (result_256) free(result_256);
  if (result_true) free(result_true);
  cleanup_image(image);
}

#else
// Placeholder tests when NEON is not supported
Test(neon_color_renderers, neon_not_supported) {
  cr_skip("NEON SIMD not supported on this platform");
}
#endif // SIMD_SUPPORT_NEON

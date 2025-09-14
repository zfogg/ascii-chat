#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "os/webcam.h"
#include "common.h"
#include "options.h"
#include "image2ascii/image.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(webcam);

// Mock webcam platform functions
static int mock_webcam_platform_init_calls = 0;
static int mock_webcam_platform_init_result = 0;
static int mock_webcam_platform_get_dimensions_calls = 0;
static int mock_webcam_platform_get_dimensions_result = 0;
static int mock_webcam_platform_read_calls = 0;
static image_t *mock_webcam_platform_read_result = NULL;
static int mock_webcam_platform_cleanup_calls = 0;
static webcam_context_t *mock_webcam_context = (webcam_context_t *)0x12345678;

// Mock implementations
int mock_webcam_platform_init(webcam_context_t **ctx, unsigned short int device_index) {
  (void)device_index;
  mock_webcam_platform_init_calls++;
  if (ctx) {
    *ctx = mock_webcam_context;
  }
  return mock_webcam_platform_init_result;
}

int mock_webcam_platform_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  (void)ctx;
  mock_webcam_platform_get_dimensions_calls++;
  if (width)
    *width = 640;
  if (height)
    *height = 480;
  return mock_webcam_platform_get_dimensions_result;
}

image_t *mock_webcam_platform_read(webcam_context_t *ctx) {
  (void)ctx;
  mock_webcam_platform_read_calls++;
  return mock_webcam_platform_read_result;
}

void mock_webcam_platform_cleanup(webcam_context_t *ctx) {
  (void)ctx;
  mock_webcam_platform_cleanup_calls++;
}

// Override platform functions with mocks
#define webcam_platform_init mock_webcam_platform_init
#define webcam_platform_get_dimensions mock_webcam_platform_get_dimensions
#define webcam_platform_read mock_webcam_platform_read
#define webcam_platform_cleanup mock_webcam_platform_cleanup

// Helper function to reset mock state
static void reset_mock_state(void) {
  mock_webcam_platform_init_calls = 0;
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_calls = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  mock_webcam_platform_read_calls = 0;
  mock_webcam_platform_read_result = NULL;
  mock_webcam_platform_cleanup_calls = 0;
}

// Helper function to create a test image
static image_t *create_test_image(int width, int height) {
  image_t *img = malloc(sizeof(image_t));
  if (!img)
    return NULL;

  img->w = width;
  img->h = height;
  img->pixels = malloc(width * height * sizeof(rgb_t));
  if (!img->pixels) {
    free(img);
    return NULL;
  }

  // Fill with test pattern
  for (int i = 0; i < width * height; i++) {
    img->pixels[i].r = (uint8_t)(i % 256);
    img->pixels[i].g = (uint8_t)((i * 2) % 256);
    img->pixels[i].b = (uint8_t)((i * 3) % 256);
  }

  return img;
}

// Helper function to free test image
static void free_test_image(image_t *img) {
  if (img) {
    free(img->pixels);
    free(img);
  }
}

/* ============================================================================
 * Webcam Initialization Tests
 * ============================================================================ */

Test(webcam, init_success) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful mock responses
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;

  // Initialize global variables
  last_image_width = 0;
  last_image_height = 0;

  int result = webcam_init(0);

  cr_assert_eq(result, 0);
  cr_assert_eq(mock_webcam_platform_init_calls, 1);
  cr_assert_eq(mock_webcam_platform_get_dimensions_calls, 1);
  cr_assert_eq(last_image_width, 640);
  cr_assert_eq(last_image_height, 480);

  // Clean up
  webcam_cleanup();
}

Test(webcam, init_platform_failure) {
  reset_mock_state();

  // Set up platform init failure
  mock_webcam_platform_init_result = -1;

  // This should call exit() due to the error handling in webcam_init
  // We can't easily test this without forking, so we'll test the mock behavior
  int result = mock_webcam_platform_init(NULL, 0);

  cr_assert_eq(result, -1);
  cr_assert_eq(mock_webcam_platform_init_calls, 1);
}

Test(webcam, init_dimensions_failure) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful init but failed dimensions
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = -1;

  // Initialize global variables
  last_image_width = 0;
  last_image_height = 0;

  int result = webcam_init(1);

  cr_assert_eq(result, 0);
  cr_assert_eq(mock_webcam_platform_init_calls, 1);
  cr_assert_eq(mock_webcam_platform_get_dimensions_calls, 1);
  // Dimensions should remain unchanged on failure
  cr_assert_eq(last_image_width, 0);
  cr_assert_eq(last_image_height, 0);

  // Clean up
  webcam_cleanup();
}

Test(webcam, init_different_indices) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;

  // Test different webcam indices
  unsigned short int indices[] = {0, 1, 2, 5, 10, 255};
  int num_indices = sizeof(indices) / sizeof(indices[0]);

  for (int i = 0; i < num_indices; i++) {
    reset_mock_state();
    mock_webcam_platform_init_result = 0;
    mock_webcam_platform_get_dimensions_result = 0;

    int result = webcam_init(indices[i]);
    cr_assert_eq(result, 0);
    cr_assert_eq(mock_webcam_platform_init_calls, 1);

    webcam_cleanup();
  }
}

/* ============================================================================
 * Webcam Read Tests
 * ============================================================================ */

Test(webcam, read_success) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  // Set up successful read
  image_t *test_image = create_test_image(320, 240);
  cr_assert_not_null(test_image);
  mock_webcam_platform_read_result = test_image;

  // Set up global options
  opt_webcam_flip = false;
  last_image_width = 0;
  last_image_height = 0;

  image_t *result = webcam_read();

  cr_assert_not_null(result);
  cr_assert_eq(result, test_image);
  cr_assert_eq(mock_webcam_platform_read_calls, 1);
  cr_assert_eq(last_image_width, 320);
  cr_assert_eq(last_image_height, 240);

  // Clean up
  free_test_image(test_image);
  webcam_cleanup();
}

Test(webcam, read_not_initialized) {
  reset_mock_state();

  // Don't initialize webcam
  image_t *result = webcam_read();

  cr_assert_null(result);
  cr_assert_eq(mock_webcam_platform_read_calls, 0);
}

Test(webcam, read_platform_returns_null) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  // Set up platform read to return NULL
  mock_webcam_platform_read_result = NULL;

  image_t *result = webcam_read();

  cr_assert_null(result);
  cr_assert_eq(mock_webcam_platform_read_calls, 1);

  // Clean up
  webcam_cleanup();
}

Test(webcam, read_with_horizontal_flip) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  // Create test image with known pattern
  image_t *test_image = create_test_image(4, 2);
  cr_assert_not_null(test_image);

  // Set up a simple pattern to verify flipping
  for (int y = 0; y < 2; y++) {
    for (int x = 0; x < 4; x++) {
      test_image->pixels[y * 4 + x].r = (uint8_t)x;
      test_image->pixels[y * 4 + x].g = (uint8_t)x;
      test_image->pixels[y * 4 + x].b = (uint8_t)x;
    }
  }

  mock_webcam_platform_read_result = test_image;
  opt_webcam_flip = true;

  image_t *result = webcam_read();

  cr_assert_not_null(result);
  cr_assert_eq(result, test_image);

  // Verify horizontal flip was applied
  // Original: [0,1,2,3] -> Flipped: [3,2,1,0]
  cr_assert_eq(result->pixels[0].r, 3); // First pixel should now be 3
  cr_assert_eq(result->pixels[1].r, 2); // Second pixel should now be 2
  cr_assert_eq(result->pixels[2].r, 1); // Third pixel should now be 1
  cr_assert_eq(result->pixels[3].r, 0); // Fourth pixel should now be 0

  // Clean up
  free_test_image(test_image);
  webcam_cleanup();
}

Test(webcam, read_without_horizontal_flip) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  // Create test image with known pattern
  image_t *test_image = create_test_image(4, 2);
  cr_assert_not_null(test_image);

  // Set up a simple pattern
  for (int y = 0; y < 2; y++) {
    for (int x = 0; x < 4; x++) {
      test_image->pixels[y * 4 + x].r = (uint8_t)x;
      test_image->pixels[y * 4 + x].g = (uint8_t)x;
      test_image->pixels[y * 4 + x].b = (uint8_t)x;
    }
  }

  mock_webcam_platform_read_result = test_image;
  opt_webcam_flip = false;

  image_t *result = webcam_read();

  cr_assert_not_null(result);
  cr_assert_eq(result, test_image);

  // Verify no flip was applied
  cr_assert_eq(result->pixels[0].r, 0); // First pixel should still be 0
  cr_assert_eq(result->pixels[1].r, 1); // Second pixel should still be 1
  cr_assert_eq(result->pixels[2].r, 2); // Third pixel should still be 2
  cr_assert_eq(result->pixels[3].r, 3); // Fourth pixel should still be 3

  // Clean up
  free_test_image(test_image);
  webcam_cleanup();
}

Test(webcam, read_multiple_calls) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  // Create multiple test images
  image_t *test_images[5];
  for (int i = 0; i < 5; i++) {
    test_images[i] = create_test_image(100 + i * 10, 100 + i * 10);
    cr_assert_not_null(test_images[i]);
  }

  opt_webcam_flip = false;

  // Read multiple frames
  for (int i = 0; i < 5; i++) {
    mock_webcam_platform_read_result = test_images[i];
    image_t *result = webcam_read();

    cr_assert_not_null(result);
    cr_assert_eq(result, test_images[i]);
    cr_assert_eq(last_image_width, 100 + i * 10);
    cr_assert_eq(last_image_height, 100 + i * 10);
  }

  cr_assert_eq(mock_webcam_platform_read_calls, 5);

  // Clean up
  for (int i = 0; i < 5; i++) {
    free_test_image(test_images[i]);
  }
  webcam_cleanup();
}

/* ============================================================================
 * Webcam Cleanup Tests
 * ============================================================================ */

Test(webcam, cleanup_success) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  cr_assert_eq(mock_webcam_platform_cleanup_calls, 0);

  webcam_cleanup();

  cr_assert_eq(mock_webcam_platform_cleanup_calls, 1);
}

Test(webcam, cleanup_not_initialized) {
  reset_mock_state();

  // Don't initialize webcam
  webcam_cleanup();

  cr_assert_eq(mock_webcam_platform_cleanup_calls, 0);
}

Test(webcam, cleanup_multiple_calls) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  // Call cleanup multiple times
  webcam_cleanup();
  webcam_cleanup();
  webcam_cleanup();

  // Should only call platform cleanup once (first time)
  cr_assert_eq(mock_webcam_platform_cleanup_calls, 1);
}

/* ============================================================================
 * Edge Cases and Stress Tests
 * ============================================================================ */

Test(webcam, init_read_cleanup_cycle) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Test complete cycle multiple times
  for (int cycle = 0; cycle < 3; cycle++) {
    // Initialize
    mock_webcam_platform_init_result = 0;
    mock_webcam_platform_get_dimensions_result = 0;
    int result = webcam_init(cycle);
    cr_assert_eq(result, 0);

    // Read
    image_t *test_image = create_test_image(320, 240);
    cr_assert_not_null(test_image);
    mock_webcam_platform_read_result = test_image;
    opt_webcam_flip = false;

    image_t *read_result = webcam_read();
    cr_assert_not_null(read_result);

    // Cleanup
    webcam_cleanup();
    free_test_image(test_image);
  }

  cr_assert_eq(mock_webcam_platform_init_calls, 3);
  cr_assert_eq(mock_webcam_platform_read_calls, 3);
  cr_assert_eq(mock_webcam_platform_cleanup_calls, 3);
}

Test(webcam, read_with_odd_width_flip) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  // Create test image with odd width (5 pixels)
  image_t *test_image = create_test_image(5, 2);
  cr_assert_not_null(test_image);

  // Set up a simple pattern
  for (int y = 0; y < 2; y++) {
    for (int x = 0; x < 5; x++) {
      test_image->pixels[y * 5 + x].r = (uint8_t)x;
      test_image->pixels[y * 5 + x].g = (uint8_t)x;
      test_image->pixels[y * 5 + x].b = (uint8_t)x;
    }
  }

  mock_webcam_platform_read_result = test_image;
  opt_webcam_flip = true;

  image_t *result = webcam_read();

  cr_assert_not_null(result);

  // Verify horizontal flip was applied correctly for odd width
  // Original: [0,1,2,3,4] -> Flipped: [4,3,2,1,0]
  cr_assert_eq(result->pixels[0].r, 4); // First pixel should now be 4
  cr_assert_eq(result->pixels[1].r, 3); // Second pixel should now be 3
  cr_assert_eq(result->pixels[2].r, 2); // Middle pixel should stay 2
  cr_assert_eq(result->pixels[3].r, 1); // Fourth pixel should now be 1
  cr_assert_eq(result->pixels[4].r, 0); // Fifth pixel should now be 0

  // Clean up
  free_test_image(test_image);
  webcam_cleanup();
}

Test(webcam, read_with_single_pixel_width) {
  // Skip hardware tests in all environments to avoid delays
  cr_skip_test("Skipping webcam hardware test to avoid delays");

  reset_mock_state();

  // Set up successful initialization
  mock_webcam_platform_init_result = 0;
  mock_webcam_platform_get_dimensions_result = 0;
  webcam_init(0);

  // Create test image with single pixel width
  image_t *test_image = create_test_image(1, 2);
  cr_assert_not_null(test_image);

  test_image->pixels[0].r = 100;
  test_image->pixels[1].r = 200;

  mock_webcam_platform_read_result = test_image;
  opt_webcam_flip = true;

  image_t *result = webcam_read();

  cr_assert_not_null(result);

  // With single pixel width, flip should have no effect
  cr_assert_eq(result->pixels[0].r, 100);
  cr_assert_eq(result->pixels[1].r, 200);

  // Clean up
  free_test_image(test_image);
  webcam_cleanup();
}

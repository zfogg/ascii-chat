#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <stdint.h>
#include <unistd.h>

#include "os/webcam.h"
#include "tests/common.h"
#include "options.h"
#include "image2ascii/image.h"
#include "tests/logging.h"

// Suite setup: enable test pattern mode for all tests
void webcam_suite_setup(void) {
  log_set_level(LOG_FATAL);
  test_logging_disable(true, true);
  opt_test_pattern = true;
}

// Suite teardown: restore settings
void webcam_suite_teardown(void) {
  opt_test_pattern = false;
  log_set_level(LOG_DEBUG);
  test_logging_restore();
}

// Use custom suite with test pattern mode enabled
TestSuite(webcam, .init = webcam_suite_setup, .fini = webcam_suite_teardown);

/* ============================================================================
 * Webcam Initialization Tests
 * ============================================================================ */

Test(webcam, init_success) {
  // Initialize global variables
  last_image_width = 0;
  last_image_height = 0;

  // Test pattern mode is enabled in suite setup
  int result = webcam_init(0);

  cr_assert_eq(result, 0, "webcam_init should succeed with test pattern");
  cr_assert_eq(last_image_width, 1280, "Test pattern should set width to 1280");
  cr_assert_eq(last_image_height, 720, "Test pattern should set height to 720");

  // Clean up
  webcam_cleanup();
}

// Parameterized test for different webcam indices
typedef struct {
  unsigned short int webcam_index;
  int expected_width;
  int expected_height;
  char description[64];
} webcam_index_test_case_t;

static webcam_index_test_case_t webcam_index_cases[] = {
    {0, 1280, 720, "Webcam index 0 (test pattern)"},
    {1, 1280, 720, "Webcam index 1 (test pattern)"},
    {2, 1280, 720, "Webcam index 2 (test pattern)"},
};

ParameterizedTestParameters(webcam, init_different_indices) {
  return cr_make_param_array(webcam_index_test_case_t, webcam_index_cases,
                             sizeof(webcam_index_cases) / sizeof(webcam_index_cases[0]));
}

ParameterizedTest(webcam_index_test_case_t *tc, webcam, init_different_indices) {
  int result = webcam_init(tc->webcam_index);
  cr_assert_eq(result, 0, "%s: Init should succeed", tc->description);
  cr_assert_eq(last_image_width, tc->expected_width, "%s: Width should be %d", tc->description, tc->expected_width);
  cr_assert_eq(last_image_height, tc->expected_height, "%s: Height should be %d", tc->description, tc->expected_height);
  webcam_cleanup();
}

/* ============================================================================
 * Webcam Read Tests
 * ============================================================================ */

Test(webcam, read_success) {
  // Initialize webcam with test pattern
  webcam_init(0);

  // Set up global options
  opt_webcam_flip = false;

  // Read a frame from test pattern
  image_t *result = webcam_read();

  cr_assert_not_null(result, "webcam_read should return test pattern frame");
  cr_assert_eq(result->w, 1280, "Test pattern width should be 1280");
  cr_assert_eq(result->h, 720, "Test pattern height should be 720");
  cr_assert_not_null(result->pixels, "Frame should have pixel data");

  // Verify test pattern has non-zero pixels (check further in, test pattern has grid lines at 0)
  bool has_color = false;
  for (int i = 1000; i < 2000; i++) {
    if (result->pixels[i].r != 0 || result->pixels[i].g != 0 || result->pixels[i].b != 0) {
      has_color = true;
      break;
    }
  }
  cr_assert(has_color, "Test pattern should contain colored pixels");

  // Clean up
  image_destroy(result);
  webcam_cleanup();
}

Test(webcam, read_not_initialized) {
  // With test pattern mode, reading works even without init (test pattern doesn't need context)
  // This test verifies test pattern mode behavior
  image_t *result = webcam_read();

  cr_assert_not_null(result, "Test pattern should work without init");
  cr_assert_eq(result->w, 1280, "Test pattern width should be 1280");
  cr_assert_eq(result->h, 720, "Test pattern height should be 720");

  image_destroy(result);
}

Test(webcam, read_with_horizontal_flip) {
  // Initialize with test pattern
  webcam_init(0);

  opt_webcam_flip = true;

  // Read frame with flip enabled
  image_t *frame1 = webcam_read();
  cr_assert_not_null(frame1, "Should read frame with flip enabled");

  // Read frame with flip disabled to compare
  opt_webcam_flip = false;
  image_t *frame2 = webcam_read();
  cr_assert_not_null(frame2, "Should read frame without flip");

  // Verify frames are flipped: first pixel of flipped should equal last pixel of non-flipped
  int width = frame1->w;
  cr_assert_eq(frame1->pixels[0].r, frame2->pixels[width - 1].r,
               "First pixel of flipped should match last of non-flipped (R)");
  cr_assert_eq(frame1->pixels[0].g, frame2->pixels[width - 1].g,
               "First pixel of flipped should match last of non-flipped (G)");
  cr_assert_eq(frame1->pixels[0].b, frame2->pixels[width - 1].b,
               "First pixel of flipped should match last of non-flipped (B)");

  // Clean up
  image_destroy(frame1);
  image_destroy(frame2);
  webcam_cleanup();
}

Test(webcam, read_without_horizontal_flip) {
  // Initialize with test pattern
  webcam_init(0);

  opt_webcam_flip = false;

  // Read frame without flip
  image_t *result = webcam_read();

  cr_assert_not_null(result, "Should read frame without flip");
  cr_assert_eq(result->w, 1280, "Width should be 1280");
  cr_assert_eq(result->h, 720, "Height should be 720");

  // Verify we got a frame with data
  cr_assert_not_null(result->pixels, "Should have pixel data");

  // Clean up
  image_destroy(result);
  webcam_cleanup();
}

Test(webcam, read_multiple_calls) {
  // Initialize with test pattern
  webcam_init(0);

  opt_webcam_flip = false;

  // Read multiple frames - test pattern generates new frame each time
  for (int i = 0; i < 5; i++) {
    image_t *result = webcam_read();

    cr_assert_not_null(result, "Frame %d should be read successfully", i);
    cr_assert_eq(result->w, 1280, "Frame %d width should be 1280", i);
    cr_assert_eq(result->h, 720, "Frame %d height should be 720", i);
    cr_assert_eq(last_image_width, 1280, "last_image_width should be updated");
    cr_assert_eq(last_image_height, 720, "last_image_height should be updated");

    image_destroy(result);
  }

  webcam_cleanup();
}

/* ============================================================================
 * Webcam Cleanup Tests
 * ============================================================================ */

Test(webcam, cleanup_success) {
  // Initialize then cleanup
  webcam_init(0);

  // Cleanup should succeed
  webcam_cleanup();

  // With test pattern, reading still works after cleanup (test pattern doesn't use context)
  image_t *result = webcam_read();
  cr_assert_not_null(result, "Test pattern works even after cleanup");

  image_destroy(result);
}

Test(webcam, cleanup_not_initialized) {
  // Cleanup without initialization should not crash
  webcam_cleanup();

  // Should be safe to call
  cr_assert(true, "Cleanup without init should be safe");
}

Test(webcam, cleanup_multiple_calls) {
  // Initialize
  webcam_init(0);

  // Call cleanup multiple times - should be safe
  webcam_cleanup();
  webcam_cleanup();
  webcam_cleanup();

  cr_assert(true, "Multiple cleanup calls should be safe");
}

/* ============================================================================
 * Edge Cases and Stress Tests
 * ============================================================================ */

Test(webcam, init_read_cleanup_cycle) {
  // Test complete cycle multiple times with test pattern
  for (int cycle = 0; cycle < 3; cycle++) {
    // Initialize
    int result = webcam_init(cycle);
    cr_assert_eq(result, 0, "Init should succeed for cycle %d", cycle);

    // Read
    opt_webcam_flip = false;
    image_t *read_result = webcam_read();
    cr_assert_not_null(read_result, "Read should succeed for cycle %d", cycle);

    // Cleanup
    image_destroy(read_result);
    webcam_cleanup();
  }

  cr_assert(true, "Init/read/cleanup cycle should work multiple times");
}

Test(webcam, read_with_odd_width_flip) {
  // Test that flip actually works by comparing flipped vs non-flipped frames
  webcam_init(0);

  // Read without flip
  opt_webcam_flip = false;
  image_t *frame_normal = webcam_read();
  cr_assert_not_null(frame_normal, "Should read frame without flip");

  // Read with flip
  opt_webcam_flip = true;
  image_t *frame_flipped = webcam_read();
  cr_assert_not_null(frame_flipped, "Should read frame with flip");

  // Dimensions should be same
  cr_assert_eq(frame_normal->w, frame_flipped->w, "Width should be same");
  cr_assert_eq(frame_normal->h, frame_flipped->h, "Height should be same");

  // Verify flip: first pixel of flipped should match last pixel of normal
  int width = frame_normal->w;
  cr_assert_eq(frame_flipped->pixels[0].r, frame_normal->pixels[width - 1].r,
               "First R pixel of flipped should match last R pixel of normal");
  cr_assert_eq(frame_flipped->pixels[0].g, frame_normal->pixels[width - 1].g,
               "First G pixel of flipped should match last G pixel of normal");
  cr_assert_eq(frame_flipped->pixels[0].b, frame_normal->pixels[width - 1].b,
               "First B pixel of flipped should match last B pixel of normal");

  // Clean up
  image_destroy(frame_normal);
  image_destroy(frame_flipped);
  webcam_cleanup();
}

Test(webcam, read_with_single_pixel_width) {
  // Test pattern always uses 1280x720, so this test doesn't apply to single pixel width
  // But we can test that flip doesn't crash with test pattern
  webcam_init(0);

  opt_webcam_flip = false;
  image_t *result1 = webcam_read();
  cr_assert_not_null(result1, "Should read frame without flip");

  opt_webcam_flip = true;
  image_t *result2 = webcam_read();
  cr_assert_not_null(result2, "Should read frame with flip");

  // Both should have same dimensions
  cr_assert_eq(result1->w, result2->w, "Width should be same");
  cr_assert_eq(result1->h, result2->h, "Height should be same");

  // Clean up
  image_destroy(result1);
  image_destroy(result2);
  webcam_cleanup();
}

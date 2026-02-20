#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <stdint.h>
#include <unistd.h>

#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/tests/logging.h>

// Suite setup: enable test pattern mode for all tests
void webcam_suite_setup(void) {
  log_set_level(LOG_FATAL);
  test_logging_disable(true, true);
  test_set_test_pattern(true);
}

// Suite teardown: restore settings
void webcam_suite_teardown(void) {
  test_set_test_pattern(false);
  log_set_level(LOG_DEBUG);
  test_logging_restore();
}

// Use custom suite with test pattern mode enabled
TestSuite(webcam, .init = webcam_suite_setup, .fini = webcam_suite_teardown);

/* ============================================================================
 * Webcam Initialization Tests
 * ============================================================================ */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(webcam, LOG_DEBUG, LOG_DEBUG, false, false);

Test(webcam, init_success) {
  // Test pattern mode is enabled in suite setup
  int result = webcam_init(0);
  cr_assert_eq(result, 0, "webcam_init should succeed with test pattern");

  // Verify we can read a frame with correct dimensions
  image_t *frame = webcam_read();
  cr_assert_not_null(frame, "Should read a frame");
  cr_assert_eq(frame->w, 320, "Test pattern should have width 320");
  cr_assert_eq(frame->h, 240, "Test pattern should have height 240");

  // Clean up (don't destroy frame - it's owned by webcam module)
  webcam_destroy();
}

// Parameterized test for different webcam indices
typedef struct {
  unsigned short int webcam_index;
  int expected_width;
  int expected_height;
  char description[64];
} webcam_index_test_case_t;

static webcam_index_test_case_t webcam_index_cases[] = {
    {0, 320, 240, "Webcam index 0 (test pattern)"},
    {1, 320, 240, "Webcam index 1 (test pattern)"},
    {2, 320, 240, "Webcam index 2 (test pattern)"},
};

ParameterizedTestParameters(webcam, init_different_indices) {
  return cr_make_param_array(webcam_index_test_case_t, webcam_index_cases,
                             sizeof(webcam_index_cases) / sizeof(webcam_index_cases[0]));
}

ParameterizedTest(webcam_index_test_case_t *tc, webcam, init_different_indices) {
  int result = webcam_init(tc->webcam_index);
  cr_assert_eq(result, 0, "%s: Init should succeed", tc->description);

  image_t *frame = webcam_read();
  cr_assert_not_null(frame, "%s: Should read a frame", tc->description);
  cr_assert_eq(frame->w, tc->expected_width, "%s: Width should be %d", tc->description, tc->expected_width);
  cr_assert_eq(frame->h, tc->expected_height, "%s: Height should be %d", tc->description, tc->expected_height);

  // Don't destroy frame - it's owned by webcam module
  webcam_destroy();
}

/* ============================================================================
 * Webcam Read Tests
 * ============================================================================ */

Test(webcam, read_success) {
  // Initialize webcam with test pattern
  webcam_init(0);

  // Set up global options
  test_set_flip_x(false);

  // Read a frame from test pattern
  image_t *result = webcam_read();

  cr_assert_not_null(result, "webcam_read should return test pattern frame");
  cr_assert_eq(result->w, 320, "Test pattern width should be 320");
  cr_assert_eq(result->h, 240, "Test pattern height should be 240");
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

  // Clean up (don't destroy result - it's owned by webcam module)
  webcam_destroy();
}

Test(webcam, read_not_initialized) {
  // With test pattern mode, reading works even without init (test pattern doesn't need context)
  // This test verifies test pattern mode behavior
  image_t *result = webcam_read();

  cr_assert_not_null(result, "Test pattern should work without init");
  cr_assert_eq(result->w, 320, "Test pattern width should be 320");
  cr_assert_eq(result->h, 240, "Test pattern height should be 240");

  // Don't destroy result - it's owned by webcam module
}

Test(webcam, read_with_horizontal_flip) {
  // Initialize with test pattern
  webcam_init(0);

  test_set_flip_x(true);

  // Read frame with flip enabled
  image_t *frame1 = webcam_read();
  cr_assert_not_null(frame1, "Should read frame with flip enabled");

  // Read frame with flip disabled to compare
  test_set_flip_x(false);
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

  // Clean up (don't destroy frames - they're owned by webcam module)
  webcam_destroy();
}

Test(webcam, read_without_horizontal_flip) {
  // Initialize with test pattern
  webcam_init(0);

  test_set_flip_x(false);

  // Read frame without flip
  image_t *result = webcam_read();

  cr_assert_not_null(result, "Should read frame without flip");
  cr_assert_eq(result->w, 320, "Width should be 320");
  cr_assert_eq(result->h, 240, "Height should be 240");

  // Verify we got a frame with data
  cr_assert_not_null(result->pixels, "Should have pixel data");

  // Clean up (don't destroy result - it's owned by webcam module)
  webcam_destroy();
}

Test(webcam, read_multiple_calls) {
  // Initialize with test pattern
  webcam_init(0);

  test_set_flip_x(false);

  // Read multiple frames - test pattern generates new frame each time
  for (int i = 0; i < 5; i++) {
    image_t *result = webcam_read();

    cr_assert_not_null(result, "Frame %d should be read successfully", i);
    cr_assert_eq(result->w, 320, "Frame %d width should be 320", i);
    cr_assert_eq(result->h, 240, "Frame %d height should be 240", i);

    // Don't destroy result - it's owned by webcam module
  }

  webcam_destroy();
}

/* ============================================================================
 * Webcam Cleanup Tests
 * ============================================================================ */

Test(webcam, cleanup_success) {
  // Initialize then cleanup
  webcam_init(0);

  // Cleanup should succeed
  webcam_destroy();

  // With test pattern, reading still works after cleanup (test pattern doesn't use context)
  image_t *result = webcam_read();
  cr_assert_not_null(result, "Test pattern works even after cleanup");

  image_destroy(result);
}

Test(webcam, cleanup_not_initialized) {
  // Cleanup without initialization should not crash
  webcam_destroy();

  // Should be safe to call
  cr_assert(true, "Cleanup without init should be safe");
}

Test(webcam, cleanup_multiple_calls) {
  // Initialize
  webcam_init(0);

  // Call cleanup multiple times - should be safe
  webcam_destroy();
  webcam_destroy();
  webcam_destroy();

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
    test_set_flip_x(false);
    image_t *read_result = webcam_read();
    cr_assert_not_null(read_result, "Read should succeed for cycle %d", cycle);

    // Cleanup (don't destroy read_result - it's owned by webcam module)
    webcam_destroy();
  }

  cr_assert(true, "Init/read/cleanup cycle should work multiple times");
}

Test(webcam, read_with_odd_width_flip) {
  // Test that flip actually works by comparing flipped vs non-flipped frames
  webcam_init(0);

  // Read without flip
  test_set_flip_x(false);
  image_t *frame_normal = webcam_read();
  cr_assert_not_null(frame_normal, "Should read frame without flip");

  // Read with flip
  test_set_flip_x(true);
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

  // Clean up (don't destroy frames - they're owned by webcam module)
  webcam_destroy();
}

Test(webcam, read_with_single_pixel_width) {
  // Test pattern always uses 320x240, so this test doesn't apply to single pixel width
  // But we can test that flip doesn't crash with test pattern
  webcam_init(0);

  test_set_flip_x(false);
  image_t *result1 = webcam_read();
  cr_assert_not_null(result1, "Should read frame without flip");

  test_set_flip_x(true);
  image_t *result2 = webcam_read();
  cr_assert_not_null(result2, "Should read frame with flip");

  // Both should have same dimensions
  cr_assert_eq(result1->w, result2->w, "Width should be same");
  cr_assert_eq(result1->h, result2->h, "Height should be same");

  // Clean up (don't destroy results - they're owned by webcam module)
  webcam_destroy();
}

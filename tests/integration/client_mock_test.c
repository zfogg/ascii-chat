// Test webcam functionality with test pattern mode
// Uses real webcam API with --test-pattern instead of mocks

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdlib.h>

// Platform-specific includes
#ifndef _WIN32
#include <unistd.h>
#endif

#include "common.h"
#include "os/webcam.h"
#include "options.h"
#include "image2ascii/image.h"
#include "tests/logging.h"

// Suite setup: enable test pattern mode
void client_test_setup(void) {
  log_set_level(LOG_FATAL);
  test_logging_disable(true, true);
  opt_test_pattern = true;
}

// Suite teardown: restore settings
void client_test_teardown(void) {
  opt_test_pattern = false;
  log_set_level(LOG_DEBUG);
  test_logging_restore();
}

TestSuite(client_test_pattern, .init = client_test_setup, .fini = client_test_teardown);

Test(client_test_pattern, test_video_capture_with_test_pattern) {
  // Initialize with test pattern
  int result = webcam_init(0);
  cr_assert_eq(result, 0, "Test pattern webcam should initialize");

  // Read a frame
  image_t *frame = webcam_read();
  cr_assert_not_null(frame, "Test pattern should return a frame");
  cr_assert_eq(frame->w, 1280, "Test pattern width should be 1280");
  cr_assert_eq(frame->h, 720, "Test pattern height should be 720");

  // Cleanup
  image_destroy(frame);
  webcam_cleanup();
}

Test(client_test_pattern, test_client_with_test_pattern_video) {
  // Initialize with test pattern
  int result = webcam_init(0);
  cr_assert_eq(result, 0, "Test pattern init should succeed");

  // Verify dimensions
  cr_assert_eq(last_image_width, 1280, "Width should be 1280");
  cr_assert_eq(last_image_height, 720, "Height should be 720");

  // Simulate multiple frame captures with test pattern
  for (int i = 0; i < 10; i++) {
    image_t *frame = webcam_read();
    cr_assert_not_null(frame, "Frame %d should be captured", i);
    cr_assert_eq(frame->w, 1280, "Width should be 1280");
    cr_assert_eq(frame->h, 720, "Height should be 720");
    cr_assert_not_null(frame->pixels, "Should have pixel data");

    // Verify test pattern data exists (check further in to avoid grid lines)
    bool has_data = false;
    for (int j = 1000; j < 2000; j++) {
      if (frame->pixels[j].r != 0 || frame->pixels[j].g != 0 || frame->pixels[j].b != 0) {
        has_data = true;
        break;
      }
    }
    cr_assert(has_data, "Frame should contain test pattern data");

    image_destroy(frame);
  }

  webcam_cleanup();
}

Test(client_test_pattern, test_multiple_init_cleanup_cycles) {
  // Test that webcam can be initialized and cleaned up multiple times
  for (int cycle = 0; cycle < 3; cycle++) {
    int result = webcam_init(cycle);
    cr_assert_eq(result, 0, "Init should succeed for cycle %d", cycle);

    image_t *frame = webcam_read();
    cr_assert_not_null(frame, "Should get frame in cycle %d", cycle);

    image_destroy(frame);
    webcam_cleanup();
  }
}
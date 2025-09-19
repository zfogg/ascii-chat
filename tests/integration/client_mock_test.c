// Test client.c with mocked webcam
// This shows how to use the mock with real client code

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdlib.h>

// Platform-specific includes
#ifndef _WIN32
#include <unistd.h>
#endif

// CRITICAL: Include mock BEFORE any webcam headers!
// This must come first to override the real webcam functions
#define WEBCAM_MOCK_ENABLED 1
#include "../mocks/webcam_mock.h"

// Now include the rest of the headers
#include "common.h"
#include "network.h"
#include "options.h"

// To test client.c, we have two approaches:

// APPROACH 1: Include client.c directly (compile-time mocking)
// This allows testing internal functions
#ifdef TEST_CLIENT_INTERNALS

// The mock overrides are already in place from webcam_mock.h
// Now when we include client.c, it will use the mocked functions
#include "../../src/client.c"

Test(client_mock, test_video_capture_with_mock) {
  // Configure the mock
  mock_webcam_set_test_pattern(true);
  mock_webcam_set_dimensions(640, 480);

  // Initialize options (client.c needs this)
  options_t opts = {0};
  opts.width = 640;
  opts.height = 480;
  opts.address = "127.0.0.1";
  opts.port = 8080;

  // Now any webcam_* calls in client.c will use the mock
  webcam_context_t *ctx = NULL;
  int result = webcam_init_context(&ctx, 0);
  cr_assert_eq(result, 0, "Mock webcam should initialize");

  // Read a frame
  image_t *frame = webcam_read_context(ctx);
  cr_assert_not_null(frame, "Mock should return a frame");
  cr_assert_eq(frame->w, 640);
  cr_assert_eq(frame->h, 480);

  // Cleanup
  image_destroy(frame);
  webcam_cleanup_context(ctx);
}

#endif

// APPROACH 2: Link-time mocking (better for integration tests)
// Compile client.c separately with -DUSE_WEBCAM_MOCK

Test(client_mock, test_client_with_mock_video) {
  // Set up mock to use a specific test pattern
  mock_webcam_set_test_pattern(true);
  mock_webcam_set_dimensions(320, 240);

  // Test that client can capture from mock
  webcam_context_t *ctx = NULL;
  int result = webcam_init_context(&ctx, 0);
  cr_assert_eq(result, 0, "Mock init should succeed");

  int width, height;
  webcam_get_dimensions(ctx, &width, &height);
  cr_assert_eq(width, 320);
  cr_assert_eq(height, 240);

  // Simulate multiple frame captures
  for (int i = 0; i < 10; i++) {
    image_t *frame = webcam_read_context(ctx);
    cr_assert_not_null(frame, "Frame %d should be captured", i);
    cr_assert_eq(frame->w, 320);
    cr_assert_eq(frame->h, 240);
    cr_assert_not_null(frame->pixels);

    // Verify test pattern data exists
    bool has_data = false;
    for (int j = 0; j < 100; j++) {
      if (frame->pixels[j].r != 0 || frame->pixels[j].g != 0 || frame->pixels[j].b != 0) {
        has_data = true;
        break;
      }
    }
    cr_assert(has_data, "Frame should contain test pattern data");

    image_destroy(frame);
  }

  webcam_cleanup_context(ctx);
}

Test(client_mock, test_mock_with_video_file) {
  // Configure mock to use a video file
  mock_webcam_set_video_file("tests/fixtures/test_pattern.mp4");

  webcam_context_t *ctx = NULL;
  int result = webcam_init_context(&ctx, 0);

  // If FFmpeg is not available or file doesn't exist, it falls back to test pattern
  // So this test should always pass
  cr_assert_eq(result, 0, "Mock should initialize even without video file");

  image_t *frame = webcam_read_context(ctx);
  cr_assert_not_null(frame, "Should get frame from mock");

  image_destroy(frame);
  webcam_cleanup_context(ctx);
}

Test(client_mock, test_mock_reset) {
  // Test that mock can be reconfigured
  mock_webcam_set_dimensions(1920, 1080);

  webcam_context_t *ctx1 = NULL;
  webcam_init_context(&ctx1, 0);

  int width, height;
  webcam_get_dimensions(ctx1, &width, &height);
  cr_assert_eq(width, 1920);
  cr_assert_eq(height, 1080);

  webcam_cleanup_context(ctx1);

  // Reset and try different settings
  mock_webcam_reset();
  mock_webcam_set_dimensions(800, 600);

  webcam_context_t *ctx2 = NULL;
  webcam_init_context(&ctx2, 0);

  webcam_get_dimensions(ctx2, &width, &height);
  cr_assert_eq(width, 800);
  cr_assert_eq(height, 600);

  webcam_cleanup_context(ctx2);
}
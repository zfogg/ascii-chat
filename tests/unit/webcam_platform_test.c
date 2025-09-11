#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "webcam_platform.h"
#include "common.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(webcam_platform);

/* ============================================================================
 * Platform Detection Tests
 * ============================================================================ */

Test(webcam_platform, get_platform) {
  webcam_platform_type_t platform = webcam_get_platform();

  // Platform should be one of the valid types
  cr_assert(platform == WEBCAM_PLATFORM_V4L2 || platform == WEBCAM_PLATFORM_AVFOUNDATION ||
            platform == WEBCAM_PLATFORM_UNKNOWN);

  // On supported platforms, should not be UNKNOWN
#ifdef __linux__
  cr_assert_eq(platform, WEBCAM_PLATFORM_V4L2);
#elif defined(__APPLE__)
  cr_assert_eq(platform, WEBCAM_PLATFORM_AVFOUNDATION);
#else
  cr_assert_eq(platform, WEBCAM_PLATFORM_UNKNOWN);
#endif
}

Test(webcam_platform, platform_name_v4l2) {
  const char *name = webcam_platform_name(WEBCAM_PLATFORM_V4L2);

  cr_assert_not_null(name);
  cr_assert_str_eq(name, "V4L2 (Linux)");
}

Test(webcam_platform, platform_name_avfoundation) {
  const char *name = webcam_platform_name(WEBCAM_PLATFORM_AVFOUNDATION);

  cr_assert_not_null(name);
  cr_assert_str_eq(name, "AVFoundation (macOS)");
}

Test(webcam_platform, platform_name_unknown) {
  const char *name = webcam_platform_name(WEBCAM_PLATFORM_UNKNOWN);

  cr_assert_not_null(name);
  cr_assert_str_eq(name, "Unknown");
}

Test(webcam_platform, platform_name_invalid) {
  // Test with invalid enum value
  const char *name = webcam_platform_name((webcam_platform_type_t)999);

  cr_assert_not_null(name);
  cr_assert_str_eq(name, "Unknown");
}

Test(webcam_platform, platform_name_all_values) {
  // Test all valid platform types
  webcam_platform_type_t platforms[] = {WEBCAM_PLATFORM_UNKNOWN, WEBCAM_PLATFORM_V4L2, WEBCAM_PLATFORM_AVFOUNDATION};

  const char *expected_names[] = {"Unknown", "V4L2 (Linux)", "AVFoundation (macOS)"};

  int num_platforms = sizeof(platforms) / sizeof(platforms[0]);

  for (int i = 0; i < num_platforms; i++) {
    const char *name = webcam_platform_name(platforms[i]);
    cr_assert_not_null(name);
    cr_assert_str_eq(name, expected_names[i]);
  }
}

/* ============================================================================
 * Platform-Specific Function Tests (Fallback Implementations)
 * ============================================================================ */

#if !defined(__linux__) && !defined(__APPLE__)
// These tests only run on unsupported platforms where fallback implementations exist

Test(webcam_platform, platform_init_fallback) {
  webcam_context_t *ctx = NULL;
  int result = webcam_platform_init(&ctx, 0);

  cr_assert_eq(result, -1);
  cr_assert_null(ctx);
}

Test(webcam_platform, platform_init_fallback_null_ctx) {
  int result = webcam_platform_init(NULL, 0);

  cr_assert_eq(result, -1);
}

Test(webcam_platform, platform_init_fallback_different_indices) {
  webcam_context_t *ctx = NULL;

  // Test different device indices
  unsigned short int indices[] = {0, 1, 2, 5, 10, 255};
  int num_indices = sizeof(indices) / sizeof(indices[0]);

  for (int i = 0; i < num_indices; i++) {
    ctx = NULL;
    int result = webcam_platform_init(&ctx, indices[i]);

    cr_assert_eq(result, -1);
    cr_assert_null(ctx);
  }
}

Test(webcam_platform, platform_cleanup_fallback) {
  // Should not crash when called with any context
  webcam_context_t *ctx = (webcam_context_t *)0x12345678;
  webcam_platform_cleanup(ctx);

  // Also test with NULL
  webcam_platform_cleanup(NULL);

  // Test should complete without crashing
  cr_assert(1);
}

Test(webcam_platform, platform_read_fallback) {
  webcam_context_t *ctx = (webcam_context_t *)0x12345678;
  image_t *result = webcam_platform_read(ctx);

  cr_assert_null(result);

  // Also test with NULL
  result = webcam_platform_read(NULL);
  cr_assert_null(result);
}

Test(webcam_platform, platform_get_dimensions_fallback) {
  webcam_context_t *ctx = (webcam_context_t *)0x12345678;
  int width, height;

  int result = webcam_platform_get_dimensions(ctx, &width, &height);

  cr_assert_eq(result, -1);

  // Also test with NULL context
  result = webcam_platform_get_dimensions(NULL, &width, &height);
  cr_assert_eq(result, -1);

  // Test with NULL width/height pointers
  result = webcam_platform_get_dimensions(ctx, NULL, &height);
  cr_assert_eq(result, -1);

  result = webcam_platform_get_dimensions(ctx, &width, NULL);
  cr_assert_eq(result, -1);

  result = webcam_platform_get_dimensions(ctx, NULL, NULL);
  cr_assert_eq(result, -1);
}

Test(webcam_platform, platform_functions_consistency) {
  // Test that all platform functions behave consistently on unsupported platforms
  webcam_context_t *ctx = NULL;

  // Init should fail
  int init_result = webcam_platform_init(&ctx, 0);
  cr_assert_eq(init_result, -1);
  cr_assert_null(ctx);

  // Read should return NULL
  image_t *read_result = webcam_platform_read(ctx);
  cr_assert_null(read_result);

  // Get dimensions should fail
  int width, height;
  int dim_result = webcam_platform_get_dimensions(ctx, &width, &height);
  cr_assert_eq(dim_result, -1);

  // Cleanup should not crash
  webcam_platform_cleanup(ctx);
}

#endif // !defined(__linux__) && !defined(__APPLE__)

/* ============================================================================
 * Edge Cases and Stress Tests
 * ============================================================================ */

Test(webcam_platform, platform_name_stress) {
  // Test platform_name with many calls
  for (int i = 0; i < 1000; i++) {
    webcam_platform_type_t platform = (webcam_platform_type_t)(i % 4);
    const char *name = webcam_platform_name(platform);
    cr_assert_not_null(name);
  }
}

Test(webcam_platform, get_platform_consistency) {
  // Test that get_platform returns consistent results
  webcam_platform_type_t platform1 = webcam_get_platform();
  webcam_platform_type_t platform2 = webcam_get_platform();

  cr_assert_eq(platform1, platform2);

  // Test multiple calls
  for (int i = 0; i < 100; i++) {
    webcam_platform_type_t platform = webcam_get_platform();
    cr_assert_eq(platform, platform1);
  }
}

Test(webcam_platform, platform_enum_values) {
  // Test that enum values are as expected
  cr_assert_eq(WEBCAM_PLATFORM_UNKNOWN, 0);
  cr_assert_eq(WEBCAM_PLATFORM_V4L2, 1);
  cr_assert_eq(WEBCAM_PLATFORM_AVFOUNDATION, 2);
}

Test(webcam_platform, platform_name_null_safety) {
  // Test that platform_name never returns NULL
  for (int i = -10; i < 10; i++) {
    const char *name = webcam_platform_name((webcam_platform_type_t)i);
    cr_assert_not_null(name);
    cr_assert_gt(strlen(name), 0);
  }
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

Test(webcam_platform, platform_detection_integration) {
  // Test the complete platform detection flow
  webcam_platform_type_t platform = webcam_get_platform();
  const char *name = webcam_platform_name(platform);

  cr_assert_not_null(name);

  // Verify the name matches the platform
  switch (platform) {
  case WEBCAM_PLATFORM_V4L2:
    cr_assert_str_eq(name, "V4L2 (Linux)");
    break;
  case WEBCAM_PLATFORM_AVFOUNDATION:
    cr_assert_str_eq(name, "AVFoundation (macOS)");
    break;
  case WEBCAM_PLATFORM_UNKNOWN:
    cr_assert_str_eq(name, "Unknown");
    break;
  default:
    cr_assert(false, "Invalid platform type: %d", platform);
  }
}

Test(webcam_platform, platform_consistency_across_calls) {
  // Test that platform detection is consistent across multiple calls
  webcam_platform_type_t platforms[10];
  const char *names[10];

  // Get platform info multiple times
  for (int i = 0; i < 10; i++) {
    platforms[i] = webcam_get_platform();
    names[i] = webcam_platform_name(platforms[i]);
  }

  // All should be the same
  for (int i = 1; i < 10; i++) {
    cr_assert_eq(platforms[i], platforms[0]);
    cr_assert_str_eq(names[i], names[0]);
  }
}

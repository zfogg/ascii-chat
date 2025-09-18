#include "../common.h"
#include <time.h>
#include <math.h>
#include "aspect_ratio.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(aspect_ratio);

/* ============================================================================
 * Basic Aspect Ratio Function Tests
 * ============================================================================ */

Test(aspect_ratio, basic_functionality) {
  ssize_t out_width, out_height;

  // Test basic aspect ratio calculation
  aspect_ratio(1920, 1080, 80, 24, false, &out_width, &out_height);

  // Should maintain aspect ratio (16:9)
  // With CHAR_ASPECT = 2.0, the calculation should be:
  // height = 24, so width = 24 * (1920/1080) * 2.0 = 24 * 1.777 * 2.0 = 85.33
  // But it should fit within 80x24, so it will be width-constrained
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(aspect_ratio, stretch_mode) {
  ssize_t out_width, out_height;

  // Test stretch mode - should use full terminal dimensions
  aspect_ratio(1920, 1080, 80, 24, true, &out_width, &out_height);

  cr_assert_eq(out_width, 80);
  cr_assert_eq(out_height, 24);
}

Test(aspect_ratio, square_image) {
  ssize_t out_width, out_height;

  // Test square image (1:1 aspect ratio)
  aspect_ratio(100, 100, 80, 24, false, &out_width, &out_height);

  // Should fit within bounds while maintaining square aspect
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(aspect_ratio, tall_image) {
  ssize_t out_width, out_height;

  // Test tall image (portrait)
  aspect_ratio(100, 200, 80, 24, false, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(aspect_ratio, wide_image) {
  ssize_t out_width, out_height;

  // Test wide image (landscape)
  aspect_ratio(200, 100, 80, 24, false, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(aspect_ratio, null_output_pointers) {
  // Test with NULL output pointers - should not crash
  aspect_ratio(1920, 1080, 80, 24, false, NULL, NULL);
  aspect_ratio(1920, 1080, 80, 24, false, NULL, NULL);
  // If we get here without crashing, the test passes
  cr_assert(true);
}

Test(aspect_ratio, invalid_image_dimensions) {
  ssize_t out_width, out_height;

  // Test with zero dimensions
  aspect_ratio(0, 1080, 80, 24, false, &out_width, &out_height);
  cr_assert_eq(out_width, 1); // MIN_DIMENSION
  cr_assert_eq(out_height, 1);

  aspect_ratio(1920, 0, 80, 24, false, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  // Test with negative dimensions
  aspect_ratio(-1920, 1080, 80, 24, false, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  aspect_ratio(1920, -1080, 80, 24, false, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);
}

Test(aspect_ratio, very_small_image) {
  ssize_t out_width, out_height;

  // Test with very small image
  aspect_ratio(1, 1, 80, 24, false, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(aspect_ratio, very_large_image) {
  ssize_t out_width, out_height;

  // Test with very large image
  aspect_ratio(10000, 10000, 80, 24, false, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

/* ============================================================================
 * aspect_ratio2 Function Tests
 * ============================================================================ */

Test(aspect_ratio2, basic_functionality) {
  ssize_t out_width, out_height;

  // Test basic aspect ratio calculation without terminal character correction
  aspect_ratio2(1920, 1080, 80, 24, &out_width, &out_height);

  // Should maintain aspect ratio and fit within target dimensions
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(aspect_ratio2, null_output_pointers) {
  // Test with NULL output pointers - should not crash
  aspect_ratio2(1920, 1080, 80, 24, NULL, NULL);
  // If we get here without crashing, the test passes
  cr_assert(true);
}

Test(aspect_ratio2, invalid_dimensions) {
  ssize_t out_width, out_height;

  // Test with zero image dimensions
  aspect_ratio2(0, 1080, 80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 1); // MIN_DIMENSION
  cr_assert_eq(out_height, 1);

  aspect_ratio2(1920, 0, 80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  // Test with zero target dimensions
  aspect_ratio2(1920, 1080, 0, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  aspect_ratio2(1920, 1080, 80, 0, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  // Test with negative dimensions
  aspect_ratio2(-1920, 1080, 80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  aspect_ratio2(1920, -1080, 80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  aspect_ratio2(1920, 1080, -80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  aspect_ratio2(1920, 1080, 80, -24, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);
}

Test(aspect_ratio2, square_image) {
  ssize_t out_width, out_height;

  // Test square image
  aspect_ratio2(100, 100, 80, 24, &out_width, &out_height);

  // Should maintain square aspect ratio
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(aspect_ratio2, tall_image) {
  ssize_t out_width, out_height;

  // Test tall image (portrait)
  aspect_ratio2(100, 200, 80, 24, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(aspect_ratio2, wide_image) {
  ssize_t out_width, out_height;

  // Test wide image (landscape)
  aspect_ratio2(200, 100, 80, 24, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

/* ============================================================================
 * calculate_fit_dimensions_pixel Function Tests
 * ============================================================================ */

Test(calculate_fit_dimensions_pixel, basic_functionality) {
  int out_width, out_height;

  // Test basic functionality
  calculate_fit_dimensions_pixel(1920, 1080, 80, 24, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(calculate_fit_dimensions_pixel, null_output_pointers) {
  // Test with NULL output pointers - should not crash
  calculate_fit_dimensions_pixel(1920, 1080, 80, 24, NULL, NULL);
  // If we get here without crashing, the test passes
  cr_assert(true);
}

Test(calculate_fit_dimensions_pixel, invalid_dimensions) {
  int out_width, out_height;

  // Test with zero image dimensions
  calculate_fit_dimensions_pixel(0, 1080, 80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 80);  // Should use max_width
  cr_assert_eq(out_height, 24); // Should use max_height

  calculate_fit_dimensions_pixel(1920, 0, 80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 80);
  cr_assert_eq(out_height, 24);

  // Test with negative image dimensions
  calculate_fit_dimensions_pixel(-1920, 1080, 80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 80);
  cr_assert_eq(out_height, 24);

  calculate_fit_dimensions_pixel(1920, -1080, 80, 24, &out_width, &out_height);
  cr_assert_eq(out_width, 80);
  cr_assert_eq(out_height, 24);
}

Test(calculate_fit_dimensions_pixel, square_image) {
  int out_width, out_height;

  // Test square image
  calculate_fit_dimensions_pixel(100, 100, 80, 24, &out_width, &out_height);

  // Should maintain square aspect ratio
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(calculate_fit_dimensions_pixel, tall_image) {
  int out_width, out_height;

  // Test tall image (portrait)
  calculate_fit_dimensions_pixel(100, 200, 80, 24, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(calculate_fit_dimensions_pixel, wide_image) {
  int out_width, out_height;

  // Test wide image (landscape)
  calculate_fit_dimensions_pixel(200, 100, 80, 24, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

Test(calculate_fit_dimensions_pixel, exact_fit) {
  int out_width, out_height;

  // Test image that exactly fits the target dimensions
  calculate_fit_dimensions_pixel(80, 24, 80, 24, &out_width, &out_height);

  cr_assert_eq(out_width, 80);
  cr_assert_eq(out_height, 24);
}

Test(calculate_fit_dimensions_pixel, very_small_target) {
  int out_width, out_height;

  // Test with very small target dimensions
  calculate_fit_dimensions_pixel(1920, 1080, 1, 1, &out_width, &out_height);

  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);
}

Test(calculate_fit_dimensions_pixel, very_large_image) {
  int out_width, out_height;

  // Test with very large image
  calculate_fit_dimensions_pixel(10000, 10000, 80, 24, &out_width, &out_height);

  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 80);
  cr_assert_leq(out_height, 24);
}

/* ============================================================================
 * Comprehensive Random Value Tests
 * ============================================================================ */

Test(aspect_ratio, random_values_comprehensive) {
  srand((unsigned int)time(NULL));
  ssize_t out_width, out_height;

  // Test 1000 random combinations
  for (int i = 0; i < 1000; i++) {
    ssize_t img_w = (ssize_t)(rand() % 10000) + 1;
    ssize_t img_h = (ssize_t)(rand() % 10000) + 1;
    ssize_t term_w = (ssize_t)(rand() % 1000) + 1;
    ssize_t term_h = (ssize_t)(rand() % 1000) + 1;
    bool stretch = (rand() % 2) == 0;

    aspect_ratio(img_w, img_h, term_w, term_h, stretch, &out_width, &out_height);

    cr_assert_gt(out_width, 0, "Random test %d: img_w=%zd, img_h=%zd, term_w=%zd, term_h=%zd, stretch=%d", i, img_w,
                 img_h, term_w, term_h, stretch);
    cr_assert_gt(out_height, 0, "Random test %d: img_w=%zd, img_h=%zd, term_w=%zd, term_h=%zd, stretch=%d", i, img_w,
                 img_h, term_w, term_h, stretch);

    if (stretch) {
      cr_assert_eq(out_width, term_w);
      cr_assert_eq(out_height, term_h);
    } else {
      cr_assert_leq(out_width, term_w);
      cr_assert_leq(out_height, term_h);
    }
  }
}

Test(aspect_ratio2, random_values_comprehensive) {
  srand((unsigned int)time(NULL));
  ssize_t out_width, out_height;

  // Test 1000 random combinations
  for (int i = 0; i < 1000; i++) {
    ssize_t img_w = (ssize_t)(rand() % 10000) + 1;
    ssize_t img_h = (ssize_t)(rand() % 10000) + 1;
    ssize_t target_w = (ssize_t)(rand() % 1000) + 1;
    ssize_t target_h = (ssize_t)(rand() % 1000) + 1;

    aspect_ratio2(img_w, img_h, target_w, target_h, &out_width, &out_height);

    cr_assert_gt(out_width, 0, "Random test %d: img_w=%zd, img_h=%zd, target_w=%zd, target_h=%zd", i, img_w, img_h,
                 target_w, target_h);
    cr_assert_gt(out_height, 0, "Random test %d: img_w=%zd, img_h=%zd, target_w=%zd, target_h=%zd", i, img_w, img_h,
                 target_w, target_h);
    cr_assert_leq(out_width, target_w);
    cr_assert_leq(out_height, target_h);
  }
}

Test(calculate_fit_dimensions_pixel, random_values_comprehensive) {
  srand((unsigned int)time(NULL));
  int out_width, out_height;

  // Test 1000 random combinations
  for (int i = 0; i < 1000; i++) {
    int img_w = (rand() % 10000) + 1;
    int img_h = (rand() % 10000) + 1;
    int max_w = (rand() % 1000) + 1;
    int max_h = (rand() % 1000) + 1;

    calculate_fit_dimensions_pixel(img_w, img_h, max_w, max_h, &out_width, &out_height);

    cr_assert_gt(out_width, 0, "Random test %d: img_w=%d, img_h=%d, max_w=%d, max_h=%d", i, img_w, img_h, max_w, max_h);
    cr_assert_gt(out_height, 0, "Random test %d: img_w=%d, img_h=%d, max_w=%d, max_h=%d", i, img_w, img_h, max_w,
                 max_h);
    cr_assert_leq(out_width, max_w);
    cr_assert_leq(out_height, max_h);
  }
}

/* ============================================================================
 * Edge Cases and Boundary Tests
 * ============================================================================ */

Test(aspect_ratio, boundary_values) {
  ssize_t out_width, out_height;

  // Test with minimum valid dimensions
  aspect_ratio(1, 1, 1, 1, false, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);

  // Test with very large terminal dimensions
  aspect_ratio(1920, 1080, 1000, 1000, false, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 1000);
  cr_assert_leq(out_height, 1000);

  // Test with extreme aspect ratios
  aspect_ratio(10000, 1, 100, 100, false, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100);
  cr_assert_leq(out_height, 100);

  aspect_ratio(1, 10000, 100, 100, false, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100);
  cr_assert_leq(out_height, 100);
}

Test(aspect_ratio2, boundary_values) {
  ssize_t out_width, out_height;

  // Test with minimum valid dimensions
  aspect_ratio2(1, 1, 1, 1, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);

  // Test with very large target dimensions
  aspect_ratio2(1920, 1080, 1000, 1000, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 1000);
  cr_assert_leq(out_height, 1000);

  // Test with extreme aspect ratios
  aspect_ratio2(10000, 1, 100, 100, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100);
  cr_assert_leq(out_height, 100);

  aspect_ratio2(1, 10000, 100, 100, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100);
  cr_assert_leq(out_height, 100);
}

Test(calculate_fit_dimensions_pixel, boundary_values) {
  int out_width, out_height;

  // Test with minimum valid dimensions
  calculate_fit_dimensions_pixel(1, 1, 1, 1, &out_width, &out_height);
  cr_assert_eq(out_width, 1);
  cr_assert_eq(out_height, 1);

  // Test with very large target dimensions
  calculate_fit_dimensions_pixel(1920, 1080, 1000, 1000, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 1000);
  cr_assert_leq(out_height, 1000);

  // Test with extreme aspect ratios
  calculate_fit_dimensions_pixel(10000, 1, 100, 100, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100);
  cr_assert_leq(out_height, 100);

  calculate_fit_dimensions_pixel(1, 10000, 100, 100, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100);
  cr_assert_leq(out_height, 100);
}

/* ============================================================================
 * Mathematical Precision Tests
 * ============================================================================ */

Test(aspect_ratio, mathematical_precision) {
  ssize_t out_width, out_height;

  // Test common aspect ratios with known results
  // 16:9 aspect ratio
  aspect_ratio(1920, 1080, 80, 24, false, &out_width, &out_height);
  float aspect_ratio_result = (float)out_width / (float)out_height;
  float expected_aspect = 1920.0f / 1080.0f * 2.0f; // CHAR_ASPECT = 2.0
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);

  // 4:3 aspect ratio
  aspect_ratio(1024, 768, 80, 24, false, &out_width, &out_height);
  aspect_ratio_result = (float)out_width / (float)out_height;
  expected_aspect = 1024.0f / 768.0f * 2.0f;
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);

  // 1:1 aspect ratio (square)
  aspect_ratio(512, 512, 80, 24, false, &out_width, &out_height);
  aspect_ratio_result = (float)out_width / (float)out_height;
  expected_aspect = 512.0f / 512.0f * 2.0f;
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);
}

Test(aspect_ratio2, mathematical_precision) {
  ssize_t out_width, out_height;

  // Test common aspect ratios with known results (no CHAR_ASPECT correction)
  // 16:9 aspect ratio
  aspect_ratio2(1920, 1080, 80, 24, &out_width, &out_height);
  float aspect_ratio_result = (float)out_width / (float)out_height;
  float expected_aspect = 1920.0f / 1080.0f;
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);

  // 4:3 aspect ratio
  aspect_ratio2(1024, 768, 80, 24, &out_width, &out_height);
  aspect_ratio_result = (float)out_width / (float)out_height;
  expected_aspect = 1024.0f / 768.0f;
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);

  // 1:1 aspect ratio (square)
  aspect_ratio2(512, 512, 80, 24, &out_width, &out_height);
  aspect_ratio_result = (float)out_width / (float)out_height;
  expected_aspect = 512.0f / 512.0f;
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);
}

Test(calculate_fit_dimensions_pixel, mathematical_precision) {
  int out_width, out_height;

  // Test common aspect ratios with known results
  // 16:9 aspect ratio
  calculate_fit_dimensions_pixel(1920, 1080, 80, 24, &out_width, &out_height);
  float aspect_ratio_result = (float)out_width / (float)out_height;
  float expected_aspect = 1920.0f / 1080.0f;
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);

  // 4:3 aspect ratio
  calculate_fit_dimensions_pixel(1024, 768, 80, 24, &out_width, &out_height);
  aspect_ratio_result = (float)out_width / (float)out_height;
  expected_aspect = 1024.0f / 768.0f;
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);

  // 1:1 aspect ratio (square)
  calculate_fit_dimensions_pixel(512, 512, 80, 24, &out_width, &out_height);
  aspect_ratio_result = (float)out_width / (float)out_height;
  expected_aspect = 512.0f / 512.0f;
  cr_assert_float_eq(aspect_ratio_result, expected_aspect, 0.1f);
}

/* ============================================================================
 * Stress Tests with Large Values
 * ============================================================================ */

Test(aspect_ratio, stress_test_large_values) {
  ssize_t out_width, out_height;

  // Test with very large image dimensions
  aspect_ratio(100000, 100000, 1000, 1000, false, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 1000);
  cr_assert_leq(out_height, 1000);

  // Test with very large terminal dimensions
  aspect_ratio(1920, 1080, 100000, 100000, false, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100000);
  cr_assert_leq(out_height, 100000);
}

Test(aspect_ratio2, stress_test_large_values) {
  ssize_t out_width, out_height;

  // Test with very large image dimensions
  aspect_ratio2(100000, 100000, 1000, 1000, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 1000);
  cr_assert_leq(out_height, 1000);

  // Test with very large target dimensions
  aspect_ratio2(1920, 1080, 100000, 100000, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100000);
  cr_assert_leq(out_height, 100000);
}

Test(calculate_fit_dimensions_pixel, stress_test_large_values) {
  int out_width, out_height;

  // Test with very large image dimensions
  calculate_fit_dimensions_pixel(100000, 100000, 1000, 1000, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 1000);
  cr_assert_leq(out_height, 1000);

  // Test with very large target dimensions
  calculate_fit_dimensions_pixel(1920, 1080, 100000, 100000, &out_width, &out_height);
  cr_assert_gt(out_width, 0);
  cr_assert_gt(out_height, 0);
  cr_assert_leq(out_width, 100000);
  cr_assert_leq(out_height, 100000);
}

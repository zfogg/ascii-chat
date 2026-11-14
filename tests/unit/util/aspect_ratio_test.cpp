#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <time.h>
#include <math.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/util/aspect_ratio.h>

// Use the enhanced macro to create complete test suite with basic quiet logging
/* ============================================================================
 * Basic Aspect Ratio Function Tests
 * ============================================================================ */

TEST_CASE("aspect_ratio/basic_functionality", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test basic aspect ratio calculation
  aspect_ratio(1920, 1080, 80, 24, false, &out_width, &out_height);

  // Should maintain aspect ratio (16:9)
  // With CHAR_ASPECT = 2.0, the calculation should be:
  // height = 24, so width = 24 * (1920/1080) * 2.0 = 24 * 1.777 * 2.0 = 85.33
  // But it should fit within 80x24, so it will be width-constrained
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("aspect_ratio/stretch_mode", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test stretch mode - should use full terminal dimensions
  aspect_ratio(1920, 1080, 80, 24, true, &out_width, &out_height);

  ASCIICHAT_REQUIRE_EQ(out_width, 80);
  ASCIICHAT_REQUIRE_EQ(out_height, 24);
}

TEST_CASE("aspect_ratio/square_image", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test square image (1:1 aspect ratio)
  aspect_ratio(100, 100, 80, 24, false, &out_width, &out_height);

  // Should fit within bounds while maintaining square aspect
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("aspect_ratio/tall_image", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test tall image (portrait)
  aspect_ratio(100, 200, 80, 24, false, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("aspect_ratio/wide_image", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test wide image (landscape)
  aspect_ratio(200, 100, 80, 24, false, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("aspect_ratio/null_output_pointers", "[aspect_ratio]") {
  // Test with NULL output pointers - should not crash
  aspect_ratio(1920, 1080, 80, 24, false, NULL, NULL);
  aspect_ratio(1920, 1080, 80, 24, false, NULL, NULL);
  // If we get here without crashing, the test passes
  ASCIICHAT_REQUIRE(true);
}

// Parameterized test for invalid dimensions across all three functions
typedef struct {
  ssize_t img_w;
  ssize_t img_h;
  ssize_t target_w;
  ssize_t target_h;
  ssize_t expected_w;
  ssize_t expected_h;
  char description[128];
} invalid_dimensions_test_case_t;

static invalid_dimensions_test_case_t invalid_dimensions_cases[] = {
    // aspect_ratio invalid image dimensions
    {0, 1080, 80, 24, 1, 1, "aspect_ratio: zero image width"},
    {1920, 0, 80, 24, 1, 1, "aspect_ratio: zero image height"},
    {-1920, 1080, 80, 24, 1, 1, "aspect_ratio: negative image width"},
    {1920, -1080, 80, 24, 1, 1, "aspect_ratio: negative image height"},

    // aspect_ratio2 invalid image dimensions
    {0, 1080, 80, 24, 1, 1, "aspect_ratio2: zero image width"},
    {1920, 0, 80, 24, 1, 1, "aspect_ratio2: zero image height"},
    {-1920, 1080, 80, 24, 1, 1, "aspect_ratio2: negative image width"},
    {1920, -1080, 80, 24, 1, 1, "aspect_ratio2: negative image height"},

    // aspect_ratio2 invalid target dimensions
    {1920, 1080, 0, 24, 1, 1, "aspect_ratio2: zero target width"},
    {1920, 1080, 80, 0, 1, 1, "aspect_ratio2: zero target height"},
    {1920, 1080, -80, 24, 1, 1, "aspect_ratio2: negative target width"},
    {1920, 1080, 80, -24, 1, 1, "aspect_ratio2: negative target height"},

    // calculate_fit_dimensions_pixel invalid image dimensions
    {0, 1080, 80, 24, 80, 24, "calculate_fit_dimensions_pixel: zero image width"},
    {1920, 0, 80, 24, 80, 24, "calculate_fit_dimensions_pixel: zero image height"},
    {-1920, 1080, 80, 24, 80, 24, "calculate_fit_dimensions_pixel: negative image width"},
    {1920, -1080, 80, 24, 80, 24, "calculate_fit_dimensions_pixel: negative image height"}};



TEST_CASE("aspect_ratio/invalid_dimensions_parameterized", "[aspect_ratio]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(invalid_dimensions_cases)); auto *test_case = &generated_case;
  ssize_t out_width, out_height;

  // Determine which function to test based on description
  if (strstr(test_case->description, "aspect_ratio2:") != NULL) {
    aspect_ratio2(test_case->img_w, test_case->img_h, test_case->target_w, test_case->target_h, &out_width,
                  &out_height);
  } else if (strstr(test_case->description, "calculate_fit_dimensions_pixel:") != NULL) {
    int out_w, out_h;
    calculate_fit_dimensions_pixel((int)test_case->img_w, (int)test_case->img_h, (int)test_case->target_w,
                                   (int)test_case->target_h, &out_w, &out_h);
    out_width = (ssize_t)out_w;
    out_height = (ssize_t)out_h;
  } else {
    aspect_ratio(test_case->img_w, test_case->img_h, test_case->target_w, test_case->target_h, false, &out_width,
                 &out_height);
  }

  ASCIICHAT_REQUIRE_EQ(out_width, test_case->expected_w, "%s: expected width %zd, got %zd", test_case->description,
               test_case->expected_w, out_width);
  ASCIICHAT_REQUIRE_EQ(out_height, test_case->expected_h, "%s: expected height %zd, got %zd", test_case->description,
               test_case->expected_h, out_height);
}

TEST_CASE("aspect_ratio/very_small_image", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test with very small image
  aspect_ratio(1, 1, 80, 24, false, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("aspect_ratio/very_large_image", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test with very large image
  aspect_ratio(10000, 10000, 80, 24, false, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

/* ============================================================================
 * aspect_ratio2 Function Tests
 * ============================================================================ */

TEST_CASE("aspect_ratio2/basic_functionality", "[aspect_ratio2]") {
  ssize_t out_width, out_height;

  // Test basic aspect ratio calculation without terminal character correction
  aspect_ratio2(1920, 1080, 80, 24, &out_width, &out_height);

  // Should maintain aspect ratio and fit within target dimensions
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("aspect_ratio2/null_output_pointers", "[aspect_ratio2]") {
  // Test with NULL output pointers - should not crash
  aspect_ratio2(1920, 1080, 80, 24, NULL, NULL);
  // If we get here without crashing, the test passes
  ASCIICHAT_REQUIRE(true);
}

TEST_CASE("aspect_ratio2/square_image", "[aspect_ratio2]") {
  ssize_t out_width, out_height;

  // Test square image
  aspect_ratio2(100, 100, 80, 24, &out_width, &out_height);

  // Should maintain square aspect ratio
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("aspect_ratio2/tall_image", "[aspect_ratio2]") {
  ssize_t out_width, out_height;

  // Test tall image (portrait)
  aspect_ratio2(100, 200, 80, 24, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("aspect_ratio2/wide_image", "[aspect_ratio2]") {
  ssize_t out_width, out_height;

  // Test wide image (landscape)
  aspect_ratio2(200, 100, 80, 24, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

/* ============================================================================
 * calculate_fit_dimensions_pixel Function Tests
 * ============================================================================ */

TEST_CASE("calculate_fit_dimensions_pixel/basic_functionality", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test basic functionality
  calculate_fit_dimensions_pixel(1920, 1080, 80, 24, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("calculate_fit_dimensions_pixel/null_output_pointers", "[calculate_fit_dimensions_pixel]") {
  // Test with NULL output pointers - should not crash
  calculate_fit_dimensions_pixel(1920, 1080, 80, 24, NULL, NULL);
  // If we get here without crashing, the test passes
  ASCIICHAT_REQUIRE(true);
}

TEST_CASE("calculate_fit_dimensions_pixel/square_image", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test square image
  calculate_fit_dimensions_pixel(100, 100, 80, 24, &out_width, &out_height);

  // Should maintain square aspect ratio
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("calculate_fit_dimensions_pixel/tall_image", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test tall image (portrait)
  calculate_fit_dimensions_pixel(100, 200, 80, 24, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("calculate_fit_dimensions_pixel/wide_image", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test wide image (landscape)
  calculate_fit_dimensions_pixel(200, 100, 80, 24, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

TEST_CASE("calculate_fit_dimensions_pixel/exact_fit", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test image that exactly fits the target dimensions
  calculate_fit_dimensions_pixel(80, 24, 80, 24, &out_width, &out_height);

  ASCIICHAT_REQUIRE_EQ(out_width, 80);
  ASCIICHAT_REQUIRE_EQ(out_height, 24);
}

TEST_CASE("calculate_fit_dimensions_pixel/very_small_target", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test with very small target dimensions
  calculate_fit_dimensions_pixel(1920, 1080, 1, 1, &out_width, &out_height);

  ASCIICHAT_REQUIRE_EQ(out_width, 1);
  ASCIICHAT_REQUIRE_EQ(out_height, 1);
}

TEST_CASE("calculate_fit_dimensions_pixel/very_large_image", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test with very large image
  calculate_fit_dimensions_pixel(10000, 10000, 80, 24, &out_width, &out_height);

  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 80);
  ASCIICHAT_REQUIRE_LE(out_height, 24);
}

/* ============================================================================
 * Comprehensive Random Value Tests
 * ============================================================================ */

TEST_CASE("aspect_ratio/random_values_comprehensive", "[aspect_ratio]") {
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

    ASCIICHAT_REQUIRE_GT(out_width, 0, "Random test %d: img_w=%zd, img_h=%zd, term_w=%zd, term_h=%zd, stretch=%d", i, img_w,
                 img_h, term_w, term_h, stretch);
    ASCIICHAT_REQUIRE_GT(out_height, 0, "Random test %d: img_w=%zd, img_h=%zd, term_w=%zd, term_h=%zd, stretch=%d", i, img_w,
                 img_h, term_w, term_h, stretch);

    if (stretch) {
      ASCIICHAT_REQUIRE_EQ(out_width, term_w);
      ASCIICHAT_REQUIRE_EQ(out_height, term_h);
    } else {
      ASCIICHAT_REQUIRE_LE(out_width, term_w);
      ASCIICHAT_REQUIRE_LE(out_height, term_h);
    }
  }
}

TEST_CASE("aspect_ratio2/random_values_comprehensive", "[aspect_ratio2]") {
  srand((unsigned int)time(NULL));
  ssize_t out_width, out_height;

  // Test 1000 random combinations
  for (int i = 0; i < 1000; i++) {
    ssize_t img_w = (ssize_t)(rand() % 10000) + 1;
    ssize_t img_h = (ssize_t)(rand() % 10000) + 1;
    ssize_t target_w = (ssize_t)(rand() % 1000) + 1;
    ssize_t target_h = (ssize_t)(rand() % 1000) + 1;

    aspect_ratio2(img_w, img_h, target_w, target_h, &out_width, &out_height);

    ASCIICHAT_REQUIRE_GT(out_width, 0, "Random test %d: img_w=%zd, img_h=%zd, target_w=%zd, target_h=%zd", i, img_w, img_h,
                 target_w, target_h);
    ASCIICHAT_REQUIRE_GT(out_height, 0, "Random test %d: img_w=%zd, img_h=%zd, target_w=%zd, target_h=%zd", i, img_w, img_h,
                 target_w, target_h);
    ASCIICHAT_REQUIRE_LE(out_width, target_w);
    ASCIICHAT_REQUIRE_LE(out_height, target_h);
  }
}

TEST_CASE("calculate_fit_dimensions_pixel/random_values_comprehensive", "[calculate_fit_dimensions_pixel]") {
  srand((unsigned int)time(NULL));
  int out_width, out_height;

  // Test 1000 random combinations
  for (int i = 0; i < 1000; i++) {
    int img_w = (rand() % 10000) + 1;
    int img_h = (rand() % 10000) + 1;
    int max_w = (rand() % 1000) + 1;
    int max_h = (rand() % 1000) + 1;

    calculate_fit_dimensions_pixel(img_w, img_h, max_w, max_h, &out_width, &out_height);

    ASCIICHAT_REQUIRE_GT(out_width, 0, "Random test %d: img_w=%d, img_h=%d, max_w=%d, max_h=%d", i, img_w, img_h, max_w, max_h);
    ASCIICHAT_REQUIRE_GT(out_height, 0, "Random test %d: img_w=%d, img_h=%d, max_w=%d, max_h=%d", i, img_w, img_h, max_w,
                 max_h);
    ASCIICHAT_REQUIRE_LE(out_width, max_w);
    ASCIICHAT_REQUIRE_LE(out_height, max_h);
  }
}

/* ============================================================================
 * Edge Cases and Boundary Tests
 * ============================================================================ */

TEST_CASE("aspect_ratio/boundary_values", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test with minimum valid dimensions
  aspect_ratio(1, 1, 1, 1, false, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);

  // Test with very large terminal dimensions
  aspect_ratio(1920, 1080, 1000, 1000, false, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 1000);
  ASCIICHAT_REQUIRE_LE(out_height, 1000);

  // Test with extreme aspect ratios
  aspect_ratio(10000, 1, 100, 100, false, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100);
  ASCIICHAT_REQUIRE_LE(out_height, 100);

  aspect_ratio(1, 10000, 100, 100, false, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100);
  ASCIICHAT_REQUIRE_LE(out_height, 100);
}

TEST_CASE("aspect_ratio2/boundary_values", "[aspect_ratio2]") {
  ssize_t out_width, out_height;

  // Test with minimum valid dimensions
  aspect_ratio2(1, 1, 1, 1, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);

  // Test with very large target dimensions
  aspect_ratio2(1920, 1080, 1000, 1000, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 1000);
  ASCIICHAT_REQUIRE_LE(out_height, 1000);

  // Test with extreme aspect ratios
  aspect_ratio2(10000, 1, 100, 100, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100);
  ASCIICHAT_REQUIRE_LE(out_height, 100);

  aspect_ratio2(1, 10000, 100, 100, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100);
  ASCIICHAT_REQUIRE_LE(out_height, 100);
}

TEST_CASE("calculate_fit_dimensions_pixel/boundary_values", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test with minimum valid dimensions
  calculate_fit_dimensions_pixel(1, 1, 1, 1, &out_width, &out_height);
  ASCIICHAT_REQUIRE_EQ(out_width, 1);
  ASCIICHAT_REQUIRE_EQ(out_height, 1);

  // Test with very large target dimensions
  calculate_fit_dimensions_pixel(1920, 1080, 1000, 1000, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 1000);
  ASCIICHAT_REQUIRE_LE(out_height, 1000);

  // Test with extreme aspect ratios
  calculate_fit_dimensions_pixel(10000, 1, 100, 100, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100);
  ASCIICHAT_REQUIRE_LE(out_height, 100);

  calculate_fit_dimensions_pixel(1, 10000, 100, 100, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100);
  ASCIICHAT_REQUIRE_LE(out_height, 100);
}

/* ============================================================================
 * Mathematical Precision Tests - Theory-Based Aspect Ratio Preservation
 * ============================================================================ */

// Theory test: aspect ratio preservation property
// PROPERTY: Output aspect ratio should match input aspect ratio (within tolerance)
// Tests aspect_ratio2 and calculate_fit_dimensions_pixel (no CHAR_ASPECT correction)
static constexpr ssize_t aspect_ratio_image_widths[] = {1920, 1024, 800, 512, 100, 1};
static constexpr ssize_t aspect_ratio_image_heights[] = {1080, 768, 600, 512, 200, 1};
static constexpr ssize_t aspect_ratio_target_widths[] = {80, 120, 160, 200, 40, 1000};
static constexpr ssize_t aspect_ratio_target_heights[] = {24, 40, 60, 80, 20, 500};

TEST_CASE("aspect_ratio/aspect_ratio_preservation_property", "[aspect_ratio]") {
  const size_t index = GENERATE(Catch::Generators::range<size_t>(0, sizeof(aspect_ratio_image_widths) / sizeof(*aspect_ratio_image_widths)));
  const ssize_t img_w = aspect_ratio_image_widths[index];
  const ssize_t img_h = aspect_ratio_image_heights[index];
  const ssize_t target_w = aspect_ratio_target_widths[index];
  const ssize_t target_h = aspect_ratio_target_heights[index];
  // Skip invalid combinations
  ASCIICHAT_ASSUME(img_w > 0);
  ASCIICHAT_ASSUME(img_h > 0);
  ASCIICHAT_ASSUME(target_w > 0);
  ASCIICHAT_ASSUME(target_h > 0);

  // Skip extreme aspect ratios that can't be preserved accurately in small target dimensions
  float input_aspect_ratio = (float)img_w / (float)img_h;
  ASCIICHAT_ASSUME(input_aspect_ratio >= 0.1f);  // Skip very tall images (>10:1 height:width)
  ASCIICHAT_ASSUME(input_aspect_ratio <= 10.0f); // Skip very wide images (>10:1 width:height)

  // Skip cases where target dimensions are too small to preserve aspect ratio
  ASCIICHAT_ASSUME(target_w >= 10); // Need reasonable minimum target size
  ASCIICHAT_ASSUME(target_h >= 10);

  // Test aspect_ratio2 (no CHAR_ASPECT correction)
  ssize_t out_w2, out_h2;
  aspect_ratio2(img_w, img_h, target_w, target_h, &out_w2, &out_h2);

  // PROPERTY 1: Output dimensions must fit within target bounds
  ASCIICHAT_REQUIRE_LE(out_w2, target_w, "aspect_ratio2: width %zd exceeds target %zd", out_w2, target_w);
  ASCIICHAT_REQUIRE_LE(out_h2, target_h, "aspect_ratio2: height %zd exceeds target %zd", out_h2, target_h);

  // PROPERTY 2: Output dimensions must be positive
  ASCIICHAT_REQUIRE_GT(out_w2, 0, "aspect_ratio2: width must be positive");
  ASCIICHAT_REQUIRE_GT(out_h2, 0, "aspect_ratio2: height must be positive");

  // PROPERTY 3: Aspect ratio preservation (within 25% tolerance for discrete dimensions)
  float input_aspect = (float)img_w / (float)img_h;
  float output_aspect = (float)out_w2 / (float)out_h2;
  float aspect_error = fabsf(output_aspect - input_aspect) / input_aspect;
  ASCIICHAT_REQUIRE_LT(aspect_error, 0.25f, "aspect_ratio2: aspect ratio not preserved (input=%.2f, output=%.2f, error=%.1f%%)",
               input_aspect, output_aspect, aspect_error * 100.0f);

  // Test calculate_fit_dimensions_pixel (same properties)
  int out_w_pix, out_h_pix;
  calculate_fit_dimensions_pixel((int)img_w, (int)img_h, (int)target_w, (int)target_h, &out_w_pix, &out_h_pix);

  ASCIICHAT_REQUIRE_LE(out_w_pix, (int)target_w, "calculate_fit_dimensions_pixel: width %d exceeds target %zd", out_w_pix,
                target_w);
  ASCIICHAT_REQUIRE_LE(out_h_pix, (int)target_h, "calculate_fit_dimensions_pixel: height %d exceeds target %zd", out_h_pix,
                target_h);
  ASCIICHAT_REQUIRE_GT(out_w_pix, 0, "calculate_fit_dimensions_pixel: width must be positive");
  ASCIICHAT_REQUIRE_GT(out_h_pix, 0, "calculate_fit_dimensions_pixel: height must be positive");

  output_aspect = (float)out_w_pix / (float)out_h_pix;
  aspect_error = fabsf(output_aspect - input_aspect) / input_aspect;
  ASCIICHAT_REQUIRE_LT(aspect_error, 0.25f,
               "calculate_fit_dimensions_pixel: aspect ratio not preserved (input=%.2f, output=%.2f, error=%.1f%%)",
               input_aspect, output_aspect, aspect_error * 100.0f);
}

// Keep one simple precision test for aspect_ratio with CHAR_ASPECT correction
TEST_CASE("aspect_ratio/char_aspect_correction", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test that CHAR_ASPECT correction is applied
  // 16:9 aspect ratio should have width/height ≈ (16/9) * 2.0 = 3.556
  aspect_ratio(1920, 1080, 80, 24, false, &out_width, &out_height);
  float aspect_ratio_result = (float)out_width / (float)out_height;
  float expected_aspect = 1920.0f / 1080.0f * 2.0f; // CHAR_ASPECT = 2.0
  ASCIICHAT_REQUIRE_FLOAT_EQ(aspect_ratio_result, expected_aspect, 0.2f);
}

/* ============================================================================
 * Stress Tests with Large Values
 * ============================================================================ */

TEST_CASE("aspect_ratio/stress_test_large_values", "[aspect_ratio]") {
  ssize_t out_width, out_height;

  // Test with very large image dimensions
  aspect_ratio(100000, 100000, 1000, 1000, false, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 1000);
  ASCIICHAT_REQUIRE_LE(out_height, 1000);

  // Test with very large terminal dimensions
  aspect_ratio(1920, 1080, 100000, 100000, false, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100000);
  ASCIICHAT_REQUIRE_LE(out_height, 100000);
}

TEST_CASE("aspect_ratio2/stress_test_large_values", "[aspect_ratio2]") {
  ssize_t out_width, out_height;

  // Test with very large image dimensions
  aspect_ratio2(100000, 100000, 1000, 1000, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 1000);
  ASCIICHAT_REQUIRE_LE(out_height, 1000);

  // Test with very large target dimensions
  aspect_ratio2(1920, 1080, 100000, 100000, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100000);
  ASCIICHAT_REQUIRE_LE(out_height, 100000);
}

TEST_CASE("calculate_fit_dimensions_pixel/stress_test_large_values", "[calculate_fit_dimensions_pixel]") {
  int out_width, out_height;

  // Test with very large image dimensions
  calculate_fit_dimensions_pixel(100000, 100000, 1000, 1000, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 1000);
  ASCIICHAT_REQUIRE_LE(out_height, 1000);

  // Test with very large target dimensions
  calculate_fit_dimensions_pixel(1920, 1080, 100000, 100000, &out_width, &out_height);
  ASCIICHAT_REQUIRE_GT(out_width, 0);
  ASCIICHAT_REQUIRE_GT(out_height, 0);
  ASCIICHAT_REQUIRE_LE(out_width, 100000);
  ASCIICHAT_REQUIRE_LE(out_height, 100000);
}

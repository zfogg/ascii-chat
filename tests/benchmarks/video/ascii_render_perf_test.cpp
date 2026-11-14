#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <string.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/rgba/image.h>

static void build_luminance_palette(char dest[257], const char *palette) {
  const size_t palette_len = strlen(palette);
  for (int i = 0; i < 256; i++) {
    dest[i] = palette[palette_len > 0 ? (size_t)i % palette_len : 0];
  }
  dest[256] = '\0';
}

static void fill_gradient_image(image_t *image, const int width, const int height) {
  const int total_pixels = width * height;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const int index = y * width + x;
      const uint8_t value = (uint8_t)((index * 255) / total_pixels);
      image->pixels[index] = (rgb_pixel_t){value, (uint8_t)(value / 2), (uint8_t)(255 - value)};
    }
  }
}

static size_t benchmark_ascii_conversion(image_t *image, const bool color_mode) {
  const char *palette = "@%#*+=-:. ";
  char luminance_palette[257];
  build_luminance_palette(luminance_palette, palette);
  char *frame = ascii_convert(image, 320, 180, color_mode, true, false, palette, luminance_palette);
  const size_t length = frame == nullptr ? 0 : strlen(frame);
  SAFE_FREE(frame);
  return length;
}

static void run_ascii_conversion_batch(image_t *image, const ssize_t width, const ssize_t height,
                                       const bool color_mode) {
  const char *palette = "@%#*+=-:. ";
  char luminance_palette[257];
  build_luminance_palette(luminance_palette, palette);

  size_t total_output = 0;
  size_t iteration_count = 40;

  for (size_t i = 0; i < iteration_count; i++) {
    char *frame = ascii_convert(image, width, height, color_mode, true, false, palette, luminance_palette);
    REQUIRE(frame != nullptr);
    total_output += strlen(frame);
    SAFE_FREE(frame);
  }

  REQUIRE(total_output > 0);
}

TEST_CASE("ASCII renderer correctness", "[benchmark][renderer]") {
  const int width = 320;
  const int height = 180;

  image_t *image = image_new(width, height);
  REQUIRE(image != nullptr);

  fill_gradient_image(image, width, height);
  SECTION("monochrome") {
    run_ascii_conversion_batch(image, width, height, false);
  }
  SECTION("color") {
    run_ascii_conversion_batch(image, width, height, true);
  }

  image_destroy(image);
}

TEST_CASE("ASCII renderer benchmark", "[benchmark][renderer]") {
  const int width = 320;
  const int height = 180;

  image_t *image = image_new(width, height);
  REQUIRE(image != nullptr);
  fill_gradient_image(image, width, height);
  BENCHMARK("monochrome conversion") { return benchmark_ascii_conversion(image, false); };
  BENCHMARK("color conversion") { return benchmark_ascii_conversion(image, true); };

  image_destroy(image);
}

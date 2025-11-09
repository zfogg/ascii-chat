#include <criterion/criterion.h>
#include <string.h>
#include "tests/common.h"
#include "image2ascii/ascii.h"
#include "image2ascii/image.h"
#include "logging.h"

static void ascii_perf_init(void) {
  log_set_level(LOG_WARN);
  test_logging_disable(true, true);
}

static void ascii_perf_fini(void) {
  test_logging_restore();
}

TestSuite(performance_ascii_render, .init = ascii_perf_init, .fini = ascii_perf_fini);

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
      image->pixels[index] = (rgb_t){value, (uint8_t)(value / 2), (uint8_t)(255 - value)};
    }
  }
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
    cr_assert_not_null(frame, "ascii_convert returned NULL on iteration %zu", i);
    total_output += strlen(frame);
    SAFE_FREE(frame);
  }

  cr_assert_gt(total_output, 0, "ascii_convert produced empty output across iterations");
}

Test(performance_ascii_render, convert_full_hd_mono_batch) {
  const int width = 320;
  const int height = 180;

  image_t *image = image_new(width, height);
  cr_assert_not_null(image, "image_new failed for %dx%d frame", width, height);

  fill_gradient_image(image, width, height);
  run_ascii_conversion_batch(image, width, height, false);

  image_destroy(image);
}

Test(performance_ascii_render, convert_full_hd_color_batch) {
  const int width = 320;
  const int height = 180;

  image_t *image = image_new(width, height);
  cr_assert_not_null(image, "image_new failed for %dx%d frame", width, height);

  fill_gradient_image(image, width, height);
  run_ascii_conversion_batch(image, width, height, true);

  image_destroy(image);
}


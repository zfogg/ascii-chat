/**
 * @file video/color_filter.c
 * @brief Monochromatic color filter implementation for video frames
 * @ingroup video
 */

#include "color_filter.h"
#include "common.h"
#include <string.h>

/**
 * @brief Color filter registry with metadata for all 11 filters
 *
 * Single source of truth for filter definitions. Each filter includes:
 * - name: Human-readable name for logging/help
 * - cli_name: CLI argument name (used in --color-filter option)
 * - r, g, b: RGB color values for this filter
 * - foreground_on_bg: Rendering mode (true=color on white, false=white on color)
 */
static const color_filter_def_t color_filter_registry[COLOR_FILTER_COUNT] = {
    [COLOR_FILTER_NONE] =
        {
            .name = "none",
            .cli_name = "none",
            .r = 0,
            .g = 0,
            .b = 0,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_BLACK] =
        {
            .name = "black",
            .cli_name = "black",
            .r = 0,
            .g = 0,
            .b = 0,
            .foreground_on_bg = true,
        },
    [COLOR_FILTER_WHITE] =
        {
            .name = "white",
            .cli_name = "white",
            .r = 255,
            .g = 255,
            .b = 255,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_GREEN] =
        {
            .name = "green",
            .cli_name = "green",
            .r = 0,
            .g = 255,
            .b = 65,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_MAGENTA] =
        {
            .name = "magenta",
            .cli_name = "magenta",
            .r = 255,
            .g = 0,
            .b = 255,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_FUCHSIA] =
        {
            .name = "fuchsia",
            .cli_name = "fuchsia",
            .r = 255,
            .g = 0,
            .b = 170,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_ORANGE] =
        {
            .name = "orange",
            .cli_name = "orange",
            .r = 255,
            .g = 136,
            .b = 0,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_TEAL] =
        {
            .name = "teal",
            .cli_name = "teal",
            .r = 0,
            .g = 221,
            .b = 221,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_CYAN] =
        {
            .name = "cyan",
            .cli_name = "cyan",
            .r = 0,
            .g = 255,
            .b = 255,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_PINK] =
        {
            .name = "pink",
            .cli_name = "pink",
            .r = 255,
            .g = 182,
            .b = 193,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_RED] =
        {
            .name = "red",
            .cli_name = "red",
            .r = 255,
            .g = 51,
            .b = 51,
            .foreground_on_bg = false,
        },
    [COLOR_FILTER_YELLOW] =
        {
            .name = "yellow",
            .cli_name = "yellow",
            .r = 255,
            .g = 235,
            .b = 153,
            .foreground_on_bg = false,
        },
};

const color_filter_def_t *color_filter_get_metadata(color_filter_t filter) {
  if (filter <= COLOR_FILTER_NONE || filter >= COLOR_FILTER_COUNT) {
    return NULL;
  }
  return &color_filter_registry[filter];
}

color_filter_t color_filter_from_cli_name(const char *cli_name) {
  if (!cli_name) {
    return COLOR_FILTER_NONE;
  }

  for (int i = 0; i < COLOR_FILTER_COUNT; i++) {
    if (strcasecmp(color_filter_registry[i].cli_name, cli_name) == 0) {
      return (color_filter_t)i;
    }
  }

  return COLOR_FILTER_NONE;
}

/**
 * @brief Colorize a single grayscale pixel using filter parameters
 *
 * Internal helper for apply_color_filter. Applies colorization to a
 * single grayscale value using the filter's RGB color and mode.
 *
 * @param gray Grayscale value (0-255, already computed from RGB)
 * @param filter Filter definition with RGB and mode info
 * @param out_r Output red channel (pointer, modified)
 * @param out_g Output green channel (pointer, modified)
 * @param out_b Output blue channel (pointer, modified)
 */
static inline void colorize_grayscale_pixel(uint8_t gray, const color_filter_def_t *filter, uint8_t *out_r,
                                            uint8_t *out_g, uint8_t *out_b) {
  if (filter->foreground_on_bg) {
    // Black-on-white mode: dark pixels get full color, light pixels become white
    // Blend: color * (255 - gray) + white * gray, all / 255
    // When gray=0 (black), we get full color. When gray=255 (white), we get white.
    *out_r = (uint8_t)((filter->r * (255U - gray) + 255U * gray) / 255U);
    *out_g = (uint8_t)((filter->g * (255U - gray) + 255U * gray) / 255U);
    *out_b = (uint8_t)((filter->b * (255U - gray) + 255U * gray) / 255U);
  } else {
    // White-on-color mode: scale color intensity by grayscale
    *out_r = (uint8_t)((filter->r * gray) / 255U);
    *out_g = (uint8_t)((filter->g * gray) / 255U);
    *out_b = (uint8_t)((filter->b * gray) / 255U);
  }
}

int apply_color_filter(uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride, color_filter_t filter) {
  if (!pixels || width == 0 || height == 0 || stride == 0) {
    return -1;
  }

  if (filter == COLOR_FILTER_NONE) {
    return 0; // No-op for none filter
  }

  const color_filter_def_t *filter_def = color_filter_get_metadata(filter);
  if (!filter_def) {
    return -1; // Invalid filter
  }

  // Process each pixel in the image
  for (uint32_t y = 0; y < height; y++) {
    uint8_t *row = pixels + y * stride;
    for (uint32_t x = 0; x < width; x++) {
      uint8_t *pixel = row + x * 3; // Assume RGB24 (3 bytes per pixel)
      uint8_t r = pixel[0];
      uint8_t g = pixel[1];
      uint8_t b = pixel[2];

      // Convert RGB to grayscale using ITU-R BT.601
      uint8_t gray = rgb_to_grayscale(r, g, b);

      // Colorize the grayscale value
      colorize_grayscale_pixel(gray, filter_def, &pixel[0], &pixel[1], &pixel[2]);
    }
  }

  return 0;
}

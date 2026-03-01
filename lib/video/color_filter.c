/**
 * @file video/color_filter.c
 * @brief Monochromatic color filter implementation for video frames
 * @ingroup video
 */

#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/memory.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

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
    [COLOR_FILTER_RAINBOW] =
        {
            .name = "rainbow",
            .cli_name = "rainbow",
            .r = 255,
            .g = 0,
            .b = 0,
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
    if (platform_strcasecmp(color_filter_registry[i].cli_name, cli_name) == 0) {
      return (color_filter_t)i;
    }
  }

  return COLOR_FILTER_NONE;
}

/**
 * Calculate rainbow color from time
 * Cycles through full spectrum over 3.5 seconds
 * This is the centralized rainbow implementation used by both matrix and color filter
 */
void color_filter_calculate_rainbow(float time, uint8_t *r, uint8_t *g, uint8_t *b) {
  // Cycle period: 3.5 seconds
  const float cycle_period = 3.5f;

  // Normalize time to [0, 1] range within cycle
  float phase = fmodf(time, cycle_period) / cycle_period;

  // Convert to hue angle [0, 360)
  float hue = phase * 360.0f;

  // HSV to RGB conversion (S=1, V=1 for full saturation and brightness)
  // Hue divided into 6 segments
  float h = hue / 60.0f;
  int i = (int)floorf(h);
  float f = h - (float)i;

  // Calculate RGB based on hue segment
  float q = 1.0f - f;
  float t = f;

  switch (i % 6) {
  case 0:
    *r = 255;
    *g = (uint8_t)(t * 255.0f + 0.5f);
    *b = 0;
    break; // Red to Yellow
  case 1:
    *r = (uint8_t)(q * 255.0f + 0.5f);
    *g = 255;
    *b = 0;
    break; // Yellow to Green
  case 2:
    *r = 0;
    *g = 255;
    *b = (uint8_t)(t * 255.0f + 0.5f);
    break; // Green to Cyan
  case 3:
    *r = 0;
    *g = (uint8_t)(q * 255.0f + 0.5f);
    *b = 255;
    break; // Cyan to Blue
  case 4:
    *r = (uint8_t)(t * 255.0f + 0.5f);
    *g = 0;
    *b = 255;
    break; // Blue to Magenta
  case 5:
    *r = 255;
    *g = 0;
    *b = (uint8_t)(q * 255.0f + 0.5f);
    break; // Magenta to Red
  default:
    *r = 255;
    *g = 0;
    *b = 0;
    break; // Fallback to red
  }

  // Ensure minimum perceived brightness by boosting luminance
  // Pure blue (0,0,255) has luminance ~18, which appears very dark
  // We need minimum luminance of ~120 for bright, vibrant colors
  const float min_luminance = 120.0f;

  // Calculate current luminance using ITU-R BT.709 coefficients
  float luminance = 0.2126f * *r + 0.7152f * *g + 0.0722f * *b;

  if (luminance < min_luminance) {
    // Boost luminance by adding white (increasing all channels proportionally)
    // This desaturates the color but makes it much brighter
    float boost = (min_luminance - luminance) / 3.0f;
    *r = (uint8_t)fminf(255.0f, *r + boost);
    *g = (uint8_t)fminf(255.0f, *g + boost);
    *b = (uint8_t)fminf(255.0f, *b + boost);
  }
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

int apply_color_filter(uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride, color_filter_t filter,
                       float time) {
  if (!pixels || width == 0 || height == 0 || stride == 0) {
    return -1;
  }

  if (filter == COLOR_FILTER_NONE) {
    return 0; // No-op for none filter
  }

  // Special handling for rainbow filter
  if (filter == COLOR_FILTER_RAINBOW) {
    uint8_t rainbow_r, rainbow_g, rainbow_b;
    color_filter_calculate_rainbow(time, &rainbow_r, &rainbow_g, &rainbow_b);

    // Create temporary filter def with current rainbow color
    color_filter_def_t rainbow_filter = {
        .name = "rainbow",
        .cli_name = "rainbow",
        .r = rainbow_r,
        .g = rainbow_g,
        .b = rainbow_b,
        .foreground_on_bg = false,
    };

    // Process each pixel with minimum brightness to prevent black
    // Minimum brightness is 70% to ensure rainbow colors stay vibrant even for dark input
    const uint8_t min_brightness = 179; // 70% of 255

    for (uint32_t y = 0; y < height; y++) {
      uint8_t *row = pixels + y * stride;
      for (uint32_t x = 0; x < width; x++) {
        uint8_t *pixel = row + x * 3;
        uint8_t r = pixel[0];
        uint8_t g = pixel[1];
        uint8_t b = pixel[2];
        uint8_t gray = rgb_to_grayscale(r, g, b);

        // Boost grayscale to maintain minimum brightness for rainbow
        // Formula: adjusted = min + gray * (1 - min/255)
        // This ensures: black pixels -> min_brightness, white pixels -> 255
        uint8_t adjusted_gray = min_brightness + (uint8_t)((gray * (255U - min_brightness)) / 255U);

        colorize_grayscale_pixel(adjusted_gray, &rainbow_filter, &pixel[0], &pixel[1], &pixel[2]);
      }
    }
    return 0;
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

char *rainbow_replace_ansi_colors(const char *ansi_string, float time_seconds) {
  if (!ansi_string) {
    return NULL;
  }

  // Calculate rainbow color once for this frame
  uint8_t rainbow_r = 0, rainbow_g = 0, rainbow_b = 0;
  color_filter_calculate_rainbow(time_seconds, &rainbow_r, &rainbow_g, &rainbow_b);

  // Build the replacement ANSI code string once
  char rainbow_code[32];
  snprintf(rainbow_code, sizeof(rainbow_code), "\x1b[38;2;%d;%d;%dm", rainbow_r, rainbow_g, rainbow_b);
  size_t rainbow_code_len = strlen(rainbow_code);

  // Check if there are any ANSI codes to replace
  if (!strstr(ansi_string, "\x1b[38;2;")) {
    return NULL; // No ANSI codes found, no replacement needed
  }

  // Allocate buffer for result (2x size for safety with many replacements)
  size_t input_len = strlen(ansi_string);
  char *new_result = SAFE_MALLOC(input_len * 2, char *);
  if (!new_result) {
    return NULL;
  }

  // Replace all RGB color codes with rainbow color
  const char *src = ansi_string;
  char *dst = new_result;

  while (*src) {
    const char *ansi_start = strstr(src, "\x1b[38;2;");
    if (ansi_start) {
      // Copy everything before this ANSI code
      size_t before_len = (size_t)(ansi_start - src);
      memcpy(dst, src, before_len);
      dst += before_len;

      // Skip the old ANSI code (find the 'm' that ends it)
      const char *ansi_end = strchr(ansi_start + 7, 'm');
      if (ansi_end) {
        // Copy the rainbow replacement code
        memcpy(dst, rainbow_code, rainbow_code_len);
        dst += rainbow_code_len;

        // Move source past the old code
        src = ansi_end + 1;
      } else {
        // Malformed ANSI code, just copy it
        *dst++ = *src++;
      }
    } else {
      // No more ANSI codes, copy the rest
      memcpy(dst, src, strlen(src) + 1);
      break;
    }
  }

  return new_result;
}

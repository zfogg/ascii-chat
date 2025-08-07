#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "image.h"
#include "ascii.h"
#include "options.h"
#include "round.h"

image_t *image_new(int width, int height) {
  image_t *p;

  SAFE_MALLOC(p, sizeof(image_t), image_t *);

  // Check for integer overflow before multiplication
  if (width < 0 || height < 0) {
    log_error("Invalid image dimensions: %d x %d", width, height);
    free(p);
    return NULL;
  }

  const unsigned long w_ul = (unsigned long)width;
  const unsigned long h_ul = (unsigned long)height;

  // Check if multiplication would overflow
  if (w_ul > 0 && h_ul > ULONG_MAX / w_ul) {
    log_error("Image dimensions too large (would overflow): %d x %d", width, height);
    free(p);
    return NULL;
  }

  const unsigned long total_pixels = w_ul * h_ul;

  // Check if final size calculation would overflow
  if (total_pixels > ULONG_MAX / sizeof(rgb_t)) {
    log_error("Image pixel count too large: %lu pixels", total_pixels);
    free(p);
    return NULL;
  }

  const size_t pixels_size = total_pixels * sizeof(rgb_t);
  if (pixels_size > IMAGE_MAX_PIXELS_SIZE) {
    log_error("Image size exceeds maximum allowed: %d x %d (%zu bytes)", width, height, pixels_size);
    free(p);
    return NULL;
  }

  SAFE_MALLOC(p->pixels, pixels_size, rgb_t *);

  p->w = width;
  p->h = height;
  return p;
}

void image_destroy(image_t *p) {
  if (!p) {
    log_error("image_destroy: p is NULL");
    return;
  }

  free(p->pixels);
  free(p);
}

void image_clear(image_t *p) {
  memset(p->pixels, 0, (unsigned long)p->w * (unsigned long)p->h * sizeof(rgb_t));
}

inline rgb_t *image_pixel(image_t *p, const int x, const int y) {
  return &p->pixels[x + y * p->w];
}

void image_resize(const image_t *s, image_t *d) {
  if (!s || !d) {
    log_error("image_resize: s or d is NULL");
    return;
  }

  image_resize_interpolation(s, d);
}

// Optimized interpolation function with better integer arithmetic and memory
// access
void image_resize_interpolation(const image_t *source, image_t *dest) {
  if (!source || !dest || !source->pixels || !dest->pixels) {
    log_error("Invalid parameters to image_resize_interpolation");
    return;
  }

  const int src_w = source->w;
  const int src_h = source->h;
  const int dst_w = dest->w;
  const int dst_h = dest->h;

  // Handle edge cases
  if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
    log_error("Invalid image dimensions for resize");
    return;
  }

  // Use fixed-point arithmetic for better performance
  const uint32_t x_ratio = ((src_w << 16) / dst_w) + 1;
  const uint32_t y_ratio = ((src_h << 16) / dst_h) + 1;

  const rgb_t *src_pixels = source->pixels;
  rgb_t *dst_pixels = dest->pixels;

  for (int y = 0; y < dst_h; y++) {
    const uint32_t src_y = (y * y_ratio) >> 16;
    const uint32_t safe_src_y = (src_y >= (uint32_t)src_h) ? (uint32_t)(src_h - 1) : src_y;
    const rgb_t *src_row = src_pixels + (safe_src_y * src_w);

    rgb_t *dst_row = dst_pixels + (y * dst_w);

    for (int x = 0; x < dst_w; x++) {
      const uint32_t src_x = (x * x_ratio) >> 16;
      const uint32_t safe_src_x = (src_x >= (uint32_t)src_w) ? (uint32_t)(src_w - 1) : src_x;
      dst_row[x] = src_row[safe_src_x];
    }
  }
}

// Optimized palette generation with caching
static char luminance_palette[ASCII_LUMINANCE_LEVELS * sizeof(char)];

void precalc_luminance_palette() {
  const int palette_len = strlen(ascii_palette) - 1;
  for (int n = 0; n < ASCII_LUMINANCE_LEVELS; n++) {
    luminance_palette[n] = ascii_palette[ROUND((float)palette_len * (float)n / (ASCII_LUMINANCE_LEVELS - 1))];
  }
}

void precalc_rgb_palettes(const float red, const float green, const float blue) {
  for (int n = 0; n < ASCII_LUMINANCE_LEVELS; ++n) {
    RED[n] = ((float)n) * red;
    GREEN[n] = ((float)n) * green;
    BLUE[n] = ((float)n) * blue;
    GRAY[n] = ((float)n);
  }
}

// Optimized image printing with better memory access patterns
char *image_print(const image_t *p) {
  if (!p || !p->pixels) {
    log_error("image_print: p is NULL");
    return NULL;
  }

  const int h = p->h;
  const int w = p->w;
  const ssize_t len = (ssize_t)h * (ssize_t)w;

  const rgb_t *pix = p->pixels;
  const unsigned short int *red_lut = RED;
  const unsigned short int *green_lut = GREEN;
  const unsigned short int *blue_lut = BLUE;

  char *lines;
  SAFE_MALLOC(lines, (len + 1) * sizeof(char), char *);

  lines[len] = '\0';

  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w; x++) {
      const rgb_t pixel = pix[row_offset + x];
      const int luminance = red_lut[pixel.r] + green_lut[pixel.g] + blue_lut[pixel.b];
      lines[row_offset + x] = luminance_palette[luminance];
    }
    if (y != h - 1) {
      lines[row_offset + w - 1] = '\n';
    }
  }

  return lines;
}

// Color quantization to reduce frame size and improve performance
void quantize_color(int *r, int *g, int *b, int levels) {
  int step = 256 / levels;
  *r = (*r / step) * step;
  *g = (*g / step) * step;
  *b = (*b / step) * step;
}

/**
 * Converts an image to colored ASCII art with ANSI escape codes.
 *
 * This function generates a string representation of an image where each pixel
 * is converted to an ASCII character with ANSI color codes. The character is
 * chosen based on luminance, and colors are applied using 24-bit RGB ANSI
 * escape sequences.
 *
 * Buffer allocation is precisely calculated to avoid waste and prevent overflows:
 * - Each pixel: 1 ASCII char + foreground ANSI code (19 bytes max)
 * - Background mode: adds background ANSI code (19 bytes max per pixel)
 * - Each row: reset sequence (\033[0m = 4 bytes) + newline (except last row)
 * - At the end: null terminator (1 byte)
 *
 * Color modes:
 * - Foreground only (default): ASCII characters with colored foreground
 * - Background mode (opt_background_color): colored background with contrasting
 *   foreground (black on bright backgrounds, white on dark backgrounds)
 *
 * ANSI escape code format:
 * - Foreground: \033[38;2;R;G;Bm (11-19 bytes depending on RGB values)
 * - Background: \033[48;2;R;G;Bm (11-19 bytes depending on RGB values)
 * - Reset: \033[0m (4 bytes)
 *
 * @param p Pointer to image_t structure containing pixel data
 * @return Dynamically allocated string containing colored ASCII art, or NULL on error.
 *         Caller is responsible for freeing the returned string.
 *
 * @note The function performs overflow checks to prevent integer overflow when
 *       calculating buffer sizes for very large images.
 * @note Uses global opt_background_color to determine color mode.
 * @note Exits with ASCIICHAT_ERR_BUFFER_ACCESS if buffer overflow is detected
 *       during string construction (should never happen with correct calculation).
 */
char *image_print_colored(const image_t *p) {
  if (!p || !p->pixels) {
    log_error("p or p->pixels is NULL");
    return NULL;
  }

  const int h = p->h;
  const int w = p->w;

  // Constants for ANSI escape codes
  const size_t max_fg_ansi = 19; // \033[38;2;255;255;255m
  const size_t max_bg_ansi = 19; // \033[48;2;255;255;255m
  const size_t reset_len = 4;    // \033[0m

  const size_t h_sz = (size_t)h;
  const size_t w_sz = (size_t)w;

  // Ensure h * w won't overflow
  if (h_sz > 0 && w_sz > SIZE_MAX / h_sz) {
    log_error("Image dimensions too large: %d x %d", h, w);
    return NULL;
  }

  const size_t total_pixels = h_sz * w_sz;
  const size_t bytes_per_pixel = 1 + max_fg_ansi + (opt_background_color ? max_bg_ansi : 0);

  // Ensure total_pixels * bytes_per_pixel won't overflow
  if (total_pixels > SIZE_MAX / bytes_per_pixel) {
    log_error("Pixel data too large for buffer: %d x %d", h, w);
    return NULL;
  }

  const size_t pixel_bytes = total_pixels * bytes_per_pixel;

  // Per row: reset sequence + newline (except last row)
  const size_t total_resets = h_sz * reset_len;
  const size_t total_newlines = (h_sz > 0) ? (h_sz - 1) : 0;

  // Final buffer size: pixel bytes + per-row extras + null terminator
  const size_t extra_bytes = total_resets + total_newlines + 1;

  if (pixel_bytes > SIZE_MAX - extra_bytes) {
    log_error("Final buffer size would overflow: %d x %d", h, w);
    return NULL;
  }

  const size_t lines_size = pixel_bytes + extra_bytes;
  char *lines;
  SAFE_MALLOC(lines, lines_size, char *);

  const rgb_t *pix = p->pixels;
  char *current_pos = lines;
  const char *buffer_end = lines + lines_size - 1; // reserve space for '\0'

  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w; x++) {
      const rgb_t pixel = pix[row_offset + x];
      int r = pixel.r, g = pixel.g, b = pixel.b;
      const int luminance = RED[r] + GREEN[g] + BLUE[b];
      const char ascii_char = luminance_palette[luminance];

      const size_t remaining = buffer_end - current_pos;
      if (remaining < 64) {
        log_error("Buffer overflow prevented in pixel write");
        exit(ASCIICHAT_ERR_BUFFER_ACCESS);
      }

      if (opt_background_color) {
        int fg_r = (luminance < 127) ? 0 : 255;
        int fg_g = fg_r, fg_b = fg_r;

        const char *ascii_fg = rgb_to_ansi_fg(fg_r, fg_g, fg_b);
        const char *ascii_bg = rgb_to_ansi_bg(r, g, b);

        const int written = snprintf(current_pos, remaining, "%s%s%c", ascii_fg, ascii_bg, ascii_char);
        if (written < 0 || (size_t)written >= remaining) {
          log_error("Buffer overflow (background)");
          exit(ASCIICHAT_ERR_BUFFER_ACCESS);
        }
        current_pos += written;
      } else {
        const char *ascii_fg = rgb_to_ansi_fg(r, g, b);

        const int written = snprintf(current_pos, remaining, "%s%c", ascii_fg, ascii_char);
        if (written < 0 || (size_t)written >= remaining) {
          log_error("Buffer overflow (foreground)");
          exit(ASCIICHAT_ERR_BUFFER_ACCESS);
        }
        current_pos += written;
      }
    }

    // Write reset + optional newline
    const size_t remaining = buffer_end - current_pos;
    if (remaining < 8) {
      log_error("Buffer overflow during reset");
      exit(ASCIICHAT_ERR_BUFFER_ACCESS);
    }

    memcpy(current_pos, "\033[0m", reset_len);
    current_pos += reset_len;

    if (y != h - 1) {
      *current_pos++ = '\n';
    }
  }

  *current_pos = '\0';
  return lines;
}

// RGB to ANSI color conversion functions
char *rgb_to_ansi_fg(int r, int g, int b) {
  static char color_code[32];
  snprintf(color_code, sizeof(color_code), "\033[38;2;%d;%d;%dm", r, g, b);
  return color_code;
}

char *rgb_to_ansi_bg(int r, int g, int b) {
  static char color_code[32];
  snprintf(color_code, sizeof(color_code), "\033[48;2;%d;%d;%dm", r, g, b);
  return color_code;
}

void rgb_to_ansi_8bit(int r, int g, int b, int *fg_code, int *bg_code) {
  // Convert RGB to 8-bit color code (216 color cube + 24 grayscale)
  if (r == g && g == b) {
    // Grayscale
    if (r < 8) {
      *fg_code = 16;
    } else if (r > 248) {
      *fg_code = 231;
    } else {
      *fg_code = 232 + (r - 8) / 10;
    }
  } else {
    // Color cube: 16 + 36*r + 6*g + b where r,g,b are 0-5
    int r_level = (r * 5) / 255;
    int g_level = (g * 5) / 255;
    int b_level = (b * 5) / 255;
    *fg_code = 16 + 36 * r_level + 6 * g_level + b_level;
  }
  *bg_code = *fg_code; // Same logic for background
}
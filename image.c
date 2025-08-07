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
  image_resize_interpolation(s, d);
}

// Optimized interpolation function with better integer arithmetic and memory
// access
void image_resize_interpolation(const image_t *source, image_t *dest) {
  const int src_w = source->w;
  const int src_h = source->h;
  const int dst_w = dest->w;
  const int dst_h = dest->h;

  // Use fixed-point arithmetic for better performance
  const uint32_t x_ratio = ((src_w << 16) / dst_w) + 1;
  const uint32_t y_ratio = ((src_h << 16) / dst_h) + 1;

  const rgb_t *src_pixels = source->pixels;
  rgb_t *dst_pixels = dest->pixels;

  for (int y = 0; y < dst_h; y++) {
    const uint32_t src_y = (y * y_ratio) >> 16;
    const rgb_t *src_row = src_pixels + (src_y * src_w);
    rgb_t *dst_row = dst_pixels + (y * dst_w);

    for (int x = 0; x < dst_w; x++) {
      const uint32_t src_x = (x * x_ratio) >> 16;
      dst_row[x] = src_row[src_x];
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

// Colored ASCII art printing function with quantization
char *image_print_colored(const image_t *p) {
  const int h = p->h;
  const int w = p->w;

  // Allocate a reasonable buffer for the ASCII output
  // Maximum expected size: height * width * (chars per pixel + color codes) + newlines
  size_t lines_size = h * w * 40 + h * 2 + 1024; // Conservative estimate with margin
  char *lines;
  SAFE_MALLOC(lines, lines_size, char *);

  // Now we can use the lines buffer safely in this function.
  // No buffer overflows and don't worry about snprintf's return value.

  const rgb_t *pix = p->pixels;

  char *current_pos = lines;
  char *buffer_end = lines + lines_size - 1; // Reserve space for null terminator

  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w; x++) {
      const rgb_t pixel = pix[row_offset + x];
      int r = pixel.r, g = pixel.g, b = pixel.b;
      const int luminance = RED[r] + GREEN[g] + BLUE[b];
      const char ascii_char = luminance_palette[luminance];

      // Quantize colors.
      // quantize_color(&r, &g, &b, 8);

      // Calculate remaining buffer space
      size_t remaining = buffer_end - current_pos;
      if (remaining < 64) { // Safety margin for longest possible ANSI sequence
        log_error("Buffer overflow prevented in color processing");
        exit(ASCIICHAT_ERR_BUFFER_ACCESS);
      }

      if (opt_background_color) {
        // Choose contrasting foreground color (black or white) based on
        // luminance
        int fg_r, fg_g, fg_b;
        if (luminance < 127) { // Dim background, use black text
          fg_r = fg_g = fg_b = 0;
        } else { // Bright background, use white text
          fg_r = fg_g = fg_b = 255;
        }

        const char *ascii_fg = rgb_to_ansi_fg(fg_r, fg_g, fg_b);
        const char *ascii_bg = rgb_to_ansi_bg(r, g, b);

        const int written = snprintf(current_pos, remaining, "%s%s%c", ascii_fg, ascii_bg, ascii_char);
        if (written < 0 || (size_t)written >= remaining) {
          log_error("Buffer overflow prevented in color processing (background)");
          exit(ASCIICHAT_ERR_BUFFER_ACCESS);
        }
        current_pos += written;

      } else {
        const char *ascii_fg = rgb_to_ansi_fg(r, g, b);

        const int written = snprintf(current_pos, remaining, "%s%c", ascii_fg, ascii_char);
        if (written < 0 || (size_t)written >= remaining) {
          log_error("Buffer overflow prevented in color processing (foreground)");
          exit(ASCIICHAT_ERR_BUFFER_ACCESS);
        }
        current_pos += written;
      }
    }

    // Add reset sequence; newline only when this is not the last row
    size_t remaining = buffer_end - current_pos;
    if (remaining < 8) { // Need space for reset + newline
      log_error("Buffer overflow prevented in reset sequence");
      exit(ASCIICHAT_ERR_BUFFER_ACCESS);
    }
    memcpy(current_pos, "\033[0m", 4);
    current_pos += 4;
    if (y != h - 1) {
      *current_pos++ = '\n';
    }
  }

  // Add null terminator making it a valid C string.
  *current_pos = '\0';

  return lines;
}

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "common.h"
#include "image.h"
#include "ascii.h"
#include "simd/ascii_simd.h"
#include "image2ascii/simd/common.h"
#include "ansi_fast.h"
#include "options.h"
#include "round.h"
#include "buffer_pool.h" // For buffer pool allocation functions
#include "palette.h"

// NOTE: luminance_palette is now passed as parameter to functions instead of using global cache

// ansi_fast functions are declared in ansi_fast.h (already included)

image_t *image_new(size_t width, size_t height) {
  image_t *p;

  SAFE_MALLOC(p, sizeof(image_t), image_t *);

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

// Buffer pool allocation for video pipeline (consistent memory management)
image_t *image_new_from_pool(size_t width, size_t height) {
  if (width == 0 || height == 0) {
    log_error("image_new_from_pool: invalid dimensions %zux%zu", width, height);
    return NULL;
  }

  if (width > IMAGE_MAX_WIDTH || height > IMAGE_MAX_HEIGHT) {
    log_error("image_new_from_pool: dimensions %zux%zu exceed maximum %ux%u", width, height, IMAGE_MAX_WIDTH,
              IMAGE_MAX_HEIGHT);
    return NULL;
  }

  // Calculate total allocation size (structure + pixel data in single buffer)
  size_t pixel_count = width * height;
  size_t pixels_size = pixel_count * sizeof(rgb_t);
  size_t total_size = sizeof(image_t) + pixels_size;

  // Allocate from buffer pool as single contiguous block
  void *buffer = buffer_pool_alloc(total_size);
  if (!buffer) {
    log_error("image_new_from_pool: buffer pool allocation failed for %zu bytes (%zux%zu)", total_size, width, height);
    return NULL;
  }

  // Set up image structure at start of buffer
  image_t *image = (image_t *)buffer;
  image->w = (int)width;
  image->h = (int)height;
  // Pixel data immediately follows the image structure
  image->pixels = (rgb_t *)((uint8_t *)buffer + sizeof(image_t));

  return image;
}

void image_destroy_to_pool(image_t *image) {
  if (!image) {
    log_error("image_destroy_to_pool: image is NULL");
    return;
  }

  // Calculate original allocation size for proper buffer pool free
  size_t pixel_count = (size_t)image->w * (size_t)image->h;
  size_t pixels_size = pixel_count * sizeof(rgb_t);
  size_t total_size = sizeof(image_t) + pixels_size;

  // Free the entire contiguous buffer back to pool
  buffer_pool_free(image, total_size);
  // Note: Don't set image = NULL here since caller owns the pointer
}

void image_clear(image_t *p) {
  if (!p || !p->pixels) {
    log_error("image_clear: p or p->pixels is NULL");
    return;
  }
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

// Note: luminance_palette is now handled by ascii_simd.c via g_ascii_cache.luminance_palette

void precalc_rgb_palettes(const float red, const float green, const float blue) {
  for (int n = 0; n < ASCII_LUMINANCE_LEVELS; ++n) {
    RED[n] = ((float)n) * red;
    GREEN[n] = ((float)n) * green;
    BLUE[n] = ((float)n) * blue;
    GRAY[n] = ((float)n);
  }
}

// Optimized image printing with better memory access patterns
char *image_print(const image_t *p, const char *palette) {
  if (!p || !p->pixels || !palette) {
    log_error("image_print: p or p->pixels or palette is NULL");
    // exit(ASCIICHAT_ERR_INVALID_PARAM);
    return NULL;
  }

  const int h = p->h;
  const int w = p->w;

  if (h <= 0 || w <= 0) {
    log_error("image_print: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Get UTF-8 character cache for proper multi-byte character support
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache for scalar rendering");
    return NULL;
  }

  // Character index ramp is now part of UTF-8 cache - no separate cache needed

  // Need space for h rows with UTF-8 characters, plus h-1 newlines, plus null terminator
  const size_t max_char_bytes = 4; // Max UTF-8 character size
  const ssize_t len = (ssize_t)h * ((ssize_t)w * max_char_bytes + 1);

  const rgb_t *pix = p->pixels;

  char *lines;
  SAFE_MALLOC(lines, len * sizeof(char), char *);

  // Use outbuf_t for efficient UTF-8 RLE emission (same as SIMD renderers)
  outbuf_t ob = {0};
  ob.cap = (size_t)h * ((size_t)w * 4 + 1); // 4 = max UTF-8 char bytes
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf) {
    free(lines);
    log_error("Failed to allocate output buffer for scalar rendering");
    return NULL;
  }

  // Process pixels with UTF-8 RLE emission (same approach as SIMD)
  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w;) {
      const rgb_t pixel = pix[row_offset + x];
      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      const int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      int safe_luminance = (luminance > 255) ? 255 : luminance;
      uint8_t luma_idx = safe_luminance >> 2;                   // 0-63 index (same as SIMD)
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx]; // Map to character index (same as SIMD)

      // Use same 64-entry cache as SIMD for consistency
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

      // Find run length for same character (RLE optimization)
      int j = x + 1;
      while (j < w) {
        const rgb_t next_pixel = pix[row_offset + j];
        const int next_luminance = (77 * next_pixel.r + 150 * next_pixel.g + 29 * next_pixel.b + 128) >> 8;
        int next_safe_luminance = (next_luminance > 255) ? 255 : next_luminance;
        uint8_t next_luma_idx = next_safe_luminance >> 2;                   // 0-63 index (same as SIMD)
        uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx]; // Map to character index (same as SIMD)
        if (next_char_idx != char_idx)
          break;
        j++;
      }
      uint32_t run = (uint32_t)(j - x);

      // Emit UTF-8 character with RLE (same as SIMD)
      ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
      if (rep_is_profitable(run)) {
        emit_rep(&ob, run - 1);
      } else {
        for (uint32_t k = 1; k < run; k++) {
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        }
      }
      x = j;
    }

    // Add newline between rows (except last row)
    if (y != h - 1) {
      ob_putc(&ob, '\n');
    }
  }

  ob_term(&ob);
  free(lines); // Free the old buffer
  return ob.buf;
}

// Color quantization to reduce frame size and improve performance
void quantize_color(int *r, int *g, int *b, int levels) {
  if (!r || !g || !b) {
    log_error("quantize_color: r, g, or b is NULL");
    return;
  }

  if (levels <= 0) {
    log_error("quantize_color: levels must be positive, got %d", levels);
    return;
  }

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
char *image_print_color(const image_t *p, const char *palette) {
  if (!p || !p->pixels || !palette) {
    log_error("p or p->pixels or palette is NULL");
    // exit(ASCIICHAT_ERR_INVALID_PARAM);
    return NULL;
  }

  // Get UTF-8 character cache for proper multi-byte character support
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache for scalar color rendering");
    return NULL;
  }

  const int h = p->h;
  const int w = p->w;

  // Constants for ANSI escape codes (using exact sizes from ansi_fast.c)
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
  const size_t bytes_per_pixel = 1 + max_fg_ansi + max_bg_ansi; // Conservative estimate for max possible

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
  // char *current_pos = lines;
  // const char *buffer_end = lines + lines_size - 1; // reserve space for '\0'

  // Initialize the optimized RLE context for color sequence caching
  // Note: This function should be called via image_print_with_capabilities() for proper per-client rendering
  ansi_rle_context_t rle_ctx;
  ansi_color_mode_t color_mode = ANSI_MODE_FOREGROUND; // Default to foreground-only for legacy usage
  ansi_rle_init(&rle_ctx, lines, lines_size, color_mode);

  // Process each pixel using the optimized RLE context
  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w; x++) {
      const rgb_t pixel = pix[row_offset + x];
      int r = pixel.r, g = pixel.g, b = pixel.b;
      const int luminance = RED[r] + GREEN[g] + BLUE[b];

      // Use UTF-8 character cache for proper character selection
      int safe_luminance = (luminance > 255) ? 255 : luminance;
      const utf8_char_t *char_info = &utf8_cache->cache[safe_luminance];

      // For RLE, we need to pass the first byte of the UTF-8 character
      // Note: RLE system may need updates for full UTF-8 support
      const char ascii_char = char_info->utf8_bytes[0];

      // Legacy function - always use foreground mode with RLE optimization
      // For proper per-client background support, use image_print_with_capabilities() instead
      ansi_rle_add_pixel(&rle_ctx, r, g, b, ascii_char);
    }

    // Add newline between rows (except last row)
    if (y != h - 1) {
      if (rle_ctx.length < rle_ctx.capacity - 1) {
        rle_ctx.buffer[rle_ctx.length++] = '\n';
      }
    }
  }

  // Finalize the RLE output with reset sequence
  ansi_rle_finish(&rle_ctx);
  return lines;
}

// RGB to ANSI color conversion functions
char *rgb_to_ansi_fg(int r, int g, int b) {
  _Thread_local static char color_code[20]; // \033[38;2;255;255;255m + \0 = 20 bytes max
  snprintf(color_code, sizeof(color_code), "\033[38;2;%d;%d;%dm", r, g, b);
  return color_code;
}

char *rgb_to_ansi_bg(int r, int g, int b) {
  _Thread_local static char color_code[20]; // \033[48;2;255;255;255m + \0 = 20 bytes max
  snprintf(color_code, sizeof(color_code), "\033[48;2;%d;%d;%dm", r, g, b);
  return color_code;
}

void rgb_to_ansi_8bit(int r, int g, int b, int *fg_code, int *bg_code) {
  if (!fg_code || !bg_code) {
    log_error("rgb_to_ansi_8bit: fg_code or bg_code is NULL");
    return;
  }

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

// Capability-aware image printing function
char *image_print_with_capabilities(const image_t *image, const terminal_capabilities_t *caps, const char *palette,
                                    const char luminance_palette[256] __attribute__((unused))) {
  if (!image || !image->pixels || !caps || !palette) {
    log_error("Invalid parameters for image_print_with_capabilities");
    // exit(ASCIICHAT_ERR_INVALID_PARAM);
    return NULL;
  }

  // Handle half-block mode first (requires NEON)
  if (caps->render_mode == RENDER_MODE_HALF_BLOCK) {
#ifdef SIMD_SUPPORT_NEON
    // Use NEON half-block renderer
    const uint8_t *rgb_data = (const uint8_t *)image->pixels;
    return rgb_to_truecolor_halfblocks_neon(rgb_data, image->w, image->h, 0);
#else
    log_error("Half-block mode requires NEON support (ARM architecture)");
    return NULL;
#endif
  }

  // Standard color modes
  bool use_background_mode = (caps->render_mode == RENDER_MODE_BACKGROUND);

  char *result = NULL;

  // Choose the appropriate printing method based on terminal capabilities
  switch (caps->color_level) {
  case TERM_COLOR_TRUECOLOR:
    // Use existing truecolor printing function with client's palette
#ifdef SIMD_SUPPORT
    result = image_print_color_simd((image_t *)image, use_background_mode, false, palette);
#else
    result = image_print_color(image, palette);
#endif
    break;

  case TERM_COLOR_256:
#ifdef SIMD_SUPPORT
    result = image_print_color_simd((image_t *)image, use_background_mode, true, palette);
#else
    // Use 256-color conversion
    result = image_print_256color(image, palette);
#endif
    break;

  case TERM_COLOR_16:
    // Use 16-color conversion with Floyd-Steinberg dithering for better quality
    result = image_print_16color_dithered_with_background(image, use_background_mode, palette);
    break;

  case TERM_COLOR_NONE:
  default:
    // Use grayscale/monochrome conversion with client's custom palette
#ifdef SIMD_SUPPORT
    result = image_print_simd((image_t *)image, palette);
#else
    result = image_print(image, palette);
#endif
    break;
  }

  // Note: Background mode is now handled via capabilities rather than global state
  return result;
}

// 256-color image printing function using existing SIMD optimized code
char *image_print_256color(const image_t *p, const char *palette) {
  if (!p || !p->pixels || !palette) {
    log_error("image_print_256color: p or p->pixels or palette is NULL");
    // exit(ASCIICHAT_ERR_INVALID_PARAM);
    return NULL;
  }

  // Use the existing optimized SIMD colored printing (no background for 256-color mode)
#ifdef SIMD_SUPPORT
  char *result = image_print_color_simd((image_t *)p, false, true, palette);
#else
  char *result = image_print_color(p, palette);
#endif

  return result;
}

// 16-color image printing function using ansi_fast color conversion
char *image_print_16color(const image_t *p, const char *palette) {
  if (!p || !p->pixels || !palette) {
    log_error("image_print_16color: p or p->pixels or palette is NULL");
    // exit(ASCIICHAT_ERR_INVALID_PARAM);
    return NULL;
  }

  int h = p->h;
  int w = p->w;

  if (h <= 0 || w <= 0) {
    log_error("image_print_16color: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Calculate buffer size (smaller than 256-color due to shorter ANSI sequences)
  size_t buffer_size = (size_t)h * (size_t)w * 12 + (size_t)h; // Space for ANSI codes + newlines
  char *buffer;
  SAFE_MALLOC(buffer, buffer_size, char *);
  if (!buffer) {
    log_error("image_print_16color: failed to allocate buffer");
    return NULL;
  }

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_t pixel = p->pixels[y * w + x];

      // Convert RGB to 16-color index and generate ANSI sequence
      uint8_t color_index = rgb_to_16color(pixel.r, pixel.g, pixel.b);
      ptr = append_16color_fg(ptr, color_index);

      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use UTF-8 cache which contains character index ramp
      utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
      if (!utf8_cache) {
        log_error("Failed to get UTF-8 cache");
        return NULL;
      }

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      int safe_luminance = (luminance > 255) ? 255 : luminance;
      uint8_t luma_idx = safe_luminance >> 2;                   // 0-63 index (same as SIMD)
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx]; // Map to character index (same as SIMD)

      // Use direct palette character lookup (same as SIMD would do)
      char ascii_char = palette[char_idx];
      const utf8_char_t *char_info = &utf8_cache->cache[(unsigned char)ascii_char];

      if (char_info) {
        // Copy UTF-8 character bytes
        for (int byte_idx = 0; byte_idx < char_info->byte_len; byte_idx++) {
          *ptr++ = char_info->utf8_bytes[byte_idx];
        }
      } else {
        // Fallback to simple ASCII if UTF-8 cache fails
        size_t palette_len = strlen(palette);
        int palette_index = (luminance * ((int)palette_len - 1) + 127) / 255;
        *ptr++ = palette[palette_index];
      }
    }

    // Add reset and newline at end of each row
    strcpy(ptr, reset_code);
    ptr += strlen(reset_code);
    if (y < h - 1) {
      *ptr++ = '\n';
    }
  }

  *ptr = '\0';
  return buffer;
}

// 16-color image printing with Floyd-Steinberg dithering
char *image_print_16color_dithered(const image_t *p, const char *palette) {
  if (!p || !p->pixels || !palette) {
    log_error("image_print_16color_dithered: p or p->pixels or palette is NULL");
    // exit(ASCIICHAT_ERR_INVALID_PARAM);
    return NULL;
  }

  int h = p->h;
  int w = p->w;

  if (h <= 0 || w <= 0) {
    log_error("image_print_16color_dithered: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Allocate error buffer for Floyd-Steinberg dithering
  size_t pixel_count = (size_t)h * (size_t)w;
  rgb_error_t *error_buffer;
  SAFE_CALLOC(error_buffer, pixel_count, sizeof(rgb_error_t), rgb_error_t *);
  if (!error_buffer) {
    log_error("image_print_16color_dithered: failed to allocate error buffer");
    return NULL;
  }

  // Calculate buffer size (same as non-dithered version)
  size_t buffer_size = (size_t)h * (size_t)w * 12 + (size_t)h; // Space for ANSI codes + newlines
  char *buffer;
  SAFE_MALLOC(buffer, buffer_size, char *);
  if (!buffer) {
    log_error("image_print_16color_dithered: failed to allocate buffer");
    free(error_buffer);
    return NULL;
  }

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_t pixel = p->pixels[y * w + x];

      // Convert RGB to 16-color index using dithering
      uint8_t color_index = rgb_to_16color_dithered(pixel.r, pixel.g, pixel.b, x, y, w, h, error_buffer);
      ptr = append_16color_fg(ptr, color_index);

      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use UTF-8 cache which contains character index ramp
      utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
      if (!utf8_cache) {
        log_error("Failed to get UTF-8 cache");
        return NULL;
      }

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      int safe_luminance = (luminance > 255) ? 255 : luminance;
      uint8_t luma_idx = safe_luminance >> 2;                   // 0-63 index (same as SIMD)
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx]; // Map to character index (same as SIMD)

      // Use direct palette character lookup (same as SIMD would do)
      char ascii_char = palette[char_idx];
      const utf8_char_t *char_info = &utf8_cache->cache[(unsigned char)ascii_char];

      if (char_info) {
        // Copy UTF-8 character bytes
        for (int byte_idx = 0; byte_idx < char_info->byte_len; byte_idx++) {
          *ptr++ = char_info->utf8_bytes[byte_idx];
        }
      } else {
        // Fallback to simple ASCII if UTF-8 cache fails
        size_t palette_len = strlen(palette);
        int palette_index = (luminance * ((int)palette_len - 1) + 127) / 255;
        *ptr++ = palette[palette_index];
      }
    }

    // Add reset and newline at end of each row
    strcpy(ptr, reset_code);
    ptr += strlen(reset_code);
    if (y < h - 1) {
      *ptr++ = '\n';
    }
  }

  *ptr = '\0';
  free(error_buffer); // Clean up error buffer
  return buffer;
}

// 16-color image printing with Floyd-Steinberg dithering and background mode support
char *image_print_16color_dithered_with_background(const image_t *p, bool use_background, const char *palette) {
  if (!p || !p->pixels || !palette) {
    log_error("image_print_16color_dithered_with_background: p or p->pixels or palette is NULL");
    // exit(ASCIICHAT_ERR_INVALID_PARAM);
    return NULL;
  }

  int h = p->h;
  int w = p->w;

  if (h <= 0 || w <= 0) {
    log_error("image_print_16color_dithered_with_background: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Allocate error buffer for Floyd-Steinberg dithering
  size_t pixel_count = (size_t)h * (size_t)w;
  rgb_error_t *error_buffer;
  SAFE_CALLOC(error_buffer, pixel_count, sizeof(rgb_error_t), rgb_error_t *);
  if (!error_buffer) {
    log_error("image_print_16color_dithered_with_background: failed to allocate error buffer");
    return NULL;
  }

  // Calculate buffer size (larger for background mode due to more ANSI sequences)
  size_t buffer_size =
      (size_t)h * (size_t)w * (use_background ? 24 : 12) + (size_t)h; // Space for ANSI codes + newlines
  char *buffer;
  SAFE_MALLOC(buffer, buffer_size, char *);
  if (!buffer) {
    log_error("image_print_16color_dithered_with_background: failed to allocate buffer");
    free(error_buffer);
    return NULL;
  }

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_t pixel = p->pixels[y * w + x];

      // Convert RGB to 16-color index using dithering
      uint8_t color_index = rgb_to_16color_dithered(pixel.r, pixel.g, pixel.b, x, y, w, h, error_buffer);

      if (use_background) {
        // Background mode: use contrasting foreground on background color
        uint8_t bg_r, bg_g, bg_b;
        get_16color_rgb(color_index, &bg_r, &bg_g, &bg_b);

        // Calculate luminance to choose contrasting foreground
        int bg_luminance = (bg_r * 77 + bg_g * 150 + bg_b * 29) / 256;
        uint8_t fg_color = (bg_luminance < 127) ? 15 : 0; // White on dark, black on bright

        ptr = append_16color_bg(ptr, color_index); // Set background to pixel color
        ptr = append_16color_fg(ptr, fg_color);    // Set contrasting foreground
      } else {
        // Foreground mode: set foreground to pixel color
        ptr = append_16color_fg(ptr, color_index);
      }

      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use UTF-8 cache which contains character index ramp
      utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
      if (!utf8_cache) {
        log_error("Failed to get UTF-8 cache");
        return NULL;
      }

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      int safe_luminance = (luminance > 255) ? 255 : luminance;
      uint8_t luma_idx = safe_luminance >> 2;                   // 0-63 index (same as SIMD)
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx]; // Map to character index (same as SIMD)

      // Use direct palette character lookup (same as SIMD would do)
      char ascii_char = palette[char_idx];
      const utf8_char_t *char_info = &utf8_cache->cache[(unsigned char)ascii_char];

      if (char_info) {
        // Copy UTF-8 character bytes
        for (int byte_idx = 0; byte_idx < char_info->byte_len; byte_idx++) {
          *ptr++ = char_info->utf8_bytes[byte_idx];
        }
      } else {
        // Fallback to simple ASCII if UTF-8 cache fails
        size_t palette_len = strlen(palette);
        int palette_index = (luminance * ((int)palette_len - 1) + 127) / 255;
        *ptr++ = palette[palette_index];
      }
    }

    // Add reset and newline at end of each row
    strcpy(ptr, reset_code);
    ptr += strlen(reset_code);
    if (y < h - 1) {
      *ptr++ = '\n';
    }
  }

  *ptr = '\0';
  free(error_buffer); // Clean up error buffer
  return buffer;
}

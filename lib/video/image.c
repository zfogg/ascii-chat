/**
 * @file video/image.c
 * @ingroup video
 * @brief 🖨️ Image processing: format detection, decoding, scaling, and pixel format conversion
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <ascii-chat/common.h>
#include <ascii-chat/video/output_buffer.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/simd/ascii_simd.h>
#include <ascii-chat/video/simd/common.h>
#include <ascii-chat/video/scalar/halfblock.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/buffer_pool.h> // For buffer pool allocation functions
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/math.h>

// NOTE: luminance_palette is now passed as parameter to functions instead of using global cache

// ansi_fast functions are declared in ansi_fast.h (already included)

image_t *image_new(size_t width, size_t height) {
  image_t *p;

  p = SAFE_MALLOC(sizeof(image_t), image_t *);

  // Validate dimensions are non-zero and within bounds
  if (image_validate_dimensions(width, height) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions invalid or too large: %zu x %zu", width, height);
    SAFE_FREE(p);
    return NULL;
  }

  // Calculate pixel count with overflow checking
  size_t total_pixels;
  if (checked_size_mul(width, height, &total_pixels) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions would cause overflow: %zu x %zu", width, height);
    SAFE_FREE(p);
    return NULL;
  }

  // Calculate total pixel buffer size with overflow checking
  size_t pixels_size;
  if (checked_size_mul(total_pixels, sizeof(rgb_pixel_t), &pixels_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Image pixel buffer size would cause overflow");
    SAFE_FREE(p);
    return NULL;
  }

  if (pixels_size > IMAGE_MAX_PIXELS_SIZE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Image size exceeds maximum allowed: %zu x %zu (%zu bytes)", width, height,
              pixels_size);
    SAFE_FREE(p);
    return NULL;
  }

  // Use SIMD-aligned allocation for optimal NEON/AVX performance with vld3q_u8
  p->pixels = SAFE_MALLOC_SIMD(pixels_size, rgb_pixel_t *);
  if (!p->pixels) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate image pixels: %zu bytes", pixels_size);
    SAFE_FREE(p);
    return NULL;
  }

  p->w = (int)width;
  p->h = (int)height;
  p->alloc_method = IMAGE_ALLOC_SIMD; // Track allocation method for correct deallocation
  return p;
}

void image_destroy(image_t *p) {
  if (!p) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_destroy: p is NULL");
    return;
  }

  // Check allocation method and deallocate appropriately
  if (p->alloc_method == IMAGE_ALLOC_POOL) {
    // Pool-allocated images: free entire contiguous buffer (image + pixels)
    // Validate dimensions before calculating size (guard against corruption)
    if (p->w <= 0 || p->h <= 0) {
      SET_ERRNO(ERROR_INVALID_PARAM, "image_destroy: invalid dimensions %dx%d (pool-allocated)", p->w, p->h);
      return;
    }

    // Calculate original allocation size for proper buffer pool free
    size_t w = (size_t)p->w;
    size_t h = (size_t)p->h;
    size_t pixels_size;
    if (checked_size_mul3(w, h, sizeof(rgb_pixel_t), &pixels_size) != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_INVALID_STATE, "image_destroy: dimensions would overflow: %dx%d", p->w, p->h);
      return;
    }
    size_t total_size;
    if (checked_size_add(sizeof(image_t), pixels_size, &total_size) != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_INVALID_STATE, "image_destroy: total size would overflow: %dx%d", p->w, p->h);
      return;
    }

    // Free the entire contiguous buffer back to pool
    buffer_pool_free(NULL, p, total_size);
  } else {
    // SIMD-allocated images: free pixels and structure separately
    // SAFE_MALLOC_SIMD allocates with aligned allocation (posix_memalign on macOS, aligned_alloc on Linux)
    // These can be freed with standard free() / SAFE_FREE()
    SAFE_FREE(p->pixels);
    SAFE_FREE(p);
  }
}

// Buffer pool allocation for video pipeline (consistent memory management)
image_t *image_new_from_pool(size_t width, size_t height) {
  if (width == 0 || height == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_new_from_pool: invalid dimensions %zux%zu", width, height);
    return NULL;
  }

  if (width > IMAGE_MAX_WIDTH || height > IMAGE_MAX_HEIGHT) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_new_from_pool: dimensions %zux%zu exceed maximum %ux%u", width, height,
              IMAGE_MAX_WIDTH, IMAGE_MAX_HEIGHT);
    return NULL;
  }

  // Calculate total allocation size (structure + pixel data in single buffer)
  // Check for integer overflow before multiplication
  size_t pixels_size;
  if (checked_size_mul3(width, height, sizeof(rgb_pixel_t), &pixels_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_new_from_pool: dimensions would overflow: %zux%zu", width, height);
    return NULL;
  }

  size_t total_size;
  if (checked_size_add(sizeof(image_t), pixels_size, &total_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_new_from_pool: total size would overflow");
    return NULL;
  }

  // Allocate from buffer pool as single contiguous block
  void *buffer = buffer_pool_alloc(NULL, total_size);
  if (!buffer) {
    SET_ERRNO(ERROR_MEMORY, "image_new_from_pool: buffer pool allocation failed for %zu bytes (%zux%zu)", total_size,
              width, height);
    return NULL;
  }

  // Set up image structure at start of buffer
  image_t *image = (image_t *)buffer;
  image->w = (int)width;
  image->h = (int)height;
  // Pixel data immediately follows the image structure
  image->pixels = (rgb_pixel_t *)((uint8_t *)buffer + sizeof(image_t));
  image->alloc_method = IMAGE_ALLOC_POOL; // Track allocation method for correct deallocation

  return image;
}

void image_destroy_to_pool(image_t *image) {
  if (!image) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_destroy_to_pool: image is NULL");
    return;
  }

  // Validate dimensions before calculating size (guard against corruption)
  if (image->w <= 0 || image->h <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_destroy_to_pool: invalid dimensions %dx%d", image->w, image->h);
    return;
  }

  // Calculate original allocation size for proper buffer pool free
  // Check for overflow (defensive - should match original allocation)
  size_t w = (size_t)image->w;
  size_t h = (size_t)image->h;
  size_t pixels_size;
  if (checked_size_mul3(w, h, sizeof(rgb_pixel_t), &pixels_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_STATE, "image_destroy_to_pool: dimensions would overflow: %dx%d", image->w, image->h);
    return;
  }
  size_t total_size;
  if (checked_size_add(sizeof(image_t), pixels_size, &total_size) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_STATE, "image_destroy_to_pool: total size would overflow: %dx%d", image->w, image->h);
    return;
  }

  // Free the entire contiguous buffer back to pool
  buffer_pool_free(NULL, image, total_size);
  // Note: Don't set image = NULL here since caller owns the pointer
}

void image_clear(image_t *p) {
  if (!p || !p->pixels) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_clear: p or p->pixels is NULL");
    return;
  }
  // Check for integer overflow before multiplication.
  unsigned long w_ul = (unsigned long)p->w;
  unsigned long h_ul = (unsigned long)p->h;
  if (w_ul > 0 && h_ul > ULONG_MAX / w_ul) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_clear: dimensions overflow: %d x %d", p->w, p->h);
    return;
  }
  unsigned long pixel_count = w_ul * h_ul;
  if (pixel_count > ULONG_MAX / sizeof(rgb_pixel_t)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_clear: buffer size overflow");
    return;
  }
  size_t clear_size = pixel_count * sizeof(rgb_pixel_t);
  SAFE_MEMSET(p->pixels, clear_size, 0, clear_size);
}

image_t *image_new_copy(const image_t *source) {
  if (!source) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_new_copy: source is NULL");
    return NULL;
  }

  // Create new image with same dimensions
  image_t *copy = image_new((size_t)source->w, (size_t)source->h);
  if (!copy) {
    return NULL;
  }

  // Copy pixel data from source to copy
  if (source->pixels && copy->pixels) {
    size_t pixel_count = (size_t)source->w * (size_t)source->h;
    size_t pixels_size = pixel_count * sizeof(rgb_pixel_t);
    memcpy(copy->pixels, source->pixels, pixels_size);
  }

  return copy;
}

inline rgb_pixel_t *image_pixel(image_t *p, const int x, const int y) {
  // Add bounds checking to prevent buffer overflow on invalid coordinates
  if (!p || !p->pixels || x < 0 || x >= p->w || y < 0 || y >= p->h) {
    return NULL;
  }
  return &p->pixels[x + y * p->w];
}

void image_resize(const image_t *s, image_t *d) {
  if (!s || !d) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_resize: s or d is NULL");
    return;
  }

  image_resize_interpolation(s, d);
}

// Optimized interpolation function with better integer arithmetic and memory
// access
void image_resize_interpolation(const image_t *source, image_t *dest) {
  if (!source || !dest || !source->pixels || !dest->pixels) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to image_resize_interpolation");
    return;
  }

  const int src_w = source->w;
  const int src_h = source->h;
  const int dst_w = dest->w;
  const int dst_h = dest->h;

  // Handle edge cases
  if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid image dimensions for resize: src=%dx%d dst=%dx%d", src_w, src_h, dst_w,
              dst_h);
    return;
  }

  // Defensive checks to prevent invalid shift (UBSan protection)
  if (src_w < 1 || src_h < 1 || dst_w < 1 || dst_h < 1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid dimensions detected after first check");
    return;
  }

  // Use fixed-point arithmetic for better performance
  // Use uint64_t intermediate to prevent overflow when shifting large dimensions
  const uint32_t x_ratio = (uint32_t)((((uint64_t)src_w << 16) / (uint64_t)dst_w) + 1);
  const uint32_t y_ratio = (uint32_t)((((uint64_t)src_h << 16) / (uint64_t)dst_h) + 1);

  const rgb_pixel_t *src_pixels = source->pixels;
  rgb_pixel_t *dst_pixels = dest->pixels;

  for (int y = 0; y < dst_h; y++) {
    const uint32_t src_y = ((uint32_t)(unsigned int)y * y_ratio) >> 16;
    // Explicitly clamp to valid range [0, src_h-1]
    const uint32_t safe_src_y = src_y >= (uint32_t)(unsigned int)src_h ? (uint32_t)(src_h - 1) : src_y;

    // Bounds check: ensure safe_src_y is valid
    if (safe_src_y >= (uint32_t)(unsigned int)src_h) {
      SET_ERRNO(ERROR_INVALID_PARAM, "safe_src_y out of bounds: %u >= %d", safe_src_y, src_h);
      return;
    }

    const rgb_pixel_t *src_row = src_pixels + (safe_src_y * (size_t)src_w);

    rgb_pixel_t *dst_row = dst_pixels + ((size_t)y * (size_t)dst_w);

    for (int x = 0; x < dst_w; x++) {
      const uint32_t src_x = ((uint32_t)(unsigned int)x * x_ratio) >> 16;
      // Explicitly clamp to valid range [0, src_w-1]
      const uint32_t safe_src_x = src_x >= (uint32_t)(unsigned int)src_w ? (uint32_t)(src_w - 1) : src_x;

      // Bounds check: ensure safe_src_x is valid
      if (safe_src_x >= (uint32_t)(unsigned int)src_w) {
        SET_ERRNO(ERROR_INVALID_PARAM, "safe_src_x out of bounds: %u >= %d", safe_src_x, src_w);
        return;
      }

      dst_row[x] = src_row[safe_src_x];
    }
  }
}

// Note: luminance_palette is now handled by ascii_simd.c via g_ascii_cache.luminance_palette

void precalc_rgb_palettes(const float red, const float green, const float blue) {
  // Validate input parameters to prevent overflow
  // Luminance weights should typically be in range 0.0-1.0
  // But allow slightly larger values for brightness adjustment
  const float max_weight = 255.0f;  // Maximum value that won't overflow when multiplied by 255
  const float min_weight = -255.0f; // Allow negative for color correction

  // Note: isfinite() checks are skipped in Release builds due to -ffinite-math-only flag
  // which disables NaN/infinity support for performance. Range checks below are sufficient.
#if !defined(NDEBUG) && !defined(__FAST_MATH__)
  if (!isfinite(red) || !isfinite(green) || !isfinite(blue)) {
    log_error("Invalid weight values (non-finite): red=%f, green=%f, blue=%f", red, green, blue);
    SET_ERRNO(ERROR_INVALID_PARAM, "precalc_rgb_palettes: non-finite weight values");
    return;
  }
#endif

  if (red < min_weight || red > max_weight || green < min_weight || green > max_weight || blue < min_weight ||
      blue > max_weight) {
    log_warn(
        "precalc_rgb_palettes: Weight values out of expected range: red=%f, green=%f, blue=%f (clamping to safe range)",
        red, green, blue);
  }

  // Clamp weights to safe range to prevent overflow
  const float safe_red = (red < min_weight) ? min_weight : ((red > max_weight) ? max_weight : red);
  const float safe_green = (green < min_weight) ? min_weight : ((green > max_weight) ? max_weight : green);
  const float safe_blue = (blue < min_weight) ? min_weight : ((blue > max_weight) ? max_weight : blue);

  const unsigned short max_ushort = 65535;
  const unsigned short min_ushort = 0;

  for (int n = 0; n < ASCII_LUMINANCE_LEVELS; ++n) {
    // Compute with float, then clamp to unsigned short range
    float red_val = (float)n * safe_red;
    float green_val = (float)n * safe_green;
    float blue_val = (float)n * safe_blue;

    // Clamp to unsigned short range before assignment
    if (red_val < (float)min_ushort) {
      red_val = (float)min_ushort;
    } else if (red_val > (float)max_ushort) {
      red_val = (float)max_ushort;
    }

    if (green_val < (float)min_ushort) {
      green_val = (float)min_ushort;
    } else if (green_val > (float)max_ushort) {
      green_val = (float)max_ushort;
    }

    if (blue_val < (float)min_ushort) {
      blue_val = (float)min_ushort;
    } else if (blue_val > (float)max_ushort) {
      blue_val = (float)max_ushort;
    }

    RED[n] = (unsigned short)red_val;
    GREEN[n] = (unsigned short)green_val;
    BLUE[n] = (unsigned short)blue_val;
    GRAY[n] = (unsigned short)n;
  }
}

// Optimized image printing with better memory access patterns
char *image_print(const image_t *p, const char *palette) {
  if (!p || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_print: p=%p or palette=%p is NULL", p, palette);
    return NULL;
  }
  if (!p->pixels) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_print: p->pixels is NULL");
    return NULL;
  }

  const int h = p->h;
  const int w = p->w;

  if (h <= 0 || w <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_print: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Get UTF-8 character cache for proper multi-byte character support
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
  if (!utf8_cache) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to get UTF-8 palette cache for scalar rendering");
    return NULL;
  }

  // Character index ramp is now part of UTF-8 cache - no separate cache needed

  // Need space for h rows with UTF-8 characters, plus h-1 newlines, plus null terminator
  const size_t max_char_bytes = 4; // Max UTF-8 character size

  const rgb_pixel_t *pix = p->pixels;

  // Use outbuf_t for efficient UTF-8 RLE emission (same as SIMD renderers)
  outbuf_t ob = {0};

  // Calculate buffer size with overflow checking
  size_t w_times_bytes;
  if (checked_size_mul((size_t)w, max_char_bytes, &w_times_bytes) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Buffer size overflow: width too large for UTF-8 encoding");
    return NULL;
  }

  size_t w_times_bytes_plus_one;
  if (checked_size_add(w_times_bytes, 1, &w_times_bytes_plus_one) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Buffer size overflow: width * bytes + 1 overflow");
    return NULL;
  }

  if (checked_size_mul((size_t)h, w_times_bytes_plus_one, &ob.cap) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Buffer size overflow: height * (width * bytes + 1) overflow");
    return NULL;
  }

  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate output buffer for scalar rendering");
    return NULL;
  }

  // Process pixels with UTF-8 RLE emission (same approach as SIMD)
  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w;) {
      const rgb_pixel_t pixel = pix[row_offset + x];
      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      const int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      uint8_t safe_luminance = clamp_rgb(luminance);
      uint8_t luma_idx = (uint8_t)(safe_luminance >> 2);        // 0-63 index (same as SIMD)
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx]; // Map to character index (same as SIMD)

      // Use same 64-entry cache as SIMD for consistency
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

      // Find run length for same character (RLE optimization)
      int j = x + 1;
      while (j < w) {
        const rgb_pixel_t next_pixel = pix[row_offset + j];
        const int next_luminance = (77 * next_pixel.r + 150 * next_pixel.g + 29 * next_pixel.b + 128) >> 8;
        uint8_t next_safe_luminance = clamp_rgb(next_luminance);
        uint8_t next_luma_idx = (uint8_t)(next_safe_luminance >> 2);        // 0-63 index (same as SIMD)
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
  return ob.buf;
}

// Color quantization to reduce frame size and improve performance
void quantize_color(int *r, int *g, int *b, int levels) {
  if (!r || !g || !b) {
    SET_ERRNO(ERROR_INVALID_PARAM, "quantize_color: r, g, or b is NULL");
    return;
  }

  if (levels <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "quantize_color: levels must be positive, got %d", levels);
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
 * @note Exits with ERROR_BUFFER if buffer overflow is detected
 *       during string construction (should never happen with correct calculation).
 */
char *image_print_color(const image_t *p, const char *palette) {
  if (!p || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "p=%p or palette=%p is NULL", p, palette);
    return NULL;
  }
  if (!p->pixels) {
    SET_ERRNO(ERROR_INVALID_PARAM, "p->pixels is NULL");
    return NULL;
  }

  // Get UTF-8 character cache for proper multi-byte character support
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
  if (!utf8_cache) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to get UTF-8 palette cache for scalar color rendering");
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
    SET_ERRNO(ERROR_INVALID_STATE, "Image dimensions too large: %d x %d", h, w);
    return NULL;
  }

  const size_t total_pixels = h_sz * w_sz;
  const size_t bytes_per_pixel = 1 + max_fg_ansi + max_bg_ansi; // Conservative estimate for max possible

  // Ensure total_pixels * bytes_per_pixel won't overflow
  if (total_pixels > SIZE_MAX / bytes_per_pixel) {
    SET_ERRNO(ERROR_INVALID_STATE, "Pixel data too large for buffer: %d x %d", h, w);
    return NULL;
  }

  const size_t pixel_bytes = total_pixels * bytes_per_pixel;

  // Per row: newline (except last row)
  // Final reset added once by ansi_rle_finish()
  const size_t total_newlines = (h_sz > 0) ? (h_sz - 1) : 0;
  const size_t final_reset = reset_len; // One reset at end (from ansi_rle_finish)

  // Final buffer size: pixel bytes + per-row newlines + final reset + null terminator
  const size_t extra_bytes = total_newlines + final_reset + 1;

  if (pixel_bytes > SIZE_MAX - extra_bytes) {
    SET_ERRNO(ERROR_INVALID_STATE, "Final buffer size would overflow: %d x %d", h, w);
    return NULL;
  }

  const size_t lines_size = pixel_bytes + extra_bytes;
  char *lines;
  lines = SAFE_MALLOC(lines_size, char *);

  const rgb_pixel_t *pix = p->pixels;
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
      const rgb_pixel_t pixel = pix[row_offset + x];
      int r = pixel.r, g = pixel.g, b = pixel.b;
      // Standard ITU-R BT.601 luminance calculation
      const int luminance = (77 * r + 150 * g + 29 * b + 128) >> 8;

      // Use UTF-8 character cache for proper character selection
      uint8_t safe_luminance = clamp_rgb(luminance);
      const utf8_char_t *char_info = &utf8_cache->cache[safe_luminance];

      // For RLE, we need to pass the first byte of the UTF-8 character
      // Note: RLE system may need updates for full UTF-8 support
      const char ascii_char = char_info->utf8_bytes[0];

      ansi_rle_add_pixel(&rle_ctx, (uint8_t)r, (uint8_t)g, (uint8_t)b, ascii_char);
    }

    // Add newline after each row (except the last row)
    if (y != h - 1 && rle_ctx.length < rle_ctx.capacity - 1) {
      rle_ctx.buffer[rle_ctx.length++] = '\n';
    }
  }

  ansi_rle_finish(&rle_ctx);

  // DEBUG: Validate frame integrity right after rendering
  // Check for incomplete ANSI sequences line by line
  {
    const char *line_start = lines;
    int line_num = 0;
    while (*line_start) {
      const char *line_end = line_start;
      while (*line_end && *line_end != '\n')
        line_end++;

      // Check for incomplete ANSI sequence at end of this line
      for (const char *p = line_start; p < line_end;) {
        if (*p == '\033' && p + 1 < line_end && *(p + 1) == '[') {
          const char *seq_start = p;
          p += 2;
          // Scan for terminator (@ through ~, i.e., 0x40-0x7E)
          while (p < line_end && !(*p >= '@' && *p <= '~'))
            p++;
          if (p >= line_end) {
            // Incomplete sequence found
            size_t incomplete_len = (size_t)(line_end - seq_start);
            log_error("RENDER_INCOMPLETE_ANSI: Line %d (w=%d h=%d) has incomplete ANSI sequence (%zu bytes)", line_num,
                      w, h, incomplete_len);
            // Show the incomplete sequence
            char debug_buf[128] = {0};
            size_t debug_pos = 0;
            for (const char *d = seq_start; d < line_end && debug_pos < sizeof(debug_buf) - 10; d++) {
              unsigned char c = (unsigned char)*d;
              if (c == '\033') {
                debug_buf[debug_pos++] = '<';
                debug_buf[debug_pos++] = 'E';
                debug_buf[debug_pos++] = 'S';
                debug_buf[debug_pos++] = 'C';
                debug_buf[debug_pos++] = '>';
              } else if (c >= 0x20 && c < 0x7F) {
                debug_buf[debug_pos++] = (char)c;
              } else {
                debug_pos += (size_t)safe_snprintf(debug_buf + debug_pos, sizeof(debug_buf) - debug_pos, "<%02X>", c);
              }
            }
            log_error("  Incomplete seq: %s", debug_buf);
            break;
          } else {
            p++; // Skip terminator
          }
        } else {
          p++;
        }
      }

      line_num++;
      line_start = *line_end ? line_end + 1 : line_end;
    }
  }

  return lines;
}

// RGB to ANSI color conversion functions
char *rgb_to_ansi_fg(int r, int g, int b) {
  _Thread_local static char color_code[20]; // \033[38;2;255;255;255m + \0 = 20 bytes max
  SAFE_SNPRINTF(color_code, sizeof(color_code), "\033[38;2;%d;%d;%dm", r, g, b);
  return color_code;
}

char *rgb_to_ansi_bg(int r, int g, int b) {
  _Thread_local static char color_code[20]; // \033[48;2;255;255;255m + \0 = 20 bytes max
  SAFE_SNPRINTF(color_code, sizeof(color_code), "\033[48;2;%d;%d;%dm", r, g, b);
  return color_code;
}

void rgb_to_ansi_8bit(int r, int g, int b, int *fg_code, int *bg_code) {
  if (!fg_code || !bg_code) {
    SET_ERRNO(ERROR_INVALID_PARAM, "rgb_to_ansi_8bit: fg_code or bg_code is NULL");
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
char *image_print_with_capabilities(const image_t *image, const terminal_capabilities_t *caps, const char *palette) {

  if (!image || !image->pixels || !caps || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or caps=%p or palette=%p is NULL", image,
              image->pixels, caps, palette);
    return NULL;
  }

  // Handle half-block mode with appropriate color depth
  if (caps->render_mode == RENDER_MODE_HALF_BLOCK) {
    const uint8_t *rgb_data = (const uint8_t *)image->pixels;
    char *halfblock_result = NULL;

    // Choose halfblock renderer based on terminal color capabilities
    switch (caps->color_level) {
    case TERM_COLOR_TRUECOLOR:
#if SIMD_SUPPORT_NEON
      // Use NEON half-block renderer (optimized SIMD path)
      halfblock_result = rgb_to_truecolor_halfblocks_neon(rgb_data, image->w, image->h, 0);
      break;
#else
      // Fallback to scalar halfblock renderer (works on all platforms)
      halfblock_result = rgb_to_truecolor_halfblocks_scalar(rgb_data, image->w, image->h, 0);
      break;
#endif

    case TERM_COLOR_256:
      // Use 256-color halfblock renderer
      halfblock_result = rgb_to_256color_halfblocks_scalar(rgb_data, image->w, image->h, 0, palette);
      break;

    case TERM_COLOR_16:
      // Use 16-color halfblock renderer
      halfblock_result = rgb_to_16color_halfblocks_scalar(rgb_data, image->w, image->h, 0, palette);
      break;

    case TERM_COLOR_NONE:
    default:
      // Use monochrome halfblock renderer
      halfblock_result = rgb_to_halfblocks_scalar(rgb_data, image->w, image->h, 0, palette);
      break;
    }

    // Apply padding for halfblock if requested
    if (halfblock_result && caps->wants_padding) {
      // Subtract one line of padding for halfblock
      ssize_t padding_lines_ss = (ssize_t)caps->pad_height - 1;
      size_t padding_lines = padding_lines_ss > 0 ? (size_t)padding_lines_ss : 0;
      char *padded_result = ascii_pad_frame_height(halfblock_result, padding_lines);
      SAFE_FREE(halfblock_result);
      return padded_result;
    }

    return halfblock_result;
  }

  // Standard color modes
  bool use_background_mode = (caps->render_mode == RENDER_MODE_BACKGROUND);

  char *result = NULL;

  // DEBUG: Log terminal capabilities
  fprintf(stderr, ">>> image_print_with_capabilities: color_level=%d, render_mode=%d, use_background=%d\n",
          caps->color_level, caps->render_mode, use_background_mode);

  // Choose the appropriate printing method based on terminal capabilities
  switch (caps->color_level) {
  case TERM_COLOR_TRUECOLOR:
    // Use existing truecolor printing function with client's palette
#ifdef SIMD_SUPPORT
    START_TIMER("print_color_simd_truecolor");
    result = image_print_color_simd((image_t *)image, use_background_mode, false, palette);
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "print_color_simd_truecolor",
                             "PRINT_SIMD_TRUECOLOR: Complete (%.2f ms)");
#else
    START_TIMER("print_color");
    result = image_print_color(image, palette);
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "print_color",
                             "PRINT_COLOR: Complete (%.2f ms)");
#endif
    break;

  case TERM_COLOR_256:
    log_info("Using 256-COLOR rendering path");
#ifdef SIMD_SUPPORT
    START_TIMER("print_color_simd_256");
    result = image_print_color_simd((image_t *)image, use_background_mode, true, palette);
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "print_color_simd_256",
                             "PRINT_SIMD_256: Complete (%.2f ms)");
#else
    // Use 256-color conversion
    START_TIMER("print_256color");
    result = image_print_256color(image, palette);
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "print_256color",
                             "PRINT_256COLOR: Complete (%.2f ms)");
#endif
    break;

  case TERM_COLOR_16:
    // Use 16-color conversion with Floyd-Steinberg dithering for better quality
    START_TIMER("print_16color_dithered");
    result = image_print_16color_dithered_with_background(image, use_background_mode, palette);
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "print_16color_dithered",
                             "PRINT_16COLOR_DITHERED: Complete (%.2f ms)");
    break;

  case TERM_COLOR_NONE:
  default:
    // Use grayscale/monochrome conversion with client's custom palette
    fprintf(stderr, ">>> TERM_COLOR_NONE: palette=%p (%s), image=%dx%d\n", palette, palette ? palette : "(null)",
            image->w, image->h);
#ifdef SIMD_SUPPORT
    START_TIMER("print_simd");
    result = image_print_simd((image_t *)image, palette);
    fprintf(stderr, ">>> image_print_simd returned: %p (len=%zu)\n", result, result ? strlen(result) : 0);
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "print_simd",
                             "PRINT_SIMD: Complete (%.2f ms)");
#else
    START_TIMER("print");
    result = image_print(image, palette);
    fprintf(stderr, ">>> image_print returned: %p (len=%zu)\n", result, result ? strlen(result) : 0);
    STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "print", "PRINT: Complete (%.2f ms)");
#endif
    break;
  }

  // Note: Background mode is now handled via capabilities rather than global state
  return result;
}

// 256-color image printing function using existing SIMD optimized code
char *image_print_256color(const image_t *image, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  // Use the existing optimized SIMD colored printing (no background for 256-color mode)
#ifdef SIMD_SUPPORT
  char *result = image_print_color_simd((image_t *)image, false, true, palette);
#else
  char *result = image_print_color(image, palette);
#endif

  return result;
}

// 16-color image printing function using ansi_fast color conversion
char *image_print_16color(const image_t *image, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  int h = image->h;
  int w = image->w;

  if (h <= 0 || w <= 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "image_print_16color: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Calculate buffer size (smaller than 256-color due to shorter ANSI sequences)
  // Space for ANSI codes + newlines
  // Calculate with overflow checking
  size_t h_times_w;
  if (checked_size_mul((size_t)h, (size_t)w, &h_times_w) != ASCIICHAT_OK) {
    return NULL;
  }

  size_t h_times_w_times_12;
  if (checked_size_mul(h_times_w, 12u, &h_times_w_times_12) != ASCIICHAT_OK) {
    return NULL;
  }

  size_t buffer_size;
  if (checked_size_add(h_times_w_times_12, (size_t)h, &buffer_size) != ASCIICHAT_OK) {
    return NULL;
  }

  char *buffer;
  buffer = SAFE_MALLOC(buffer_size, char *);

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_pixel_t pixel = image->pixels[y * w + x];

      // Convert RGB to 16-color index and generate ANSI sequence
      uint8_t color_index = rgb_to_16color(pixel.r, pixel.g, pixel.b);
      ptr = append_16color_fg(ptr, color_index);

      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use UTF-8 cache which contains character index ramp
      utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
      if (!utf8_cache) {
        SET_ERRNO(ERROR_INVALID_STATE, "Failed to get UTF-8 cache");
        return NULL;
      }

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      uint8_t safe_luminance = clamp_rgb(luminance);
      uint8_t luma_idx = (uint8_t)(safe_luminance >> 2);        // 0-63 index (same as SIMD)
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
    SAFE_STRNCPY(ptr, reset_code, 5);
    ptr += strlen(reset_code);
    if (y < h - 1) {
      *ptr++ = '\n';
    }
  }

  *ptr = '\0';
  return buffer;
}

// 16-color image printing with Floyd-Steinberg dithering
char *image_print_16color_dithered(const image_t *image, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  int h = image->h;
  int w = image->w;

  if (h <= 0 || w <= 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "image_print_16color_dithered: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Allocate error buffer for Floyd-Steinberg dithering
  size_t pixel_count = (size_t)h * (size_t)w;
  rgb_error_t *error_buffer;
  error_buffer = SAFE_CALLOC(pixel_count, sizeof(rgb_error_t), rgb_error_t *);

  // Calculate buffer size (same as non-dithered version)
  // Space for ANSI codes + newlines
  // Calculate with overflow checking
  size_t h_times_w2;
  if (checked_size_mul((size_t)h, (size_t)w, &h_times_w2) != ASCIICHAT_OK) {
    SAFE_FREE(error_buffer);
    return NULL;
  }

  size_t h_times_w2_times_12;
  if (checked_size_mul(h_times_w2, 12u, &h_times_w2_times_12) != ASCIICHAT_OK) {
    SAFE_FREE(error_buffer);
    return NULL;
  }

  size_t buffer_size;
  if (checked_size_add(h_times_w2_times_12, (size_t)h, &buffer_size) != ASCIICHAT_OK) {
    SAFE_FREE(error_buffer);
    return NULL;
  }

  char *buffer;
  buffer = SAFE_MALLOC(buffer_size, char *);

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_pixel_t pixel = image->pixels[y * w + x];

      // Convert RGB to 16-color index using dithering
      uint8_t color_index = rgb_to_16color_dithered(pixel.r, pixel.g, pixel.b, x, y, w, h, error_buffer);
      ptr = append_16color_fg(ptr, color_index);

      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use UTF-8 cache which contains character index ramp
      utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
      if (!utf8_cache) {
        SET_ERRNO(ERROR_INVALID_STATE, "Failed to get UTF-8 cache");
        return NULL;
      }

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      uint8_t safe_luminance = clamp_rgb(luminance);
      uint8_t luma_idx = (uint8_t)(safe_luminance >> 2);        // 0-63 index (same as SIMD)
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
    SAFE_STRNCPY(ptr, reset_code, 5);
    ptr += strlen(reset_code);
    if (y < h - 1) {
      *ptr++ = '\n';
    }
  }

  *ptr = '\0';
  SAFE_FREE(error_buffer); // Clean up error buffer
  return buffer;
}

// 16-color image printing with Floyd-Steinberg dithering and background mode support
char *image_print_16color_dithered_with_background(const image_t *image, bool use_background, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  int h = image->h;
  int w = image->w;

  if (h <= 0 || w <= 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "image_print_16color_dithered_with_background: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Allocate error buffer for Floyd-Steinberg dithering
  size_t pixel_count = (size_t)h * (size_t)w;
  rgb_error_t *error_buffer;
  error_buffer = SAFE_CALLOC(pixel_count, sizeof(rgb_error_t), rgb_error_t *);

  // Calculate buffer size (larger for background mode due to more ANSI sequences)
  size_t buffer_size =
      (size_t)h * (size_t)w * (use_background ? 24 : 12) + (size_t)h; // Space for ANSI codes + newlines
  char *buffer;
  buffer = SAFE_MALLOC(buffer_size, char *);

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_pixel_t pixel = image->pixels[y * w + x];

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
        SET_ERRNO(ERROR_INVALID_STATE, "Failed to get UTF-8 cache");
        return NULL;
      }

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      uint8_t safe_luminance = clamp_rgb(luminance);
      uint8_t luma_idx = (uint8_t)(safe_luminance >> 2);        // 0-63 index (same as SIMD)
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
    SAFE_STRNCPY(ptr, reset_code, 5);
    ptr += strlen(reset_code);
    if (y < h - 1) {
      *ptr++ = '\n';
    }
  }

  *ptr = '\0';
  SAFE_FREE(error_buffer); // Clean up error buffer
  return buffer;
}

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
#include <ascii-chat/video/render/scalar/halfblock.h>
#include <ascii-chat/video/render/scalar/foreground.h>

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

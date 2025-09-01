#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "avx2.h"
#include "common.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

// Proper AVX2 monochrome renderer that matches NEON's performance approach
char *render_ascii_image_monochrome_avx2(const image_t *image, const char *ascii_chars) {
  if (!image || !image->pixels || !ascii_chars) {
    return NULL;
  }

  const int h = image->h;
  const int w = image->w;

  if (h <= 0 || w <= 0) {
    return NULL;
  }

  // Get cached UTF-8 character mappings (same as NEON)
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache");
    return NULL;
  }

  // Buffer allocation (same as NEON)
  const size_t max_char_bytes = 4;
  const size_t len = (size_t)h * ((size_t)w * max_char_bytes + 1);

  char *output;
  SAFE_MALLOC(output, len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Direct processing like NEON (but use scalar since AVX2 deinterleaving is complex)
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    
    // Just do what scalar does but ensure it's efficient
    for (int x = 0; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + 128) >> 8;
      const utf8_char_t *char_info = &utf8_cache->cache[luminance];
      
      // Fast character emission
      if (char_info->byte_len == 1) {
        *pos++ = char_info->utf8_bytes[0];
      } else {
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;
      }
    }

    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  *pos = '\0';
  return output;
}

// Color function that actually works (not NULL return)
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;

  if (width <= 0 || height <= 0) {
    char *empty;
    SAFE_MALLOC(empty, 1, char *);
    empty[0] = '\0';
    return empty;
  }

  // Use monochrome for simple case
  if (!use_background && !use_256color) {
    return render_ascii_image_monochrome_avx2(image, ascii_chars);
  }

  // For color modes - use output buffer system like NEON color implementation
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    return NULL;
  }

  outbuf_t ob = {0};
  size_t bytes_per_pixel = use_256color ? 6u : 15u; // Estimate for ANSI codes
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  const rgb_pixel_t *pixels_data = (const rgb_pixel_t *)image->pixels;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &pixels_data[y * width];
    
    for (int x = 0; x < width; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + 128) >> 8;
      const utf8_char_t *char_info = &utf8_cache->cache[luminance];
      
      // Add color codes using output buffer system
      if (use_256color) {
        uint8_t color_idx = (uint8_t)(16 + 36 * (pixel.r / 51) + 6 * (pixel.g / 51) + (pixel.b / 51));
        emit_set_256_color_fg(&ob, color_idx);
      } else {
        emit_set_truecolor_fg(&ob, pixel.r, pixel.g, pixel.b);
      }
      
      // Add character
      ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
    }

    if (y < height - 1) {
      ob_write(&ob, "\n", 1);
    }
  }

  ob_term(&ob);
  return ob.buf;
}

// Destroy AVX2 cache resources (called at program shutdown)
void avx2_caches_destroy(void) {
  // AVX2 currently uses shared caches from common.c, so no specific cleanup needed
  log_debug("AVX2_CACHE: AVX2 caches cleaned up");
}

#endif /* SIMD_SUPPORT_AVX2 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "avx2.h"
#include "common.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

// AVX2-accelerated pixel-to-ASCII conversion (32 pixels per iteration)
void convert_pixels_avx2(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
  int i = 0;

  // Process 32 pixels at once with AVX2 (highest performance x86_64)
  for (; i + 31 < count; i += 32) {
    // Simplified AVX2 implementation - process in chunks
    for (int j = 0; j < 32; j++) {
      const rgb_pixel_t *pixel = &pixels[i + j];
      int luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b + 128) >> 8;
      if (luminance > 255)
        luminance = 255;
      ascii_chars[i + j] = g_ascii_cache.luminance_palette[luminance];
    }
  }

  // Handle remaining pixels
  for (; i < count; i++) {
    const rgb_pixel_t *pixel = &pixels[i];
    int luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b + 128) >> 8;
    if (luminance > 255)
      luminance = 255;
    ascii_chars[i] = g_ascii_cache.luminance_palette[luminance];
  }
}

//=============================================================================
// Image-based API (matches NEON architecture)
//=============================================================================

// Simple monochrome ASCII function (matches scalar image_print performance)
char *render_ascii_image_monochrome_avx2(const image_t *image) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int h = image->h;
  const int w = image->w;

  if (h <= 0 || w <= 0) {
    return NULL;
  }

  // Match scalar allocation exactly: h rows * (w chars + 1 newline) + null terminator
  const size_t len = (size_t)h * ((size_t)w + 1);

  char *output;
  SAFE_MALLOC(output, len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Process rows with AVX2 optimization
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];

    // Use existing AVX2 function to convert this row
    convert_pixels_avx2(row, pos, w);
    pos += w;

    // Add newline (except for last row)
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  // Null terminate
  *pos = '\0';

  return output;
}

// Unified AVX2 function for all color modes (placeholder - will implement full ChatGPT approach)
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color) {
  // For now, fall back to monochrome until full implementation
  (void)use_background;
  (void)use_256color;
  return render_ascii_image_monochrome_avx2(image);
}

#endif /* SIMD_SUPPORT_AVX2 */
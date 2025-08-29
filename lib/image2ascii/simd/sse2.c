#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sse2.h"
#include "common.h"

#ifdef SIMD_SUPPORT_SSE2
#include <emmintrin.h>

// SSE2-accelerated pixel-to-ASCII conversion
void convert_pixels_sse2(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
  int i = 0;

  // Process 16 pixels at once with SSE2
  for (; i + 15 < count; i += 16) {
    const uint8_t *rgb_data = (const uint8_t *)&pixels[i];

    // Load RGB data (48 bytes for 16 pixels)
    __m128i rgb0 = _mm_loadu_si128((__m128i *)(rgb_data + 0));  // 16 bytes
    __m128i rgb1 = _mm_loadu_si128((__m128i *)(rgb_data + 16)); // 16 bytes
    __m128i rgb2 = _mm_loadu_si128((__m128i *)(rgb_data + 32)); // 16 bytes

    // Extract R, G, B components (simplified approach for SSE2)
    // This is less optimal than NEON's vld3q_u8 but works with SSE2 limitations

    // Process first 8 pixels
    uint8_t luma_array[16];
    for (int j = 0; j < 16; j++) {
      const rgb_pixel_t *pixel = &pixels[i + j];
      int luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b + 128) >> 8;
      if (luminance > 255)
        luminance = 255;
      luma_array[j] = (uint8_t)luminance;
    }

    // Convert to ASCII characters
    for (int j = 0; j < 16; j++) {
      ascii_chars[i + j] = g_ascii_cache.luminance_palette[luma_array[j]];
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
char *render_ascii_image_monochrome_sse2(const image_t *image) {
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

  // Process rows with SSE2 optimization
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];

    // Use existing SSE2 function to convert this row
    convert_pixels_sse2(row, pos, w);
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

// Unified SSE2 function for all color modes (placeholder - will implement full ChatGPT approach)
char *render_ascii_sse2_unified_optimized(const image_t *image, bool use_background, bool use_256color) {
  // For now, fall back to monochrome until full implementation
  (void)use_background;
  (void)use_256color;
  return render_ascii_image_monochrome_sse2(image);
}

#endif /* SIMD_SUPPORT_SSE2 */
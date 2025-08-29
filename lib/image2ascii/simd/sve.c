#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sve.h"
#include "common.h"

#ifdef SIMD_SUPPORT_SVE
#include <arm_sve.h>

// SVE-accelerated pixel-to-ASCII conversion (scalable vector length)
void convert_pixels_sve(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
  // TODO: Implement ARM SVE scalable vector processing
  // For now, fall back to scalar until SVE implementation is added
  for (int i = 0; i < count; i++) {
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
char *render_ascii_image_monochrome_sve(const image_t *image) {
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

  // Process rows with SVE optimization
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];

    // Use existing SVE function to convert this row
    convert_pixels_sve(row, pos, w);
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

// Unified SVE function for all color modes (placeholder - will implement full ChatGPT approach)
char *render_ascii_sve_unified_optimized(const image_t *image, bool use_background, bool use_256color) {
  // For now, fall back to monochrome until full implementation
  (void)use_background;
  (void)use_256color;
  return render_ascii_image_monochrome_sve(image);
}

#endif /* SIMD_SUPPORT_SVE */
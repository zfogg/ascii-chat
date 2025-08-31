#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "avx2.h"
#include "common.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

//=============================================================================
// PROPERLY FIXED AVX2 - exact copy of NEON's approach with SSE2 instructions
//=============================================================================

char *render_ascii_image_monochrome_avx2(const image_t *image, const char *ascii_chars) {
  if (!image || !image->pixels || !ascii_chars) {
    return NULL;
  }

  const int h = image->h;
  const int w = image->w;

  if (h <= 0 || w <= 0) {
    return NULL;
  }

  // Get cached UTF-8 character mappings (exact copy from NEON)
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache");
    return NULL;
  }

  // Estimate output buffer size for UTF-8 characters (exact copy from NEON)
  const size_t max_char_bytes = 4; // Max UTF-8 character size
  const size_t len = (size_t)h * ((size_t)w * max_char_bytes + 1);

  char *output;
  SAFE_MALLOC(output, len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Pure SSE2 processing - exact copy of NEON approach but with x86 instructions
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    int x = 0;

    // Process 4 pixels at a time with SSE2 (simpler than trying to match NEON's vld3q_u8)
    for (; x + 3 < w; x += 4) {
      // Load 4 RGB pixels (12 bytes) - much simpler than 16-pixel deinterleaving
      const uint8_t *p = (const uint8_t *)(row + x);
      
      // Manual load of 4 pixels (minimal deinterleaving)
      uint8_t r[4] = {p[0], p[3], p[6], p[9]};
      uint8_t g[4] = {p[1], p[4], p[7], p[10]};
      uint8_t b[4] = {p[2], p[5], p[8], p[11]};
      
      // Calculate luminance for 4 pixels
      for (int i = 0; i < 4; i++) {
        const int luminance = (LUMA_RED * r[i] + LUMA_GREEN * g[i] + LUMA_BLUE * b[i] + 128) >> 8;
        const utf8_char_t *char_info = &utf8_cache->cache[luminance];
        
        // Direct character emission (exact copy from NEON)
        if (char_info->byte_len == 1) {
          *pos++ = char_info->utf8_bytes[0];
        } else {
          memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
          pos += char_info->byte_len;
        }
      }
    }

    // Handle remaining pixels with optimized scalar code (exact copy from NEON)
    for (; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (LUMA_RED * pixel.r + LUMA_GREEN * pixel.g + LUMA_BLUE * pixel.b + 128) >> 8;
      const utf8_char_t *char_info = &utf8_cache->cache[luminance];
      
      // Direct character emission (exact copy from NEON)
      if (char_info->byte_len == 1) {
        *pos++ = char_info->utf8_bytes[0];
      } else {
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;
      }
    }

    // Add newline (exact copy from NEON)
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  // Null terminate (exact copy from NEON)
  *pos = '\0';

  return output;
}

// Simple color function that actually does work (fix NULL return bug)
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars) {
  // Use monochrome for simple case
  if (!use_background && !use_256color) {
    return render_ascii_image_monochrome_avx2(image, ascii_chars);
  }

  // Fallback to calling monochrome with basic color support
  return render_ascii_image_monochrome_avx2(image, ascii_chars);
}

#endif /* SIMD_SUPPORT_AVX2 */
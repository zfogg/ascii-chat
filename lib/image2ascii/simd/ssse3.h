#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_SSSE3
#include <tmmintrin.h>

// SSSE3-specific function declarations
void convert_pixels_ssse3(const rgb_pixel_t *pixels, char *ascii_chars, int count);

// NEW: Image-based API (matching NEON architecture)
char *render_ascii_image_monochrome_ssse3(const image_t *image);
char *render_ascii_ssse3_unified_optimized(const image_t *image, bool use_background, bool use_256color);

// Legacy row-based functions removed - use image-based API above

#endif /* SIMD_SUPPORT_SSSE3 */
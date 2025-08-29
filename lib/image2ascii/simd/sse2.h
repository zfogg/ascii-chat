#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_SSE2
#include <emmintrin.h>

// SSE2-specific function declarations
void convert_pixels_sse2(const rgb_pixel_t *pixels, char *ascii_chars, int count);

// NEW: Image-based API (matching NEON architecture)
char *render_ascii_image_monochrome_sse2(const image_t *image);
char *render_ascii_sse2_unified_optimized(const image_t *image, bool use_background, bool use_256color);

// Legacy row-based functions removed - use image-based API above

#endif /* SIMD_SUPPORT_SSE2 */
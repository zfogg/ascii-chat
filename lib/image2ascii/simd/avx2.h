#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

// NEW: Image-based API (matching NEON architecture)
char *render_ascii_image_monochrome_avx2(const image_t *image, const char luminance_palette[256]);
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

// Legacy row-based functions removed - use image-based API above

#endif /* SIMD_SUPPORT_AVX2 */

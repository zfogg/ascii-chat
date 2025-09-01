#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_SSE2
#include <emmintrin.h>

// NEW: Image-based API (matching NEON architecture)
char *render_ascii_image_monochrome_sse2(const image_t *image, const char *ascii_chars);
char *render_ascii_sse2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

// Cache cleanup
void sse2_caches_destroy(void);

// Legacy row-based functions removed - use image-based API above

#endif /* SIMD_SUPPORT_SSE2 */

#pragma once

#include "../image.h"

#if SIMD_SUPPORT_AVX2

// Optimized single-pass AVX2 implementations with buffer pools
char *render_ascii_image_monochrome_avx2(const image_t *image, const char *ascii_chars);
char *render_ascii_avx2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

// Cleanup function
void avx2_caches_destroy(void);

#endif /* SIMD_SUPPORT_AVX2 */

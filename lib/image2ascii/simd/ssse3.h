#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_SSSE3
#ifdef _WIN32
// Clang 20+ workaround: Use immintrin.h with proper feature detection
#if !defined(__SSSE3__)
#error "SSSE3 support required"
#endif
#include <immintrin.h>
#else
#include <tmmintrin.h>
#endif

// NEW: Image-based API (matching NEON architecture)
char *render_ascii_image_monochrome_ssse3(const image_t *image, const char *ascii_chars);
char *render_ascii_ssse3_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                           const char *ascii_chars);

// Cache cleanup
void ssse3_caches_destroy(void);

// Legacy row-based functions removed - use image-based API above

#endif /* SIMD_SUPPORT_SSSE3 */

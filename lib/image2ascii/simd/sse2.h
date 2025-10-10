#pragma once

#include "common.h"

#ifdef SIMD_SUPPORT_SSE2
#ifdef _WIN32
// Windows: Use immintrin.h with proper feature detection
// MSVC defines _M_X64 for x64 (which always has SSE2) or _M_IX86_FP >= 2 for x86 with SSE2
#if !defined(__SSE2__) && !defined(_M_X64) && !(defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#error "SSE2 support required"
#endif
#include <immintrin.h>
#else
#include <emmintrin.h>
#endif

// NEW: Image-based API (matching NEON architecture)
char *render_ascii_image_monochrome_sse2(const image_t *image, const char *ascii_chars);
char *render_ascii_sse2_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

// Cache cleanup
void sse2_caches_destroy(void);

// Legacy row-based functions removed - use image-based API above

#endif /* SIMD_SUPPORT_SSE2 */

/**
 * @file video/ascii/sse2/background.c
 * @ingroup video
 * @brief SSE2-accelerated ASCII background color rendering
 *
 * Wrapper functions for SSE2 background color rendering.
 * Uses emit_set_*_bg functions instead of emit_set_*_fg for background colors.
 */

#if SIMD_SUPPORT_SSE2
#include <ascii-chat/video/ascii/sse2/background.h>
#include <ascii-chat/video/ascii/sse2/foreground.h>
#include <ascii-chat/common.h>

// Wrapper for background color rendering
char *render_ascii_sse2_background(const image_t *image, bool use_256color, const char *ascii_chars) {
  if (!image)
    return NULL;
  // Delegates to unified function with use_background=true
  return render_ascii_sse2_unified_optimized(image, true, use_256color, ascii_chars);
}

#endif

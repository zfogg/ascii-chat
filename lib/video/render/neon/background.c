/**
 * @file video/render/neon/background.c
 * @ingroup video
 * @brief ARM NEON-accelerated ASCII background color rendering
 *
 * Wrapper functions for NEON background color rendering.
 * Uses emit_set_*_bg functions instead of emit_set_*_fg for background colors.
 */

#if SIMD_SUPPORT_NEON
#include <ascii-chat/video/render/neon/background.h>
#include <ascii-chat/video/render/neon/foreground.h>
#include <ascii-chat/common.h>

// Wrapper for background color rendering
char *render_ascii_neon_background(const image_t *image, bool use_256color, const char *ascii_chars) {
  if (!image)
    return NULL;
  // Delegates to unified function with use_background=true
  return render_ascii_neon_unified_optimized(image, true, use_256color, ascii_chars);
}

#endif

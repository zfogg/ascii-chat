/**
 * @file video/render/scalar/background.c
 * @ingroup video
 * @brief Scalar ASCII background color rendering
 *
 * Wrapper functions for scalar background color rendering.
 * Uses background colors instead of foreground colors for output.
 */

#include <ascii-chat/video/render/scalar/background.h>
#include <ascii-chat/video/render/scalar/foreground.h>
#include <ascii-chat/common.h>

// Scalar background rendering wrapper
char *image_print_color_background(const image_t *p, const char *palette) {
  if (!p)
    return NULL;
  // For scalar, we use the color function which can be configured for background
  // This is a placeholder for background-specific scalar rendering
  return image_print_color(p, palette);
}

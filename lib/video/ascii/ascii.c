/**
 * @file video/ascii/ascii.c
 * @ingroup video
 * @brief ASCII rendering dispatcher and main API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/output_buffer.h>
#include <ascii-chat/log/log.h>

/**
 * @brief Main ASCII rendering API - dispatches to best SIMD implementation
 */
char *render_ascii(image_t *image, const char *ascii_chars) {
  if (!image || !ascii_chars) {
    return NULL;
  }

  log_debug("render_ascii: dispatching to SIMD implementation");
  return image_print_color_simd(image, false, false, ascii_chars);
}

/**
 * @brief ASCII rendering with color support
 */
char *render_ascii_color(image_t *image, bool use_256color, const char *ascii_chars) {
  if (!image || !ascii_chars) {
    return NULL;
  }

  log_debug("render_ascii_color: use_256color=%d", use_256color);
  return image_print_color_simd(image, false, use_256color, ascii_chars);
}

/**
 * @brief ASCII rendering with background color mode
 */
char *render_ascii_background(image_t *image, bool use_256color, const char *ascii_chars) {
  if (!image || !ascii_chars) {
    return NULL;
  }

  log_debug("render_ascii_background: use_256color=%d", use_256color);
  return image_print_color_simd(image, true, use_256color, ascii_chars);
}

/**
 * @file video/scalar/256color.c
 * @ingroup video
 * @brief Scalar 256-color ASCII rendering implementation
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <ascii-chat/common.h>
#include <ascii-chat/video/output_buffer.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/scalar/256color.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/math.h>

char *image_print_256color(const image_t *image, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  // Use the existing optimized SIMD colored printing (no background for 256-color mode)
#ifdef SIMD_SUPPORT
  char *result = image_print_color_simd((image_t *)image, false, true, palette);
#else
  char *result = image_print_color(image, palette);
#endif

  return result;
}

// 16-color image printing function using ansi_fast color conversion

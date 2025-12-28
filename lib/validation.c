/**
 * @file validation.c
 * @ingroup validation
 * @brief 🛡️ Validation helpers for protocol handlers and data processing
 */

#include <limits.h>
#include <stddef.h>
#include "validation.h"
#include "video/image.h"
#include "common.h"
#include "asciichat_errno.h"

asciichat_error_t image_validate_dimensions(uint32_t width, uint32_t height, size_t *out_rgb_size) {
  if (!out_rgb_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output size pointer is NULL");
  }

  // Validate dimensions are non-zero
  if (width == 0 || height == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions must be non-zero: %ux%u", width, height);
  }

  // Check if width * height would overflow
  const size_t w_size = (size_t)width;
  const size_t h_size = (size_t)height;

  if (h_size > SIZE_MAX / w_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions cause integer overflow: %ux%u", width, height);
  }

  const size_t pixel_count = w_size * h_size;

  // Check if pixel_count * sizeof(rgb_t) would overflow
  if (pixel_count > SIZE_MAX / sizeof(rgb_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image pixel count too large: %zu pixels (max %zu)", pixel_count,
                     SIZE_MAX / sizeof(rgb_t));
  }

  *out_rgb_size = pixel_count * sizeof(rgb_t);
  return ASCIICHAT_OK;
}

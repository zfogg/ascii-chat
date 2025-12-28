/**
 * @file util/image.c
 * @ingroup util
 * @brief 🖼️ Safe overflow-checked buffer size calculations for images and video frames
 */

#include "image.h"
#include "common.h"
#include "video/image.h"
#include <limits.h>

asciichat_error_t image_calc_pixel_count(size_t width, size_t height, size_t *out_pixel_count) {
  if (!out_pixel_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "out_pixel_count is NULL");
  }

  // Check for zero dimensions
  if (width == 0 || height == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions must be non-zero: %zu x %zu", width, height);
  }

  // Check overflow before multiplication
  if (height > SIZE_MAX / width) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions too large (would overflow): %zu x %zu", width, height);
  }

  *out_pixel_count = width * height;
  return ASCIICHAT_OK;
}

asciichat_error_t image_calc_pixel_buffer_size(size_t pixel_count, size_t bytes_per_pixel, size_t *out_size) {
  if (!out_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "out_size is NULL");
  }

  if (pixel_count == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Pixel count must be non-zero");
  }

  if (bytes_per_pixel == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "bytes_per_pixel must be non-zero");
  }

  // Check overflow before multiplication
  if (pixel_count > SIZE_MAX / bytes_per_pixel) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Pixel buffer too large (would overflow): %zu pixels * %zu bpp", pixel_count,
                     bytes_per_pixel);
  }

  *out_size = pixel_count * bytes_per_pixel;
  return ASCIICHAT_OK;
}

asciichat_error_t image_calc_rgb_size(size_t width, size_t height, size_t *out_size) {
  if (!out_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "out_size is NULL");
  }

  // Calculate pixel count first
  size_t pixel_count;
  asciichat_error_t err = image_calc_pixel_count(width, height, &pixel_count);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Calculate size with 3 bytes per pixel (RGB)
  return image_calc_pixel_buffer_size(pixel_count, 3, out_size);
}

asciichat_error_t image_calc_total_allocation(size_t width, size_t height, size_t struct_size, size_t bytes_per_pixel,
                                              size_t *out_size) {
  if (!out_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "out_size is NULL");
  }

  // Calculate pixel count first with overflow checking
  size_t pixel_count;
  asciichat_error_t err = image_calc_pixel_count(width, height, &pixel_count);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Calculate pixel buffer size
  size_t pixel_buffer_size;
  err = image_calc_pixel_buffer_size(pixel_count, bytes_per_pixel, &pixel_buffer_size);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Check overflow when adding struct size
  if (pixel_buffer_size > SIZE_MAX - struct_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Total allocation size would overflow: struct=%zu + pixels=%zu", struct_size,
                     pixel_buffer_size);
  }

  *out_size = struct_size + pixel_buffer_size;
  return ASCIICHAT_OK;
}

asciichat_error_t image_validate_dimensions(size_t width, size_t height) {
  // Check for zero dimensions
  if (width == 0 || height == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions must be non-zero: %zu x %zu", width, height);
  }

  // Check against maximum allowed dimensions
  if (width > IMAGE_MAX_WIDTH || height > IMAGE_MAX_HEIGHT) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions exceed maximum: %zu x %zu (max %u x %u)", width, height,
                     IMAGE_MAX_WIDTH, IMAGE_MAX_HEIGHT);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t image_validate_buffer_size(size_t requested_size) {
  if (requested_size > IMAGE_MAX_PIXELS_SIZE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Image buffer size exceeds maximum: %zu > %zu bytes", requested_size,
                     IMAGE_MAX_PIXELS_SIZE);
  }

  return ASCIICHAT_OK;
}

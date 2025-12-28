/**
 * @file util/image.h
 * @ingroup util
 * @brief 🖼️ Safe overflow-checked buffer size calculations for images and video frames
 *
 * Provides helpers for calculating buffer sizes with proper overflow detection.
 * All functions return error codes and set errno for detailed error context.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "asciichat_errno.h"

/**
 * @brief Calculate pixel count with overflow checking
 * @param width Image width (pixels)
 * @param height Image height (pixels)
 * @param out_pixel_count Output: calculated pixel count
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on overflow
 *
 * Safe wrapper for: pixel_count = width * height
 * Prevents integer overflow by checking before multiplication
 */
asciichat_error_t image_calc_pixel_count(size_t width, size_t height, size_t *out_pixel_count);

/**
 * @brief Calculate pixel buffer size with overflow checking
 * @param pixel_count Number of pixels
 * @param bytes_per_pixel Size of each pixel (e.g., 3 for RGB, 4 for RGBA)
 * @param out_size Output: calculated buffer size in bytes
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on overflow
 *
 * Safe wrapper for: size = pixel_count * bytes_per_pixel
 * Prevents integer overflow by checking before multiplication
 */
asciichat_error_t image_calc_pixel_buffer_size(size_t pixel_count, size_t bytes_per_pixel, size_t *out_size);

/**
 * @brief Calculate total RGB buffer size from dimensions
 * @param width Image width (pixels)
 * @param height Image height (pixels)
 * @param out_size Output: total buffer size (width * height * 3 bytes)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on overflow
 *
 * Convenience function for RGB images (3 bytes per pixel).
 * Safe wrapper for: size = width * height * 3
 */
asciichat_error_t image_calc_rgb_size(size_t width, size_t height, size_t *out_size);

/**
 * @brief Calculate combined size of image struct + pixel buffer
 * @param width Image width (pixels)
 * @param height Image height (pixels)
 * @param struct_size Size of image_t structure (typically sizeof(image_t))
 * @param bytes_per_pixel Size of each pixel
 * @param out_size Output: total allocation size
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on overflow
 *
 * Used for pool allocations where struct and pixels are contiguous.
 * Safe calculation of: struct_size + (width * height * bytes_per_pixel)
 */
asciichat_error_t image_calc_total_allocation(size_t width, size_t height, size_t struct_size, size_t bytes_per_pixel,
                                              size_t *out_size);

/**
 * @brief Validate image dimensions (non-zero, within limits)
 * @param width Image width to validate
 * @param height Image height to validate
 * @return ASCIICHAT_OK if valid, ERROR_INVALID_PARAM if invalid
 *
 * Checks that:
 * - Both width and height are non-zero
 * - Dimensions don't exceed IMAGE_MAX_WIDTH and IMAGE_MAX_HEIGHT
 * Sets errno with detailed context if validation fails
 */
asciichat_error_t image_validate_dimensions(size_t width, size_t height);

/**
 * @brief Validate buffer size against maximum allocation limit
 * @param requested_size Requested buffer size
 * @return ASCIICHAT_OK if valid, ERROR_INVALID_PARAM if exceeds maximum
 *
 * Checks that requested_size <= IMAGE_MAX_PIXELS_SIZE
 * Sets errno with detailed context if validation fails
 */
asciichat_error_t image_validate_buffer_size(size_t requested_size);

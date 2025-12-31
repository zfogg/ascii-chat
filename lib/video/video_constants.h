/**
 * @file video/video_constants.h
 * @brief Video-related constants and limits
 * @ingroup video
 *
 * Centralized location for video domain constants used across the codebase.
 * This allows utilities and other modules to use video limits without
 * creating circular dependencies.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

/* ============================================================================
 * Image Size Constants
 * ============================================================================
 */

/**
 * @brief Maximum image width (4K resolution)
 *
 * Maximum supported image width in pixels. Set to 3840 pixels
 * (4K UHD width) to support high-resolution video capture.
 *
 * @note Larger images may exceed memory limits.
 * @note Video pipeline may enforce lower limits for performance.
 *
 * @ingroup video
 */
#define IMAGE_MAX_WIDTH 3840

/**
 * @brief Maximum image height (4K resolution)
 *
 * Maximum supported image height in pixels. Set to 2160 pixels
 * (4K UHD height) to support high-resolution video capture.
 *
 * @note Larger images may exceed memory limits.
 * @note Video pipeline may enforce lower limits for performance.
 *
 * @ingroup video
 */
#define IMAGE_MAX_HEIGHT 2160

/**
 * @brief Maximum pixel data size in bytes
 *
 * Maximum size in bytes for pixel data array. Calculated as:
 * IMAGE_MAX_WIDTH * IMAGE_MAX_HEIGHT * sizeof(rgb_t)
 * Used for buffer allocation and validation.
 *
 * @note This is approximately 24.88 MB for 4K RGB images.
 * @note Actual memory usage may be higher due to alignment.
 *
 * @ingroup video
 */
#define IMAGE_MAX_PIXELS_SIZE (IMAGE_MAX_WIDTH * IMAGE_MAX_HEIGHT * 3)

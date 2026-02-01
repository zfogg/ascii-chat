/**
 * @defgroup buffer_sizes Buffer Sizes
 * @ingroup module_core
 * @brief Standard buffer size constants
 *
 * @file buffer_sizes.h
 * @brief Common buffer size definitions
 * @ingroup buffer_sizes
 * @addtogroup buffer_sizes
 * @{
 */

#pragma once

/**
 * @brief Small buffer size (256 bytes)
 *
 * Used for short strings, error messages, and small temporary buffers.
 *
 * @ingroup buffer_sizes
 */
#define BUFFER_SIZE_SMALL 256

/**
 * @brief Medium buffer size (512 bytes)
 *
 * Used for medium-length strings, paths, and intermediate buffers.
 *
 * @ingroup buffer_sizes
 */
#define BUFFER_SIZE_MEDIUM 512

/**
 * @brief Large buffer size (1024 bytes)
 *
 * Used for longer strings, full paths, and larger temporary buffers.
 *
 * @ingroup buffer_sizes
 */
#define BUFFER_SIZE_LARGE 1024

/**
 * @brief Extra large buffer size (2048 bytes)
 *
 * Used for very long strings, multiple paths, and large temporary buffers.
 *
 * @ingroup buffer_sizes
 */
#define BUFFER_SIZE_XLARGE 2048

/**
 * @brief Extra extra large buffer size (4096 bytes)
 *
 * Used for maximum-length paths and very large temporary buffers.
 *
 * @ingroup buffer_sizes
 */
#define BUFFER_SIZE_XXLARGE 4096

/**
 * @brief Extra extra extra large buffer size (8192 bytes)
 *
 * Used for extremely large buffers like exception messages and stack traces.
 *
 * @ingroup buffer_sizes
 */
#define BUFFER_SIZE_XXXLARGE 8192

/**
 * @brief Huge buffer size (16kb)
 *
 * Used for huge buffers like stack traces and other system messages.
 *
 * @ingroup buffer_sizes
 */
#define BUFFER_SIZE_HUGE 16384

/* ============================================================================
 * Image Frame Buffer Constants
 * ============================================================================ */

/**
 * @brief Bytes per pixel in RGB24 format (red + green + blue)
 *
 * @ingroup buffer_sizes
 */
#define BYTES_PER_RGB_PIXEL 3

/**
 * @brief Legacy image frame header size (width:4 + height:4)
 *
 * Used for backward compatibility with older frame format.
 *
 * @ingroup buffer_sizes
 */
#define IMAGE_FRAME_HEADER_SIZE_LEGACY 8

/**
 * @brief New image frame header size (width:4 + height:4 + compressed:4 + size:4)
 *
 * Used for new compressed frame format with compression flag and data size.
 *
 * @ingroup buffer_sizes
 */
#define IMAGE_FRAME_HEADER_SIZE_NEW 16

/**
 * @brief Maximum reasonable frame width (prevents memory exhaustion)
 *
 * @ingroup buffer_sizes
 */
#define MAX_FRAME_WIDTH 1920

/**
 * @brief Maximum reasonable frame height (prevents memory exhaustion)
 *
 * @ingroup buffer_sizes
 */
#define MAX_FRAME_HEIGHT 1080

/**
 * @brief Maximum total frame pixels (width * height)
 *
 * @ingroup buffer_sizes
 */
#define MAX_FRAME_PIXELS (MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT)

/**
 * @brief Maximum RGB buffer size (MAX_FRAME_PIXELS * BYTES_PER_RGB_PIXEL)
 *
 * @ingroup buffer_sizes
 */
#define MAX_FRAME_RGB_SIZE (MAX_FRAME_PIXELS * BYTES_PER_RGB_PIXEL)

/**
 * @brief Maximum frame buffer size including headers and compression
 *
 * Used as a practical limit to prevent memory exhaustion from oversized frames.
 * This is larger than typical compressed frames but smaller than uncompressed RGB.
 *
 * @ingroup buffer_sizes
 */
#define MAX_FRAME_BUFFER_SIZE (2 * 1024 * 1024)

/** @} */

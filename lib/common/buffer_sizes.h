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

/** @} */

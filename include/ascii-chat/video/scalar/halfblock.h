#pragma once

/**
 * @file video/scalar/halfblock.h
 * @brief Scalar truecolor halfblock renderer
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * This header provides scalar (non-SIMD) halfblock rendering functions
 * for converting RGB images to ANSI-escaped halfblock characters with
 * truecolor foreground and background colors.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Convert RGB to truecolor half-blocks using scalar code
 * @param rgb RGB pixel data (RGB24 format, 3 bytes per pixel)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride_bytes Row stride in bytes (if 0, calculated as width * 3)
 * @return Allocated ASCII string with half-block characters (caller must free), or NULL on error
 *
 * Renders an RGB image using halfblock characters (▀) with truecolor
 * foreground and background colors. Processes 2 rows of source pixels
 * per 1 output line:
 * - Top pixel: foreground color for halfblock
 * - Bottom pixel: background color for halfblock
 * - Last row (odd height): duplicates top row as bottom
 *
 * Features:
 * - Run-length encoding (RLE) for repeated halfblocks
 * - Transparent area detection (fully black pixels)
 * - Color state tracking to minimize ANSI sequences
 * - Proper vertical resolution (height / 2 output rows)
 *
 * @note Caller must free the returned buffer with free().
 * @note Returns empty string for invalid dimensions (width <= 0 or height <= 0).
 * @note Stride of 0 defaults to width * 3 (tightly packed RGB24).
 *
 * @ingroup video
 */
char *rgb_to_truecolor_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride_bytes);

/**
 * @brief Convert RGB to monochrome (grayscale) half-blocks using scalar code
 * @param rgb RGB pixel data (RGB24 format, 3 bytes per pixel)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride_bytes Row stride in bytes (if 0, calculated as width * 3)
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string with half-block characters (caller must free), or NULL on error
 *
 * Renders an RGB image using halfblock characters without color codes.
 * Uses ITU-R BT.601 luminance formula for grayscale conversion.
 *
 * @note Caller must free the returned buffer with free().
 * @ingroup video
 */
char *rgb_to_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride_bytes, const char *palette);

/**
 * @brief Convert RGB to 16-color half-blocks using scalar code
 * @param rgb RGB pixel data (RGB24 format, 3 bytes per pixel)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride_bytes Row stride in bytes (if 0, calculated as width * 3)
 * @param palette Character palette (unused, for API consistency)
 * @param pad_height Top padding height (number of newlines to output before frame)
 * @return Allocated ASCII string with half-block characters (caller must free), or NULL on error
 *
 * Renders an RGB image using halfblock characters with 16-color ANSI codes.
 * Processes 2 rows of source pixels per 1 output line with foreground and
 * background color selection from 16-color palette.
 *
 * @note Caller must free the returned buffer with free().
 * @ingroup video
 */
char *rgb_to_16color_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride_bytes,
                                       const char *palette);

/**
 * @brief Convert RGB to 256-color half-blocks using scalar code
 * @param rgb RGB pixel data (RGB24 format, 3 bytes per pixel)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride_bytes Row stride in bytes (if 0, calculated as width * 3)
 * @param palette Character palette (unused, for API consistency)
 * @param pad_height Top padding height (number of newlines to output before frame)
 * @return Allocated ASCII string with half-block characters (caller must free), or NULL on error
 *
 * Renders an RGB image using halfblock characters with 256-color ANSI codes.
 * Processes 2 rows of source pixels per 1 output line with foreground and
 * background color selection from 256-color palette.
 *
 * @note Caller must free the returned buffer with free().
 * @ingroup video
 */
char *rgb_to_256color_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride_bytes,
                                        const char *palette);

/** @} */

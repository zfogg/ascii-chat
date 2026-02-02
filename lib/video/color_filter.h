/**
 * @file video/color_filter.h
 * @brief Monochromatic color filter implementation for video frames
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * This module provides color filtering capabilities to apply monochromatic color
 * tints to grayscale video feeds. The server processes color filters; clients see
 * each user in their chosen filter color.
 *
 * FEATURES:
 * =========
 * - 11 built-in color filters (matrix green, cyan, magenta, etc.)
 * - ITU-R BT.601 grayscale conversion formula
 * - Two colorization modes: "color on white" and "white on color"
 * - Per-pixel colorization for maximum quality
 * - Protocol integration via CLIENT_CAPABILITIES packet
 *
 * COLOR FILTERS:
 * ==============
 * 0.  none (no filtering)
 * 1.  black (dark content on white background)
 * 2.  white (white content on black background)
 * 3.  green (#00FF41)
 * 4.  magenta (#FF00FF)
 * 5.  fuchsia (#FF00AA)
 * 6.  orange (#FF8800)
 * 7.  teal (#00DDDD)
 * 8.  cyan (#00FFFF)
 * 9.  pastel-pink (#FFB6C1)
 * 10. error-red (#FF3333)
 * 11. yellow (#FFEB99)
 *
 * ARCHITECTURE:
 * ==============
 * - color_filter_registry[]: Metadata for all 11 filters (name, CLI name, RGB values, mode)
 * - rgb_to_grayscale(): ITU-R BT.601 conversion (efficient fixed-point math)
 * - colorize_grayscale_pixel(): Single pixel colorization (inline for performance)
 * - apply_color_filter(): Image-wide colorization (in-place operation)
 *
 * USAGE:
 * ======
 * @code{.c}
 * // Convert image to grayscale and apply filter
 * if (filter != COLOR_FILTER_NONE) {
 *     apply_color_filter(image, width, height, filter);
 * }
 * @endcode
 *
 * PROTOCOL INTEGRATION:
 * =====================
 * The color_filter field is included in the CLIENT_CAPABILITIES packet:
 * - Client sends chosen filter to server during connection
 * - Server applies filter to client's video before ASCII conversion
 * - All clients see the filtered output
 *
 * @note All filter colors are applied using ITU-R BT.601 grayscale conversion
 * @note Filters automatically set --color-mode mono (monochromatic display)
 * @note "--color-filter foo --color-mode 256" is an error
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "platform/terminal.h"

/**
 * @brief Color filter metadata structure
 *
 * Contains all information about a color filter including its name,
 * CLI identifier, RGB color values, and rendering mode.
 *
 * @ingroup video
 */
typedef struct {
  const char *name;      ///< Human-readable filter name (e.g., "matrix-green")
  const char *cli_name;  ///< CLI argument name (e.g., "matrix-green")
  uint8_t r, g, b;       ///< RGB color values for this filter
  bool foreground_on_bg; ///< true=color on white, false=white on color
} color_filter_def_t;

/**
 * @brief Get color filter metadata by enum value
 * @param filter Filter enum value (color_filter_t)
 * @return Pointer to filter metadata, or NULL if invalid filter
 *
 * Returns the metadata definition for a color filter including its name,
 * RGB values, and rendering mode.
 *
 * @note The returned pointer is statically allocated and valid for program lifetime
 * @note Returns NULL for COLOR_FILTER_NONE
 *
 * @ingroup video
 */
const color_filter_def_t *color_filter_get_metadata(color_filter_t filter);

/**
 * @brief Convert color filter CLI name to enum value
 * @param cli_name CLI argument name (e.g., "matrix-green")
 * @return Filter enum value, or COLOR_FILTER_NONE if not found
 *
 * Converts a CLI argument name (like "matrix-green") to the corresponding
 * color_filter_t enum value.
 *
 * @note CLI names are case-insensitive
 * @note Returns COLOR_FILTER_NONE for unknown filter names
 *
 * @ingroup video
 */
color_filter_t color_filter_from_cli_name(const char *cli_name);

/**
 * @brief Convert ITU-R BT.601 RGB to grayscale using fixed-point math
 * @param r Red channel (0-255)
 * @param g Green channel (0-255)
 * @param b Blue channel (0-255)
 * @return Grayscale value (0-255)
 *
 * Converts RGB color to grayscale using the ITU-R BT.601 formula:
 * gray = 0.299 * R + 0.587 * G + 0.114 * B
 *
 * This formula is efficient using fixed-point math:
 * gray = (77 * R + 150 * G + 29 * B) >> 8
 *
 * @ingroup video
 */
static inline uint8_t rgb_to_grayscale(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)((77U * r + 150U * g + 29U * b) >> 8);
}

/**
 * @brief Apply color filter to entire image in-place
 * @param pixels Image pixel buffer (modified in-place)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride Bytes per row (typically width * 3 for RGB24)
 * @param filter Color filter to apply (COLOR_FILTER_NONE = no-op)
 * @return 0 on success, -1 on error (invalid parameters)
 *
 * Applies the specified color filter to an RGB image. The operation:
 * 1. Converts each RGB pixel to grayscale (ITU-R BT.601)
 * 2. Scales grayscale by the filter's RGB color values
 * 3. Modifies pixels in-place
 *
 * For "color on white" mode (black-on-white only):
 *   Dark pixels get full color, light pixels become white
 *
 * For "white on color" mode (all other filters):
 *   Grayscale value scales the color intensity
 *
 * @note This function modifies the image in-place
 * @note If filter == COLOR_FILTER_NONE, this is a no-op
 * @note Expected stride is typically width * 3 for RGB24 format
 *
 * @ingroup video
 */
int apply_color_filter(uint8_t *pixels, uint32_t width, uint32_t height, uint32_t stride, color_filter_t filter);

/** @} */

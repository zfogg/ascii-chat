#pragma once

/**
 * @file image2ascii/simd/neon.h
 * @ingroup image2ascii
 * @brief NEON-optimized ASCII rendering functions
 *
 * This header provides NEON (Advanced SIMD) optimized functions for
 * converting images to ASCII art on ARM processors.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#include <stdint.h>
#include <stddef.h>

#if SIMD_SUPPORT_NEON
#if (!defined(__ARM_NEON) && !defined(__aarch64__))
#error "NEON support required"
#endif
#include <arm_neon.h>

/**
 * @brief Quantize RGB to 256-color palette with dithering (NEON)
 * @param r Red components (16 pixels)
 * @param g Green components (16 pixels)
 * @param b Blue components (16 pixels)
 * @param pixel_offset Pixel offset for dithering
 * @return 256-color palette indices (16 pixels)
 *
 * @ingroup image2ascii
 */
uint8x16_t palette256_index_dithered_neon(uint8x16_t r, uint8x16_t g, uint8x16_t b, int pixel_offset);

/**
 * @brief Initialize NEON decimal lookup table
 *
 * Must be called once at startup before using NEON functions.
 *
 * @ingroup image2ascii
 */
void init_neon_decimal_table(void);

/**
 * @brief Render image as monochrome ASCII using NEON
 * @param image Source image
 * @param ascii_chars Character palette
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * Matches scalar image_print() interface for compatibility.
 *
 * @ingroup image2ascii
 */
char *render_ascii_image_monochrome_neon(const image_t *image, const char *ascii_chars);

/**
 * @brief Render image as ASCII with color using NEON (unified optimized)
 * @param image Source image
 * @param use_background Use background colors
 * @param use_256color Use 256-color mode (vs truecolor)
 * @param ascii_chars Character palette
 * @return Allocated ASCII string with ANSI codes (caller must free), or NULL on error
 *
 * @ingroup image2ascii
 */
char *render_ascii_neon_unified_optimized(const image_t *image, bool use_background, bool use_256color,
                                          const char *ascii_chars);

/**
 * @brief Convert RGB to truecolor half-blocks using NEON
 * @param rgb RGB pixel data
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride_bytes Row stride in bytes
 * @return Allocated ASCII string with half-block characters (caller must free), or NULL on error
 *
 * Uses Unicode half-block characters (▀▄) for improved rendering.
 *
 * @ingroup image2ascii
 */
char *rgb_to_truecolor_halfblocks_neon(const uint8_t *rgb, int width, int height, int stride_bytes);

/**
 * @brief Destroy NEON caches
 *
 * @ingroup image2ascii
 */
void neon_caches_destroy(void);
#endif

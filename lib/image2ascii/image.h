#pragma once

/**
 * @file image2ascii/image.h
 * @ingroup module_video
 * @brief Image Data Structures and Operations
 *
 * This header provides comprehensive image data structures and operations
 * for ASCII-Chat. Supports RGB images with standard and SIMD-optimized
 * formats for efficient image processing and ASCII art conversion.
 *
 * CORE FEATURES:
 * ==============
 * - RGB image data structures (packed and SIMD-aligned)
 * - Image allocation and memory management
 * - Buffer pool integration for video pipeline efficiency
 * - Image-to-ASCII conversion with multiple color modes
 * - Color quantization and palette generation
 * - Image resizing (nearest-neighbor and bilinear interpolation)
 * - ANSI color code generation and conversion
 * - Dithering support for improved color accuracy
 *
 * IMAGE FORMATS:
 * ==============
 * The system supports:
 * - Standard RGB24 format (packed, 3 bytes per pixel)
 * - SIMD-aligned RGB format (4-byte aligned for NEON/AVX optimization)
 * - Maximum 4K resolution (3840x2160)
 * - Memory-efficient buffer pool allocation
 *
 * COLOR MODES:
 * ============
 * ASCII conversion supports multiple color modes:
 * - Monochrome: No color, character-based only
 * - 16-color: Standard ANSI colors
 * - 256-color: Extended ANSI palette
 * - Truecolor: 24-bit RGB (terminal-dependent)
 * - Dithering: Floyd-Steinberg dithering for color accuracy
 *
 * @note All image allocation functions must be matched with corresponding
 *       destroy functions (image_destroy() or image_destroy_to_pool()).
 * @note ASCII conversion functions return allocated strings that must be
 *       freed by the caller.
 * @note Image structures are compatible with webcam capture (image_t).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#ifdef _WIN32
#pragma pack(push, 1)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "../platform/abstraction.h"
#include "../common.h"

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

/**
 * @brief RGB pixel structure
 *
 * Standard RGB pixel structure with 3 bytes per pixel (R, G, B).
 * Packed structure (no padding) for efficient memory usage.
 *
 * @note Structure is packed for wire format compatibility.
 * @note Color components are in range 0-255.
 * @note Size: 3 bytes per pixel.
 *
 * @ingroup module_video
 */
typedef struct rgb_t {
  uint8_t r; ///< Red color component (0-255)
  uint8_t g; ///< Green color component (0-255)
  uint8_t b; ///< Blue color component (0-255)
} PACKED_ATTR rgb_t;

/**
 * @brief SIMD-aligned RGB pixel structure for optimal NEON/AVX performance
 *
 * SIMD-optimized RGB pixel structure with 4-byte alignment for efficient
 * vector operations. Includes padding byte to align to 16-byte boundary
 * for optimal SIMD (NEON/AVX/SSE) performance.
 *
 * @note Structure is 16-byte aligned for SIMD optimization.
 * @note Color components are in range 0-255.
 * @note Size: 4 bytes per pixel (includes padding).
 * @note Use this structure for SIMD-optimized image processing.
 *
 * @ingroup module_video
 */
typedef struct rgb_pixel_simd_t {
  uint8_t r;       ///< Red color component (0-255)
  uint8_t g;       ///< Green color component (0-255)
  uint8_t b;       ///< Blue color component (0-255)
  uint8_t padding; ///< Padding byte to align to 4-byte boundary (SIMD alignment)
} ALIGNED_ATTR(16) rgb_pixel_simd_t;

/**
 * @brief Image structure
 *
 * Complete image structure containing dimensions and pixel data.
 * Pixel data is stored as a contiguous array of RGB pixels in row-major
 * order. Compatible with webcam capture and image processing pipeline.
 *
 * MEMORY LAYOUT:
 * - pixels array is allocated separately from image structure
 * - pixels array size: width * height * sizeof(rgb_t)
 * - Pixel access: pixels[row * width + col]
 * - Row-major order (first row, then second row, etc.)
 *
 * @note Pixel array must be allocated (by image_new() or image_new_from_pool()).
 * @note Image structure and pixels array can be freed separately if needed.
 * @note Compatible with webcam capture functions (webcam_read()).
 *
 * @ingroup module_video
 */
typedef struct image_t {
  int w;         ///< Image width in pixels (must be > 0)
  int h;         ///< Image height in pixels (must be > 0)
  rgb_t *pixels; ///< Pixel data array (width * height RGB pixels, row-major order)
} image_t;

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
 * @ingroup module_video
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
 * @ingroup module_video
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
 * @ingroup module_video
 */
#define IMAGE_MAX_PIXELS_SIZE (IMAGE_MAX_WIDTH * IMAGE_MAX_HEIGHT * sizeof(rgb_t))

/* ============================================================================
 * Image Allocation and Management Functions
 * @{
 */

/**
 * @brief Create a new image with standard allocation
 * @param width Image width in pixels (must be > 0 and <= IMAGE_MAX_WIDTH)
 * @param height Image height in pixels (must be > 0 and <= IMAGE_MAX_HEIGHT)
 * @return Allocated image structure, or NULL on error
 *
 * Allocates a new image structure and pixel data array using standard
 * malloc() allocation. Image is initialized with specified dimensions
 * and all pixels set to black (0, 0, 0).
 *
 * @note Caller must free with image_destroy() when done.
 * @note Returns NULL on error (invalid dimensions, memory allocation failure).
 * @note Pixel data is allocated separately and freed automatically on destroy.
 *
 * @par Example
 * @code
 * image_t *img = image_new(1920, 1080);
 * if (img) {
 *     // Use image...
 *     image_destroy(img);
 * }
 * @endcode
 *
 * @ingroup module_video
 */
image_t *image_new(size_t width, size_t height);

/**
 * @brief Destroy an image allocated with image_new()
 * @param p Image to destroy (can be NULL, no-op if NULL)
 *
 * Frees an image structure and its associated pixel data array
 * allocated by image_new(). Frees both the image structure and
 * the pixels array.
 *
 * @note Safe to call with NULL pointer (no-op).
 * @note Must NOT be used for images allocated with image_new_from_pool().
 * @note Use image_destroy_to_pool() for pool-allocated images.
 *
 * @ingroup module_video
 */
void image_destroy(image_t *p);

/**
 * @brief Create a new image from buffer pool
 * @param width Image width in pixels (must be > 0 and <= IMAGE_MAX_WIDTH)
 * @param height Image height in pixels (must be > 0 and <= IMAGE_MAX_HEIGHT)
 * @return Allocated image structure from pool, or NULL on error
 *
 * Allocates a new image structure and pixel data from the buffer pool
 * for efficient memory management in video pipeline. Buffer pool
 * reduces allocation overhead and improves performance for high-frequency
 * operations (video frame capture/processing).
 *
 * @note Caller must free with image_destroy_to_pool() when done.
 * @note Returns NULL on error (invalid dimensions, pool exhaustion).
 * @note Buffer pool allocation is faster than standard malloc().
 *
 * @par Example
 * @code
 * image_t *img = image_new_from_pool(1920, 1080);
 * if (img) {
 *     // Process video frame...
 *     image_destroy_to_pool(img);
 * }
 * @endcode
 *
 * @ingroup module_video
 */
image_t *image_new_from_pool(size_t width, size_t height);

/**
 * @brief Destroy an image allocated from buffer pool
 * @param image Image to destroy (can be NULL, no-op if NULL)
 *
 * Returns an image structure and its pixel data to the buffer pool
 * for reuse. This is faster than standard free() because buffers
 * are reused instead of being deallocated.
 *
 * @note Safe to call with NULL pointer (no-op).
 * @note Must NOT be used for images allocated with image_new().
 * @note Use image_destroy() for standard-allocated images.
 *
 * @ingroup module_video
 */
void image_destroy_to_pool(image_t *image);

/**
 * @brief Clear image (set all pixels to black)
 * @param p Image to clear (must not be NULL)
 *
 * Sets all pixels in the image to black (RGB = 0, 0, 0). Useful for
 * resetting image state before filling with new data.
 *
 * @note Image dimensions are not modified (only pixel data is cleared).
 * @note This is equivalent to memset() of pixels array to zero.
 *
 * @ingroup module_video
 */
void image_clear(image_t *p);

/** @} */

/* ============================================================================
 * ASCII Conversion Functions
 * @{
 */

/**
 * @brief Print image as ASCII art (monochrome)
 * @param p Image to print (must not be NULL)
 * @param palette Character palette to use (or NULL for default ASCII palette)
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * Converts an image to ASCII art without color output. Uses character
 * palette to map pixel luminance to ASCII characters. Returns a
 * null-terminated string containing the ASCII art frame.
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note Character palette is used to map luminance to characters.
 * @note ASCII output has width*height characters (one character per pixel).
 *
 * @ingroup module_video
 */
char *image_print(const image_t *p, const char *palette);

/**
 * @brief Print image as ASCII art with color
 * @param p Image to print (must not be NULL)
 * @param palette Character palette to use (or NULL for default ASCII palette)
 * @return Allocated ASCII string with ANSI color codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art with color output. Uses character
 * palette for character mapping and ANSI color codes for color output.
 * Returns a null-terminated string containing the ASCII art frame
 * with embedded ANSI escape sequences.
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note ANSI color codes are embedded in the output string.
 * @note Color output requires terminal color support for proper display.
 *
 * @ingroup module_video
 */
char *image_print_color(const image_t *p, const char *palette);

/* ============================================================================
 * Capability-Aware ASCII Conversion Functions
 * @{
 */

// Capability-aware image printing functions
#include "../platform/terminal.h"

/**
 * @brief Print image with terminal capability awareness
 * @param image Image to print (must not be NULL)
 * @param caps Terminal capabilities structure (must not be NULL)
 * @param palette Character palette to use (or NULL for default ASCII palette)
 * @param luminance_palette Luminance-to-character mapping palette (must not be NULL, 256 elements)
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * Converts an image to ASCII art with automatic color mode selection
 * based on terminal capabilities. Automatically chooses the best
 * color mode (16-color, 256-color, or truecolor) for the terminal.
 *
 * COLOR MODE SELECTION:
 * - Truecolor: If terminal supports 24-bit RGB colors
 * - 256-color: If terminal supports extended ANSI palette
 * - 16-color: Fallback to standard ANSI colors
 * - Monochrome: If terminal has no color support
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note Color mode is selected automatically based on terminal capabilities.
 * @note This is the recommended function for capability-aware ASCII conversion.
 *
 * @ingroup module_video
 */
char *image_print_with_capabilities(const image_t *image, const terminal_capabilities_t *caps, const char *palette,
                                    const char luminance_palette[256]);

/**
 * @brief Print image using 256-color ANSI mode
 * @param image Image to print (must not be NULL)
 * @param palette Character palette to use (or NULL for default ASCII palette)
 * @return Allocated ASCII string with 256-color ANSI codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art using 256-color ANSI mode. Uses
 * extended ANSI color palette (0-255) for color output. Colors are
 * quantized to 256-color palette.
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note Requires terminal 256-color support for proper display.
 * @note Colors are quantized to 256-color palette automatically.
 *
 * @ingroup module_video
 */
char *image_print_256color(const image_t *image, const char *palette);

/**
 * @brief Print image using 16-color ANSI mode
 * @param image Image to print (must not be NULL)
 * @param palette Character palette to use (or NULL for default ASCII palette)
 * @return Allocated ASCII string with 16-color ANSI codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art using 16-color ANSI mode. Uses
 * standard ANSI color palette (0-15) for color output. Colors are
 * quantized to 16-color palette.
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note Requires terminal 16-color support for proper display.
 * @note Colors are quantized to 16-color palette automatically.
 *
 * @ingroup module_video
 */
char *image_print_16color(const image_t *image, const char *palette);

/**
 * @brief Print image using 16-color ANSI mode with dithering
 * @param image Image to print (must not be NULL)
 * @param palette Character palette to use (or NULL for default ASCII palette)
 * @return Allocated ASCII string with dithered 16-color ANSI codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art using 16-color ANSI mode with
 * Floyd-Steinberg dithering. Dithering improves color accuracy by
 * distributing quantization errors across neighboring pixels.
 *
 * DITHERING:
 * - Floyd-Steinberg error diffusion algorithm
 * - Distributes quantization errors to adjacent pixels
 * - Improves perceived color accuracy in 16-color mode
 * - Slight performance overhead compared to non-dithered mode
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note Dithering improves color accuracy but may add noise.
 * @note Requires terminal 16-color support for proper display.
 *
 * @ingroup module_video
 */
char *image_print_16color_dithered(const image_t *image, const char *palette);

/**
 * @brief Print image using 16-color ANSI mode with dithering and background colors
 * @param image Image to print (must not be NULL)
 * @param use_background Whether to use background colors (true) or foreground colors only (false)
 * @param palette Character palette to use (or NULL for default ASCII palette)
 * @return Allocated ASCII string with dithered 16-color ANSI codes (caller must free), or NULL on error
 *
 * Converts an image to ASCII art using 16-color ANSI mode with
 * Floyd-Steinberg dithering and optional background color support.
 * Background colors provide better color coverage for block-style
 * rendering.
 *
 * RENDERING MODES:
 * - Foreground colors: Character color is set (text color)
 * - Background colors: Character background is set (block color)
 * - Background mode provides better color coverage for block characters
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note Background colors require terminal background color support.
 * @note Dithering improves color accuracy but may add noise.
 *
 * @ingroup module_video
 */
char *image_print_16color_dithered_with_background(const image_t *image, bool use_background, const char *palette);

/** @} */

/* ============================================================================
 * Color Processing Functions
 * @{
 */

/**
 * @brief Quantize color to specified number of levels
 * @param r Red component (input/output, must not be NULL, range 0-255)
 * @param g Green component (input/output, must not be NULL, range 0-255)
 * @param b Blue component (input/output, must not be NULL, range 0-255)
 * @param levels Number of quantization levels (must be > 0)
 *
 * Quantizes RGB color components to a specified number of levels.
 * Reduces color precision for color palette mapping. Input colors
 * are modified in-place to quantized values.
 *
 * QUANTIZATION:
 * - Divides color range (0-255) into 'levels' bins
 * - Maps each component to nearest bin center
 * - Reduces color precision for palette mapping
 * - Used for 16-color and 256-color mode conversion
 *
 * @note Input/output parameters are modified in-place.
 * @note Quantization reduces color accuracy but enables palette mapping.
 * @note Typical values: 2 levels for 16-color, 6 levels for 256-color.
 *
 * @ingroup module_video
 */
void quantize_color(int *r, int *g, int *b, int levels);

/**
 * @brief Precalculate RGB palettes with color adjustment
 * @param red Red adjustment factor (1.0 = no adjustment, >1.0 = increase, <1.0 = decrease)
 * @param green Green adjustment factor (1.0 = no adjustment, >1.0 = increase, <1.0 = decrease)
 * @param blue Blue adjustment factor (1.0 = no adjustment, >1.0 = increase, <1.0 = decrease)
 *
 * Precalculates RGB color palettes with color adjustment factors.
 * Color adjustment allows brightness, contrast, and color balance
 * adjustments to be applied efficiently during ASCII conversion.
 *
 * COLOR ADJUSTMENT:
 * - Adjustment factors are multiplied with color components
 * - Factors > 1.0 increase color intensity
 * - Factors < 1.0 decrease color intensity
 * - Precalculated palettes improve conversion performance
 *
 * @note Precalculated palettes are cached for efficient conversion.
 * @note Color adjustment is applied during ASCII conversion.
 * @note Useful for brightness/contrast control.
 *
 * @ingroup module_video
 */
void precalc_rgb_palettes(const float red, const float green, const float blue);

/** @} */

/* ============================================================================
 * Image Resizing Functions
 * @{
 */

/**
 * @brief Resize image using nearest-neighbor interpolation
 * @param source Source image (must not be NULL)
 * @param dest Destination image (must be allocated and have valid dimensions)
 *
 * Resizes an image using nearest-neighbor interpolation. Fast but
 * may produce aliasing artifacts. Source pixels are sampled to
 * nearest destination pixel positions.
 *
 * NEAREST-NEIGHBOR INTERPOLATION:
 * - Fast algorithm (no interpolation calculations)
 * - May produce aliasing artifacts
 * - Pixel values are copied directly (no averaging)
 * - Suitable for integer scaling (2x, 3x, etc.)
 *
 * @note Destination image dimensions must be set before calling.
 * @note Source and destination images must be valid (not NULL).
 * @note Fast algorithm suitable for real-time resizing.
 *
 * @ingroup module_video
 */
void image_resize(const image_t *source, image_t *dest);

/**
 * @brief Resize image using bilinear interpolation
 * @param source Source image (must not be NULL)
 * @param dest Destination image (must be allocated and have valid dimensions)
 *
 * Resizes an image using bilinear interpolation. Produces smoother
 * results than nearest-neighbor but slower. Pixel values are
 * interpolated from four nearest source pixels.
 *
 * BILINEAR INTERPOLATION:
 * - Slower algorithm (interpolation calculations required)
 * - Produces smoother results (reduced aliasing)
 * - Pixel values are interpolated from 4 nearest neighbors
 * - Suitable for fractional scaling (1.5x, 2.3x, etc.)
 *
 * @note Destination image dimensions must be set before calling.
 * @note Source and destination images must be valid (not NULL).
 * @note Slower algorithm but produces better quality.
 *
 * @ingroup module_video
 */
void image_resize_interpolation(const image_t *source, image_t *dest);

/** @} */

/* ============================================================================
 * ANSI Color Code Generation Functions
 * @{
 */

/**
 * @brief Convert RGB to ANSI foreground color code
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Allocated ANSI color code string (caller must free), or NULL on error
 *
 * Converts RGB color values to ANSI foreground color escape sequence.
 * Returns a string containing the ANSI escape sequence for foreground
 * color (e.g., "\033[38;2;255;0;0m" for red).
 *
 * @note Returns NULL on error (memory allocation failure).
 * @note ANSI string must be freed by caller using free().
 * @note ANSI sequence format: ESC[38;2;r;g;bm (truecolor mode).
 * @note Color components are clamped to 0-255 range automatically.
 *
 * @ingroup module_video
 */
char *rgb_to_ansi_fg(int r, int g, int b);

/**
 * @brief Convert RGB to ANSI background color code
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Allocated ANSI color code string (caller must free), or NULL on error
 *
 * Converts RGB color values to ANSI background color escape sequence.
 * Returns a string containing the ANSI escape sequence for background
 * color (e.g., "\033[48;2;255;0;0m" for red background).
 *
 * @note Returns NULL on error (memory allocation failure).
 * @note ANSI string must be freed by caller using free().
 * @note ANSI sequence format: ESC[48;2;r;g;bm (truecolor mode).
 * @note Color components are clamped to 0-255 range automatically.
 *
 * @ingroup module_video
 */
char *rgb_to_ansi_bg(int r, int g, int b);

/**
 * @brief Convert RGB to 8-bit ANSI color codes
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param fg_code Output foreground color code (must not be NULL, range 0-255)
 * @param bg_code Output background color code (must not be NULL, range 0-255)
 *
 * Converts RGB color values to 8-bit ANSI color codes (256-color palette).
 * Quantizes RGB colors to 256-color palette indices for foreground and
 * background colors. Output codes are indices into 256-color ANSI palette.
 *
 * COLOR QUANTIZATION:
 * - RGB colors are quantized to 256-color palette
 * - Quantization uses standard 6x6x6 color cube
 * - Output codes are palette indices (0-255)
 * - Foreground and background codes may differ
 *
 * @note Output parameters are modified to contain palette indices.
 * @note Color codes are indices into 256-color ANSI palette.
 * @note Quantization reduces color accuracy but enables 256-color mode.
 *
 * @ingroup module_video
 */
void rgb_to_ansi_8bit(int r, int g, int b, int *fg_code, int *bg_code);

/** @} */

#ifdef _WIN32
#pragma pack(pop)
#endif

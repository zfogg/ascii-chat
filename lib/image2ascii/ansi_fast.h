#pragma once

/**
 * @file image2ascii/ansi_fast.h
 * @ingroup image2ascii
 * @brief Fast ANSI escape sequence generation
 *
 * This header provides optimized ANSI escape sequence generation using
 * precomputed lookup tables and run-length encoding for efficient
 * terminal output.
 *
 * The interface provides:
 * - Fast truecolor ANSI sequence generation
 * - Run-length encoded color output
 * - 256-color and 16-color ANSI mode support
 * - Floyd-Steinberg dithering for color approximation
 * - Terminal capability-aware color functions
 *
 * @note Uses precomputed decimal lookup tables for maximum performance.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include "image2ascii/simd/ascii_simd.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** @brief Access to internal decimal lookup table for testing */
extern dec3_t dec3[256];

/**
 * @brief Color mode for ANSI generation
 *
 * @ingroup image2ascii
 */
typedef enum {
  ANSI_MODE_FOREGROUND,           /**< Foreground color mode: \033[38;2;R;G;Bm */
  ANSI_MODE_BACKGROUND,           /**< Background color mode: \033[48;2;R;G;Bm */
  ANSI_MODE_FOREGROUND_BACKGROUND /**< Both foreground and background: \033[38;2;R;G;B;48;2;r;g;bm */
} ansi_color_mode_t;

/**
 * @brief Timing breakdown for performance measurement
 *
 * @ingroup image2ascii
 */
typedef struct {
  double pixel_time;  /**< Luminance/ASCII conversion time */
  double string_time; /**< ANSI string generation time */
  double output_time; /**< Terminal write time */
  double total_time;  /**< Overall frame time */
} ansi_timing_t;

/**
 * @brief Initialize the decimal lookup table
 *
 * Must be called once at startup before using any ANSI generation functions.
 *
 * @ingroup image2ascii
 */
void ansi_fast_init(void);

/**
 * @brief Append truecolor foreground ANSI sequence
 * @param dst Destination buffer pointer
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Append truecolor background ANSI sequence
 * @param dst Destination buffer pointer
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Append truecolor foreground and background ANSI sequence
 * @param dst Destination buffer pointer
 * @param fg_r Foreground red component (0-255)
 * @param fg_g Foreground green component (0-255)
 * @param fg_b Foreground blue component (0-255)
 * @param bg_r Background red component (0-255)
 * @param bg_g Background green component (0-255)
 * @param bg_b Background blue component (0-255)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_truecolor_fg_bg(char *dst, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b, uint8_t bg_r, uint8_t bg_g,
                             uint8_t bg_b);

/**
 * @brief Run-length encoded color output context
 *
 * Emits SGR (Select Graphic Rendition) sequences only when colors change,
 * reducing terminal output size.
 *
 * @ingroup image2ascii
 */
typedef struct {
  char *buffer;                   /**< Output buffer */
  size_t capacity;                /**< Buffer capacity */
  size_t length;                  /**< Current buffer length */
  uint8_t last_r, last_g, last_b; /**< Previous pixel color */
  ansi_color_mode_t mode;         /**< Color mode */
  bool first_pixel;               /**< First pixel flag */
} ansi_rle_context_t;

/**
 * @brief Initialize run-length encoding context
 * @param ctx Context structure to initialize
 * @param buffer Output buffer
 * @param capacity Buffer capacity in bytes
 * @param mode Color mode to use
 *
 * @ingroup image2ascii
 */
void ansi_rle_init(ansi_rle_context_t *ctx, char *buffer, size_t capacity, ansi_color_mode_t mode);

/**
 * @brief Add a pixel with run-length encoding
 * @param ctx RLE context structure
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param ascii_char ASCII character to output
 *
 * Only emits SGR when color changes from previous pixel.
 *
 * @ingroup image2ascii
 */
void ansi_rle_add_pixel(ansi_rle_context_t *ctx, uint8_t r, uint8_t g, uint8_t b, char ascii_char);

/**
 * @brief Finish RLE sequence
 * @param ctx RLE context structure
 *
 * Adds ANSI reset sequence and null terminator.
 *
 * @ingroup image2ascii
 */
void ansi_rle_finish(ansi_rle_context_t *ctx);

/**
 * @brief Initialize 256-color mode lookup tables
 *
 * Must be called before using 256-color functions.
 *
 * @ingroup image2ascii
 */
void ansi_fast_init_256color(void);

/**
 * @brief Append 256-color foreground ANSI sequence
 * @param dst Destination buffer pointer
 * @param color_index 256-color palette index (0-255)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_256color_fg(char *dst, uint8_t color_index);

/**
 * @brief Convert RGB to 256-color palette index
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return 256-color palette index (0-255)
 *
 * @ingroup image2ascii
 */
uint8_t rgb_to_256color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Initialize 16-color mode lookup tables
 *
 * Must be called before using 16-color functions.
 *
 * @ingroup image2ascii
 */
void ansi_fast_init_16color(void);

/**
 * @brief Append 16-color foreground ANSI sequence
 * @param dst Destination buffer pointer
 * @param color_index 16-color ANSI index (0-15)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_16color_fg(char *dst, uint8_t color_index);

/**
 * @brief Append 16-color background ANSI sequence
 * @param dst Destination buffer pointer
 * @param color_index 16-color ANSI index (0-15)
 * @return Pointer to end of appended sequence
 *
 * @ingroup image2ascii
 */
char *append_16color_bg(char *dst, uint8_t color_index);

/**
 * @brief Convert RGB to 16-color ANSI index
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return 16-color ANSI index (0-15)
 *
 * @ingroup image2ascii
 */
uint8_t rgb_to_16color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief RGB error structure for dithering
 *
 * @ingroup image2ascii
 */
typedef struct {
  int r; /**< Red error component */
  int g; /**< Green error component */
  int b; /**< Blue error component */
} rgb_error_t;

/**
 * @brief Get the actual RGB values for a 16-color ANSI index
 * @param color_index 16-color ANSI index (0-15)
 * @param r Output red component (0-255)
 * @param g Output green component (0-255)
 * @param b Output blue component (0-255)
 *
 * @ingroup image2ascii
 */
void get_16color_rgb(uint8_t color_index, uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * @brief Convert RGB to 16-color with Floyd-Steinberg dithering
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param x Pixel X coordinate
 * @param y Pixel Y coordinate
 * @param width Image width
 * @param height Image height
 * @param error_buffer Error diffusion buffer
 * @return 16-color ANSI index (0-15)
 *
 * Uses Floyd-Steinberg dithering algorithm for improved color approximation.
 *
 * @ingroup image2ascii
 */
uint8_t rgb_to_16color_dithered(int r, int g, int b, int x, int y, int width, int height, rgb_error_t *error_buffer);

// Terminal capability-aware color functions
#include "options.h"

/** @brief Alias for terminal color mode type */
typedef terminal_color_mode_t color_mode_t;

/**
 * @brief Append color foreground sequence for specified mode
 * @param dst Destination buffer pointer
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param mode Color mode to use
 * @return Pointer to end of appended sequence
 *
 * Automatically selects appropriate ANSI sequence based on color mode.
 *
 * @ingroup image2ascii
 */
char *append_color_fg_for_mode(char *dst, uint8_t r, uint8_t g, uint8_t b, color_mode_t mode);

// REMOVED: generate_ansi_frame_optimized function not implemented
// Complete frame generation with optimized ANSI string generation
// size_t generate_ansi_frame_optimized(const uint8_t *pixels, int width, int height, char *output_buffer,
//                                      size_t buffer_capacity, ansi_color_mode_t mode);

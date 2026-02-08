#pragma once

/**
 * @file video/digital_rain.h
 * @brief Matrix-style digital rain effect for ASCII frames
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * Implements a Matrix-inspired digital rain effect that can be applied to ASCII
 * frames as a post-processing step. The effect creates the illusion of "falling"
 * characters by modulating brightness in a sawtooth wave pattern.
 *
 * ALGORITHM OVERVIEW:
 * ===================
 * The digital rain effect is based on the observation that Matrix code rain isn't
 * actually falling characters - it's stationary characters whose brightness changes
 * to create the illusion of rain. Key concepts:
 *
 * 1. **Sawtooth Wave**: Each column has a brightness wave that repeats, creating
 *    multiple "raindrops" falling at different speeds.
 * 2. **Cursor/Tracer**: The bright leading character is detected where brightness
 *    increases (where the wave rises).
 * 3. **Organic Variation**: Random timing offsets and wobble functions prevent
 *    mechanical-looking patterns.
 *
 * IMPLEMENTATION:
 * ===============
 * - Per-column state tracking (timing, speed, phase)
 * - Brightness calculation using fractional time wrapping
 * - ANSI color code injection for brightness modulation
 * - Cursor highlighting with increased brightness
 * - Smooth brightness transitions via decay blending
 *
 * @note This effect is applied AFTER ASCII conversion
 * @note Works with both color and monochrome ASCII frames
 * @note Minimal performance impact (O(frame_size) single pass)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../common.h"
#include "../platform/terminal.h"

/* ============================================================================
 * Types and Constants
 * ============================================================================ */

/**
 * @brief Digital rain column state
 *
 * Tracks the state of a single column in the digital rain effect.
 * Each column has independent timing and speed characteristics.
 */
typedef struct {
  float time_offset;      ///< Random time offset for this column (prevents synchronization)
  float speed_multiplier; ///< Speed variation multiplier (0.5 to 1.0)
  float phase_offset;     ///< Phase offset for wobble variation
} digital_rain_column_t;

/**
 * @brief Digital rain effect context
 *
 * Maintains state for the entire digital rain effect across frames.
 * Tracks column states and effect parameters.
 */
typedef struct {
  digital_rain_column_t *columns; ///< Per-column state array
  int num_columns;                ///< Number of columns in grid
  int num_rows;                   ///< Number of rows in grid

  // Effect parameters
  float time;             ///< Current simulation time (accumulated)
  float fall_speed;       ///< Base fall speed multiplier
  float raindrop_length;  ///< Length of each raindrop (in cells)
  float brightness_decay; ///< Brightness smoothing factor (0-1)
  float animation_speed;  ///< Overall animation speed multiplier

  // Color parameters
  uint8_t color_r;         ///< Matrix green R component
  uint8_t color_g;         ///< Matrix green G component
  uint8_t color_b;         ///< Matrix green B component
  float cursor_brightness; ///< Cursor brightness multiplier
  bool rainbow_mode;       ///< True if rainbow color cycling is enabled

  // Frame tracking
  bool first_frame;           ///< True if this is the first frame
  float *previous_brightness; ///< Previous frame brightness (for smoothing)
} digital_rain_t;

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/**
 * @brief Default fall speed for raindrops
 *
 * Controls how fast the brightness wave moves down each column.
 * Higher values = faster falling rain.
 */
#define DIGITAL_RAIN_DEFAULT_FALL_SPEED 3.0f

/**
 * @brief Default raindrop length
 *
 * Controls the length of each raindrop in grid cells.
 * Smaller values = shorter, more frequent raindrops.
 */
#define DIGITAL_RAIN_DEFAULT_RAINDROP_LENGTH 12.0f

/**
 * @brief Default brightness decay factor
 *
 * Controls how quickly brightness transitions occur (0-1).
 * Higher values = more immediate transitions, lower = smoother.
 */
#define DIGITAL_RAIN_DEFAULT_BRIGHTNESS_DECAY 0.1f

/**
 * @brief Default animation speed multiplier
 *
 * Overall animation speed. 1.0 = normal speed.
 */
#define DIGITAL_RAIN_DEFAULT_ANIMATION_SPEED 1.0f

/**
 * @brief Default Matrix green color (R, G, B)
 *
 * Classic Matrix green: (0, 255, 80)
 */
#define DIGITAL_RAIN_DEFAULT_COLOR_R 0
#define DIGITAL_RAIN_DEFAULT_COLOR_G 255
#define DIGITAL_RAIN_DEFAULT_COLOR_B 80

/**
 * @brief Cursor brightness multiplier
 *
 * How much brighter the cursor should be compared to regular characters.
 */
#define DIGITAL_RAIN_DEFAULT_CURSOR_BRIGHTNESS 2.0f

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

/**
 * @brief Initialize digital rain effect context
 * @param num_columns Number of columns in the ASCII grid
 * @param num_rows Number of rows in the ASCII grid
 * @return Allocated digital rain context, or NULL on error
 *
 * Creates and initializes a digital rain effect context for the specified
 * grid dimensions. Allocates per-column state and initializes random offsets.
 *
 * @note Caller must call digital_rain_destroy() to free resources
 * @note Returns NULL on memory allocation failure
 */
digital_rain_t *digital_rain_init(int num_columns, int num_rows);

/**
 * @brief Destroy digital rain effect context
 * @param rain Digital rain context to destroy (can be NULL)
 *
 * Frees all resources associated with the digital rain context.
 * Safe to call with NULL pointer.
 */
void digital_rain_destroy(digital_rain_t *rain);

/* ============================================================================
 * Effect Application
 * ============================================================================ */

/**
 * @brief Apply digital rain effect to ASCII frame
 * @param rain Digital rain context (must not be NULL)
 * @param frame Input ASCII frame (must not be NULL, must be null-terminated)
 * @param delta_time Time elapsed since last frame in seconds
 * @return Allocated frame with digital rain effect applied, or NULL on error
 *
 * Applies the digital rain effect to an ASCII frame by analyzing character
 * positions and injecting ANSI color codes to modulate brightness. The effect:
 *
 * 1. Parses the frame to identify grid positions
 * 2. Calculates per-cell brightness using sawtooth wave pattern
 * 3. Detects cursor positions (brightness increases)
 * 4. Injects ANSI color codes to darken/brighten characters
 * 5. Returns new frame with effect applied
 *
 * FRAME FORMAT:
 * - Input can be plain ASCII or contain existing ANSI codes
 * - Output contains ANSI color codes for brightness modulation
 * - Frame dimensions must match num_columns x num_rows
 * - Newlines are preserved in output
 *
 * @note Caller must free returned frame string
 * @note Input frame is not modified
 * @note Works with both color and monochrome frames
 * @note Preserves existing ANSI codes (may override colors)
 */
char *digital_rain_apply(digital_rain_t *rain, const char *frame, float delta_time);

/**
 * @brief Reset digital rain effect state
 * @param rain Digital rain context (must not be NULL)
 *
 * Resets the digital rain effect to its initial state, clearing all
 * timing and brightness history. Useful when switching frames or
 * restarting the effect.
 */
void digital_rain_reset(digital_rain_t *rain);

/* ============================================================================
 * Parameter Adjustment
 * ============================================================================ */

/**
 * @brief Set digital rain fall speed
 * @param rain Digital rain context (must not be NULL)
 * @param speed Fall speed multiplier (suggested range: 0.1 to 5.0)
 */
void digital_rain_set_fall_speed(digital_rain_t *rain, float speed);

/**
 * @brief Set digital rain raindrop length
 * @param rain Digital rain context (must not be NULL)
 * @param length Raindrop length in cells (suggested range: 5.0 to 50.0)
 */
void digital_rain_set_raindrop_length(digital_rain_t *rain, float length);

/**
 * @brief Set digital rain color
 * @param rain Digital rain context (must not be NULL)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void digital_rain_set_color(digital_rain_t *rain, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set digital rain color from color filter
 * @param rain Digital rain context (must not be NULL)
 * @param filter Color filter to use for rain color
 *
 * Automatically sets the rain color based on the active color filter.
 * If filter is COLOR_FILTER_NONE, uses default Matrix green.
 */
void digital_rain_set_color_from_filter(digital_rain_t *rain, color_filter_t filter);

/** @} */

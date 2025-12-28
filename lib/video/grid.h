#pragma once

/**
 * @file video/grid.h
 * @brief 🎬 Grid Layout for Multi-Frame ASCII Art Display
 * @ingroup video
 * @addtogroup video
 * @{
 *
 * This header provides functions for arranging multiple ASCII frames
 * into a grid layout suitable for multi-user video display. Supports
 * various grid configurations with automatic layout optimization.
 *
 * CORE FEATURES:
 * ==============
 * - Grid layout creation from multiple ASCII frames
 * - Automatic grid dimension calculation
 * - Aspect ratio optimization for terminal display
 * - Separator rendering (| and _ characters for visual separation)
 * - Single-frame fallback with centering
 * - Multi-frame arrangement with configurable grid patterns
 *
 * GRID LAYOUT:
 * ============
 * The grid layout system arranges multiple video frames in a grid pattern:
 * - Calculates optimal rows and columns for source count
 * - Optimizes aspect ratio for terminal character dimensions
 * - Draws separators between cells for visual clarity
 * - Handles irregular frame counts (e.g., 3 frames in 2x2 grid)
 * - Centers single frame within target dimensions
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stddef.h>
#include <sys/types.h>

/**
 * @brief Frame source structure for grid layout
 *
 * Contains frame data and size for grid layout operations.
 * Used to combine multiple ASCII frames into a single grid layout.
 *
 * @note frame_data points to ASCII frame string (not owned by structure).
 * @note frame_size is the size of frame_data in bytes.
 * @note Frame data must remain valid during grid creation.
 *
 * @ingroup video
 */
typedef struct {
  const char *frame_data; ///< Frame data pointer (ASCII frame string, not owned)
  size_t frame_size;      ///< Frame data size in bytes (length of frame string)
} ascii_frame_source_t;

/**
 * @brief Create a grid layout from multiple ASCII frames
 * @param sources Array of frame sources (must not be NULL, must have source_count elements)
 * @param source_count Number of frame sources (must be > 0)
 * @param width Grid width in characters per frame (must be > 0)
 * @param height Grid height in characters per frame (must be > 0)
 * @param out_size Pointer to store output size in bytes (can be NULL)
 * @return Allocated grid frame string (caller must free), or NULL on error
 *
 * Combines multiple ASCII frames into a single grid layout. Arranges
 * frames in a grid pattern suitable for multi-user display. Each frame
 * is positioned in the grid according to its index in the sources array.
 *
 * GRID LAYOUT:
 * - Frames are arranged in grid pattern (rows and columns)
 * - Each frame has specified width and height in characters
 * - Grid dimensions are calculated based on source_count
 * - Output frame contains all frames arranged in grid
 *
 * @note Returns NULL on error (invalid sources, memory allocation failure).
 * @note Grid frame string must be freed by caller using free().
 * @note Frame sources must have valid frame_data and frame_size.
 * @note Grid layout is useful for multi-user video display.
 *
 * @par Example
 * @code
 * ascii_frame_source_t sources[4];
 * // Initialize sources...
 * char *grid = ascii_create_grid(sources, 4, 80, 40, NULL);
 * if (grid) {
 *     ascii_write(grid);
 *     free(grid);
 * }
 * @endcode
 *
 * @ingroup video
 */
char *ascii_create_grid(ascii_frame_source_t *sources, int source_count, int width, int height, size_t *out_size);

/** @} */


/**
 * @file video/grid.c
 * @ingroup video
 * @brief 🎬 Grid layout creation for multi-frame ASCII art display
 */

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "util/overflow.h"
#include "grid.h"

/**
 * Creates a grid layout from multiple ASCII frame sources with | and _ separators.
 *
 * Parameters:
 *   sources       Array of ASCII frame sources to combine
 *   source_count  Number of sources in the array
 *   width         Target width of the output grid
 *   height        Target height of the output grid
 *   out_size      Output parameter for the size of the returned buffer
 *
 * Returns:
 *   A newly allocated, null-terminated string containing the grid layout,
 *   or NULL on error. Caller must free the returned buffer.
 */
char *ascii_create_grid(ascii_frame_source_t *sources, int source_count, int width, int height, size_t *out_size) {
  if (!sources || source_count <= 0 || width <= 0 || height <= 0 || !out_size) {
    return NULL;
  }

  // If no sources, return empty frame

  // If only one source, center it properly to maintain aspect ratio and look good
  if (source_count == 1) {
    // Create a frame of the target size filled with spaces
    // Check for integer overflow before multiplication
    size_t w = (size_t)width;
    size_t h = (size_t)height;
    size_t w_times_h;
    if (checked_size_mul(w, h, &w_times_h) != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_INVALID_PARAM, "ascii_create_grid: dimensions would overflow: %dx%d", width, height);
      return NULL;
    }

    size_t w_times_h_plus_h;
    if (checked_size_add(w_times_h, h, &w_times_h_plus_h) != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_INVALID_PARAM, "ascii_create_grid: buffer size would overflow: %dx%d", width, height);
      return NULL;
    }

    size_t target_size;
    if (checked_size_add(w_times_h_plus_h, 1, &target_size) != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_INVALID_PARAM, "ascii_create_grid: buffer size would overflow: %dx%d", width, height);
      return NULL;
    }
    char *result;
    result = SAFE_MALLOC(target_size, char *);
    SAFE_MEMSET(result, target_size, ' ', target_size - 1);
    result[target_size - 1] = '\0';

    // Add newlines at the end of each row
    for (int row = 0; row < height; row++) {
      result[row * (width + 1) + width] = '\n';
    }

    // Copy the source frame into the result, line by line, centering it
    // Handle NULL frame_data gracefully
    const char *src_data = sources[0].frame_data;
    int src_pos = 0;
    int src_size = (int)sources[0].frame_size;

    // If source data is NULL or empty, just return the empty frame
    if (!src_data || src_size <= 0) {
      *out_size = target_size - 1; // Don't count null terminator
      return result;
    }

    // Count lines in source to calculate vertical padding
    int src_lines = 0;
    for (int i = 0; i < src_size; i++) {
      if (src_data[i] == '\n')
        src_lines++;
    }

    int v_padding = (height - src_lines) / 2;
    if (v_padding < 0)
      v_padding = 0;

    int dst_row = v_padding;
    src_pos = 0;

    while (src_pos < src_size && dst_row < height) {
      // Find end of current line in source
      int line_start = src_pos;
      int line_len = 0;
      while (src_pos < src_size && src_data[src_pos] != '\n') {
        line_len++;
        src_pos++;
      }

      // Calculate horizontal padding to center the line
      int h_padding = (width - line_len) / 2;
      if (h_padding < 0)
        h_padding = 0;

      // Copy line to result with padding
      // Use size_t for position calculation to prevent integer underflow
      size_t row_offset = (size_t)dst_row * (size_t)(width + 1);
      size_t dst_pos = row_offset + (size_t)h_padding;
      int copy_len = (line_len > width - h_padding) ? width - h_padding : line_len;

      if (copy_len > 0 && dst_pos + (size_t)copy_len < target_size) {
        SAFE_MEMCPY(&result[dst_pos], target_size - dst_pos, &src_data[line_start], (size_t)copy_len);
      }

      // Skip newline in source
      if (src_pos < src_size && src_data[src_pos] == '\n') {
        src_pos++;
      }

      dst_row++;
    }

    *out_size = target_size - 1; // Don't count null terminator
    return result;
  }

  // Multiple sources: create grid layout
  // Calculate grid dimensions that maximize the use of terminal space
  // Character aspect ratio: terminal chars are typically ~2x taller than wide
  float char_aspect = 2.0f;

  int grid_cols, grid_rows;
  float best_score = -1.0f;
  int best_cols = 1;
  int best_rows = source_count;

  // Try all possible grid configurations
  for (int test_cols = 1; test_cols <= source_count; test_cols++) {
    int test_rows = (int)ceil((double)source_count / test_cols);

    // Skip configurations with too many empty cells
    int empty_cells = (test_cols * test_rows) - source_count;
    if (empty_cells > source_count / 2)
      continue; // Don't waste more than 50% space

    // Calculate the size each cell would have
    int cell_width = (width - (test_cols - 1)) / test_cols;   // -1 per separator
    int cell_height = (height - (test_rows - 1)) / test_rows; // -1 per separator

    // Skip if cells would be too small
    if (cell_width < 10 || cell_height < 3)
      continue;

    // Calculate the aspect ratio of each cell (accounting for char aspect)
    float cell_aspect = ((float)cell_width / (float)cell_height) / char_aspect;

    // Score based on how close to square (1:1) each video cell would be
    // This naturally adapts to any terminal size
    float aspect_score = 1.0f - fabsf(logf(cell_aspect)); // log makes it symmetric around 1
    if (aspect_score < 0)
      aspect_score = 0;

    // Bonus for better space utilization
    float utilization = (float)source_count / (float)(test_cols * test_rows);

    // For 2 clients specifically, heavily weight the aspect score
    // This makes 2 clients naturally go horizontal on wide terminals and vertical on tall ones
    float total_score;
    if (source_count == 2) {
      // For 2 clients, we want the layout that gives the most square-ish cells
      total_score = aspect_score * 0.9f + utilization * 0.1f;
    } else {
      // For 3+ clients, balance aspect ratio with space utilization
      total_score = aspect_score * 0.7f + utilization * 0.3f;
    }

    // Small bonus for simpler grids (prefer 2x2 over 3x1, etc.)
    if (test_cols == test_rows) {
      total_score += 0.05f; // Slight preference for square grids
    }

    if (total_score > best_score) {
      best_score = total_score;
      best_cols = test_cols;
      best_rows = test_rows;
    }
  }

  grid_cols = best_cols;
  grid_rows = best_rows;

  // Calculate dimensions for each cell (leave 1 char for separators)
  int cell_width = (width - (grid_cols - 1)) / grid_cols;
  int cell_height = (height - (grid_rows - 1)) / grid_rows;

  if (cell_width < 10 || cell_height < 3) {
    // Too small for grid layout, just use first source
    char *result;
    result = SAFE_MALLOC(sources[0].frame_size + 1, char *);
    if (sources[0].frame_data && sources[0].frame_size > 0) {
      SAFE_MEMCPY(result, sources[0].frame_size + 1, sources[0].frame_data, sources[0].frame_size);
      result[sources[0].frame_size] = '\0';
      *out_size = sources[0].frame_size;
    } else {
      // Handle NULL or empty frame data
      result[0] = '\0';
      *out_size = 0;
    }
    return result;
  }

  // Allocate mixed frame buffer
  // Check for integer overflow before multiplication
  size_t w_sz = (size_t)width;
  size_t h_sz = (size_t)height;
  if (w_sz > SIZE_MAX / h_sz) {
    SET_ERRNO(ERROR_INVALID_PARAM, "ascii_create_grid: dimensions would overflow: %dx%d", width, height);
    return NULL;
  }
  size_t mixed_size = w_sz * h_sz + h_sz + 1; // +1 for null terminator, +height for newlines
  char *mixed_frame;
  mixed_frame = SAFE_MALLOC(mixed_size, char *);

  // Initialize mixed frame with spaces
  SAFE_MEMSET(mixed_frame, mixed_size, ' ', mixed_size - 1);
  mixed_frame[mixed_size - 1] = '\0';

  // Add newlines at the end of each row
  for (int row = 0; row < height; row++) {
    mixed_frame[row * (width + 1) + width] = '\n';
  }

  // Place each video source in the grid
  for (int src = 0; src < source_count; src++) {
    int grid_row = src / grid_cols;
    int grid_col = src % grid_cols;

    // Calculate position in mixed frame
    int start_row = grid_row * (cell_height + 1); // +1 for separator
    int start_col = grid_col * (cell_width + 1);  // +1 for separator

    // Parse source frame line by line and place in grid
    const char *src_data = sources[src].frame_data;
    int src_row = 0;
    int src_pos = 0;

    while (src_pos < (int)sources[src].frame_size && src_row < cell_height && start_row + src_row < height) {
      // Find end of current line in source
      int line_start = src_pos;
      while (src_pos < (int)sources[src].frame_size && src_data[src_pos] != '\n') {
        src_pos++;
      }
      int line_len = src_pos - line_start;

      // Copy line to mixed frame (truncate if too long)
      int copy_len = (line_len < cell_width) ? line_len : cell_width;
      if (copy_len > 0 && start_col + copy_len <= width) {
        int mixed_pos = (start_row + src_row) * (width + 1) + start_col;
        SAFE_MEMCPY(mixed_frame + mixed_pos, mixed_size - (size_t)mixed_pos, src_data + line_start, (size_t)copy_len);
      }

      // Move to next line
      if (src_pos < (int)sources[src].frame_size && src_data[src_pos] == '\n') {
        src_pos++;
      }
      src_row++;
    }

    // Draw separators with bounds checking to prevent buffer overflow
    if (grid_col < grid_cols - 1 && start_col + cell_width < width) {
      // Vertical separator
      for (int row = start_row; row < start_row + cell_height && row < height; row++) {
        size_t idx = (size_t)row * (size_t)(width + 1) + (size_t)(start_col + cell_width);
        if (idx < mixed_size - 1) { // -1 to preserve null terminator
          mixed_frame[idx] = '|';
        }
      }
    }

    if (grid_row < grid_rows - 1 && start_row + cell_height < height) {
      // Horizontal separator
      for (int col = start_col; col < start_col + cell_width && col < width; col++) {
        size_t idx = (size_t)(start_row + cell_height) * (size_t)(width + 1) + (size_t)col;
        if (idx < mixed_size - 1) { // -1 to preserve null terminator
          mixed_frame[idx] = '_';
        }
      }
      // Corner character where separators meet
      if (grid_col < grid_cols - 1 && start_col + cell_width < width) {
        size_t idx = (size_t)(start_row + cell_height) * (size_t)(width + 1) + (size_t)(start_col + cell_width);
        if (idx < mixed_size - 1) { // -1 to preserve null terminator
          mixed_frame[idx] = '+';
        }
      }
    }
  }

  *out_size = strlen(mixed_frame);
  return mixed_frame;
}

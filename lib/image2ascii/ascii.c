
/**
 * @file image2ascii/ascii.c
 * @ingroup image2ascii
 * @brief 🖼️ Image-to-ASCII conversion with SIMD acceleration, color matching, and terminal optimization
 */

#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>

#include "platform/abstraction.h"
#include "platform/terminal.h"

#include "ascii.h"
#include "common.h"
#include "image.h"
#include "util/aspect_ratio.h"
#include "os/webcam.h"
#include "options.h"
#include "simd/ascii_simd.h"

/* ============================================================================
 * ASCII Art Video Processing
 * ============================================================================
 */

asciichat_error_t ascii_read_init(unsigned short int webcam_index) {
  log_info("Initializing ASCII reader with webcam index %u", webcam_index);
  webcam_init(webcam_index);
  return ASCIICHAT_OK;
}

asciichat_error_t ascii_write_init(int fd, bool reset_terminal) {
  // Validate file descriptor
  if (fd < 0) {
    log_error("Invalid file descriptor %d", fd);
    return ERROR_INVALID_PARAM;
  }

  // Skip terminal control sequences in snapshot mode or when testing - just print raw ASCII
  const char *testing_env = SAFE_GETENV("TESTING");
  if (!opt_snapshot_mode && reset_terminal && testing_env == NULL) {
    console_clear(fd);
    cursor_reset(fd);

    // Disable echo using platform abstraction
    if (terminal_set_echo(false) != 0) {
      log_error("Failed to disable echo for fd %d", fd);
      return ERROR_TERMINAL;
    }
    // Hide cursor using platform abstraction
    if (terminal_hide_cursor(fd, true) != 0) {
      log_warn("Failed to hide cursor");
    }
  }
  log_debug("ASCII writer initialized");
  return ASCIICHAT_OK;
}

char *ascii_convert(image_t *original, const ssize_t width, const ssize_t height, const bool color,
                    const bool _aspect_ratio, const bool stretch, const char *palette_chars,
                    const char luminance_palette[256]) {
  if (original == NULL || !palette_chars || !luminance_palette) {
    log_error("ascii_convert: invalid parameters");
    return NULL;
  }

  // Check for empty strings
  if (palette_chars[0] == '\0' || luminance_palette[0] == '\0') {
    log_error("ascii_convert: empty palette strings");
    return NULL;
  }

  // Start with the target dimensions requested by the user (or detected from
  // the terminal). These can be modified by aspect_ratio() if stretching is
  // disabled and one of the dimensions was left to be calculated
  // automatically.
  ssize_t resized_width = width;
  ssize_t resized_height = height;

  // If stretch is enabled, use full dimensions, otherwise calculate aspect ratio
  if (_aspect_ratio) {
    // The server now provides images at width*2 x height pixels
    // The aspect_ratio function will handle terminal character aspect ratio
    aspect_ratio(original->w, original->h, resized_width, resized_height, stretch, &resized_width, &resized_height);
  }

  // Calculate padding for centering
  size_t pad_width = 0;
  size_t pad_height = 0;

  if (_aspect_ratio) {
    // Only calculate padding when not stretching
    ssize_t pad_width_ss = width > resized_width ? (width - resized_width) / 2 : 0;
    pad_width = (size_t)pad_width_ss;

    ssize_t pad_height_ss = height > resized_height ? (height - resized_height) / 2 : 0;
    pad_height = (size_t)pad_height_ss;
  }

  // Resize the captured frame to the aspect-correct dimensions.
  if (resized_width <= 0 || resized_height <= 0) {
    log_error("Invalid dimensions for resize: width=%zd, height=%zd", resized_width, resized_height);
    return NULL;
  }

  // Always resize to target dimensions
  image_t *resized = image_new((size_t)(int)resized_width, (size_t)(int)resized_height);
  if (!resized) {
    log_error("Failed to allocate resized image");
    return NULL;
  }

  image_clear(resized);
  image_resize(original, resized);

  char *ascii;
  if (color) {
    // Check for half-block mode first (requires NEON)
    if (opt_render_mode == RENDER_MODE_HALF_BLOCK) {
#if SIMD_SUPPORT_NEON
      // Use NEON half-block renderer
      const uint8_t *rgb_data = (const uint8_t *)resized->pixels;
      ascii = rgb_to_truecolor_halfblocks_neon(rgb_data, resized->w, resized->h, 0);
#else
      log_error("Half-block mode requires NEON support (ARM architecture)");
      image_destroy(resized);
      return NULL;
#endif
    } else {
#ifdef SIMD_SUPPORT
      // Standard color modes (foreground/background)
      bool use_background = (opt_render_mode == RENDER_MODE_BACKGROUND);
      ascii = image_print_color_simd(resized, use_background, false, palette_chars);
#else
      ascii = image_print_color(resized, palette_chars);
#endif
    }
  } else {
    // Use grayscale/monochrome conversion with client's palette
#ifdef SIMD_SUPPORT
    ascii = image_print_simd(resized, luminance_palette);
#else
    ascii = image_print(resized, palette_chars);
#endif
  }

  if (!ascii) {
    log_error("Failed to convert image to ASCII");
    image_destroy(resized);
    return NULL;
  }

  size_t ascii_len = strlen(ascii);
  if (ascii_len == 0) {
    log_error("ASCII conversion returned empty string (resized dimensions: %dx%d)", resized->w, resized->h);
    SAFE_FREE(ascii);
    image_destroy(resized);
    return NULL;
  }

  char *ascii_width_padded = ascii_pad_frame_width(ascii, pad_width);
  SAFE_FREE(ascii);

  char *ascii_padded = ascii_pad_frame_height(ascii_width_padded, pad_height);
  SAFE_FREE(ascii_width_padded);

  // Only destroy resized if we allocated it (not when using original directly)
  image_destroy(resized);

  return ascii_padded;
}

// Capability-aware ASCII conversion using terminal capabilities
char *ascii_convert_with_capabilities(image_t *original, const ssize_t width, const ssize_t height,
                                      const terminal_capabilities_t *caps, const bool use_aspect_ratio,
                                      const bool stretch, const char *palette_chars,
                                      const char luminance_palette[256]) {

  if (original == NULL || caps == NULL) {
    log_error("Invalid parameters for ascii_convert_with_capabilities");
    return NULL;
  }

  // Start with the target dimensions requested by the user
  ssize_t resized_width = width;
  ssize_t resized_height = height;

  // Height doubling for half-block mode is now handled by the server

  // If stretch is enabled, use full dimensions, otherwise calculate aspect ratio
  if (use_aspect_ratio && caps->render_mode != RENDER_MODE_HALF_BLOCK) {
    // Normal modes: apply aspect ratio correction
    aspect_ratio(original->w, original->h, resized_width, resized_height, stretch, &resized_width, &resized_height);
  }
  // Half-block mode: skip aspect ratio to preserve full doubled dimensions for 2x resolution

  // Calculate padding for centering
  size_t pad_width = 0;
  size_t pad_height = 0;

  if (use_aspect_ratio) {
    ssize_t pad_width_ss = width > resized_width ? (width - resized_width) / 2 : 0;
    pad_width = (size_t)pad_width_ss;

    ssize_t pad_height_ss = height > resized_height ? (height - resized_height) / 2 : 0;
    pad_height = (size_t)pad_height_ss;
  }

  // Resize the captured frame to the aspect-correct dimensions
  if (resized_width <= 0 || resized_height <= 0) {
    log_error("Invalid dimensions for resize: width=%zd, height=%zd", resized_width, resized_height);
    return NULL;
  }

  // PROFILING: Time image allocation and resize
  struct timespec prof_alloc_start, prof_alloc_end, prof_resize_start, prof_resize_end;
  (void)clock_gettime(CLOCK_MONOTONIC, &prof_alloc_start);

  image_t *resized = image_new((size_t)(int)resized_width, (size_t)(int)resized_height);
  if (!resized) {
    log_error("Failed to allocate resized image");
    return NULL;
  }

  image_clear(resized);

  (void)clock_gettime(CLOCK_MONOTONIC, &prof_alloc_end);
  (void)clock_gettime(CLOCK_MONOTONIC, &prof_resize_start);

  image_resize(original, resized);

  (void)clock_gettime(CLOCK_MONOTONIC, &prof_resize_end);

  // PROFILING: Time ASCII print
  struct timespec prof_print_start, prof_print_end;
  (void)clock_gettime(CLOCK_MONOTONIC, &prof_print_start);

  // Use the capability-aware image printing function with client's palette
  char *ascii = image_print_with_capabilities(resized, caps, palette_chars, luminance_palette);

  (void)clock_gettime(CLOCK_MONOTONIC, &prof_print_end);

  uint64_t alloc_time_us = ((uint64_t)prof_alloc_end.tv_sec * 1000000 + (uint64_t)prof_alloc_end.tv_nsec / 1000) -
                           ((uint64_t)prof_alloc_start.tv_sec * 1000000 + (uint64_t)prof_alloc_start.tv_nsec / 1000);
  uint64_t resize_time_us = ((uint64_t)prof_resize_end.tv_sec * 1000000 + (uint64_t)prof_resize_end.tv_nsec / 1000) -
                            ((uint64_t)prof_resize_start.tv_sec * 1000000 + (uint64_t)prof_resize_start.tv_nsec / 1000);
  uint64_t print_time_us = ((uint64_t)prof_print_end.tv_sec * 1000000 + (uint64_t)prof_print_end.tv_nsec / 1000) -
                           ((uint64_t)prof_print_start.tv_sec * 1000000 + (uint64_t)prof_print_start.tv_nsec / 1000);

  // PROFILING: Time padding
  struct timespec prof_pad_start, prof_pad_end;
  (void)clock_gettime(CLOCK_MONOTONIC, &prof_pad_start);

  if (!ascii) {
    log_error("Failed to convert image to ASCII using terminal capabilities");
    image_destroy(resized);
    return NULL;
  }

  size_t ascii_len = strlen(ascii);
  if (ascii_len == 0) {
    log_error("Capability-aware ASCII conversion returned empty string (resized dimensions: %dx%d)", resized->w,
              resized->h);
    SAFE_FREE(ascii);
    image_destroy(resized);
    return NULL;
  }

  char *ascii_width_padded = ascii_pad_frame_width(ascii, pad_width);
  SAFE_FREE(ascii);

  char *ascii_padded = ascii_pad_frame_height(ascii_width_padded, pad_height);
  SAFE_FREE(ascii_width_padded);

  (void)clock_gettime(CLOCK_MONOTONIC, &prof_pad_end);

  uint64_t pad_time_us = ((uint64_t)prof_pad_end.tv_sec * 1000000 + (uint64_t)prof_pad_end.tv_nsec / 1000) -
                         ((uint64_t)prof_pad_start.tv_sec * 1000000 + (uint64_t)prof_pad_start.tv_nsec / 1000);
  (void)alloc_time_us;
  (void)resize_time_us;
  (void)print_time_us;
  (void)pad_time_us;

  image_destroy(resized);

  return ascii_padded;
}

// NOTE: ascii_convert_with_custom_palette removed - use ascii_convert_with_capabilities() with enhanced
// terminal_capabilities_t

asciichat_error_t ascii_write(const char *frame) {
  if (frame == NULL) {
    log_warn("Attempted to write NULL frame");
    return ERROR_INVALID_PARAM;
  }

  // Skip cursor reset in snapshot mode or when testing - just print raw ASCII
  const char *testing_env = SAFE_GETENV("TESTING");
  if (!opt_snapshot_mode && testing_env == NULL) {
    cursor_reset(STDOUT_FILENO);
  }

  size_t frame_len = strlen(frame);
  size_t written = fwrite(frame, 1, frame_len, stdout);
  if (written != frame_len) {
    log_error("Failed to write ASCII frame");
    return ERROR_TERMINAL;
  }

  return ASCIICHAT_OK;
}

void ascii_write_destroy(int fd, bool reset_terminal) {
#if PLATFORM_WINDOWS
  (void)fd; // Unused on Windows - terminal operations use stdout directly
#endif
  // console_clear(fd);
  // cursor_reset(fd);
  // Skip cursor show in snapshot mode - leave terminal as-is
  if (!opt_snapshot_mode && reset_terminal) {
    // Show cursor using platform abstraction
    if (terminal_hide_cursor(fd, false) != 0) {
      log_warn("Failed to show cursor");
    }

    // Re-enable echo using platform abstraction
    if (terminal_set_echo(true) != 0) {
      log_warn("Failed to re-enable echo");
    }
  }
  log_debug("ASCII writer destroyed");
}

void ascii_read_destroy(void) {
  webcam_cleanup();
  log_debug("ASCII reader destroyed");
}

/*
 * Pads each line of an ASCII frame with a given number of leading space
 * characters. The function allocates a new buffer large enough to hold the
 * padded frame and returns a pointer to it. The caller is responsible for
 * freeing the returned buffer.
 *
 * Parameters:
 *   frame      The original, null-terminated ASCII frame. It is expected to
 *              contain `\n` at the end of every visual row.
 *   pad_left   How many space characters to add in front of every visual row.
 *
 * Returns:
 *   A newly allocated, null-terminated string that contains the padded frame
 *   on success, or NULL if either `frame`.
 */
char *ascii_pad_frame_width(const char *frame, size_t pad_left) {
  if (!frame) {
    return NULL;
  }

  if (pad_left == 0) {
    // Nothing to do; return a copy so the caller can free it safely without
    // worrying about the original allocation strategy.
    size_t orig_len = strlen(frame);
    char *copy;
    copy = SAFE_MALLOC(orig_len + 1, char *);
    SAFE_MEMCPY(copy, orig_len + 1, frame, orig_len + 1);
    return copy;
  }

  // Count how many visual rows we have (lines terminated by '\n') to determine
  // the final buffer size.
  size_t line_count = 1; // There is always at least the first line
  const char *char_in_frame = frame;
  while (*char_in_frame) {
    if (*char_in_frame == '\n') {
      line_count++;
    }
    char_in_frame++;
  }

  // Total length of the source plus padding.
  const size_t frame_len = strlen(frame);
  const size_t left_padding_len = line_count * pad_left;
  const size_t total_len = frame_len + left_padding_len;

  char *buffer;
  buffer = SAFE_MALLOC(total_len + 1, char *);

  // Build the padded frame.
  bool at_line_start = true;
  const char *src = frame;
  char *position = buffer;

  while (*src) {
    if (at_line_start) {
      // Insert the requested amount of spaces in front of every visual row.
      size_t remaining = (size_t)((ptrdiff_t)(buffer + total_len + 1) - (ptrdiff_t)position);
      SAFE_MEMSET(position, remaining, ' ', (size_t)pad_left);
      position += pad_left;
      at_line_start = false;
    }

    *position++ = *src;

    if (*src == '\n') {
      at_line_start = true;
    }

    src++;
  }

  *position = '\0';
  return buffer;
}

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
    size_t target_size = (size_t)width * (size_t)height + (size_t)height + 1; // +height for newlines, +1 for null
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

    // Safely calculate vertical padding (clamp to non-negative)
    int v_padding = 0;
    if (src_lines < height) {
      v_padding = (height - src_lines) / 2;
    }
    // If source is taller than available height, v_padding remains 0 (display from top)

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
      int dst_pos = dst_row * (width + 1) + h_padding;
      int copy_len = (line_len > width - h_padding) ? width - h_padding : line_len;

      if (copy_len > 0 && (size_t)dst_pos + (size_t)copy_len < target_size) {
        SAFE_MEMCPY(&result[dst_pos], target_size - (size_t)dst_pos, &src_data[line_start], (size_t)copy_len);
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
  // Algorithm: Grid Selection with Multi-Objective Scoring
  //
  // This algorithm finds the optimal rectangular grid (cols × rows) that maximizes:
  // 1. Cell readability (minimum size for ASCII art)
  // 2. Space utilization (minimize empty cells)
  // 3. Aspect ratio match (terminal character aspect ratio is ~2:1)
  // 4. Visual balance (prefer square-ish grids)
  // 5. Terminal shape matching (wide terminals prefer wide grids, etc.)
  //
  // The algorithm enumerates all possible grid configurations and scores each based
  // on these factors. O(N) complexity where N = source_count.
  //
  // Terminal character aspect ratio: chars are typically ~2:1 (height/width)
  // Optimal cell aspect for ASCII art: ~2:1 width/height
  const float TARGET_CELL_ASPECT = 2.0f;      // Ideal cell aspect ratio
  const int MIN_CELL_WIDTH = 15;               // Minimum readable width
  const int MIN_CELL_HEIGHT = 6;               // Minimum readable height

  int grid_cols, grid_rows;
  float best_score = -1.0f;
  int best_cols = 1;
  int best_rows = source_count;

  // Try all possible grid configurations
  for (int test_cols = 1; test_cols <= source_count; test_cols++) {
    int test_rows = (int)ceil((double)source_count / test_cols);

    // Filter 1: Skip configurations with excessive empty cells
    // Don't waste more than one full row/column of space
    int empty_cells = (test_cols * test_rows) - source_count;
    int max_empty = (test_cols < test_rows) ? test_cols : test_rows;
    if (empty_cells > max_empty)
      continue;

    // Calculate the size each cell would have (accounting for separators)
    // Note: separators are 1 char wide/tall between cells
    int cell_width = (width - (test_cols > 1 ? test_cols - 1 : 0)) / test_cols;
    int cell_height = (height - (test_rows > 1 ? test_rows - 1 : 0)) / test_rows;

    // Filter 2: Skip if cells would be too small to read
    if (cell_width < MIN_CELL_WIDTH || cell_height < MIN_CELL_HEIGHT)
      continue;

    // MULTI-OBJECTIVE SCORING
    // Score 1: Aspect Ratio (0-1, higher is better)
    // Target 2:1 aspect ratio (optimal for ASCII art with terminal chars)
    float cell_aspect = (float)cell_width / (float)cell_height;
    float aspect_diff = fabsf(cell_aspect - TARGET_CELL_ASPECT);
    float aspect_score = 1.0f / (1.0f + aspect_diff);  // Closer to 2.0 = higher score

    // Score 2: Space Utilization (0-1, higher is better)
    // Minimize wasted space with empty cells
    int total_cells = test_cols * test_rows;
    float utilization_score = (float)source_count / (float)total_cells;

    // Score 3: Cell Size (0-1, higher is better)
    // Larger cells are more readable. Normalize against minimum (15×6).
    // Cap at 3x minimum (additional size doesn't improve much)
    float size_w = fminf((float)cell_width / (float)MIN_CELL_WIDTH, 3.0f);
    float size_h = fminf((float)cell_height / (float)MIN_CELL_HEIGHT, 3.0f);
    float size_score = (size_w * size_h) / 9.0f;  // Normalize to 0-1

    // Score 4: Square Grid Preference (0-1, higher is better)
    // Prefer balanced grids (cols ≈ rows) over extreme aspect ratios
    float grid_aspect_diff = fabsf((float)test_cols - (float)test_rows);
    float shape_score = 1.0f / (1.0f + grid_aspect_diff);

    // Score 5: Terminal Shape Matching (0-1, higher is better)
    // Wide terminals should prefer wide grids, tall terminals prefer tall grids
    float terminal_aspect = (float)width / (float)height;
    float grid_ratio = (float)test_cols / (float)test_rows;
    float aspect_match = 1.0f / (1.0f + fabsf(terminal_aspect - grid_ratio));

    // WEIGHTED COMBINATION
    // Priorities: readability > utilization > aspect > balance > shape match
    float total_score =
        aspect_score * 0.30f +      // Cell aspect ratio (important)
        utilization_score * 0.25f +  // Space utilization (minimize waste)
        size_score * 0.35f +         // Cell size/readability (MOST important)
        shape_score * 0.05f +        // Square grid preference (minor)
        aspect_match * 0.05f;        // Terminal shape matching (minor)

    log_debug_every(1000000, "Grid %dx%d: cells %dx%d, aspect=%.2f, util=%.2f, size=%.2f, score=%.3f",
                    test_cols, test_rows, cell_width, cell_height, aspect_score, utilization_score,
                    size_score, total_score);

    if (total_score > best_score) {
      best_score = total_score;
      best_cols = test_cols;
      best_rows = test_rows;
    }
  }

  grid_cols = best_cols;
  grid_rows = best_rows;

  log_debug("Selected grid layout: %dx%d (score: %.3f)", grid_cols, grid_rows, best_score);

  // Calculate dimensions for each cell
  // Only subtract separators if there are actually separators (>1 rows/cols)
  int cell_width = (width - (grid_cols > 1 ? grid_cols - 1 : 0)) / grid_cols;
  int cell_height = (height - (grid_rows > 1 ? grid_rows - 1 : 0)) / grid_rows;

  if (cell_width < MIN_CELL_WIDTH || cell_height < MIN_CELL_HEIGHT) {
    // Too small for grid layout, just use first source
    // This can happen if terminal is very small or many clients trying to fit
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
  size_t mixed_size =
      (size_t)width * (size_t)height + (size_t)height + 1; // +1 for null terminator, +height for newlines
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

    // Draw separators
    if (grid_col < grid_cols - 1 && start_col + cell_width < width) {
      // Vertical separator
      for (int row = start_row; row < start_row + cell_height && row < height; row++) {
        mixed_frame[row * (width + 1) + start_col + cell_width] = '|';
      }
    }

    if (grid_row < grid_rows - 1 && start_row + cell_height < height) {
      // Horizontal separator
      for (int col = start_col; col < start_col + cell_width && col < width; col++) {
        mixed_frame[(start_row + cell_height) * (width + 1) + col] = '_';
      }
      // Corner character where separators meet
      if (grid_col < grid_cols - 1 && start_col + cell_width < width) {
        mixed_frame[(start_row + cell_height) * (width + 1) + start_col + cell_width] = '+';
      }
    }
  }

  *out_size = strlen(mixed_frame);
  return mixed_frame;
}

/**
 * Adds vertical padding (blank lines) to center a frame vertically.
 *
 * Parameters:
 *   frame        The input ASCII frame to pad vertically.
 *   pad_top      Number of blank lines to add at the top.
 *
 * Returns:
 *   A newly allocated, null-terminated string with vertical padding,
 *   or NULL if frame is NULL.
 */
char *ascii_pad_frame_height(const char *frame, size_t pad_top) {
  if (!frame) {
    return NULL;
  }

  if (pad_top == 0) {
    // Nothing to do; return a copy because the caller knows to free() the value.
    size_t orig_len = strlen(frame);
    char *copy;
    copy = SAFE_MALLOC(orig_len + 1, char *);
    SAFE_MEMCPY(copy, orig_len + 1, frame, orig_len + 1);
    return copy;
  }

  // Calculate buffer size needed
  size_t frame_len = strlen(frame);
  size_t top_padding_len = pad_top; // Just newlines, no spaces
  size_t total_len = top_padding_len + frame_len;

  char *buffer;
  buffer = SAFE_MALLOC(total_len + 1, char *);

  char *position = buffer;

  // Add top padding (blank lines - just newlines)
  for (size_t i = 0; i < pad_top; i++) {
    *position++ = '\n';
  }

  // Copy the original frame
  size_t remaining = total_len + 1 - pad_top;
  SAFE_MEMCPY(position, remaining, frame, frame_len);
  position += frame_len;
  *position = '\0';

  return buffer;
}

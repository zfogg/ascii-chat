#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>

#include "platform.h"

#if !PLATFORM_WINDOWS
#include <ncurses.h>
#endif

#include "ascii.h"
#include "simd/ascii_simd.h"
#include "common.h"
#include "image.h"
#include "aspect_ratio.h"
#include "webcam.h"
#include "options.h"

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
    return ASCIICHAT_ERR_INVALID_PARAM;
  }

  // Skip terminal control sequences in snapshot mode or when testing - just print raw ASCII
  if (!opt_snapshot_mode && reset_terminal && getenv("TESTING") == NULL) {
    console_clear(fd);
    cursor_reset(fd);

#if !PLATFORM_WINDOWS
    struct termios termios;
    if (tcgetattr(fd, &termios) != 0) {
      log_error("Failed to get terminal attributes for fd %d", fd);
      return ASCIICHAT_ERR_TERMINAL;
    }
    termios.c_lflag &= ~ECHO;
    tcsetattr(fd, TCSANOW, &termios);
#endif
#if !PLATFORM_WINDOWS
    // Disable blink for the terminal cursor
    if (curs_set(0) == ERR) {
      log_warn("Failed to DISable cursor blink with curs_set(0)");
    }
#endif

    // FIXME: make cursor_hide() work
    // cursor_hide(fd); // this doesn't work
    printf("\e[?25l"); // this works
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
  image_t *resized = image_new((int)resized_width, (int)resized_height);
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
#ifdef SIMD_SUPPORT_NEON
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
    free(ascii);
    image_destroy(resized);
    return NULL;
  }

  char *ascii_width_padded = ascii_pad_frame_width(ascii, pad_width);
  free(ascii);

  char *ascii_padded = ascii_pad_frame_height(ascii_width_padded, pad_height);
  free(ascii_width_padded);

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

  image_t *resized = image_new((int)resized_width, (int)resized_height);
  if (!resized) {
    log_error("Failed to allocate resized image");
    return NULL;
  }

  image_clear(resized);
  image_resize(original, resized);

  // Use the capability-aware image printing function with client's palette
  char *ascii = image_print_with_capabilities(resized, caps, palette_chars, luminance_palette);

  if (!ascii) {
    log_error("Failed to convert image to ASCII using terminal capabilities");
    image_destroy(resized);
    return NULL;
  }

  size_t ascii_len = strlen(ascii);
  if (ascii_len == 0) {
    log_error("Capability-aware ASCII conversion returned empty string (resized dimensions: %dx%d)", resized->w,
              resized->h);
    free(ascii);
    image_destroy(resized);
    return NULL;
  }

  char *ascii_width_padded = ascii_pad_frame_width(ascii, pad_width);
  free(ascii);

  char *ascii_padded = ascii_pad_frame_height(ascii_width_padded, pad_height);
  free(ascii_width_padded);

  image_destroy(resized);

  return ascii_padded;
}

// NOTE: ascii_convert_with_custom_palette removed - use ascii_convert_with_capabilities() with enhanced
// terminal_capabilities_t

asciichat_error_t ascii_write(const char *frame) {
  if (frame == NULL) {
    log_warn("Attempted to write NULL frame");
    return ASCIICHAT_ERR_INVALID_PARAM;
  }

  // Skip cursor reset in snapshot mode or when testing - just print raw ASCII
  if (!opt_snapshot_mode && getenv("TESTING") == NULL) {
    cursor_reset(STDOUT_FILENO);
  }

  size_t frame_len = strlen(frame);
  size_t written = fwrite(frame, 1, frame_len, stdout);
  if (written != frame_len) {
    log_error("Failed to write ASCII frame");
    return ASCIICHAT_ERR_TERMINAL;
  }

  return ASCIICHAT_OK;
}

void ascii_write_destroy(int fd, bool reset_terminal) {
  // console_clear(fd);
  // cursor_reset(fd);
  // Skip cursor show in snapshot mode - leave terminal as-is
  if (!opt_snapshot_mode && reset_terminal) {
    // FIXME: make cursor_show() work
    // cursor_show(fd); // this doesn't work
    printf("\033[?25h"); // this works

#if !PLATFORM_WINDOWS
    struct termios termios;
    tcgetattr(fd, &termios);
    termios.c_lflag |= ECHO;
    tcsetattr(fd, TCSANOW, &termios);
#endif
#if !PLATFORM_WINDOWS
    // Enable blink for the terminal cursor
    if (curs_set(1) == ERR) {
      log_warn("Failed to ENable cursor blink with curs_set(1)");
    }
#endif
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
    SAFE_MALLOC(copy, orig_len + 1, char *);
    memcpy(copy, frame, orig_len + 1);
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
  SAFE_MALLOC(buffer, total_len + 1, char *);

  // Build the padded frame.
  bool at_line_start = true;
  const char *src = frame;
  char *position = buffer;

  while (*src) {
    if (at_line_start) {
      // Insert the requested amount of spaces in front of every visual row.
      memset(position, ' ', pad_left);
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

  // If only one source, ensure it fills the target dimensions
  if (source_count == 1) {
    // Create a frame of the target size filled with spaces
    size_t target_size = width * height + height + 1; // +height for newlines, +1 for null
    char *result;
    SAFE_MALLOC(result, target_size, char *);
    memset(result, ' ', target_size - 1);
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
      int dst_pos = dst_row * (width + 1) + h_padding;
      int copy_len = (line_len > width - h_padding) ? width - h_padding : line_len;

      if (copy_len > 0 && dst_pos + copy_len < (int)target_size) {
        memcpy(&result[dst_pos], &src_data[line_start], copy_len);
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
  // Calculate grid dimensions (try to make it roughly square)
  int grid_cols = (int)ceil(sqrt(source_count));
  int grid_rows = (int)ceil((double)source_count / grid_cols);

  // Calculate dimensions for each cell (leave 1 char for separators)
  int cell_width = (width - (grid_cols - 1)) / grid_cols;
  int cell_height = (height - (grid_rows - 1)) / grid_rows;

  if (cell_width < 10 || cell_height < 3) {
    // Too small for grid layout, just use first source
    char *result;
    SAFE_MALLOC(result, sources[0].frame_size + 1, char *);
    if (sources[0].frame_data && sources[0].frame_size > 0) {
      memcpy(result, sources[0].frame_data, sources[0].frame_size);
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
  size_t mixed_size = width * height + height + 1; // +1 for null terminator, +height for newlines
  char *mixed_frame;
  SAFE_MALLOC(mixed_frame, mixed_size, char *);

  // Initialize mixed frame with spaces
  memset(mixed_frame, ' ', mixed_size - 1);
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
        memcpy(mixed_frame + mixed_pos, src_data + line_start, copy_len);
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
    SAFE_MALLOC(copy, orig_len + 1, char *);
    memcpy(copy, frame, orig_len + 1);
    return copy;
  }

  // Calculate buffer size needed
  size_t frame_len = strlen(frame);
  size_t top_padding_len = pad_top; // Just newlines, no spaces
  size_t total_len = top_padding_len + frame_len;

  char *buffer;
  SAFE_MALLOC(buffer, total_len + 1, char *);

  char *position = buffer;

  // Add top padding (blank lines - just newlines)
  for (size_t i = 0; i < pad_top; i++) {
    *position++ = '\n';
  }

  // Copy the original frame
  memcpy(position, frame, frame_len);
  position += frame_len;
  *position = '\0';

  return buffer;
}


/**
 * @file video/ascii.c
 * @ingroup video
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

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>

#include <ascii-chat/video/ascii.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/util/aspect_ratio.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/simd/ascii_simd.h>
#include <ascii-chat/video/scalar/halfblock.h>

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

  // Only apply terminal control sequences if:
  // 1. reset_terminal is true (caller wants terminal reset)
  // 2. terminal_should_use_control_sequences() confirms it's safe (TTY, not snapshot, not testing)
  if (reset_terminal && terminal_should_use_control_sequences(fd)) {
    console_clear(fd);
    cursor_reset(fd);

    // Disable echo using platform abstraction
    if (terminal_set_echo(false) != 0) {
      log_error("Failed to disable echo for fd %d", fd);
      return ERROR_TERMINAL;
    }
    // Hide cursor using platform abstraction
    if (terminal_cursor_hide() != 0) {
      log_warn("Failed to hide cursor");
    }
  }
  log_dev("ASCII writer initialized");
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

  // Validate dimensions fit in image_t's int fields before casting
  if (resized_width > INT_MAX || resized_height > INT_MAX) {
    log_error("Dimensions exceed INT_MAX: width=%zd, height=%zd", resized_width, resized_height);
    return NULL;
  }

  // Always resize to target dimensions
  image_t *resized = image_new((size_t)resized_width, (size_t)resized_height);
  if (!resized) {
    log_error("Failed to allocate resized image");
    return NULL;
  }

  image_clear(resized);
  image_resize(original, resized);

  char *ascii;
  if (color) {
    // Check for half-block mode first
    if (GET_OPTION(render_mode) == RENDER_MODE_HALF_BLOCK) {
      const uint8_t *rgb_data = (const uint8_t *)resized->pixels;
#if SIMD_SUPPORT_NEON
      // Use NEON half-block renderer (optimized SIMD path)
      log_dev("Using NEON halfblock renderer");
      ascii = rgb_to_truecolor_halfblocks_neon(rgb_data, resized->w, resized->h, 0);
#else
      // Fallback to scalar halfblock renderer (works on all platforms)
      log_dev("Using scalar halfblock renderer (NEON not available)");
      ascii = rgb_to_truecolor_halfblocks_scalar(rgb_data, resized->w, resized->h, 0);
#endif
    } else {
#ifdef SIMD_SUPPORT
      // Standard color modes (foreground/background)
      bool use_background = (GET_OPTION(render_mode) == RENDER_MODE_BACKGROUND);
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
                                      const bool stretch, const char *palette_chars) {

  if (original == NULL || caps == NULL) {
    log_error("Invalid parameters for ascii_convert_with_capabilities");
    return NULL;
  }

  // Start with the target dimensions requested by the user
  ssize_t resized_width = width;
  ssize_t resized_height = height;

  // Half-block mode doubles height for 2x vertical resolution
  if (caps->render_mode == RENDER_MODE_HALF_BLOCK) {
    resized_height = resized_height * 2;
  }

  // If stretch is enabled, use full dimensions, otherwise calculate aspect ratio
  if (use_aspect_ratio && caps->render_mode != RENDER_MODE_HALF_BLOCK) {
    // Normal modes: apply aspect ratio correction
    aspect_ratio(original->w, original->h, resized_width, resized_height, stretch, &resized_width, &resized_height);
  }
  // Half-block mode: skip aspect ratio to preserve full doubled dimensions for 2x resolution

  // Calculate padding for centering (only if client wants padding)
  size_t pad_width = 0;
  size_t pad_height = 0;

  if (use_aspect_ratio && caps->wants_padding) {
    ssize_t pad_width_ss = width > resized_width ? (width - resized_width) / 2 : 0;
    pad_width = (size_t)pad_width_ss;

    ssize_t pad_height_ss = height > resized_height ? (height - resized_height) / 2 : 0;
    pad_height = (size_t)pad_height_ss;

    log_debug_every(10 * US_PER_SEC_INT,
                    "ascii_convert_with_capabilities: width=%zd, height=%zd, resized_width=%zd, resized_height=%zd, "
                    "pad_width=%zu, pad_height=%zu, stretch=%d, wants_padding=%d",
                    width, height, resized_width, resized_height, pad_width, pad_height, stretch, caps->wants_padding);
  } else if (!caps->wants_padding) {
    log_debug_every(10 * US_PER_SEC_INT,
                    "ascii_convert_with_capabilities: padding disabled (wants_padding=false), width=%zd, height=%zd",
                    width, height);
  }

  // Resize the captured frame to the aspect-correct dimensions
  if (resized_width <= 0 || resized_height <= 0) {
    log_error("Invalid dimensions for resize: width=%zd, height=%zd", resized_width, resized_height);
    return NULL;
  }

  // Validate dimensions fit in image_t's int fields before casting
  if (resized_width > INT_MAX || resized_height > INT_MAX) {
    log_error("Dimensions exceed INT_MAX: width=%zd, height=%zd", resized_width, resized_height);
    return NULL;
  }

  // PROFILING: Time image allocation and resize
  START_TIMER("image_alloc");
  uint64_t prof_alloc_start_ns = time_get_ns();

  image_t *resized = image_new((size_t)resized_width, (size_t)resized_height);
  if (!resized) {
    log_error("Failed to allocate resized image");
    return NULL;
  }

  image_clear(resized);

  uint64_t prof_alloc_end_ns = time_get_ns();
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "image_alloc",
                           "IMAGE_ALLOC: Alloc+clear complete (%.2f ms)");

  START_TIMER("image_resize");
  uint64_t prof_resize_start_ns = prof_alloc_end_ns;

  image_resize(original, resized);

  uint64_t prof_resize_end_ns = time_get_ns();
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "image_resize",
                           "IMAGE_RESIZE: Resize complete (%.2f ms)");

  // PROFILING: Time ASCII print
  uint64_t prof_print_start_ns = prof_resize_end_ns;

  // DEBUG: Log dimensions going to renderer
  log_debug_every(10 * US_PER_SEC_INT,
                  "ascii_convert: original=%dx%d, requested=%zdx%zd, resized=%dx%d, pad=%zux%zu (mode=%d)", original->w,
                  original->h, width, height, resized->w, resized->h, pad_width, pad_height, caps->render_mode);

  // Use the capability-aware image printing function with client's palette
  START_TIMER("image_print_with_capabilities");
  char *ascii = image_print_with_capabilities(resized, caps, palette_chars);

  uint64_t prof_print_end_ns = time_get_ns();
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "image_print_with_capabilities",
                           "IMAGE_PRINT: Print complete (%.2f ms)");

  uint64_t alloc_time_us = time_ns_to_us(time_elapsed_ns(prof_alloc_start_ns, prof_alloc_end_ns));
  uint64_t resize_time_us = time_ns_to_us(time_elapsed_ns(prof_resize_start_ns, prof_resize_end_ns));
  uint64_t print_time_us = time_ns_to_us(time_elapsed_ns(prof_print_start_ns, prof_print_end_ns));

  // PROFILING: Time padding
  START_TIMER("ascii_padding");
  uint64_t prof_pad_start_ns = time_get_ns();

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

  uint64_t prof_pad_end_ns = time_get_ns();
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 2 * NS_PER_MS_INT, "ascii_padding",
                           "ASCII_PADDING: Padding complete (%.2f ms)");

  uint64_t pad_time_ns = time_elapsed_ns(prof_pad_start_ns, prof_pad_end_ns);
  char alloc_str[32], resize_str[32], print_str[32], pad_str[32], total_str[32];
  time_pretty(alloc_time_us * 1000, -1, alloc_str, sizeof(alloc_str));
  time_pretty(resize_time_us * 1000, -1, resize_str, sizeof(resize_str));
  time_pretty(print_time_us * 1000, -1, print_str, sizeof(print_str));
  time_pretty(pad_time_ns, -1, pad_str, sizeof(pad_str));
  uint64_t total_time_ns = (alloc_time_us + resize_time_us + print_time_us) * 1000 + pad_time_ns;
  time_pretty(total_time_ns, -1, total_str, sizeof(total_str));
  log_dev("ASCII_BREAKDOWN: alloc=%s, resize=%s, print=%s, pad=%s (total=%s)",
          alloc_str, resize_str, print_str, pad_str, total_str);

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

  // Only reset cursor if output is connected to a TTY (not piped/redirected)
  if (terminal_should_use_control_sequences(STDOUT_FILENO)) {
    cursor_reset(STDOUT_FILENO);
  }

  size_t frame_len = strlen(frame);
  // Write all frame data with automatic retry on transient errors
  platform_write_all(STDOUT_FILENO, frame, frame_len);

  // Flush C stdio buffer and terminal to ensure piped output is written immediately
  (void)fflush(stdout);
  terminal_flush(STDOUT_FILENO);

  return ASCIICHAT_OK;
}

void ascii_write_destroy(int fd, bool reset_terminal) {
#if PLATFORM_WINDOWS
  (void)fd; // Unused on Windows - terminal operations use stdout directly
#endif
  // console_clear(fd);
  // cursor_reset(fd);
  // Only restore terminal state if:
  // 1. reset_terminal is true (caller wants terminal restore)
  // 2. terminal_should_use_control_sequences() confirms it's safe (TTY, not snapshot, not testing)
  if (reset_terminal && terminal_should_use_control_sequences(fd)) {
    // Show cursor using platform abstraction
    if (terminal_cursor_show() != 0) {
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
  webcam_destroy();
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
    SET_ERRNO(ERROR_INVALID_PARAM, "ascii_pad_frame_width: frame is NULL");
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
 * Calculate the visual width of a string, excluding ANSI escape sequences.
 * ANSI escape sequences are invisible control codes that don't consume terminal columns.
 *
 * @param data     The string to measure
 * @param data_len Length of the string in bytes
 * @return         Number of visible characters (terminal columns)
 */
static int ansi_visual_width(const char *data, int data_len) {
  int visual_width = 0;
  int i = 0;

  while (i < data_len) {
    if (data[i] == '\033' && i + 1 < data_len && data[i + 1] == '[') {
      // Skip ANSI CSI sequence: ESC [ <params> <terminator>
      i += 2; // Skip ESC [
      while (i < data_len) {
        char c = data[i];
        i++;
        // ANSI CSI sequences end with a byte in the range 0x40-0x7E (@ through ~)
        if (c >= '@' && c <= '~') {
          break;
        }
      }
    } else {
      // Visible character
      visual_width++;
      i++;
    }
  }

  return visual_width;
}

/**
 * Truncate a string to a target visual width while preserving complete ANSI sequences.
 * Returns the byte position where truncation should occur.
 *
 * @param data         The string to truncate
 * @param data_len     Length of the string in bytes
 * @param target_width Target visual width (number of visible characters)
 * @return             Byte position to truncate at (includes all complete ANSI sequences)
 */
static int ansi_truncate_to_visual_width(const char *data, int data_len, int target_width) {
  int visual_width = 0;
  int i = 0;

  while (i < data_len && visual_width < target_width) {
    if (data[i] == '\033' && i + 1 < data_len && data[i + 1] == '[') {
      // Skip ANSI CSI sequence: ESC [ <params> <terminator>
      i += 2; // Skip ESC [
      while (i < data_len) {
        char c = data[i];
        i++;
        // ANSI CSI sequences end with a byte in the range 0x40-0x7E (@ through ~)
        if (c >= '@' && c <= '~') {
          break;
        }
      }
    } else {
      // Visible character - count it and advance
      visual_width++;
      i++;
    }
  }

  return i;
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

      // Calculate visual width of the line (excluding ANSI escape sequences)
      int visual_line_width = ansi_visual_width(&src_data[line_start], line_len);

      // Calculate horizontal padding to center the line based on visual width
      int h_padding = (width - visual_line_width) / 2;
      if (h_padding < 0)
        h_padding = 0;

      // Copy line to result with padding
      // Use size_t for position calculation to prevent integer underflow
      size_t row_offset = (size_t)dst_row * (size_t)(width + 1);
      size_t dst_pos = row_offset + (size_t)h_padding;
      int max_visual_width = width - h_padding;
      // Truncate to visual width while preserving complete ANSI sequences
      int copy_len = ansi_truncate_to_visual_width(&src_data[line_start], line_len, max_visual_width);

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
      // Truncate to visual cell width while preserving complete ANSI sequences
      int copy_len = ansi_truncate_to_visual_width(src_data + line_start, line_len, cell_width);
      // Check that visual width of truncated line fits
      int truncated_visual_width = ansi_visual_width(src_data + line_start, copy_len);
      if (copy_len > 0 && start_col + truncated_visual_width <= width) {
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
 *
 * NOTE: Uses plain newlines instead of ANSI escape sequences to support
 * both TTY and piped/redirected output. TTY flicker prevention is handled
 * by the display layer (e.g., display.c) when appropriate.
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
  // Each padding row needs: 1 newline character per padding row
  size_t frame_len = strlen(frame);
  size_t top_padding_len = pad_top; // 1 newline per padding row
  size_t total_len = top_padding_len + frame_len;

  char *buffer;
  buffer = SAFE_MALLOC(total_len + 1, char *);

  char *position = buffer;

  // Add top padding with plain newlines
  // Use plain newlines instead of ANSI escape sequences so the output works
  // when redirected to pipes or files, not just TTY
  for (size_t i = 0; i < pad_top; i++) {
    *position++ = '\n';
  }

  // Copy the original frame
  size_t remaining = frame_len + 1;
  SAFE_MEMCPY(position, remaining, frame, frame_len);
  position += frame_len;
  *position = '\0';

  return buffer;
}


/**
 * @file video/ascii.c
 * @ingroup video
 * @brief 🖼️ Image-to-ASCII conversion with SIMD acceleration, color matching, and terminal optimization
 */

#include <ascii-chat/ui/frame_buffer.h>
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

#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <ascii-chat/util/aspect_ratio.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/scalar/halfblock.h>
#include <ascii-chat/video/ascii/scalar/background.h>
#include <ascii-chat/video/ascii/scalar/foreground.h>

/* ============================================================================
 * ASCII Art Video Processing
 * ============================================================================
 */

/*
 * Resize a source image to a terminal viewport while preserving its visual
 * aspect ratio. The source is cropped to the viewport's aspect ratio first,
 * so the output fills the viewport instead of introducing letterbox bars.
 * Terminal cells are approximately twice as tall as they are wide, hence the
 * factor of two in the source crop ratio.
 */
static void image_resize_cover(const image_t *source, image_t *dest, ssize_t viewport_width, ssize_t viewport_height) {
  if (!source || !dest || source->w <= 0 || source->h <= 0 || dest->w <= 0 || dest->h <= 0 || viewport_width <= 0 ||
      viewport_height <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_resize_cover: invalid image or viewport dimensions");
    return;
  }

  const double source_aspect = (double)source->w / (double)source->h;
  const double viewport_aspect = (double)viewport_width / ((double)viewport_height * 2.0);
  int crop_width = source->w;
  int crop_height = source->h;
  int crop_x = 0;
  int crop_y = 0;

  if (source_aspect > viewport_aspect) {
    crop_width = (int)lround((double)source->h * viewport_aspect);
    crop_width = crop_width < 1 ? 1 : crop_width;
    crop_width = crop_width > source->w ? source->w : crop_width;
    crop_x = (source->w - crop_width) / 2;
  } else if (source_aspect < viewport_aspect) {
    crop_height = (int)lround((double)source->w / viewport_aspect);
    crop_height = crop_height < 1 ? 1 : crop_height;
    crop_height = crop_height > source->h ? source->h : crop_height;
    crop_y = (source->h - crop_height) / 2;
  }

  for (int y = 0; y < dest->h; y++) {
    int source_y = crop_y + (int)(((int64_t)y * crop_height) / dest->h);
    const rgb_pixel_t *source_row = source->pixels + ((size_t)source_y * (size_t)source->w);
    rgb_pixel_t *dest_row = dest->pixels + ((size_t)y * (size_t)dest->w);
    for (int x = 0; x < dest->w; x++) {
      int source_x = crop_x + (int)(((int64_t)x * crop_width) / dest->w);
      dest_row[x] = source_row[source_x];
    }
  }
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
      if (GET_OPTION(render_mode) == RENDER_MODE_BACKGROUND) {
        ascii = image_print_color_background(resized, palette_chars);
      } else {
        ascii = image_print_color_utf8(resized, palette_chars);
      }
#endif
    }
  } else {
    // Use grayscale/monochrome conversion with client's palette
    ascii = image_print(resized, palette_chars);
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

  // Validate original image dimensions to detect corruption
  if (original->w <= 0 || original->w > 10000 || original->h <= 0 || original->h > 10000) {
    log_error("Invalid original image dimensions detected: w=%d, h=%d (likely corrupted)", original->w, original->h);
    return NULL;
  }

  if (original->pixels == NULL) {
    log_error("Original image pixels pointer is NULL");
    return NULL;
  }

  // Start with the target dimensions requested by the user.
  ssize_t resized_width = width;
  ssize_t resized_height = height;

  // Preserve aspect ratio by cropping the source to cover the viewport. The
  // destination dimensions remain the full requested viewport dimensions.

  // Half-block mode doubles height for 2x vertical resolution (AFTER aspect ratio)
  if (caps->render_mode == RENDER_MODE_HALF_BLOCK) {
    resized_height = resized_height * 2;
  }

  // Cover rendering fills the viewport, so no aspect-ratio padding is needed.
  size_t pad_width = 0;
  size_t pad_height = 0;

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

  if (use_aspect_ratio && !stretch) {
    image_resize_cover(original, resized, resized_width, resized_height);
  } else {
    image_resize(original, resized);
  }

  uint64_t prof_resize_end_ns = time_get_ns();
  STOP_TIMER_AND_LOG_EVERY(dev, 3 * NS_PER_SEC_INT, 5 * NS_PER_MS_INT, "image_resize",
                           "IMAGE_RESIZE: Resize complete (%.2f ms)");

  // Check original image before resize (validate dimensions to prevent integer overflow)
  int orig_black = 0, orig_total = 0;
  if (original->w > 0 && original->w <= 10000 && original->h > 0 && original->h <= 10000) {
    int64_t total_pixels = (int64_t)original->w * original->h;
    int sample_size = (total_pixels < 1000) ? (int)total_pixels : 1000;
    for (int i = 0; i < sample_size; i++) {
      rgb_pixel_t p = original->pixels[i];
      orig_total++;
      if (p.r == 0 && p.g == 0 && p.b == 0)
        orig_black++;
    }
    log_debug_every(5 * NS_PER_SEC_INT,
                    ">>> ORIGINAL image: %d/%d pixels are black (%.1f%%), first pixel=RGB(%d,%d,%d)\n", orig_black,
                    orig_total, orig_total > 0 ? 100.0 * orig_black / orig_total : 0, original->pixels[0].r,
                    original->pixels[0].g, original->pixels[0].b);
  } else {
    log_warn("Skipping debug check: corrupted image dimensions (w=%d, h=%d)", original->w, original->h);
  }

  // PROFILING: Time ASCII print
  uint64_t prof_print_start_ns = prof_resize_end_ns;

  // DEBUG: Log dimensions going to renderer
  log_debug_every(10 * US_PER_SEC_INT,
                  "ascii_convert: original=%dx%d, requested=%zdx%zd, resized=%dx%d, pad=%zux%zu (mode=%d)", original->w,
                  original->h, width, height, resized->w, resized->h, pad_width, pad_height, caps->render_mode);

  // Check if image is all black
  int black_pixels = 0, total_pixels = 0;
  for (int i = 0; i < resized->w * resized->h && i < 1000; i++) {
    rgb_pixel_t p = resized->pixels[i];
    total_pixels++;
    if (p.r == 0 && p.g == 0 && p.b == 0)
      black_pixels++;
  }
  log_debug_every(5 * NS_PER_SEC_INT,
                  ">>> Resized image check: %d/%d pixels are black (%.1f%%), first pixel=RGB(%d,%d,%d)\n", black_pixels,
                  total_pixels, total_pixels > 0 ? 100.0 * black_pixels / total_pixels : 0, resized->pixels[0].r,
                  resized->pixels[0].g, resized->pixels[0].b);

  // Apply the target client's filter after resizing, so it is scoped to this
  // client and never mutates the shared source frame.
  if (caps->color_filter != COLOR_FILTER_NONE) {
    const float time_seconds = (float)time_get_ns() / (float)NS_PER_SEC_INT;
    if (apply_color_filter((uint8_t *)resized->pixels, (uint32_t)resized->w, (uint32_t)resized->h,
                           (uint32_t)(resized->w * sizeof(rgb_pixel_t)), caps->color_filter, time_seconds) != 0) {
      log_warn("Failed to apply client color filter %d", caps->color_filter);
    }
  }

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
  log_dev("ASCII_BREAKDOWN: alloc=%s, resize=%s, print=%s, pad=%s (total=%s)", alloc_str, resize_str, print_str,
          pad_str, total_str);

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

  if (source_count > 1 && (cell_width < 10 || cell_height < 3)) {
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

  if (source_count == 1) {
    source_count = 1;
    grid_cols = grid_rows = 1;
    cell_width = width;
    cell_height = height;
  }

  // ANSI bytes consume storage but no terminal columns. Build rows in order
  // instead of copying colored strings into fixed one-byte character slots.
  frame_buffer_t *buf = frame_buffer_create(1, 1);
  if (!buf)
    return NULL;
  size_t *positions = SAFE_CALLOC((size_t)source_count, sizeof(size_t), size_t *);
  int vertical_padding = 0;
  if (source_count == 1 && sources[0].frame_data) {
    int lines = 0;
    size_t length = strnlen(sources[0].frame_data, sources[0].frame_size);
    for (size_t i = 0; i < length; ++i)
      if (sources[0].frame_data[i] == '\n')
        ++lines;
    if (length && sources[0].frame_data[length - 1] != '\n')
      ++lines;
    vertical_padding = lines < height ? (height - lines) / 2 : 0;
  }

  for (int row = 0; row < height; ++row) {
    int grid_row = row / (cell_height + 1);
    int cell_row = row % (cell_height + 1);
    int columns = 0;
    for (int col = 0; col < grid_cols; ++col) {
      int src = grid_row * grid_cols + col;
      bool separator = cell_row == cell_height && grid_row < grid_rows - 1;
      int visible = 0;
      if (!separator && src < source_count && sources[src].frame_data && row >= vertical_padding) {
        const char *data = sources[src].frame_data;
        size_t start = positions[src];
        size_t end = start;
        while (end < sources[src].frame_size && data[end] && data[end] != '\n')
          ++end;
        size_t line_length = end - start;
        if (line_length > INT_MAX) {
          SAFE_FREE(positions);
          frame_buffer_destroy(buf);
          SET_ERRNO(ERROR_INVALID_PARAM, "ASCII source line exceeds supported length");
          return NULL;
        }
        int copy = ansi_truncate_to_visual_width(data + start, (int)line_length, cell_width);
        visible = ansi_visual_width(data + start, copy);
        int padding = source_count == 1 ? (cell_width - visible) / 2 : 0;
        for (int i = 0; i < padding; ++i)
          frame_buffer_append(buf, " ", 1);
        frame_buffer_append(buf, data + start, (size_t)copy);
        if (memchr(data + start, '\033', (size_t)copy))
          frame_buffer_append(buf, "\033[0m", 4);
        visible += padding;
        positions[src] = end < sources[src].frame_size && data[end] == '\n' ? end + 1 : end;
      }
      for (int i = visible; i < cell_width; ++i)
        frame_buffer_append(buf, separator ? "_" : " ", 1);
      columns += cell_width;
      if (col < grid_cols - 1) {
        frame_buffer_append(buf, separator ? "+" : "|", 1);
        ++columns;
      }
    }
    for (; columns < width; ++columns)
      frame_buffer_append(buf, " ", 1);
    if (row < height - 1)
      frame_buffer_append(buf, "\r\n", 2);
  }
  SAFE_FREE(positions);
  *out_size = frame_buffer_get_length(buf);
  char *result = SAFE_MALLOC(*out_size + 1, char *);
  memcpy(result, frame_buffer_get_content(buf), *out_size);
  result[*out_size] = '\0';
  frame_buffer_destroy(buf);
  return result;
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

/**
 * @brief Convert image to ASCII art with terminal capabilities
 * @param image Image to convert (must not be NULL)
 * @param caps Terminal capabilities structure
 * @param palette Palette characters to use for conversion
 * @return ASCII art string, or NULL on error
 *
 * Converts an image to ASCII art using the provided terminal capabilities.
 * Automatically selects the best rendering method based on terminal capabilities.
 *
 * @note Returns dynamically allocated string that must be freed by caller.
 */
char *image_print_with_capabilities(const image_t *image, const terminal_capabilities_t *caps, const char *palette) {
  if (!image || !caps || !palette) {
    return NULL;
  }

  terminal_color_mode_t color_level = caps->color_level;
  render_mode_t render_mode = caps->render_mode;

  // Half-block mode: dispatch to halfblock renderers by color depth
  if (render_mode == RENDER_MODE_HALF_BLOCK) {
    const uint8_t *rgb_data = (const uint8_t *)image->pixels;
    switch (color_level) {
    case TERM_COLOR_TRUECOLOR:
      return rgb_to_truecolor_halfblocks_scalar(rgb_data, image->w, image->h, 0);
    case TERM_COLOR_256:
      return rgb_to_256color_halfblocks_scalar(rgb_data, image->w, image->h, 0, palette);
    case TERM_COLOR_16:
      return rgb_to_16color_halfblocks_scalar(rgb_data, image->w, image->h, 0, palette);
    case TERM_COLOR_NONE:
    case TERM_COLOR_AUTO:
    default:
      return rgb_to_halfblocks_scalar(rgb_data, image->w, image->h, 0, palette);
    }
  }

  // Foreground/background modes: dispatch by color depth
  switch (color_level) {
  case TERM_COLOR_TRUECOLOR: {
    bool use_background = (render_mode == RENDER_MODE_BACKGROUND);
#ifdef SIMD_SUPPORT
    return image_print_color_simd((image_t *)image, use_background, false, palette);
#else
    if (use_background) {
      return image_print_color_background(image, palette);
    }
    return image_print_color_utf8(image, palette);
#endif
  }
  case TERM_COLOR_256:
    return image_print_256color(image, palette);
  case TERM_COLOR_16:
    return image_print_16color(image, palette);
  case TERM_COLOR_NONE:
  case TERM_COLOR_AUTO:
  default:
    return image_print(image, palette);
  }
}

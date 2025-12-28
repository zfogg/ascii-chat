
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

#include "platform/abstraction.h"
#include "platform/terminal.h"

#include "ascii.h"
#include "grid.h"
#include "common.h"
#include "image.h"
#include "util/aspect_ratio.h"
#include "util/overflow.h"
#include "video/webcam/webcam.h"
#include "options/options.h"
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

  // Validate dimensions fit in image_t's int fields before casting
  if (resized_width > INT_MAX || resized_height > INT_MAX) {
    log_error("Dimensions exceed INT_MAX: width=%zd, height=%zd", resized_width, resized_height);
    return NULL;
  }

  // PROFILING: Time image allocation and resize
  struct timespec prof_alloc_start, prof_alloc_end, prof_resize_start, prof_resize_end;
  (void)clock_gettime(CLOCK_MONOTONIC, &prof_alloc_start);

  image_t *resized = image_new((size_t)resized_width, (size_t)resized_height);
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

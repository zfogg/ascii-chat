#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ascii.h"
#include "common.h"
#include "image.h"
#include "options.h"
#include "aspect_ratio.h"
#include "webcam.hpp"

/* ============================================================================
 * ASCII Art Video Processing
 * ============================================================================
 */

asciichat_error_t ascii_read_init(unsigned short int webcam_index) {
  log_info("Initializing ASCII reader with webcam index %u", webcam_index);
  webcam_init(webcam_index);
  return ASCIICHAT_OK;
}

asciichat_error_t ascii_write_init(void) {
  console_clear();
  cursor_reset();
  cursor_hide();
  log_debug("ASCII writer initialized");
  return ASCIICHAT_OK;
}

char *ascii_read(void) {
  FILE *jpeg = webcam_read();

  if (jpeg == NULL) {
    // Return a simple error message if webcam read fails
    log_error(ASCIICHAT_WEBCAM_ERROR_STRING);
    char *err_msg;
    size_t err_len = strlen(ASCIICHAT_WEBCAM_ERROR_STRING);
    SAFE_MALLOC(err_msg, err_len, char *);
    strncpy(err_msg, ASCIICHAT_WEBCAM_ERROR_STRING, err_len);
    return err_msg;
  }

  image_t *original = image_read(jpeg);
  if (!original) {
    log_error("Failed to read JPEG image");
    fclose(jpeg);
    return NULL;
  }

  // Start with the target dimensions requested by the user (or detected from
  // the terminal). These can be modified by aspect_ratio() if stretching is
  // disabled and one of the dimensions was left to be calculated
  // automatically.
  ssize_t width = opt_width;
  ssize_t height = opt_height;
  aspect_ratio(original->w, original->h, &width, &height);

  // Calculate how many leading spaces are required to centre the image inside
  // the overall width requested by the user.  Make sure the value is
  // non-negative so we don't end up passing a huge number to ascii_pad_frame
  // when width happens to exceed opt_width.
  ssize_t pad_width_ss = opt_width > width ? (opt_width - width) / 2 : 0;
  size_t pad_width = (size_t)pad_width_ss;

  // Calculate how many blank lines are required to centre the image inside
  // the overall height requested by the user.
  ssize_t pad_height_ss = opt_height > height ? (opt_height - height) / 2 : 0;
  size_t pad_height = (size_t)pad_height_ss;

  // Resize the captured frame to the aspect-correct dimensions.
  image_t *resized = image_new((int)width, (int)height);
  if (!resized) {
    log_error("Failed to allocate resized image");
    image_destroy(original);
    fclose(jpeg);
    return NULL;
  }

  fclose(jpeg);

  image_clear(resized);
  image_resize(original, resized);

  char *ascii;
  if (opt_color_output) {
    ascii = image_print_colored(resized);
  } else {
    ascii = image_print(resized);
  }

  if (!ascii) {
    log_error("Failed to convert image to ASCII");
  }

  char *ascii_width_padded = ascii_pad_frame(ascii, pad_width);
  free(ascii);

  // Apply vertical centering (height padding)
  char *ascii_padded = ascii_pad_frame_height(ascii_width_padded, pad_height, opt_width);
  free(ascii_width_padded);

  image_destroy(original);
  image_destroy(resized);

  return ascii_padded;
  // return ascii;
}

asciichat_error_t ascii_write(const char *frame) {
  if (frame == NULL) {
    log_warn("Attempted to write NULL frame");
    return ASCIICHAT_ERR_INVALID_PARAM;
  }

  cursor_reset();

  size_t frame_len = strlen(frame);
  if (fwrite(frame, 1, frame_len, stdout) != frame_len) {
    log_error("Failed to write ASCII frame");
    return ASCIICHAT_ERR_TERMINAL;
  }

  return ASCIICHAT_OK;
}

void ascii_write_destroy(void) {
  // console_clear();
  // cursor_reset();
  cursor_show();
  log_debug("ASCII writer destroyed");
}

void ascii_read_destroy(void) {
  // console_clear();
  // cursor_reset();
  cursor_show();
  webcam_cleanup();
  log_debug("ASCII reader destroyed");
}

// RGB to ANSI color conversion functions
char *rgb_to_ansi_fg(int r, int g, int b) {
  static char color_code[32];
  snprintf(color_code, sizeof(color_code), "\033[38;2;%d;%d;%dm", r, g, b);
  return color_code;
}

char *rgb_to_ansi_bg(int r, int g, int b) {
  static char color_code[32];
  snprintf(color_code, sizeof(color_code), "\033[48;2;%d;%d;%dm", r, g, b);
  return color_code;
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
 *   pad        How many space characters to add in front of every visual row.
 *
 * Returns:
 *   A newly allocated, null-terminated string that contains the padded frame
 *   on success, or NULL if either `frame` is NULL or a memory allocation
 *   fails.
 */
char *ascii_pad_frame(const char *frame, size_t pad) {
  if (!frame) {
    return NULL;
  }

  if (pad == 0) {
    // Nothing to do; return a copy so the caller can free it safely without
    // worrying about the original allocation strategy.
    size_t orig_len = strlen(frame);
    char *copy;
    SAFE_MALLOC(copy, orig_len + 1, char *);
    memcpy(copy, frame, orig_len + 1);
    return copy;
  }

  /* -------------------------------------------------------------------------
   * Count how many visual rows we have (lines terminated by '\n')
   * to determine the final buffer size.
   * ----------------------------------------------------------------------- */
  size_t line_count = 1; // There is always at least the first line
  const char *p = frame;
  while (*p) {
    if (*p == '\n') {
      line_count++;
    }
    p++;
  }

  /* Total length of the source plus padding */
  const size_t src_len = strlen(frame);
  const size_t extra = line_count * pad;
  const size_t dst_len = src_len + extra;

  char *dst;
  SAFE_MALLOC(dst, dst_len + 1, char *);

  /* -------------------------------------------------------------------------
   * Build the padded frame.
   * ----------------------------------------------------------------------- */
  bool at_line_start = true;
  const char *src = frame;
  char *out = dst;

  while (*src) {
    if (at_line_start) {
      /* Insert the requested amount of spaces in front of every visual row. */
      memset(out, ' ', pad);
      out += pad;
      at_line_start = false;
    }

    *out++ = *src;

    if (*src == '\n') {
      at_line_start = true;
    }

    src++;
  }

  *out = '\0';
  return dst;
}

/**
 * Adds vertical padding (blank lines) to center a frame vertically.
 * 
 * Parameters:
 *   frame        The input ASCII frame to pad vertically.
 *   pad_top      Number of blank lines to add at the top.
 *   frame_width  Width of each line (for padding blank lines).
 * 
 * Returns:
 *   A newly allocated, null-terminated string with vertical padding,
 *   or NULL if frame is NULL or memory allocation fails.
 */
char *ascii_pad_frame_height(const char *frame, size_t pad_top, size_t frame_width) {
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

void rgb_to_ansi_8bit(int r, int g, int b, int *fg_code, int *bg_code) {
  // Convert RGB to 8-bit color code (216 color cube + 24 grayscale)
  if (r == g && g == b) {
    // Grayscale
    if (r < 8) {
      *fg_code = 16;
    } else if (r > 248) {
      *fg_code = 231;
    } else {
      *fg_code = 232 + (r - 8) / 10;
    }
  } else {
    // Color cube: 16 + 36*r + 6*g + b where r,g,b are 0-5
    int r_level = (r * 5) / 255;
    int g_level = (g * 5) / 255;
    int b_level = (b * 5) / 255;
    *fg_code = 16 + 36 * r_level + 6 * g_level + b_level;
  }
  *bg_code = *fg_code; // Same logic for background
}

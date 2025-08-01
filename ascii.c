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
  if (original) {
    // Adjust rendering size based on the newly discovered webcam frame size
    // aspect_ratio(original->w, original->h);
  }
  if (!original) {
    log_error("Failed to read JPEG image");
    fclose(jpeg);
    return NULL;
  }

  image_t *resized = image_new(opt_width, opt_height);
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

  image_destroy(original);
  image_destroy(resized);

  return ascii;
}

asciichat_error_t ascii_write(const char *frame) {
  if (frame == NULL) {
    log_warn("Attempted to write NULL frame");
    return ASCIICHAT_ERR_INVALID_PARAM;
  }

  const char *current = frame;
  const char *segment_start = frame;

  while (*current != 0) {
    if (*current == ASCII_DELIMITER) {
      // Output the segment before this tab
      size_t len = current - segment_start;
      if (len > 0) {
        if (fwrite(segment_start, 1, len, stdout) != len) {
          log_error("Failed to write ASCII frame segment");
          return ASCIICHAT_ERR_TERMINAL;
        }
      }

      cursor_reset();
      segment_start = current + 1; // Next segment starts after tab
    }
    current++;
  }

  // Output the final segment
  size_t remaining = current - segment_start;
  if (remaining > 0) {
    if (fwrite(segment_start, 1, remaining, stdout) != remaining) {
      log_error("Failed to write final ASCII frame segment");
      return ASCIICHAT_ERR_TERMINAL;
    }
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

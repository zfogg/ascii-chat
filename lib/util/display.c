/**
 * @file lib/util/display.c
 * @brief Display width and centering utilities implementation
 */

#include <ascii-chat/util/display.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/video/ascii/ansi.h>
#include <ascii-chat/common.h>
#include <stdlib.h>
#include <string.h>

int display_width(const char *text) {
  if (!text) {
    return 0;
  }

  // Strip ANSI escape codes
  char *stripped = ansi_strip_escapes(text, strlen(text));

  // Use stripped version if available, otherwise use original text
  const char *to_measure = stripped ? stripped : text;

  // Calculate display width using UTF-8 aware calculation
  int width = utf8_display_width(to_measure);
  SAFE_FREE(stripped);

  // Return 0 for empty or invalid input, otherwise return calculated width
  return width < 0 ? 0 : width;
}

int display_center_horizontal(const char *text, int terminal_width) {
  if (!text || terminal_width <= 0) {
    return 0;
  }

  int text_width = display_width(text);

  // If text is empty or wider than terminal, don't add padding
  if (text_width == 0 || text_width >= terminal_width) {
    return 0;
  }

  // Calculate left padding to center
  return (terminal_width - text_width) / 2;
}

int display_center_vertical(int content_height, int terminal_height) {
  if (content_height <= 0 || terminal_height <= 0) {
    return 0;
  }

  if (content_height >= terminal_height) {
    return 0;
  }

  return (terminal_height - content_height) / 2;
}

int display_height(const char *text, int terminal_width) {
  if (!text || terminal_width <= 0) {
    return 0;
  }

  int total_lines = 0;
  const char *p = text;

  // Count lines by processing the text and counting newlines + final segment
  while (*p) {
    // Find the end of this line (next newline or end of string)
    const char *line_end = p;
    while (*line_end && *line_end != '\n') {
      line_end++;
    }

    // Calculate display width of this line
    size_t line_len = line_end - p;
    if (line_len > 0) {
      // Strip ANSI codes and calculate display width
      char *stripped = ansi_strip_escapes(p, line_len);
      if (stripped) {
        int line_width = utf8_display_width(stripped);
        SAFE_FREE(stripped);

        if (line_width > 0) {
          // Calculate how many display lines this wraps to
          int display_lines = (line_width + terminal_width - 1) / terminal_width; // Ceiling division
          total_lines += display_lines;
        } else {
          total_lines += 1;
        }
      } else {
        // If stripping fails, count as 1 line
        total_lines += 1;
      }
    } else {
      // Empty line still counts as 1 line
      total_lines += 1;
    }

    // Move to next line
    if (*line_end == '\n') {
      p = line_end + 1;
    } else {
      break;
    }
  }

  return total_lines;
}

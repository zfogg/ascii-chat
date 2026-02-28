/**
 * @file lib/util/display.c
 * @brief Display width and centering utilities implementation
 */

#include "../../include/ascii-chat/util/display.h"
#include "../../include/ascii-chat/util/utf8.h"
#include "../../include/ascii-chat/video/ansi.h"
#include "../../include/ascii-chat/common.h"
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
  const char *segment_start = text;

  // Process text segment by segment, split by newlines
  while (*segment_start) {
    // Find the end of this segment (next newline or end of string)
    const char *segment_end = segment_start;
    while (*segment_end && *segment_end != '\n') {
      segment_end++;
    }

    // Calculate display width of this segment (excluding ANSI codes)
    if (segment_end > segment_start) {
      // Create temporary null-terminated copy of segment
      size_t segment_len = segment_end - segment_start;
      char *segment_copy = SAFE_MALLOC(segment_len + 1, char *);
      if (segment_copy) {
        strncpy(segment_copy, segment_start, segment_len);
        segment_copy[segment_len] = '\0';

        // Calculate display width using ANSI-aware function
        int segment_width = display_width(segment_copy);
        if (segment_width < 0) {
          segment_width = (int)segment_len;
        }

        // Calculate how many lines this segment wraps to
        int segment_lines = (segment_width + terminal_width - 1) / terminal_width;
        if (segment_lines <= 0) {
          segment_lines = 1;
        }
        total_lines += segment_lines;

        SAFE_FREE(segment_copy);
      } else {
        // Fallback: assume 1 line per segment if allocation fails
        total_lines += 1;
      }
    } else {
      // Empty segment (e.g., two consecutive newlines) still counts as 1 line
      total_lines += 1;
    }

    // Move to next segment (skip the newline if present)
    if (*segment_end == '\n') {
      segment_start = segment_end + 1;
    } else {
      // End of string
      break;
    }
  }

  return total_lines;
}

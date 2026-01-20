/**
 * @file layout.c
 * @brief Two-column layout implementation
 *
 * Implements reusable layout functions for two-column formatting with text wrapping.
 */

#include "layout.h"
#include "log/logging.h"
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <locale.h>

/* ============================================================================
 * UTF-8 Display Width Calculation
 * ============================================================================ */

/**
 * @brief Calculate the display width of a UTF-8 string
 *
 * Converts UTF-8 bytes to wide characters and calculates the terminal display width
 * using wcwidth() for each character. This accounts for multi-byte UTF-8 sequences
 * and wide characters (emoji, CJK, etc).
 *
 * @param str UTF-8 string to measure
 * @return Display width in columns, or 0 for invalid input
 */
static int utf8_display_width(const char *str) {
  if (!str)
    return 0;

  int width = 0;
  mbstate_t state = {0};
  const char *p = str;

  while (*p) {
    wchar_t wc;
    size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &state);

    if (len == 0) {
      // Null terminator reached
      break;
    } else if (len == (size_t)-1 || len == (size_t)-2) {
      // Invalid UTF-8 sequence, stop processing
      break;
    } else {
      // Get display width of this character
      int char_width = wcwidth(wc);
      if (char_width < 0) {
        // Control character or unprintable - treat as 0 width
        char_width = 0;
      }
      width += char_width;
      p += len;
    }
  }

  return width;
}

/* ============================================================================
 * Colored Segment Printing
 * ============================================================================ */

/**
 * @brief Print text segment with colored metadata labels
 *
 * Colorizes "default:" labels and "env:" labels with colored env var names.
 */
static void layout_print_colored_segment(FILE *stream, const char *seg) {
  if (!seg || !stream) return;

  // Get color codes once to avoid rotating buffer issues
  const char *magenta = log_level_color(LOG_COLOR_FATAL);
  const char *cyan = log_level_color(LOG_COLOR_DEBUG);
  const char *reset = log_level_color(LOG_COLOR_RESET);

  const char *sp = seg;
  while (*sp) {
    if (strncmp(sp, "default:", 8) == 0) {
      fprintf(stream, "%s", magenta);     // Magenta for "default:"
      fprintf(stream, "default:");
      fprintf(stream, "%s", reset);       // Reset
      sp += 8;
    } else if (strncmp(sp, "env:", 4) == 0) {
      fprintf(stream, "%s", magenta);     // Magenta for "env:"
      fprintf(stream, "env:");
      fprintf(stream, "%s", reset);       // Reset
      sp += 4;
      while (*sp == ' ') {
        fprintf(stream, " ");
        sp++;
      }
      // Find end of env var name
      const char *env_start = sp;
      while (*sp && *sp != ')') sp++;
      if (sp > env_start) {
        // Print env var name in cyan
        fprintf(stream, "%s", cyan);      // Cyan for env var
        fprintf(stream, "%.*s", (int)(sp - env_start), env_start);
        fprintf(stream, "%s", reset);     // Reset
      }
    } else {
      fprintf(stream, "%c", *sp);
      sp++;
    }
  }
}

/* ============================================================================
 * Wrapped Description Printing
 * ============================================================================ */

void layout_print_wrapped_description(FILE *stream, const char *text, int indent_width, int term_width) {
  if (!text || !stream)
    return;

  // Set locale to support UTF-8
  setlocale(LC_ALL, "");

  // Default terminal width if not specified
  if (term_width <= 0)
    term_width = 80;

  // Available width for text after indentation
  int available_width = term_width - indent_width;
  if (available_width < 20)
    available_width = 20;

  const char *line_start = text;
  const char *last_space = NULL;
  int line_display_width = 0;
  const char *p = text;
  mbstate_t state = {0};

  while (*p) {
    // Check for explicit newline
    if (*p == '\n') {
      // Print everything up to the newline
      int text_len = p - line_start;
      if (text_len > 0) {
        char seg[512];
        strncpy(seg, line_start, text_len);
        seg[text_len] = '\0';
        layout_print_colored_segment(stream, seg);
      }

      fprintf(stream, "\n");
      if (*(p + 1)) {
        for (int i = 0; i < indent_width; i++)
          fprintf(stream, " ");
      }
      p++;
      line_start = p;
      line_display_width = 0;
      last_space = NULL;
      continue;
    }

    // Track spaces for word wrapping
    if (*p == ' ')
      last_space = p;

    // Calculate display width of this UTF-8 character
    wchar_t wc;
    size_t char_len = mbrtowc(&wc, p, MB_CUR_MAX, &state);

    int char_width = 0;
    if (char_len > 0 && char_len != (size_t)-1 && char_len != (size_t)-2) {
      char_width = wcwidth(wc);
      if (char_width < 0)
        char_width = 0;  // Control characters have 0 width
    } else {
      // Invalid UTF-8, treat as 1 width and advance by 1 byte
      char_len = 1;
      char_width = 1;
    }

    line_display_width += char_width;

    // Check if we need to wrap
    if (line_display_width >= available_width && last_space && last_space > line_start) {
      // Print text up to last space with colors applied
      int text_len = last_space - line_start;
      char seg[512];
      strncpy(seg, line_start, text_len);
      seg[text_len] = '\0';
      layout_print_colored_segment(stream, seg);

      fprintf(stream, "\n");
      for (int i = 0; i < indent_width; i++)
        fprintf(stream, " ");

      p = last_space + 1;
      line_start = p;
      line_display_width = 0;
      last_space = NULL;
      memset(&state, 0, sizeof(state));  // Reset conversion state
      continue;
    }

    p += char_len;
  }

  // Print remaining text
  if (p > line_start) {
    char seg[512];
    int text_len = p - line_start;
    strncpy(seg, line_start, text_len);
    seg[text_len] = '\0';
    layout_print_colored_segment(stream, seg);
  }
}

/* ============================================================================
 * Two-Column Row Printing
 * ============================================================================ */

void layout_print_two_column_row(FILE *stream, const char *first_column, const char *second_column,
                                 int first_col_len, int term_width) {
  if (!stream || !first_column || !second_column)
    return;

  // Calculate actual display width of first column (accounts for UTF-8)
  int display_width = utf8_display_width(first_column);
  if (display_width < 0)
    display_width = first_col_len;  // Fallback to byte count on invalid UTF-8

  // If first column is short enough, put description on same line
  if (display_width < LAYOUT_COLUMN_WIDTH) {
    // Print first column
    fprintf(stream, "  %s", first_column);

    // Pad to description start column
    int current_col = 2 + display_width;  // 2 for leading spaces
    int padding = LAYOUT_DESCRIPTION_START_COL - current_col;
    if (padding > 0) {
      fprintf(stream, "%*s", padding, "");
    } else {
      fprintf(stream, " ");
    }

    // Print description with wrapping
    layout_print_wrapped_description(stream, second_column, LAYOUT_DESCRIPTION_START_COL, term_width);
    fprintf(stream, "\n");
  } else {
    // First column too long, put description on next line
    fprintf(stream, "  %s\n", first_column);

    // Print indent for description column
    for (int i = 0; i < LAYOUT_DESCRIPTION_START_COL; i++)
      fprintf(stream, " ");

    // Print description with wrapping
    layout_print_wrapped_description(stream, second_column, LAYOUT_DESCRIPTION_START_COL, term_width);
    fprintf(stream, "\n");
  }
}

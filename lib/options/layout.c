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

  // Default terminal width if not specified
  if (term_width <= 0)
    term_width = 80;

  // Available width for text after indentation
  int available_width = term_width - indent_width;
  if (available_width < 20)
    available_width = 20;

  const char *line_start = text;
  const char *last_space = NULL;
  int line_len = 0;
  const char *p = text;

  while (*p) {
    if (*p == ' ')
      last_space = p;

    line_len++;

    // Check if we need to wrap
    if (line_len >= available_width || *p == '\n') {
      // Find previous space if we exceeded width
      if (*p != '\n' && line_len >= available_width && last_space && last_space > line_start) {
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
        line_len = 0;
        last_space = NULL;
        continue;
      }

      if (*p == '\n') {
        // Print remaining segment with colors
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
        line_len = 0;
        last_space = NULL;
        continue;
      }
    }

    p++;
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

  // If first column is short enough, put description on same line
  if (first_col_len < LAYOUT_COLUMN_WIDTH) {
    // Print first column
    fprintf(stream, "  %s", first_column);

    // Pad to description start column
    int current_col = 2 + first_col_len;  // 2 for leading spaces
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

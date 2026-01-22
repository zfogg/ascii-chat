/**
 * @file layout.c
 * @brief Two-column layout implementation
 *
 * Implements reusable layout functions for two-column formatting with text wrapping.
 * Uses utf8proc library for accurate UTF-8 display width calculation.
 */

#include "layout.h"
#include "log/logging.h"
#include "util/utf8.h"
#include "video/ansi.h"
#include "common.h"
#include <ascii-chat-deps/utf8proc/utf8proc.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Colored Segment Printing
 * ============================================================================ */

/**
 * @brief Print text segment as-is
 *
 * All coloring is handled by colored_string() in builder.c before passing to layout functions.
 * This function just prints the text without any color processing.
 */
static void layout_print_colored_segment(FILE *stream, const char *seg) {
  if (!seg || !stream)
    return;

  // Just print the text as-is. All coloring is done in builder.c using colored_string()
  // before passing to layout functions. Layout functions only handle positioning and wrapping.
  fprintf(stream, "%s", seg);
}

/* ============================================================================
 * Wrapped Description Printing
 * ============================================================================ */

/**
 * @brief Calculate display width of text segment, excluding ANSI escape codes
 *
 * Strips ANSI codes and calculates actual display width using UTF-8 aware calculation.
 */
static int calculate_segment_display_width(const char *text, int len) {
  if (!text || len <= 0)
    return 0;

  // Create a stripped version for width calculation
  char *stripped = ansi_strip_escapes(text, len);
  if (!stripped)
    return 0;

  // Calculate display width of stripped text
  int width = utf8_display_width_n(stripped, strlen(stripped));
  SAFE_FREE(stripped);

  return width < 0 ? 0 : width;
}

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
  const char *p = text;

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
      last_space = NULL;
      continue;
    }

    // Track spaces for word wrapping
    if (*p == ' ')
      last_space = p;

    // Decode UTF-8 character using utf8proc_iterate
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t char_bytes = utf8proc_iterate((const utf8proc_uint8_t *)p, -1, &codepoint);

    if (char_bytes <= 0) {
      // Invalid UTF-8 or end of string, stop
      break;
    }

    // Advance to next character first
    p += char_bytes;

    // Calculate actual display width from line_start to current position (excluding ANSI codes)
    int actual_width = calculate_segment_display_width(line_start, p - line_start);

    // Check if we need to wrap
    if (actual_width >= available_width && last_space && last_space > line_start) {
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
      last_space = NULL;
      continue;
    }
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

void layout_print_two_column_row(FILE *stream, const char *first_column, const char *second_column, int first_col_len,
                                 int term_width) {
  if (!stream || !first_column || !second_column)
    return;

  // Strip ANSI escape codes before calculating display width
  char *stripped = ansi_strip_escapes(first_column, strlen(first_column));

  // Calculate actual display width of first column (accounts for UTF-8, excluding ANSI codes)
  int display_width = utf8_display_width(stripped ? stripped : first_column);
  if (display_width < 0)
    display_width = first_col_len; // Fallback to provided value if available

  SAFE_FREE(stripped);

  // Determine the description column start position and wrapping threshold
  // If first_col_len is provided (non-zero), use it as the maximum column width
  // Otherwise, use the default LAYOUT_DESCRIPTION_START_COL
  int description_start_col = LAYOUT_DESCRIPTION_START_COL;
  int wrap_threshold = LAYOUT_COLUMN_WIDTH;
  if (first_col_len > 0) {
    description_start_col = 2 + first_col_len + 2; // 2 for leading spaces, first_col_len for content, 2 for spacing
    wrap_threshold = first_col_len;                // Use provided width as wrap threshold
  }

  // At narrow terminal widths, force single-column layout (description on next line)
  // At <= 90 columns, always use single-column for readability
  bool force_single_column = (term_width <= 90);

  // If first column is short enough AND we have space for description, use same-line layout
  if (display_width <= wrap_threshold && !force_single_column) {
    // Print first column
    fprintf(stream, "  %s", first_column);

    // Pad to description start column
    int current_col = 2 + display_width; // 2 for leading spaces
    int padding = description_start_col - current_col;
    if (padding > 0) {
      fprintf(stream, "%*s", padding, "");
    } else {
      fprintf(stream, " ");
    }

    // Print description with wrapping
    layout_print_wrapped_description(stream, second_column, description_start_col, term_width);
    fprintf(stream, "\n");
  } else {
    // First column too long or terminal too narrow, put description on next line
    fprintf(stream, "  %s\n", first_column);

    // In single-column mode, use modest indent for readability
    int description_indent = force_single_column ? 4 : description_start_col;

    // Print indent for description column
    for (int i = 0; i < description_indent; i++)
      fprintf(stream, " ");

    // Print description with wrapping
    layout_print_wrapped_description(stream, second_column, description_indent, term_width);
    fprintf(stream, "\n");
  }
}


/**
 * @file layout.c
 * @brief Two-column layout implementation
 *
 * Implements reusable layout functions for two-column formatting with text wrapping.
 * Uses utf8proc library for accurate UTF-8 display width calculation.
 */

#include <ascii-chat/options/layout.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/video/ansi.h>
#include <ascii-chat/common.h>
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

/**
 * @brief Check if we're at a metadata marker, accounting for ANSI codes
 *
 * Handles cases where metadata like "(default:" or "(env:" has ANSI color codes
 * mixed in, like "(\x1b[..mdefault:\x1b[0m"
 */
static bool is_metadata_start(const char *p) {
  if (!p || *p != '(')
    return false;

  // Strip ANSI codes to check the actual text
  char buf[256];
  const char *src = p;
  char *dst = buf;
  int remaining = sizeof(buf) - 1;

  // Copy up to 20 chars, stripping ANSI codes
  int count = 0;
  while (*src && count < 20 && remaining > 0) {
    if (*src == '\x1b') {
      // Skip ANSI escape sequence
      src++;
      while (*src && *src != 'm')
        src++;
      if (*src == 'm')
        src++;
    } else {
      *dst++ = *src++;
      remaining--;
      count++;
    }
  }
  *dst = '\0';

  return strncmp(buf, "(default:", 9) == 0 || strncmp(buf, "(env:", 5) == 0;
}

void layout_print_wrapped_description(FILE *stream, const char *text, int indent_width, int term_width,
                                      int continuation_indent_extra) {
  if (!text || !stream)
    return;

  // Default terminal width if not specified
  if (term_width <= 0)
    term_width = 80;

  // Available width for text after indentation, capped at 130 for readability
  // Account for continuation lines having extra indent, so reduce available width accordingly
  // This allows the description column itself to be up to 130 chars wide,
  // starting from indent_width, for a max total line of indent_width + 130 (first line)
  // or indent_width + continuation_indent_extra + 130 (continuation lines)
  int available_width = term_width - indent_width - continuation_indent_extra;
  if (available_width > 130)
    available_width = 130;
  if (available_width < 20)
    available_width = 20;

  const char *line_start = text;
  const char *last_space = NULL;
  const char *p = text;
  bool inside_metadata = false;

  while (*p) {
    // Check for explicit newline
    if (*p == '\n') {
      // Print everything up to the newline
      int text_len = p - line_start;
      if (text_len > 0) {
        char seg[BUFFER_SIZE_MEDIUM];
        SAFE_STRNCPY(seg, line_start, text_len + 1);
        layout_print_colored_segment(stream, seg);
      }

      fprintf(stream, "\n");
      if (*(p + 1)) {
        // Continuation lines get extra indent if specified
        int continuation_indent = indent_width + continuation_indent_extra;
        for (int i = 0; i < continuation_indent; i++)
          fprintf(stream, " ");
      }
      p++;
      line_start = p;
      last_space = NULL;
      inside_metadata = false;
      continue;
    }

    // Check for start of metadata blocks (must check before space tracking)
    // Use helper that accounts for ANSI codes in the metadata
    if (!inside_metadata && is_metadata_start(p)) {
      inside_metadata = true;
    }

    // Check for end of metadata blocks - always exit on )
    // We'll re-enter when we see the next metadata marker
    if (inside_metadata && *p == ')') {
      inside_metadata = false;
    }

    // Track spaces for word wrapping, including inside metadata blocks
    // Prefer breaks before metadata blocks by updating last_space even if we already have one
    if (*p == ' ') {
      const char *next = p + 1;
      while (*next == ' ')
        next++;
      // Check if next is a metadata marker - this is a good break point to prefer
      if (is_metadata_start(next)) {
        // Update last_space to this position (prefer breaking before metadata)
        last_space = p;
      } else if (!last_space || *next != '(') {
        // Otherwise track normal spaces, but skip spaces before non-short parentheticals
        last_space = p;
      }
    }

    // Decode UTF-8 character length
    int char_bytes = utf8_next_char_bytes(p, strlen(p));

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
      // Print text up to break point with colors applied
      int text_len = last_space - line_start;
      char seg[BUFFER_SIZE_MEDIUM];
      SAFE_STRNCPY(seg, line_start, text_len + 1);
      layout_print_colored_segment(stream, seg);

      fprintf(stream, "\n");
      // Continuation lines get extra indent if specified
      int continuation_indent = indent_width + continuation_indent_extra;
      for (int i = 0; i < continuation_indent; i++)
        fprintf(stream, " ");

      p = last_space + 1;
      line_start = p;
      last_space = NULL;
      continue;
    }
  }

  // Print remaining text
  if (p > line_start) {
    char seg[BUFFER_SIZE_MEDIUM];
    int text_len = p - line_start;
    SAFE_STRNCPY(seg, line_start, text_len + 1);
    layout_print_colored_segment(stream, seg);
  }
}

/* ============================================================================
 * Two-Column Row Printing
 * ============================================================================ */

void layout_print_two_column_row(FILE *stream, const char *first_column, const char *second_column, int first_col_len,
                                 int term_width, int continuation_indent_extra) {
  if (!stream || !first_column || !second_column)
    return;

  // Strip ANSI escape codes before calculating display width
  char *stripped = ansi_strip_escapes(first_column, strlen(first_column));

  // Calculate actual display width of first column (accounts for UTF-8, excluding ANSI codes)
  int display_width = utf8_display_width(stripped ? stripped : first_column);
  if (display_width < 0)
    display_width = first_col_len; // Fallback to provided value if available

  SAFE_FREE(stripped);

  // Use calculated first_col_len if provided (accounts for UTF-8 and ANSI codes),
  // otherwise default to 35. The provided width respects both colored text and UTF-8 characters.
  // Allow the first column to expand up to 60% of terminal width, but leave room for description.
  int fixed_first_col_width = (first_col_len > 0) ? first_col_len : 35;
  int max_first_col_width = (term_width * 6) / 10;
  if (max_first_col_width < 40)
    max_first_col_width = 40;
  if (fixed_first_col_width > max_first_col_width) {
    fixed_first_col_width = max_first_col_width;
  }

  // Second column starts after first column (with 2 spaces padding)
  // This allows descriptions to be quite wide for wrapping
  int second_col_start = 2 + fixed_first_col_width + 2;

  // At narrow terminal widths, force single-column layout
  bool force_single_column = (term_width <= 90);

  // Position where first column ends (including leading spaces)
  int first_col_end = 2 + display_width;

  // Check if first column fits within the fixed width
  bool fits_in_first_column_width = (display_width <= fixed_first_col_width);

  // If first column fits within its width and we're not forcing single column, try same-line layout
  if (fits_in_first_column_width && !force_single_column) {
    // Print first column
    fprintf(stream, "  %s", first_column);

    // Pad to second column start position
    int padding = second_col_start - first_col_end;
    if (padding > 0) {
      fprintf(stream, "%*s", padding, "");
    } else {
      fprintf(stream, " ");
    }

    // Print description with wrapping at second column position
    layout_print_wrapped_description(stream, second_column, second_col_start, term_width, continuation_indent_extra);
    fprintf(stream, "\n");
  } else if (force_single_column) {
    // Terminal too narrow, put description on next line with indent
    fprintf(stream, "  %s\n", first_column);

    // Only print description if it exists
    if (second_column && second_column[0] != '\0') {
      int description_indent = 4;
      for (int i = 0; i < description_indent; i++)
        fprintf(stream, " ");

      layout_print_wrapped_description(stream, second_column, description_indent, term_width,
                                       continuation_indent_extra);
      fprintf(stream, "\n");
    }
  } else {
    // First column overflows its fixed width - bump description to next line
    // Print first column as-is (it will overflow)
    fprintf(stream, "  %s\n", first_column);

    // Only print description if it exists
    if (second_column && second_column[0] != '\0') {
      // Print description on next line at second column position, with wide wrapping
      for (int i = 0; i < second_col_start; i++)
        fprintf(stream, " ");
      layout_print_wrapped_description(stream, second_column, second_col_start, term_width, continuation_indent_extra);
      fprintf(stream, "\n");
    }
  }
}

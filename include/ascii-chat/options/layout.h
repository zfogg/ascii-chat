/**
 * @file layout.h
 * @brief Two-column layout helpers for help text and option formatting
 *
 * Provides reusable functions for formatting two-column layouts with proper
 * alignment and text wrapping. Used by both mode-level help and binary-level help.
 */

#ifndef ASCII_CHAT_OPTIONS_LAYOUT_H
#define ASCII_CHAT_OPTIONS_LAYOUT_H

#include <stdio.h>

/* ============================================================================
 * Layout Constants
 * ============================================================================ */

/** Maximum width for first column before description moves to next line */
#define LAYOUT_COLUMN_WIDTH 75

/** Starting column for description text (column 79 = 2 spaces + 75 chars + 2 spacing) */
#define LAYOUT_DESCRIPTION_START_COL 79

/** Terminal width threshold for switching to vertical layout */
#define LAYOUT_NARROW_TERMINAL_THRESHOLD 55

/* ============================================================================
 * Layout Functions
 * ============================================================================ */

/**
 * @brief Print text with wrapping and proper indentation
 *
 * Wraps text to fit within terminal width, maintaining specified indentation
 * for wrapped lines. Applies color formatting to metadata labels.
 *
 * @param stream Output file stream
 * @param text Plain text to print (may contain wrapping markers)
 * @param indent_width Number of spaces to indent wrapped lines
 * @param term_width Terminal width (0 for default 80)
 * @param continuation_indent_extra Extra spaces to add to continuation lines (0 for none, 2 for options)
 */
void layout_print_wrapped_description(FILE *stream, const char *text, int indent_width, int term_width,
                                      int continuation_indent_extra);

/**
 * @brief Print two-column row with automatic wrapping
 *
 * Prints first column text, then second column description with proper alignment.
 * If first column is too long (>45 chars), description moves to next line.
 * Second column wraps at description start column.
 *
 * @param stream Output file stream
 * @param first_column Text for first column
 * @param second_column Description for second column
 * @param first_col_len Length of first column text (excluding ANSI codes)
 * @param term_width Terminal width
 * @param continuation_indent_extra Extra spaces for continuation lines (0 for none, 2 for options)
 */
void layout_print_two_column_row(FILE *stream, const char *first_column, const char *second_column, int first_col_len,
                                 int term_width, int continuation_indent_extra);

#endif // ASCII_CHAT_OPTIONS_LAYOUT_H

#pragma once

/**
 * @file video/ansi.h
 * @ingroup video
 * @brief ANSI escape sequence utilities
 *
 * Functions for manipulating ANSI escape sequences in strings,
 * including stripping all escape codes for plain text output.
 */

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Strip all ANSI escape sequences from a string
 *
 * Removes all ANSI CSI sequences (ESC [ ... final_byte) from the input,
 * leaving only printable text. Useful for creating plain text output
 * from colorized ASCII art.
 *
 * @param input Input string containing ANSI escape sequences
 * @param input_len Length of input string
 * @return Newly allocated string with escapes removed (caller must free),
 *         or NULL on error
 */
char *ansi_strip_escapes(const char *input, size_t input_len);

/**
 * @brief Check if a position in text is already colored
 *
 * Scans from the start of the message to the given position, tracking ANSI
 * escape codes. Returns true if there is an active color code (not in reset state).
 *
 * Detects reset codes: \x1b[0m and \x1b[m
 *
 * @param message The full message
 * @param pos Position to check
 * @return true if already colored (not in reset state), false if in reset state
 */
bool ansi_is_already_colorized(const char *message, size_t pos);

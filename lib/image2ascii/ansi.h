#pragma once

/**
 * @file image2ascii/ansi.h
 * @ingroup image2ascii
 * @brief ANSI escape sequence utilities
 *
 * Functions for manipulating ANSI escape sequences in strings,
 * including stripping all escape codes for plain text output.
 */

#include <stddef.h>

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

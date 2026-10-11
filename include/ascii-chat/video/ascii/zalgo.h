#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>
#include <stdint.h>

/** Decorate an ANSI frame with deterministic combining marks. Caller frees *output.
 * REP is expanded before decoration. Only printable ASCII bases are decorated;
 * whitespace, existing UTF-8, and escape sequences are preserved. The byte limit
 * excludes the NUL terminator; on overflow returns an error with *output = NULL.
 */
asciichat_error_t zalgo_apply(const char *input, size_t length, int level, size_t max_output_bytes, char **output);

/** Shared terminal/raster distribution. Returns 0-10 marks for one cell. */
unsigned zalgo_cell_marks(uint32_t row, uint32_t col, int level, uint16_t marks[10]);

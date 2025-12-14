/**
 * @file image2ascii/ansi.c
 * @ingroup image2ascii
 * @brief ANSI escape sequence utilities
 */

#include "ansi.h"
#include "output_buffer.h"
#include "common.h"

#include <string.h>

char *ansi_strip_escapes(const char *input, size_t input_len) {
  if (!input || input_len == 0) {
    return NULL;
  }

  // Output will be at most input_len (stripping only removes chars)
  outbuf_t ob = {0};
  ob_reserve(&ob, input_len);

  size_t i = 0;
  while (i < input_len) {
    // Check for ESC character (start of ANSI sequence)
    if (input[i] == '\x1b' && i + 1 < input_len && input[i + 1] == '[') {
      // Skip ESC[
      i += 2;

      // Skip parameter bytes (digits, semicolons, and intermediate bytes)
      while (i < input_len) {
        char c = input[i];
        // Parameter bytes: 0x30-0x3F (digits, semicolon, etc.)
        // Intermediate bytes: 0x20-0x2F (space, !, ", etc.)
        if ((c >= 0x30 && c <= 0x3F) || (c >= 0x20 && c <= 0x2F)) {
          i++;
        } else {
          break;
        }
      }

      // Skip final byte (0x40-0x7E: @, A-Z, [, \, ], ^, _, `, a-z, {, |, }, ~)
      if (i < input_len) {
        char final = input[i];
        if (final >= 0x40 && final <= 0x7E) {
          i++;
        }
      }
    } else {
      // Regular character - copy to output
      ob_putc(&ob, input[i]);
      i++;
    }
  }

  ob_term(&ob);
  return ob.buf;
}

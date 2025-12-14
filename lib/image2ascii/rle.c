/**
 * @file image2ascii/rle.c
 * @ingroup image2ascii
 * @brief ANSI RLE (REP) sequence compression and expansion
 */

#include "rle.h"
#include "output_buffer.h"
#include "common.h"

#include <string.h>

char *ansi_expand_rle(const char *input, size_t input_len) {
  if (!input || input_len == 0) {
    return NULL;
  }

  // RLE expansion can make output larger than input, start with 2x capacity
  outbuf_t ob = {0};
  ob_reserve(&ob, input_len * 2);

  // Track the last character/grapheme for RLE expansion
  // UTF-8 characters can be up to 4 bytes
  char last_char[5] = " ";
  size_t last_char_len = 1;

  size_t i = 0;
  while (i < input_len) {
    // Check for ESC character (start of ANSI sequence)
    if (input[i] == '\x1b' && i + 1 < input_len && input[i + 1] == '[') {
      size_t seq_start = i;
      i += 2; // Skip ESC[

      // Parse parameter bytes (digits and semicolons)
      uint32_t param = 0;
      while (i < input_len && ((input[i] >= '0' && input[i] <= '9') || input[i] == ';')) {
        if (input[i] >= '0' && input[i] <= '9') {
          param = param * 10 + (uint32_t)(input[i] - '0');
        } else if (input[i] == ';') {
          param = 0; // Reset for next parameter
        }
        i++;
      }

      // Check for final byte
      if (i < input_len) {
        char final_byte = input[i];
        i++;

        // Handle RLE: ESC[Nb repeats previous character N times
        if (final_byte == 'b' && param > 0) {
          for (uint32_t r = 0; r < param; r++) {
            ob_write(&ob, last_char, last_char_len);
          }
        } else {
          // Not RLE - copy the entire escape sequence as-is
          ob_write(&ob, input + seq_start, i - seq_start);
        }
      }
    } else {
      // Regular character - copy to output and track for RLE
      unsigned char c = (unsigned char)input[i];
      size_t char_len = 1;

      // Determine UTF-8 character length from first byte
      if ((c & 0x80) == 0) {
        char_len = 1; // ASCII
      } else if ((c & 0xE0) == 0xC0) {
        char_len = 2; // 2-byte UTF-8
      } else if ((c & 0xF0) == 0xE0) {
        char_len = 3; // 3-byte UTF-8
      } else if ((c & 0xF8) == 0xF0) {
        char_len = 4; // 4-byte UTF-8
      }

      // Make sure we don't read past end of input
      if (i + char_len > input_len) {
        char_len = input_len - i;
      }

      // Copy full UTF-8 character to output
      ob_write(&ob, input + i, char_len);

      // Track last printable character for RLE (skip control chars)
      if (c >= 0x20 && c != 0x7F) {
        SAFE_MEMCPY(last_char, sizeof(last_char), input + i, char_len);
        last_char[char_len] = '\0';
        last_char_len = char_len;
      }
      i += char_len;
    }
  }

  ob_term(&ob);
  return ob.buf;
}

char *ansi_compress_rle(const char *input, size_t input_len) {
  if (!input || input_len == 0) {
    return NULL;
  }

  outbuf_t ob = {0};
  ob_reserve(&ob, input_len);

  size_t i = 0;
  while (i < input_len) {
    // Check for ESC character (start of ANSI sequence)
    if (input[i] == '\x1b' && i + 1 < input_len && input[i + 1] == '[') {
      size_t seq_start = i;
      i += 2; // Skip ESC[

      // Skip parameter bytes
      while (i < input_len && ((input[i] >= '0' && input[i] <= '9') || input[i] == ';')) {
        i++;
      }

      // Skip final byte
      if (i < input_len) {
        i++;
      }

      // Copy entire escape sequence as-is
      ob_write(&ob, input + seq_start, i - seq_start);
    } else {
      // Regular character - check for runs
      char c = input[i];

      // Only compress printable characters (not newlines, not control chars)
      if (c >= 0x20 && c != 0x7F) {
        // Count run length
        size_t run_len = 1;
        i++;

        while (i < input_len && input[i] == c) {
          run_len++;
          i++;
        }

        // Emit first character
        ob_putc(&ob, c);

        // Use RLE if profitable (run > overhead of ESC[Nb)
        if (run_len > 1 && rep_is_profitable((uint32_t)run_len)) {
          emit_rep(&ob, (uint32_t)(run_len - 1));
        } else {
          // Emit remaining characters directly
          for (size_t k = 1; k < run_len; k++) {
            ob_putc(&ob, c);
          }
        }
      } else {
        // Non-compressible character (newline, etc.)
        ob_putc(&ob, c);
        i++;
      }
    }
  }

  ob_term(&ob);
  return ob.buf;
}

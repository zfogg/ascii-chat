/**
 * @file video/ansi.c
 * @ingroup video
 * @brief ANSI escape sequence utilities
 */

#include <ascii-chat/video/ascii/ansi.h>
#include <ascii-chat/video/output_buffer.h>
#include <ascii-chat/common.h>

#include <string.h>

char *ansi_strip_escapes(const char *input, size_t input_len) {
  if (!input || input_len == 0) {
    return NULL;
  }

  // Allocate buffer directly for output (at most input_len bytes)
  char *output = SAFE_MALLOC(input_len + 1, char *);
  if (!output) {
    return NULL;
  }

  size_t out_pos = 0;
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
      output[out_pos++] = input[i];
      i++;
    }
  }

  // Null-terminate the output
  output[out_pos] = '\0';
  return output;
}

bool ansi_is_already_colorized(const char *message, size_t pos) {
  if (!message) {
    return false;
  }

  bool in_reset = true; // Start in reset state

  // Scan from beginning to current position looking for ANSI codes
  for (size_t i = 0; i < pos && message[i] != '\0'; i++) {
    if (message[i] == '\x1b' && message[i + 1] == '[') {
      // Found ANSI escape sequence start
      // Look for the end marker 'm'
      size_t j = i + 2;
      while (message[j] != '\0' && message[j] != 'm') {
        j++;
      }

      if (message[j] == 'm') {
        // Extract the color code part (between [ and m)
        // Check if it's a reset code: \x1b[0m or \x1b[m
        if ((j == i + 3 && message[i + 2] == '0') || (j == i + 2)) {
          // Reset code
          in_reset = true;
        } else {
          // Color code (anything else)
          in_reset = false;
        }
      }
    }
  }

  // Return true if NOT in reset state (already colorized)
  return !in_reset;
}

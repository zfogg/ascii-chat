/**
 * @file platform/windows/question.c
 * @ingroup platform
 * @brief 💬 Windows interactive prompting with _getch() for secure input
 */

#include "platform/question.h"
#include "util/utf8.h"
#include "log/logging.h"
#include "platform/abstraction.h"

#include <conio.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

bool platform_is_interactive(void) {
  return _isatty(_fileno(stdin)) != 0;
}

int platform_prompt_question(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts) {
  if (!prompt || !buffer || max_len < 2) {
    return -1;
  }

  // Check for non-interactive mode
  if (!platform_is_interactive()) {
    return -1;
  }

  // Lock terminal so only this thread can output to terminal
  // Other threads' logs are buffered until we unlock
  bool previous_terminal_state = log_lock_terminal();

  // Display prompt based on same_line option
  if (opts.same_line) {
    log_plain_stderr_nonewline("%s ", prompt);
  } else {
    log_plain("%s", prompt);
    log_plain_stderr_nonewline("> ");
  }

  int result = 0;

  if (opts.echo) {
    // Echo enabled - use simple fgets
    if (fgets(buffer, (int)max_len, stdin) == NULL) {
      result = -1;
    } else {
      // Remove trailing newline
      size_t len = strlen(buffer);
      if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
      }
    }
  } else {
    // Echo disabled - use _getch for character-by-character input with cursor support
    size_t len = 0;    // Total length of input
    size_t cursor = 0; // Cursor position within input
    int ch;

    while (len < max_len - 1) {
      ch = _getch();

      // Handle newline/carriage return (Enter key)
      if (ch == '\r' || ch == '\n') {
        break;
      }

      // Handle Ctrl+C (interrupt)
      if (ch == 3) {
        (void)fprintf(stderr, "\n");
        result = -1;
        break;
      }

      // Handle extended keys (0xE0 or 0x00 prefix)
      if (ch == 0xE0 || ch == 0x00) {
        int ext = _getch();
        switch (ext) {
        case 0x4B: // Left arrow
          if (cursor > 0) {
            cursor--;
            (void)fprintf(stderr, "\033[D");
          }
          break;
        case 0x4D: // Right arrow
          if (cursor < len) {
            cursor++;
            (void)fprintf(stderr, "\033[C");
          }
          break;
        case 0x53: // Delete key
          if (cursor < len) {
            // Shift characters left
            memmove(&buffer[cursor], &buffer[cursor + 1], len - cursor - 1);
            len--;
            // Redraw from cursor to end
            if (opts.mask_char) {
              for (size_t i = cursor; i < len; i++) {
                (void)fprintf(stderr, "%c", opts.mask_char);
              }
              (void)fprintf(stderr, " "); // Clear last char
              // Move cursor back
              for (size_t i = cursor; i <= len; i++) {
                (void)fprintf(stderr, "\033[D");
              }
            }
          }
          break;
        case 0x47: // Home
          while (cursor > 0) {
            cursor--;
            (void)fprintf(stderr, "\033[D");
          }
          break;
        case 0x4F: // End
          while (cursor < len) {
            cursor++;
            (void)fprintf(stderr, "\033[C");
          }
          break;
        }
        continue;
      }

      // Handle backspace (BS) - delete character before cursor
      if (ch == '\b') {
        if (cursor > 0) {
          // Shift characters left
          memmove(&buffer[cursor - 1], &buffer[cursor], len - cursor);
          cursor--;
          len--;
          // Redraw: move back, print rest of line, clear last char, reposition
          if (opts.mask_char) {
            (void)fprintf(stderr, "\033[D"); // Move cursor back
            for (size_t i = cursor; i < len; i++) {
              (void)fprintf(stderr, "%c", opts.mask_char);
            }
            (void)fprintf(stderr, " "); // Clear the old last char
            // Move cursor back to position
            for (size_t i = cursor; i <= len; i++) {
              (void)fprintf(stderr, "\033[D");
            }
          }
        }
        continue;
      }

      // Printable characters (ASCII or multi-byte UTF-8)
      if (ch >= 32) {
        // Determine how many continuation bytes are needed for this character
        int continuation_bytes = utf8_continuation_bytes_needed((unsigned char)ch);
        if (continuation_bytes < 0) {
          // Invalid UTF-8 start byte, skip it
          continue;
        }

        // Insert first byte (or ASCII character) at cursor position
        if (len < max_len - 1) {
          // Shift characters right to make room for this byte
          memmove(&buffer[cursor + 1], &buffer[cursor], len - cursor);
          buffer[cursor] = (char)ch;
          len++;
          cursor++;

          // Read continuation bytes for multi-byte UTF-8 if needed
          if (continuation_bytes > 0) {
            if (utf8_read_and_insert_continuation_bytes(buffer, &cursor, &len, max_len, continuation_bytes,
                                                        (int (*)(void))_getch) < 0) {
              result = -1;
            }
          }

          // Display: print from cursor-continuation_bytes-1 to end, then reposition
          if (opts.mask_char) {
            for (size_t i = cursor - continuation_bytes - 1; i < len; i++) {
              (void)fprintf(stderr, "%c", opts.mask_char);
            }
            // Move cursor back to correct position
            for (size_t i = cursor; i < len; i++) {
              (void)fprintf(stderr, "\033[D");
            }
          }
        }
      }
    }

    // Null-terminate the buffer
    buffer[len] = '\0';

    // Print newline after hidden input
    (void)fprintf(stderr, "\n");
  }

  // Unlock terminal - buffered logs from other threads will be flushed
  log_unlock_terminal(previous_terminal_state);
  return result;
}

bool platform_prompt_yes_no(const char *prompt, bool default_yes) {
  if (!prompt) {
    return false;
  }

  bool is_interactive = platform_is_interactive();

  // Only prompt if interactive (avoid blocking on non-TTY stdin)
  if (!is_interactive) {
    return default_yes;
  }

  // Display prompt with default indicator
  if (default_yes) {
    log_plain_stderr_nonewline("%s (Y/n)? ", prompt);
  } else {
    log_plain_stderr_nonewline("%s (y/N)? ", prompt);
  }

  bool result = default_yes;

  char response[16];
  if (fgets(response, sizeof(response), stdin) != NULL) {
    // Remove trailing newline
    size_t len = strlen(response);
    if (len > 0 && response[len - 1] == '\n') {
      response[len - 1] = '\0';
      len--;
    }

    // Empty response = use default
    if (len == 0) {
      result = default_yes;
    }
    // Check for yes
    else if (_stricmp(response, "yes") == 0 || _stricmp(response, "y") == 0) {
      result = true;
    }
    // Check for no
    else if (_stricmp(response, "no") == 0 || _stricmp(response, "n") == 0) {
      result = false;
    }
    // Invalid input - use default
    else {
      result = default_yes;
    }
  } else {
    // fgets failed (EOF or error) - return default
    result = default_yes;
  }

  return result;
}

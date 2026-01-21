/**
 * @file platform/posix/question.c
 * @ingroup platform
 * @brief 💬 POSIX interactive prompting with terminal control for secure input
 */

#include "platform/question.h"
#include "util/utf8.h"
#include "log/logging.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>

bool platform_is_interactive(void) {
  return platform_isatty(STDIN_FILENO);
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
    // Echo disabled - use termios for character-by-character input
    struct termios old_termios, new_termios;
    int tty_changed = 0;

    if (tcgetattr(STDIN_FILENO, &old_termios) == 0) {
      new_termios = old_termios;
      // Disable canonical mode (line buffering) and echo
      new_termios.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHOE | ECHOK | ECHONL);
      // Set minimum characters for read
      new_termios.c_cc[VMIN] = 1;
      new_termios.c_cc[VTIME] = 0;

      if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
        tty_changed = 1;
      }
    }

    // Read input character by character with cursor support
    size_t len = 0;    // Total length of input
    size_t cursor = 0; // Cursor position within input
    int c;

    while (len < max_len - 1) {
      c = getchar();

      if (c == EOF) {
        result = -1;
        break;
      }

      // Handle newline/carriage return (Enter key)
      if (c == '\n' || c == '\r') {
        break;
      }

      // Handle escape sequences (arrow keys, delete, etc.)
      if (c == 27) { // ESC
        int next = getchar();
        if (next == '[') {
          int code = getchar();
          switch (code) {
          case 'D': // Left arrow
            if (cursor > 0) {
              cursor--;
              fprintf(stderr, "\033[D");
              fflush(stderr);
            }
            break;
          case 'C': // Right arrow
            if (cursor < len) {
              cursor++;
              fprintf(stderr, "\033[C");
              fflush(stderr);
            }
            break;
          case '3': // Delete key (sends ESC[3~)
            if (getchar() == '~' && cursor < len) {
              // Shift characters left
              memmove(&buffer[cursor], &buffer[cursor + 1], len - cursor - 1);
              len--;
              // Redraw from cursor to end
              if (opts.mask_char) {
                for (size_t i = cursor; i < len; i++) {
                  fprintf(stderr, "%c", opts.mask_char);
                }
                fprintf(stderr, " "); // Clear last char
                // Move cursor back
                for (size_t i = cursor; i <= len; i++) {
                  fprintf(stderr, "\033[D");
                }
              }
              fflush(stderr);
            }
            break;
          case 'H': // Home
            while (cursor > 0) {
              cursor--;
              fprintf(stderr, "\033[D");
            }
            fflush(stderr);
            break;
          case 'F': // End
            while (cursor < len) {
              cursor++;
              fprintf(stderr, "\033[C");
            }
            fflush(stderr);
            break;
          default:
            // Ignore unknown escape sequences
            break;
          }
        }
        continue;
      }

      // Handle backspace (BS = 8 or DEL = 127) - delete character before cursor
      if (c == 8 || c == 127) {
        if (cursor > 0) {
          // Shift characters left
          memmove(&buffer[cursor - 1], &buffer[cursor], len - cursor);
          cursor--;
          len--;
          // Redraw: move back, print rest of line, clear last char, reposition
          if (opts.mask_char) {
            fprintf(stderr, "\033[D"); // Move cursor back
            for (size_t i = cursor; i < len; i++) {
              fprintf(stderr, "%c", opts.mask_char);
            }
            fprintf(stderr, " "); // Clear the old last char
            // Move cursor back to position
            for (size_t i = cursor; i <= len; i++) {
              fprintf(stderr, "\033[D");
            }
          }
          fflush(stderr);
        }
        continue;
      }

      // Handle Ctrl+C (interrupt)
      if (c == 3) {
        fprintf(stderr, "\n");
        result = -1;
        break;
      }

      // Ignore other control characters
      if (c < 32 && c != '\t') {
        continue;
      }

      // Determine how many continuation bytes are needed for this character
      int continuation_bytes = utf8_continuation_bytes_needed((unsigned char)c);
      if (continuation_bytes < 0) {
        // Invalid UTF-8 start byte, skip it
        continue;
      }

      // Insert first byte (or ASCII character) at cursor position
      if (len < max_len - 1) {
        // Shift characters right to make room for this byte
        memmove(&buffer[cursor + 1], &buffer[cursor], len - cursor);
        buffer[cursor] = (char)c;
        len++;
        cursor++;

        // Read continuation bytes for multi-byte UTF-8 if needed
        if (continuation_bytes > 0) {
          if (utf8_read_and_insert_continuation_bytes(buffer, &cursor, &len, max_len,
                                                       continuation_bytes, getchar) < 0) {
            result = -1;
          }
        }

        // Display: print from cursor-continuation_bytes-1 to end, then reposition
        if (opts.mask_char) {
          for (size_t i = cursor - continuation_bytes - 1; i < len; i++) {
            fprintf(stderr, "%c", opts.mask_char);
          }
          // Move cursor back to correct position
          for (size_t i = cursor; i < len; i++) {
            fprintf(stderr, "\033[D");
          }
          fflush(stderr);
        }
      }
    }

    // Update pos to final length for null termination
    size_t pos = len;

    // Null-terminate the buffer
    buffer[pos] = '\0';

    // Restore terminal settings
    if (tty_changed) {
      tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    }

    // Print newline after hidden input
    fprintf(stderr, "\n");
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
  bool previous_terminal_state = false;

  // Only lock terminal and show prompt if interactive
  if (is_interactive) {
    // Lock terminal so only this thread can output to terminal
    previous_terminal_state = log_lock_terminal();

    // Display prompt with default indicator
    if (default_yes) {
      log_plain_stderr_nonewline("%s (Y/n)? ", prompt);
    } else {
      log_plain_stderr_nonewline("%s (y/N)? ", prompt);
    }
  }

  char response[16];
  bool result = default_yes;

  if (fgets(response, sizeof(response), stdin) != NULL) {
    // Remove trailing newline
    size_t len = strlen(response);
    if (len > 0 && response[len - 1] == '\n') {
      response[len - 1] = '\0';
      len--;
    }

    // Check for explicit yes/no, otherwise use default
    if (strcasecmp(response, "yes") == 0 || strcasecmp(response, "y") == 0) {
      result = true;
    } else if (strcasecmp(response, "no") == 0 || strcasecmp(response, "n") == 0) {
      result = false;
    } else {
      // Empty response or invalid input = use default
      result = default_yes;
    }
  } else {
    // fgets failed (EOF or error) - return default
    result = default_yes;
  }

  // Unlock terminal if we locked it
  if (is_interactive) {
    log_unlock_terminal(previous_terminal_state);
  }

  return result;
}

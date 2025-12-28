/**
 * @file platform/windows/question.c
 * @ingroup platform
 * @brief 💬 Windows interactive prompting with _getch() for secure input
 */

#include "platform/question.h"
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
    // Echo disabled - use _getch for character-by-character input
    size_t pos = 0;
    int ch;

    while (pos < max_len - 1) {
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

      // Handle backspace
      if (ch == '\b' && pos > 0) {
        pos--;
        if (opts.mask_char) {
          // Erase mask character: backspace, space, backspace
          (void)fprintf(stderr, "\b \b");
        }
      } else if (ch >= 32 && ch <= 126) {
        // Printable character: add to buffer
        buffer[pos++] = (char)ch;
        if (opts.mask_char) {
          (void)fprintf(stderr, "%c", opts.mask_char);
        }
      }
    }

    // Null-terminate the buffer
    buffer[pos] = '\0';

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

  // Check for non-interactive mode
  if (!platform_is_interactive()) {
    return false;
  }

  // Lock terminal so only this thread can output to terminal
  bool previous_terminal_state = log_lock_terminal();

  // Display prompt with default indicator
  if (default_yes) {
    log_plain_stderr_nonewline("%s (Y/n)? ", prompt);
  } else {
    log_plain_stderr_nonewline("%s (y/N)? ", prompt);
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
  }

  // Unlock terminal
  log_unlock_terminal(previous_terminal_state);
  return result;
}

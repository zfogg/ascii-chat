/**
 * @file platform/posix/question.c
 * @ingroup platform
 * @brief 💬 POSIX interactive prompting with terminal control for secure input
 */

#include "platform/question.h"
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
      new_termios.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL);
      // Set minimum characters for read
      new_termios.c_cc[VMIN] = 1;
      new_termios.c_cc[VTIME] = 0;

      if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
        tty_changed = 1;
      }
    }

    // Read input character by character
    size_t pos = 0;
    int c;

    while (pos < max_len - 1) {
      c = getchar();

      if (c == EOF) {
        result = -1;
        break;
      }

      // Handle newline/carriage return (Enter key)
      if (c == '\n' || c == '\r') {
        break;
      }

      // Handle backspace (127 = DEL, 8 = BS)
      if (c == 127 || c == 8) {
        if (pos > 0) {
          pos--;
          if (opts.mask_char) {
            // Erase the mask character: backspace, space, backspace
            fprintf(stderr, "\b \b");
            fflush(stderr);
          }
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

      // Add character to buffer
      buffer[pos++] = (char)c;

      // Display mask character if specified
      if (opts.mask_char) {
        fprintf(stderr, "%c", opts.mask_char);
        fflush(stderr);
      }
    }

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
    else if (strcasecmp(response, "yes") == 0 || strcasecmp(response, "y") == 0) {
      result = true;
    }
    // Check for no
    else if (strcasecmp(response, "no") == 0 || strcasecmp(response, "n") == 0) {
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

/**
 * @file platform/windows/password.c
 * @ingroup platform
 * @brief 🔐 Windows password prompt with _getch() for secure input without echo
 */

#include "platform/password.h"
#include "logging.h"
#include <stdio.h>
#include <conio.h>

int platform_prompt_password(const char *prompt, char *password, size_t max_len) {
  // Disable terminal logging so other threads don't interfere with password prompt
  bool previous_terminal_state = log_lock_terminal();

  fprintf(stderr, "\n");
  fprintf(stderr, "========================================\n");
  fprintf(stderr, "%s\n", prompt);
  fprintf(stderr, "========================================\n");
  fprintf(stderr, "> ");
  fflush(stderr);

  // Use _getch for secure password input (no echo)
  int i = 0;
  int ch;
  while (i < (int)(max_len - 1)) {
    ch = _getch();

    // Handle newline/carriage return (Enter key)
    if (ch == '\r' || ch == '\n') {
      break;
    }

    // Handle Ctrl+C (interrupt)
    if (ch == 3) {
      (void)fprintf(stderr, "\n");
      log_unlock_terminal(previous_terminal_state);
      return -1;
    }

    // Handle backspace
    if (ch == '\b' && i > 0) {
      // Backspace: remove last character and erase asterisk
      i--;
      (void)fprintf(stderr, "\b \b");
    } else if (ch >= 32 && ch <= 126) {
      // Printable character: add to password and display asterisk
      password[i++] = (char)ch;
      (void)fprintf(stderr, "*");
    }
  }
  password[i] = '\0';

  (void)fprintf(stderr, "\n========================================\n\n");
  (void)fflush(stderr);

  // Re-enable terminal logging
  log_unlock_terminal(previous_terminal_state);
  return 0;
}

#include "platform/password.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

int platform_prompt_password(const char *prompt, char *password, size_t max_len) {
  fprintf(stderr, "\n");
  fprintf(stderr, "========================================\n");
  fprintf(stderr, "%s\n", prompt);
  fprintf(stderr, "========================================\n");
  fprintf(stderr, "> ");
  fflush(stderr);

  // Disable terminal echo and canonical mode for character-by-character input
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

  // Read password character by character with asterisk masking
  size_t pos = 0;
  int c;

  while (pos < max_len - 1) {
    c = getchar();

    if (c == EOF) {
      if (tty_changed) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
      }
      fprintf(stderr, "\nERROR: Failed to read password\n");
      return -1;
    }

    // Handle newline/carriage return (Enter key)
    if (c == '\n' || c == '\r') {
      break;
    }

    // Handle backspace (127 = DEL, 8 = BS)
    if (c == 127 || c == 8) {
      if (pos > 0) {
        pos--;
        // Erase the asterisk: backspace, space, backspace
        fprintf(stderr, "\b \b");
        fflush(stderr);
      }
      continue;
    }

    // Handle Ctrl+C (interrupt)
    if (c == 3) {
      if (tty_changed) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
      }
      fprintf(stderr, "\n");
      return -1;
    }

    // Ignore other control characters
    if (c < 32 && c != '\t') {
      continue;
    }

    // Add character to password and display asterisk
    password[pos++] = (char)c;
    fprintf(stderr, "*");
    fflush(stderr);
  }

  // Null-terminate the password
  password[pos] = '\0';

  // Restore terminal settings
  if (tty_changed) {
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
  }

  fprintf(stderr, "\n========================================\n\n");
  return 0;
}

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

  // Disable terminal echo for secure password input
  struct termios old_termios, new_termios;
  if (tcgetattr(STDIN_FILENO, &old_termios) == 0) {
    new_termios = old_termios;
    new_termios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
      if (fgets(password, max_len, stdin) == NULL) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
        fprintf(stderr, "\nERROR: Failed to read password\n");
        return -1;
      }
      tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    } else {
      // Fallback to normal input if tcsetattr fails
      if (fgets(password, max_len, stdin) == NULL) {
        fprintf(stderr, "\nERROR: Failed to read password\n");
        return -1;
      }
    }
  } else {
    // Fallback to normal input if tcgetattr fails
    if (fgets(password, max_len, stdin) == NULL) {
      fprintf(stderr, "\nERROR: Failed to read password\n");
      return -1;
    }
  }

  // Remove trailing newline
  size_t len = strlen(password);
  if (len > 0 && password[len - 1] == '\n') {
    password[len - 1] = '\0';
  }

  fprintf(stderr, "\n========================================\n\n");
  return 0;
}

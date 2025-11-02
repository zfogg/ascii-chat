#include "platform/password.h"
#include <stdio.h>
#include <conio.h>

int platform_prompt_password(const char *prompt, char *password, size_t max_len) {
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
      fprintf(stderr, "\n");
      return -1;
    }

    // Handle backspace
    if (ch == '\b' && i > 0) {
      // Backspace: remove last character and erase asterisk
      i--;
      fprintf(stderr, "\b \b");
      fflush(stderr);
    } else if (ch >= 32 && ch <= 126) {
      // Printable character: add to password and display asterisk
      password[i++] = ch;
      fprintf(stderr, "*");
      fflush(stderr);
    }
  }
  password[i] = '\0';

  fprintf(stderr, "\n========================================\n\n");
  return 0;
}

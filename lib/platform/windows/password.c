#include "platform/password.h"
#include <stdio.h>
#include <conio.h>

int platform_prompt_password(const char *prompt, char *password, size_t max_len) {
  fprintf(stderr, "\n");
  fprintf(stderr, "========================================\n");
  fprintf(stderr, "%s\n", prompt);
  fprintf(stderr, "========================================\n");
  fprintf(stderr, "> ");

  // Use _getch for secure password input (no echo)
  int i = 0;
  int ch;
  while (i < (int)(max_len - 1)) {
    ch = _getch();
    if (ch == '\r' || ch == '\n') {
      break;
    }
    if (ch == '\b' && i > 0) {
      // Backspace: remove last character
      i--;
      fprintf(stderr, "\b \b");
    } else if (ch >= 32 && ch <= 126) {
      // Printable character
      password[i++] = ch;
      fprintf(stderr, "*");
    }
  }
  password[i] = '\0';

  fprintf(stderr, "\n========================================\n\n");
  return 0;
}

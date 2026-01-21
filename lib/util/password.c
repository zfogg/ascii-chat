/**
 * @file util/password.c
 * @ingroup util
 * @brief 🔑 Password prompting utilities with secure input and formatting
 */

#include "util/password.h"
#include "log/logging.h"
#include "platform/question.h"
#include "util/utf8.h"

#include <string.h>

int prompt_password(const char *prompt, char *password, size_t max_len) {
  if (!prompt || !password || max_len < 2) {
    return -1;
  }

  // Check for non-interactive mode first
  if (!platform_is_interactive()) {
    return -1;
  }

  // Lock terminal for the entire operation
  bool previous_terminal_state = log_lock_terminal();

  // Calculate display width of prompt for alignment
  int prompt_width = utf8_display_width(prompt);
  if (prompt_width < 0) {
    prompt_width = 0;
  }

  // Create separator that matches prompt width (minimum 40 chars)
  int separator_width = (prompt_width > 40) ? prompt_width : 40;
  char separator[256];
  if (separator_width > (int)sizeof(separator) - 1) {
    separator_width = sizeof(separator) - 1;
  }
  for (int i = 0; i < separator_width; i++) {
    separator[i] = '=';
  }
  separator[separator_width] = '\0';

  // Display formatted header
  log_plain("\n%s", separator);
  log_plain("%s", prompt);
  log_plain("%s", separator);

  // Unlock before prompting (prompt_question will lock again)
  log_unlock_terminal(previous_terminal_state);

  // Prompt for password with asterisk masking
  prompt_opts_t opts = PROMPT_OPTS_PASSWORD;
  int result = platform_prompt_question("", password, max_len, opts);

  // Validate password is valid UTF-8 (should always succeed since platform_prompt_question handles it)
  if (result == 0 && !utf8_is_valid(password)) {
    log_warn("Password contains invalid UTF-8 sequence, input may be corrupted");
  }

  // Display footer
  previous_terminal_state = log_lock_terminal();
  log_plain("%s\n", separator);
  log_unlock_terminal(previous_terminal_state);

  return result;
}

int prompt_password_simple(const char *prompt, char *password, size_t max_len) {
  if (!prompt || !password || max_len < 2) {
    return -1;
  }

  // Check for non-interactive mode first
  if (!platform_is_interactive()) {
    return -1;
  }

  // Build prompt with colon suffix, using byte-length for memcpy
  char full_prompt[256];
  size_t prompt_byte_len = strlen(prompt);
  if (prompt_byte_len >= sizeof(full_prompt) - 2) {
    prompt_byte_len = sizeof(full_prompt) - 3;
  }
  memcpy(full_prompt, prompt, prompt_byte_len);
  full_prompt[prompt_byte_len] = ':';
  full_prompt[prompt_byte_len + 1] = '\0';

  // Prompt for password with asterisk masking, same line
  prompt_opts_t opts = PROMPT_OPTS_PASSWORD;
  int result = platform_prompt_question(full_prompt, password, max_len, opts);

  // Validate password is valid UTF-8 (should always succeed since platform_prompt_question handles it)
  if (result == 0 && !utf8_is_valid(password)) {
    log_warn("Password contains invalid UTF-8 sequence, input may be corrupted");
  }

  return result;
}

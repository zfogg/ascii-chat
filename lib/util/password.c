/**
 * @file util/password.c
 * @ingroup util
 * @brief 🔑 Password prompting utilities with secure input and formatting
 */

#include "util/password.h"
#include "logging/logging.h"
#include "platform/question.h"

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

  // Display formatted header
  log_plain("\n========================================");
  log_plain("%s", prompt);
  log_plain("========================================");

  // Unlock before prompting (prompt_question will lock again)
  log_unlock_terminal(previous_terminal_state);

  // Prompt for password with asterisk masking
  prompt_opts_t opts = PROMPT_OPTS_PASSWORD;
  int result = platform_prompt_question("", password, max_len, opts);

  // Display footer
  previous_terminal_state = log_lock_terminal();
  log_plain("========================================\n");
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

  // Build prompt with colon suffix
  char full_prompt[256];
  size_t prompt_len = strlen(prompt);
  if (prompt_len >= sizeof(full_prompt) - 2) {
    prompt_len = sizeof(full_prompt) - 3;
  }
  memcpy(full_prompt, prompt, prompt_len);
  full_prompt[prompt_len] = ':';
  full_prompt[prompt_len + 1] = '\0';

  // Prompt for password with asterisk masking, same line
  prompt_opts_t opts = PROMPT_OPTS_PASSWORD;
  return platform_prompt_question(full_prompt, password, max_len, opts);
}

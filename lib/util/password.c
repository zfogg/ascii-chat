/**
 * @file util/password.c
 * @ingroup util
 * @brief 🔑 Password prompting utilities with secure input and formatting
 */

#include <ascii-chat/util/password.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/common/buffer_sizes.h>

#include <string.h>

int prompt_password(const char *prompt, char *password, size_t max_len) {
  if (!prompt || !password || max_len < 2) {
    return -1;
  }

  // Check for non-interactive mode first
  if (!platform_is_interactive()) {
    return -1;
  }

  // The prompt renderer owns its header and masked input as one snapshot.
  int result = platform_prompt_question(prompt, password, max_len, PROMPT_OPTS_PASSWORD);

  // Validate password is valid UTF-8 (should always succeed since platform_prompt_question handles it)
  if (result == 0 && !utf8_is_valid(password)) {
    log_warn("Password contains invalid UTF-8 sequence, input may be corrupted");
  }

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
  char full_prompt[BUFFER_SIZE_SMALL];
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

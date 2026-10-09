/**
 * @file platform/posix/question.c
 * @ingroup platform
 * @brief 💬 POSIX interactive prompting with terminal control for secure input
 */

#include <ascii-chat/platform/question.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/env.h>
#include <ascii-chat/log/log.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>

bool platform_is_interactive(void) {
  // Use centralized function that checks TTY status, snapshot mode, and automation
  return terminal_can_prompt_user();
}

int platform_prompt_question(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts) {
  if (!prompt || !buffer || max_len < 2) {
    return -1;
  }

  // Check for testing environment variable override for password prompts
  if (opts.mask_char != 0) {
    char test_password[256];
    if (env_pop_prompt_response(test_password, sizeof(test_password))) {
      size_t len = strlen(test_password);
      if (len >= max_len) {
        return -1; // Password too long
      }
      memcpy(buffer, test_password, len);
      buffer[len] = '\0';
      return 0;
    }
  }

  // Check for non-interactive mode
  if (!platform_is_interactive()) {
    return -1;
  }

  return ui_input_prompt(prompt, buffer, max_len, opts) == ASCIICHAT_OK ? 0 : -1;
}

bool platform_prompt_yes_no(const char *prompt, bool default_yes) {
  if (!prompt) {
    return false;
  }

  // Check for testing environment variable override FIRST (before any TTY checks)
  char test_response[256];
  if (env_pop_prompt_response(test_response, sizeof(test_response))) {
    if (strcasecmp(test_response, "yes") == 0 || strcasecmp(test_response, "y") == 0) {
      return true;
    } else if (strcasecmp(test_response, "no") == 0 || strcasecmp(test_response, "n") == 0) {
      return false;
    }
    // Invalid response - fall through to interactive prompt
  }

  char response[16] = {0};
  if (platform_is_interactive()) {
    char question[4096];
    snprintf(question, sizeof(question), "%s %s", prompt, default_yes ? "(Y/n)?" : "(y/N)?");
    if (ui_input_prompt(question, response, sizeof(response), PROMPT_OPTS_INLINE) != ASCIICHAT_OK)
      return false;
  } else {
    return default_yes;
  }
  if (strcasecmp(response, "yes") == 0 || strcasecmp(response, "y") == 0)
    return true;
  if (strcasecmp(response, "no") == 0 || strcasecmp(response, "n") == 0)
    return false;
  return default_yes;
}

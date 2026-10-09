/**
 * @file platform/windows/question.c
 * @ingroup platform
 * @brief 💬 Windows interactive prompting with _getch() for secure input
 */

#include <ascii-chat/platform/question.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/env.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/abstraction.h>

#include <conio.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

bool platform_is_interactive(void) {
  // Callers may guard prompts before they consume an automated response.
  return _isatty(_fileno(stdin)) != 0;
}

int platform_prompt_question(const char *prompt, char *buffer, size_t max_len, prompt_opts_t opts) {
  if (!prompt || !buffer || max_len < 2) {
    return -1;
  }

  // Consume explicit answers before checking terminal availability.
  {
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
    log_error("Cannot answer prompt without an interactive terminal: %s", prompt);
    return -1;
  }

  return ui_input_prompt(prompt, buffer, max_len, opts) == ASCIICHAT_OK ? 0 : -1;
}

bool platform_prompt_yes_no(const char *prompt, bool default_yes) {
  return platform_prompt_yes_no_timeout(prompt, default_yes, 30);
}

bool platform_prompt_yes_no_timeout(const char *prompt, bool default_yes, unsigned timeout_seconds) {
  if (!prompt) {
    return false;
  }

  // Check for testing environment variable override
  char test_response[256];
  if (env_pop_prompt_response(test_response, sizeof(test_response))) {
    if (_stricmp(test_response, "yes") == 0 || _stricmp(test_response, "y") == 0) {
      return true;
    } else if (_stricmp(test_response, "no") == 0 || _stricmp(test_response, "n") == 0) {
      return false;
    }
    // Invalid response - fall through to interactive prompt
  }

  char response[16] = {0};
  if (platform_is_interactive()) {
    char question[4096];
    snprintf(question, sizeof(question), "%s %s", prompt, default_yes ? "(Y/n)?" : "(y/N)?");
    prompt_opts_t opts = PROMPT_OPTS_INLINE;
    opts.timeout_seconds = timeout_seconds;
    if (ui_input_prompt(question, response, sizeof(response), opts) != ASCIICHAT_OK)
      return false;
  } else {
    log_error("Cannot answer prompt without an interactive terminal: %s", prompt);
    return false;
  }
  if (_stricmp(response, "yes") == 0 || _stricmp(response, "y") == 0)
    return true;
  if (_stricmp(response, "no") == 0 || _stricmp(response, "n") == 0)
    return false;
  return default_yes;
}

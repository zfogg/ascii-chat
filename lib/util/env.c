/**
 * @file env.c
 * @brief Environment variable utilities with stack-based prompt responses
 */

#include <ascii-chat/util/env.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Find the first unescaped semicolon in a string
 *
 * Supports backslash escaping: \; is treated as a literal semicolon, \\ as a literal backslash.
 *
 * @param str String to search
 * @return Pointer to first unescaped semicolon, or NULL if not found
 */
static const char *find_unescaped_semicolon(const char *str) {
  if (!str) {
    return NULL;
  }

  const char *p = str;
  while (*p) {
    if (*p == '\\' && *(p + 1)) {
      // Skip escaped character (\\  or \;)
      p += 2;
      continue;
    }
    if (*p == ';') {
      return p;
    }
    p++;
  }
  return NULL;
}

/**
 * @brief Unescape a string by converting \; to ; and \\ to \
 *
 * Modifies the string in-place.
 *
 * @param str String to unescape (modified in-place)
 */
static void unescape_response(char *str) {
  if (!str) {
    return;
  }

  char *read = str;
  char *write = str;

  while (*read) {
    if (*read == '\\' && *(read + 1)) {
      // Escaped character: \; → ; or \\ → \.
      read++; // Skip backslash
      *write++ = *read++;
    } else {
      *write++ = *read++;
    }
  }
  *write = '\0';
}

/**
 * @brief Pop the first response from ASCII_CHAT_QUESTION_PROMPT_RESPONSE stack
 *
 * Format: "response1;response2;response3" or "response1;response2;response3;"
 * This function extracts the first response and updates the environment variable
 * to contain the remaining responses.
 *
 * Escaping: Use backslash to include literal semicolons or backslashes:
 * - \; → ; (literal semicolon)
 * - \\ → \ (literal backslash)
 *
 * Examples:
 * - "y;n;123" -> returns "y", sets env to "n;123"
 * - "password" -> returns "password", sets env to ""
 * - "y;" -> returns "y", sets env to ""
 * - "pass\;word;y" -> returns "pass;word", sets env to "y"
 * - "path\\to\\file;n" -> returns "path\to\file", sets env to "n"
 * - "" -> returns NULL
 * - ";;y" -> returns NULL (invalid format)
 * - ";y" -> returns NULL (invalid format)
 *
 * @param response_out Buffer to store the popped response (must be at least 256 bytes)
 * @param response_size Size of response_out buffer
 * @return true if a response was successfully popped, false otherwise
 */
bool env_pop_prompt_response(char *response_out, size_t response_size) {
  if (!response_out || response_size < 2) {
    return false;
  }

  const char *env_value = SAFE_GETENV("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
  if (!env_value || env_value[0] == '\0') {
    return false;
  }

  // Validate: no leading semicolon (unless escaped)
  if (env_value[0] == ';') {
    log_warn("Invalid ASCII_CHAT_QUESTION_PROMPT_RESPONSE format: leading semicolon");
    return false;
  }

  // Find the first unescaped semicolon
  const char *semicolon = find_unescaped_semicolon(env_value);

  if (semicolon == NULL) {
    // No semicolon - this is the last (or only) response
    size_t len = strlen(env_value);
    if (len == 0) {
      return false;
    }

    // Validate: ensure it's not empty
    if (len >= response_size) {
      log_warn("Response too long: %zu bytes (max: %zu)", len, response_size - 1);
      return false;
    }

    // Copy the response
    SAFE_STRNCPY(response_out, env_value, response_size);

    // Unescape the response (\; → ; and \\ → \)
    unescape_response(response_out);

    // Clear the environment variable
#ifdef _WIN32
    _putenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE=");
#else
    unsetenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
#endif

    log_dev("Popped last response from stack: '%s' (cleared env)", response_out);
    return true;
  }

  // Calculate response length
  size_t response_len = (size_t)(semicolon - env_value);

  // Validate: no empty segment
  if (response_len == 0) {
    log_warn("Invalid ASCII_CHAT_QUESTION_PROMPT_RESPONSE format: empty segment");
    return false;
  }

  if (response_len >= response_size) {
    log_warn("Response too long: %zu bytes (max: %zu)", response_len, response_size - 1);
    return false;
  }

  // Copy the first response
  memcpy(response_out, env_value, response_len);
  response_out[response_len] = '\0';

  // Unescape the response (\; → ; and \\ → \)
  unescape_response(response_out);

  // Calculate remaining stack (skip the semicolon)
  const char *remaining = semicolon + 1;

  // Validate: if remaining is empty or only semicolons, clear the env
  bool remaining_valid = false;
  for (const char *p = remaining; *p; p++) {
    if (*p != ';') {
      remaining_valid = true;
      break;
    }
  }

  // Update environment variable with remaining responses
  if (remaining_valid && remaining[0] != '\0') {
    char new_env[1024];
    int written = snprintf(new_env, sizeof(new_env), "ASCII_CHAT_QUESTION_PROMPT_RESPONSE=%s", remaining);
    if (written < 0 || (size_t)written >= sizeof(new_env)) {
      log_warn("Failed to format new environment variable");
      return false;
    }

#ifdef _WIN32
    _putenv(new_env);
#else
    // For POSIX, we need to use setenv
    if (setenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", remaining, 1) != 0) {
      log_warn("Failed to update ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
      return false;
    }
#endif

    log_debug("Popped response from stack: '%s' (remaining: '%s')", response_out, remaining);
  } else {
    // No valid remaining responses - clear the env
#ifdef _WIN32
    _putenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE=");
#else
    unsetenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
#endif

    log_debug("Popped response from stack: '%s' (cleared env)", response_out);
  }

  return true;
}

/**
 * @brief Check if ASCII_CHAT_QUESTION_PROMPT_RESPONSE has any responses available
 *
 * @return true if at least one response is available, false otherwise
 */
bool env_has_prompt_response(void) {
  const char *env_value = SAFE_GETENV("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
  if (!env_value || env_value[0] == '\0' || env_value[0] == ';') {
    return false;
  }

  // Check if it's all semicolons
  for (const char *p = env_value; *p; p++) {
    if (*p != ';') {
      return true;
    }
  }

  return false;
}

/**
 * @brief Validate ASCII_CHAT_QUESTION_PROMPT_RESPONSE format
 *
 * Valid formats:
 * - "y"
 * - "y;n"
 * - "y;n;123"
 * - "y;n;123;"
 * - "pass\;word;y" (escaped semicolon)
 * - "path\\to\\file" (escaped backslash)
 *
 * Invalid formats:
 * - ""
 * - ";"
 * - ";y"
 * - "y;;n" (empty segment)
 * - ";;"
 *
 * @return true if format is valid, false otherwise
 */
bool env_validate_prompt_response_format(void) {
  const char *env_value = SAFE_GETENV("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
  if (!env_value) {
    return true; // Not set is valid (no automated responses)
  }

  if (env_value[0] == '\0') {
    return true; // Empty is valid (no automated responses)
  }

  // Invalid: leading semicolon (unless escaped)
  if (env_value[0] == ';') {
    return false;
  }

  // Check for consecutive unescaped semicolons or empty segments
  const char *p = env_value;
  bool last_was_semicolon = false;

  while (*p) {
    if (*p == '\\' && *(p + 1)) {
      // Skip escaped character
      last_was_semicolon = false;
      p += 2;
      continue;
    }
    if (*p == ';') {
      if (last_was_semicolon) {
        return false; // Consecutive unescaped semicolons (empty segment)
      }
      last_was_semicolon = true;
    } else {
      last_was_semicolon = false;
    }
    p++;
  }

  // Trailing semicolon is allowed
  return true;
}

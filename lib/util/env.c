/**
 * @file env.c
 * @brief Environment variable utilities with stack-based prompt responses
 */

#include <ascii-chat/util/env.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Pop the first response from ASCII_CHAT_QUESTION_PROMPT_RESPONSE stack
 *
 * Format: "response1;response2;response3" or "response1;response2;response3;"
 * This function extracts the first response and updates the environment variable
 * to contain the remaining responses.
 *
 * Examples:
 * - "y;n;123" -> returns "y", sets env to "n;123"
 * - "password" -> returns "password", sets env to ""
 * - "y;" -> returns "y", sets env to ""
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

  // Validate: no leading semicolon
  if (env_value[0] == ';') {
    log_warn("Invalid ASCII_CHAT_QUESTION_PROMPT_RESPONSE format: leading semicolon");
    return false;
  }

  // Find the first semicolon
  const char *semicolon = strchr(env_value, ';');

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

    // Clear the environment variable
#ifdef _WIN32
    _putenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE=");
#else
    unsetenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
#endif

    log_debug("Popped last response from stack: '%s' (cleared env)", response_out);
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
 *
 * Invalid formats:
 * - ""
 * - ";"
 * - ";y"
 * - "y;;n"
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

  // Invalid: leading semicolon
  if (env_value[0] == ';') {
    return false;
  }

  // Check for consecutive semicolons or empty segments
  const char *p = env_value;
  bool last_was_semicolon = false;

  while (*p) {
    if (*p == ';') {
      if (last_was_semicolon) {
        return false; // Consecutive semicolons (empty segment)
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

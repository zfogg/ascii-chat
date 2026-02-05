/**
 * @file env.h
 * @brief Environment variable utilities with stack-based prompt responses
 */

#ifndef ASCIICHAT_UTIL_ENV_H
#define ASCIICHAT_UTIL_ENV_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Pop the first response from ASCII_CHAT_QUESTION_PROMPT_RESPONSE stack
 *
 * Format: "response1;response2;response3" or "response1;response2;response3;"
 *
 * Examples:
 * - "y;n;123" -> returns "y", sets env to "n;123"
 * - "password" -> returns "password", sets env to ""
 * - "y;" -> returns "y", sets env to ""
 *
 * @param response_out Buffer to store the popped response (must be at least 256 bytes)
 * @param response_size Size of response_out buffer
 * @return true if a response was successfully popped, false otherwise
 */
bool env_pop_prompt_response(char *response_out, size_t response_size);

/**
 * @brief Check if ASCII_CHAT_QUESTION_PROMPT_RESPONSE has any responses available
 *
 * @return true if at least one response is available, false otherwise
 */
bool env_has_prompt_response(void);

/**
 * @brief Validate ASCII_CHAT_QUESTION_PROMPT_RESPONSE format
 *
 * Valid formats: "y", "y;n", "y;n;123", "y;n;123;"
 * Invalid formats: "", ";", ";y", "y;;n", ";;"
 *
 * @return true if format is valid, false otherwise
 */
bool env_validate_prompt_response_format(void);

#endif // ASCIICHAT_UTIL_ENV_H

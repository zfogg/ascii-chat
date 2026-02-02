/**
 * @file util/password.h
 * @brief Password prompting utilities with secure input and formatting
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * Provides high-level password prompting functionality built on top of the
 * platform question API. Includes formatting with visual separators and
 * consistent user experience across the application.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Buffer size for password input
 *
 * Passwords are limited to 256 characters per validate_opt_password().
 * Adding space for null terminator and some margin.
 */
#define PASSWORD_MAX_LEN 260

/**
 * @brief Prompt the user for a password with secure input
 * @param prompt The prompt message to display
 * @param password Buffer to store the entered password
 * @param max_len Maximum length of the password buffer (including null terminator)
 * @return 0 on success, -1 on failure or user cancellation
 *
 * Displays a formatted password prompt with visual separators:
 * ```
 * ========================================
 * <prompt>
 * ========================================
 * > ********
 * ========================================
 * ```
 *
 * Input is masked with asterisks and echo is disabled for security.
 *
 * @note Returns -1 if stdin is not a TTY (non-interactive mode).
 * @note Acquires terminal lock during prompting.
 *
 * @ingroup util
 */
int prompt_password(const char *prompt, char *password, size_t max_len);

/**
 * @brief Prompt the user for a password with simple formatting
 * @param prompt The prompt message to display (shown inline)
 * @param password Buffer to store the entered password
 * @param max_len Maximum length of the password buffer (including null terminator)
 * @return 0 on success, -1 on failure or user cancellation
 *
 * Displays a simple inline password prompt:
 * ```
 * <prompt>: ********
 * ```
 *
 * Use this for simpler prompts like SSH key passphrases.
 *
 * @note Returns -1 if stdin is not a TTY (non-interactive mode).
 * @note Acquires terminal lock during prompting.
 *
 * @ingroup util
 */
int prompt_password_simple(const char *prompt, char *password, size_t max_len);

#ifdef __cplusplus
}
#endif

/** @} */

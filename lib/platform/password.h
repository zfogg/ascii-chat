#pragma once

/**
 * @file platform/password.h
 * @ingroup module_platform
 * @brief Cross-platform password prompting utilities
 *
 * Provides secure password input functionality across Windows, Linux, and macOS.
 * Automatically disables terminal echo and provides consistent user experience.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stddef.h>

/**
 * @brief Prompt the user for a password with echo disabled
 * @param prompt The prompt message to display to the user
 * @param password Buffer to store the entered password
 * @param max_len Maximum length of the password buffer (including null terminator)
 * @return 0 on success, -1 on failure
 *
 * Prompts the user for a password input, disabling terminal echo for security.
 * Works consistently across Windows, Linux, and macOS.
 *
 * @ingroup module_platform
 */
int platform_prompt_password(const char *prompt, char *password, size_t max_len);

#pragma once

/**
 * @file password.h
 * @brief Cross-platform password prompting utilities
 *
 * Provides secure password input functionality across Windows, Linux, and macOS.
 * Automatically disables terminal echo and provides consistent user experience.
 */

#include <stddef.h>

/**
 * Prompt the user for a password with echo disabled
 *
 * @param prompt The prompt message to display to the user
 * @param password Buffer to store the entered password
 * @param max_len Maximum length of the password buffer (including null terminator)
 * @return 0 on success, -1 on failure
 */
int platform_prompt_password(const char *prompt, char *password, size_t max_len);

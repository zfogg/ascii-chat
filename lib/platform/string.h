/**
 * @file platform/string.h
 * @brief Platform-independent safe string functions
 *
 * This file declares safe string functions that satisfy clang-tidy
 * cert-err33-c requirements.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date November 2025
 */

#pragma once

#include <stdio.h>
#include <stdarg.h>

/**
 * @brief Safe version of snprintf that ensures null termination
 * @param buffer Buffer to write to
 * @param buffer_size Size of the buffer
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of characters written (not including null terminator)
 */
int safe_snprintf(char *buffer, size_t buffer_size, const char *format, ...);

/**
 * @brief Safe version of fprintf
 * @param stream File stream to write to
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of characters written or negative on error
 */
int safe_fprintf(FILE *stream, const char *format, ...);

/**
 * @brief Safe version of strcat
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string
 * @return Destination buffer
 */
char *platform_strcat(char *dest, size_t dest_size, const char *src);

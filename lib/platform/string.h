#pragma once

/**
 * @file platform/string.h
 * @brief Cross-platform string operations
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent string manipulation utilities.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stddef.h>
#include "../asciichat_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Escape a path string for safe shell usage
 *
 * Escapes a file path according to platform-specific shell rules:
 *   - Windows: Wraps in double quotes, escapes internal quotes
 *   - POSIX: Wraps in single quotes (safest approach)
 *
 * @param path Input path to escape
 * @param output Buffer to store escaped path
 * @param output_size Size of output buffer
 * @return ASCIICHAT_OK on success, ERROR_BUFFER_OVERFLOW if buffer too small
 *
 * @note If output_size is too small, returns error without modifying buffer
 * @note Output string is properly null-terminated
 *
 * @par Example:
 * @code{.c}
 * char escaped[512];
 * platform_escape_shell_path("/path/to/file.txt", escaped, sizeof(escaped));
 * // Windows: "C:\path\to\file.txt"
 * // POSIX: '/path/to/file.txt'
 * @endcode
 *
 * @ingroup platform
 */
asciichat_error_t platform_escape_shell_path(const char *path, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

/** @} */

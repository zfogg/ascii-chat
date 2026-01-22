/**
 * @file log/colorize.h
 * @ingroup logging
 * @brief Log message colorization for terminal output
 *
 * Colorizes log messages for terminal output by applying colors to:
 * - Numbers and numeric units (cyan/light blue)
 * - File paths like /path/to/file or C:\path (magenta)
 * - Environment variables like $VAR_NAME (grey)
 *
 * Only applies colors when output goes to TTY (not when piped).
 * File logging output remains uncolorized.
 */

#ifndef LOG_COLORIZE_H
#define LOG_COLORIZE_H

#include <stddef.h>

/**
 * @brief Colorize a log message for terminal output
 *
 * Applies semantic colors to:
 * - Numeric values: 0, 25, 0.24, 1920x1080, 25 MB, 1024 GiB, etc. (cyan)
 * - File paths: /path/to/file, C:\path, src/main.c, etc. (magenta)
 * - Environment variables: $VAR_NAME, $HOME, etc. (grey)
 *
 * @param message The log message to colorize
 * @return Colorized string in static buffer (reused across calls)
 * @note Caller must use returned string immediately before next call
 * @note Only colorizes if output is going to TTY, otherwise returns message unchanged
 */
const char *colorize_log_message(const char *message);

#endif

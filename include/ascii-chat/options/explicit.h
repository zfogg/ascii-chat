/**
 * @file explicit.h
 * @brief Check if an option was explicitly set via command-line
 * @ingroup options
 *
 * Simple macro to check if an option was explicitly provided by the user
 * (as opposed to using a default value).
 *
 * Explicit tracking fields are stored directly in options_t as `${optionName}_explicit`.
 * Only frequently-checked options have explicit tracking to avoid struct bloat.
 *
 * ## Usage
 *
 * ```c
 * // Check if user explicitly set an option
 * if (IS_OPTION_EXPLICIT("splash_screen", opts)) {
 *     printf("User explicitly set splash_screen\n");
 * }
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Check if an option was explicitly set by the user
 *
 * Returns true if the given option was explicitly provided via command-line,
 * false if it's using a default value.
 *
 * Only a subset of high-importance options track explicit status in options_t
 * to avoid bloating the struct. Check the options struct definition for which
 * options have `${name}_explicit` fields.
 *
 * @param option_name Name of the option (e.g., "splash_screen", "status_screen")
 * @param opts Pointer to options_t struct (typically GET_OPTION convenience)
 * @return true if option was explicitly set, false otherwise
 *
 * @note This is a simple macro accessing a field in options_t
 * @note Only works for options that have an `${name}_explicit` field defined
 * @note Thread-safe: reads via RCU-published options_t
 *
 * @example
 * @code{.c}
 * const options_t *opts = options_get();
 * if (IS_OPTION_EXPLICIT("splash_screen", opts)) {
 *     // User explicitly set splash_screen
 * }
 * @endcode
 *
 * @ingroup options
 */
#define IS_OPTION_EXPLICIT(name, opts) \
  ((opts) && (opts)->name##_explicit)

/** @} */

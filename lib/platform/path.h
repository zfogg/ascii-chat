#pragma once

/**
 * @file platform/path.h
 * @brief Cross-platform path manipulation utilities
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent path handling functions for:
 * - Path separator normalization (Windows backslash vs Unix forward slash)
 * - Case-sensitive/insensitive path comparison
 * - Home directory discovery with fallbacks
 * - Configuration directory discovery (XDG on Unix, APPDATA on Windows)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stddef.h>

/**
 * Normalize path separators in-place for the current platform.
 *
 * On Windows: Converts forward slashes to backslashes.
 * On Unix: No-op (already uses forward slashes).
 *
 * @param path Path string to normalize (modified in-place)
 */
void platform_normalize_path_separators(char *path);

/**
 * Platform-aware path string comparison.
 *
 * Case-insensitive on Windows, case-sensitive on Unix.
 *
 * @param a First path string
 * @param b Second path string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if a<b, >0 if a>b
 */
int platform_path_strcasecmp(const char *a, const char *b, size_t n);

/**
 * Get home directory with platform-specific fallback.
 *
 * Returns USERPROFILE on Windows (with fallback to HOME).
 * Returns HOME on Unix/POSIX.
 *
 * @return Pointer to home directory string, or NULL if not found
 */
const char *platform_get_home_dir(void);

/**
 * Get configuration directory for the application.
 *
 * Windows: %APPDATA%\\ascii-chat\\ (fallback to %USERPROFILE%\\.ascii-chat\\)
 * Unix: $XDG_CONFIG_HOME/ascii-chat/ (fallback to ~/.ascii-chat/)
 *
 * @return Allocated string with config directory path, or NULL on error
 *         Caller must free with SAFE_FREE()
 */
char *platform_get_config_dir(void);

/** @} */

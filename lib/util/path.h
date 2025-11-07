#pragma once

/**
 * @file util/path.h
 * @ingroup util
 * @brief Path Manipulation Utilities
 *
 * This header provides cross-platform utilities for working with file paths,
 * including path expansion, configuration directory resolution, and project
 * relative path extraction.
 *
 * CORE FEATURES:
 * ==============
 * - Cross-platform path handling (Unix and Windows)
 * - Tilde (~) expansion for home directory
 * - XDG_CONFIG_HOME support for configuration paths
 * - Project relative path extraction for logging
 * - Path normalization and validation
 *
 * PATH EXPANSION:
 * ===============
 * The system supports:
 * - Tilde expansion: ~/path -> /home/user/path
 * - Environment variable expansion (Windows %VAR%)
 * - Automatic platform-specific path separator handling
 *
 * CONFIGURATION DIRECTORIES:
 * ==========================
 * Configuration directory resolution follows platform conventions:
 * - Unix: Respects XDG_CONFIG_HOME, falls back to ~/.config/ascii-chat
 * - Windows: Uses %APPDATA%\.ascii-chat
 * - Cross-platform: Falls back to ~/.ascii-chat
 *
 * @note All returned paths that require freeing are allocated with malloc()
 *       and must be freed by the caller using free().
 * @note Path operations handle both Unix (/) and Windows (\) separators.
 * @note Configuration paths include directory separator at the end.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Path Constants
 * ============================================================================
 */

/**
 * @brief Known hosts file path
 *
 * Platform-specific path to the known hosts file. On Windows, uses backslash
 * separators; on Unix, uses forward slashes.
 *
 * @note Path includes tilde (~) which can be expanded using expand_path().
 * @note This is a string literal, not an expanded path.
 *
 * @ingroup util
 */
#ifdef _WIN32
#define KNOWN_HOSTS_PATH "~\\.ascii-chat\\known_hosts"
#else
#define KNOWN_HOSTS_PATH "~/.ascii-chat/known_hosts"
#endif

/* ============================================================================
 * Path Manipulation Functions
 * @{
 */

/**
 * @brief Extract relative path from an absolute path
 * @param file Absolute file path (typically from __FILE__, must not be NULL)
 * @return Relative path from project directory (e.g., lib/platform/symbols.c),
 *         or filename if no project directory found
 *
 * Extracts a project-relative path from an absolute file path. Searches for
 * common project directories (lib/, src/, tests/, include/) and returns the
 * path relative from that directory. Useful for logging and error reporting
 * where full paths are too verbose.
 *
 * @note Handles both Unix (/) and Windows (\) path separators.
 * @note Falls back to just the filename if no project directory found.
 * @note Returns a pointer into the input string (does not allocate memory).
 * @note Input string must remain valid for the lifetime of the returned pointer.
 *
 * @par Example
 * @code
 * const char *rel = extract_project_relative_path("/home/user/src/lib/path.c");
 * // Returns: "lib/path.c"
 * @endcode
 *
 * @ingroup util
 */
const char *extract_project_relative_path(const char *file);

/**
 * @brief Expand path with tilde (~) support
 * @param path Path to expand (may contain ~, must not be NULL)
 * @return Expanded path (must be freed by caller), or NULL on failure
 *
 * Expands a file path, replacing ~ with the user's home directory. On Unix,
 * uses $HOME environment variable. On Windows, uses %USERPROFILE% environment
 * variable.
 *
 * @note Returned path is allocated with malloc() and must be freed by caller.
 * @note Returns NULL on failure (invalid path, memory allocation error, etc.).
 * @note Does not expand environment variables other than ~.
 *
 * @par Example
 * @code
 * char *expanded = expand_path("~/.config/ascii-chat");
 * // Returns: "/home/user/.config/ascii-chat" (on Unix)
 * free(expanded);
 * @endcode
 *
 * @ingroup util
 */
char *expand_path(const char *path);

/**
 * @brief Get configuration directory path with XDG_CONFIG_HOME support
 * @return Path to configuration directory (must be freed by caller), or NULL on failure
 *
 * Returns the appropriate configuration directory path according to platform
 * conventions and environment variables. The path includes the directory
 * separator at the end for convenience.
 *
 * CONFIGURATION PATH RESOLUTION:
 * - Unix: Checks XDG_CONFIG_HOME, falls back to ~/.config/ascii-chat
 * - Windows: Uses %APPDATA%\.ascii-chat
 * - Cross-platform: Falls back to ~/.ascii-chat if above fail
 *
 * @note Returned path is allocated with malloc() and must be freed by caller.
 * @note Path includes directory separator at the end (e.g., "/home/user/.config/ascii-chat/").
 * @note Returns NULL on failure (memory allocation error, etc.).
 *
 * @par Example
 * @code
 * char *config_dir = get_config_dir();
 * // Returns: "/home/user/.config/ascii-chat/" (on Unix with XDG_CONFIG_HOME unset)
 * // or: "C:\Users\user\AppData\Roaming\.ascii-chat\" (on Windows)
 * free(config_dir);
 * @endcode
 *
 * @ingroup util
 */
char *get_config_dir(void);

/**
 * @brief Normalize a path and copy it into the provided buffer.
 *
 * Resolves '.' and '..' components without requiring the path to exist on disk.
 * The output buffer receives a canonicalized representation using platform
 * preferred separators.
 *
 * @param path     Input path string (must not be NULL)
 * @param out      Destination buffer for normalized path
 * @param out_len  Size of destination buffer in bytes
 * @return true on success, false on failure (invalid arguments or buffer too small)
 */
bool path_normalize_copy(const char *path, char *out, size_t out_len);

/**
 * @brief Determine whether a path is absolute on the current platform.
 *
 * @param path Path string to test (may be NULL)
 * @return true if the path is absolute, false otherwise
 */
bool path_is_absolute(const char *path);

/**
 * @brief Check whether a path resides within a specified base directory.
 *
 * Both the candidate path and the base directory are normalized before
 * comparison. The base directory must be absolute. The comparison honours the
 * platform's case sensitivity rules (case-insensitive on Windows).
 *
 * @param path Candidate path to validate (must be absolute)
 * @param base Absolute base directory to compare against
 * @return true if @p path is inside @p base (or exactly equal), false otherwise
 */
bool path_is_within_base(const char *path, const char *base);

/**
 * @brief Check whether a path resides within any of several base directories.
 *
 * @param path        Candidate path to validate (must be absolute)
 * @param bases       Array of base directory strings
 * @param base_count  Number of entries in @p bases
 * @return true if @p path is inside any allowed base, false otherwise
 */
bool path_is_within_any_base(const char *path, const char *const *bases, size_t base_count);

/** @} */

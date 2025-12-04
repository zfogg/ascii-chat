#pragma once

/**
 * @file util/path.h
 * @brief 📂 Path Manipulation Utilities
 * @ingroup util
 * @addtogroup util
 * @{
 *
 * This header provides cross-platform utilities for working with file paths,
 * including path expansion, configuration directory resolution, and project
 * relative path extraction.
 *
 * ## Core Features
 *
 * - Cross-platform path handling (Unix and Windows)
 * - Tilde (~) expansion for home directory
 * - XDG_CONFIG_HOME support for configuration paths
 * - Project relative path extraction for logging
 * - Path normalization and validation
 *
 * ## Path Expansion
 *
 * The system supports:
 * - Tilde expansion: ~/path -> /home/user/path
 * - Environment variable expansion (Windows %VAR%)
 * - Automatic platform-specific path separator handling
 *
 * ## Configuration Directories
 *
 * All ascii-chat data files (config, known_hosts, etc.) use a single directory:
 * - Unix: $XDG_CONFIG_HOME/ascii-chat/ if set, otherwise ~/.ascii-chat/
 * - Windows: %APPDATA%\ascii-chat\ if set, otherwise ~\.ascii-chat\
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

#include "common.h"

/* ============================================================================
 * Path Constants
 * ============================================================================
 */

/**
 * @brief Path component: current directory (single dot)
 *
 * Character constant for the current directory component in paths.
 */
#define PATH_COMPONENT_DOT '.'

/**
 * @brief Path component: parent directory (double dot)
 *
 * String constant for the parent directory component in paths.
 */
#define PATH_COMPONENT_DOTDOT ".."

/**
 * @brief Path component: home directory tilde
 *
 * The home directory component in paths (e.g., "~/path").
 */
#define PATH_TILDE '~'

/**
 * @brief Path component: Windows drive separator (colon)
 */
#define PATH_DRIVE_SEPARATOR ':'

/**
 * @brief Maximum number of path base directories
 *
 * Maximum number of base directories that can be checked in path validation.
 */
#define MAX_PATH_BASES 16

/* ============================================================================
 * Path Manipulation Functions
 * ============================================================================
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
 * All ascii-chat data files use this single directory:
 * - config.toml (configuration)
 * - known_hosts (server key verification)
 *
 * Configuration path resolution:
 * - Unix: $XDG_CONFIG_HOME/ascii-chat/ if XDG_CONFIG_HOME is set, otherwise ~/.ascii-chat/
 * - Windows: %APPDATA%\ascii-chat\ if APPDATA is set, otherwise ~\.ascii-chat\
 *
 * @note Returned path is allocated with malloc() and must be freed by caller.
 * @note Path includes directory separator at the end (e.g., "/home/user/.config/ascii-chat/").
 * @note Returns NULL on failure (memory allocation error, etc.).
 *
 * @par Example
 * @code
 * char *config_dir = get_config_dir();
 * // Returns: "/home/user/.config/ascii-chat/" (on Unix with XDG_CONFIG_HOME unset)
 * // or: "C:\Users\user\AppData\Roaming\ascii-chat\" (on Windows)
 * free(config_dir);
 * @endcode
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

/**
 * @brief Classification for user-supplied filesystem paths.
 */
typedef enum {
  PATH_ROLE_CONFIG_FILE, /**< Configuration files such as config.toml */
  PATH_ROLE_LOG_FILE,    /**< Log file destinations */
  PATH_ROLE_KEY_PRIVATE, /**< Private key files (SSH/GPG) */
  PATH_ROLE_KEY_PUBLIC,  /**< Public key files or expected server keys */
  PATH_ROLE_CLIENT_KEYS  /**< Client key whitelist files */
} path_role_t;

/**
 * @brief Determine if a string is likely intended to reference the filesystem.
 *
 * Heuristics include presence of path separators, leading ~, relative prefixes,
 * or Windows drive designators. Used to avoid treating tokens like
 * "github:user" or raw hex keys as file paths.
 *
 * @param value String to analyze
 * @return true if the string looks like a filesystem path
 */
bool path_looks_like_path(const char *value);

/**
 * @brief Validate and canonicalize a user-supplied filesystem path.
 *
 * Resolves ~, relative segments, and enforces that the resulting absolute path
 * resides within the trusted base directories for the supplied role.
 *
 * Trusted base directories include:
 * - Current working directory
 * - System temp directory
 * - User config directory (~/.ascii-chat/ or %APPDATA%\ascii-chat\)
 * - User home directory
 * - User SSH directory (~/.ssh/)
 * - System config directories (Unix: /etc/ascii-chat/, /usr/local/etc/ascii-chat/)
 * - System log directories (Unix: /var/log/, /var/tmp/)
 * - System-wide app data (Windows: %PROGRAMDATA%\ascii-chat\)
 *
 * @note For PATH_ROLE_LOG_FILE, the base directory check is skipped to allow
 *       logging to any writable location.
 *
 * @param input          Original user-provided path (must not be NULL)
 * @param role           Intended usage category
 * @param normalized_out Output pointer receiving SAFE_MALLOC'd canonical path
 * @return ASCIICHAT_OK on success, error code on failure (and normalized_out is NULL)
 */
asciichat_error_t path_validate_user_path(const char *input, path_role_t role, char **normalized_out);

/** @} */

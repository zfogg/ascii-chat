#pragma once

/**
 * @file platform/fs.h
 * @brief Cross-platform file system operations
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent file system functions including directory
 * creation, file statistics, and type checking.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stddef.h>
#include "../asciichat_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File type information from stat()
 *
 * Structure containing file metadata returned by platform_stat().
 *
 * @ingroup platform
 */
typedef struct {
  size_t size;         ///< File size in bytes
  int mode;            ///< File mode (permissions and type)
  int is_regular_file; ///< Non-zero if file is a regular file
  int is_directory;    ///< Non-zero if file is a directory
  int is_symlink;      ///< Non-zero if file is a symbolic link
} platform_stat_t;

/**
 * @brief Create a directory
 *
 * Creates a directory with the specified permissions. If the directory
 * already exists, this is not an error.
 *
 * Platform-specific implementations:
 *   - POSIX: Uses mkdir() with mode parameter
 *   - Windows: Uses CreateDirectoryA(), mode is ignored
 *
 * @param path Directory path to create
 * @param mode File permissions (0700 for owner rwx only, ignored on Windows)
 * @return ASCIICHAT_OK on success (or if directory exists), error code on failure
 *
 * @note Permissions only apply to parent directories that need to be created.
 * @note On Windows, the mode parameter is ignored (uses ACLs).
 * @note Returns ASCIICHAT_OK even if the directory already exists.
 *
 * @par Example:
 * @code{.c}
 * if (platform_mkdir("~/.ascii-chat", 0700) == ASCIICHAT_OK) {
 *   // Directory created or already exists
 * }
 * @endcode
 *
 * @ingroup platform
 */
asciichat_error_t platform_mkdir(const char *path, int mode);

/**
 * @brief Create directories recursively (mkdir -p equivalent)
 *
 * Creates all parent directories needed for the given path.
 *
 * Platform-specific implementations:
 *   - POSIX: Uses mkdir() in a loop for each path component
 *   - Windows: Uses CreateDirectoryA() in a loop for each path component
 *
 * @param path Directory path to create (may contain parent directories)
 * @param mode File permissions (0700 for owner rwx only, ignored on Windows)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Handles both forward slashes (/) and backslashes (\) as separators
 * @note Safe on Windows drive letters (e.g., C:\path\to\dir)
 * @note Returns ASCIICHAT_OK if the directory already exists
 *
 * @par Example:
 * @code{.c}
 * // Create ~/.ascii-chat/config/ and all parent directories
 * if (platform_mkdir_recursive("~/.ascii-chat/config", 0700) == ASCIICHAT_OK) {
 *   // Directory and parents created or already exist
 * }
 * @endcode
 *
 * @ingroup platform
 */
asciichat_error_t platform_mkdir_recursive(const char *path, int mode);

/**
 * @brief Get file statistics
 *
 * Retrieves metadata about a file without following symbolic links.
 *
 * Platform-specific implementations:
 *   - POSIX: Uses lstat()
 *   - Windows: Uses GetFileAttributesExA()
 *
 * @param path File path to stat
 * @param stat_out Pointer to platform_stat_t to receive results
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Does not follow symbolic links (uses lstat on POSIX).
 * @note Sets errno context on failure.
 *
 * @par Example:
 * @code{.c}
 * platform_stat_t stat_info;
 * if (platform_stat("key_file", &stat_info) == ASCIICHAT_OK) {
 *   if (stat_info.is_regular_file) {
 *     // Process the file
 *   }
 * }
 * @endcode
 *
 * @ingroup platform
 */
asciichat_error_t platform_stat(const char *path, platform_stat_t *stat_out);

/**
 * @brief Check if a path is a regular file
 *
 * Convenience function that checks if a path points to a regular file.
 * Does not follow symbolic links.
 *
 * @param path File path to check
 * @return Non-zero (true) if path is a regular file, 0 (false) otherwise
 *
 * @note Does not follow symbolic links.
 * @note Returns false for directories, sockets, pipes, etc.
 * @note Returns false if the file doesn't exist.
 *
 * @ingroup platform
 */
int platform_is_regular_file(const char *path);

/**
 * @brief Check if a path is a directory
 *
 * Convenience function that checks if a path points to a directory.
 * Does not follow symbolic links.
 *
 * @param path Path to check
 * @return Non-zero (true) if path is a directory, 0 (false) otherwise
 *
 * @note Does not follow symbolic links.
 * @note Returns false for regular files, sockets, pipes, etc.
 * @note Returns false if the path doesn't exist.
 *
 * @ingroup platform
 */
int platform_is_directory(const char *path);

#ifdef __cplusplus
}
#endif

/** @} */

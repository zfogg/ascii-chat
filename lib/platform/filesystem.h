#pragma once

/**
 * @file platform/filesystem.h
 * @brief Cross-platform filesystem operations
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides unified filesystem operations across Windows and POSIX platforms:
 * - Directory creation (single and recursive)
 * - File statistics and type checking
 * - Temporary file and directory creation
 * - Recursive directory deletion
 * - Key file permission validation
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stddef.h>
#include <sys/types.h>
#include "../asciichat_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// File Mode Constants (for consistency with fcntl.h)
// ============================================================================

#ifdef _WIN32
#include <fcntl.h>
#define PLATFORM_O_RDONLY _O_RDONLY
#define PLATFORM_O_WRONLY _O_WRONLY
#define PLATFORM_O_RDWR _O_RDWR
#define PLATFORM_O_CREAT _O_CREAT
#define PLATFORM_O_EXCL _O_EXCL
#define PLATFORM_O_TRUNC _O_TRUNC
#define PLATFORM_O_APPEND _O_APPEND
#define PLATFORM_O_BINARY _O_BINARY
#else
#include <fcntl.h>
#define PLATFORM_O_RDONLY O_RDONLY
#define PLATFORM_O_WRONLY O_WRONLY
#define PLATFORM_O_RDWR O_RDWR
#define PLATFORM_O_CREAT O_CREAT
#define PLATFORM_O_EXCL O_EXCL
#define PLATFORM_O_TRUNC O_TRUNC
#define PLATFORM_O_APPEND O_APPEND
#define PLATFORM_O_BINARY 0 // Not needed on POSIX
#endif

// ============================================================================
// File Statistics
// ============================================================================

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

// ============================================================================
// Directory Management
// ============================================================================

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
 * @brief Create a temporary directory with a given prefix
 *
 * Creates an isolated temporary directory with proper permissions.
 *
 * Windows: Creates directory in temp dir with process-specific prefix
 * Unix: Creates directory via mkdtemp with prefix in /tmp
 *
 * @param path_out Buffer to store the created temp directory path (must be at least 256 bytes)
 * @param path_size Size of path_out buffer
 * @param prefix Prefix for the temp directory name (e.g., "ascii-chat-gpg")
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Caller must delete the directory when done using platform_rmdir_recursive()
 * @note Directory permissions are restricted to 0700 (owner-only access)
 *
 * @ingroup platform
 */
asciichat_error_t platform_mkdtemp(char *path_out, size_t path_size, const char *prefix);

/**
 * @brief Recursively delete a directory and all its contents
 *
 * Safely removes a directory and all files/subdirectories within it.
 * Safe to call on non-existent paths (returns ASCIICHAT_OK, no-op).
 *
 * Windows: Uses FindFirstFile/DeleteFile/RemoveDirectory
 * Unix: Uses opendir/readdir/rmdir with recursion
 *
 * @param path Path to the directory to delete
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Path must be a directory, not a file
 * @note Safe to call on non-existent paths (returns ASCIICHAT_OK)
 *
 * @ingroup platform
 */
asciichat_error_t platform_rmdir_recursive(const char *path);

// ============================================================================
// Temporary Files
// ============================================================================

/**
 * @brief Create a temporary file with a given prefix
 *
 * Windows: Creates file in temp dir via GetTempFileName with process-specific prefix
 * Unix: Creates file via mkstemp with prefix in /tmp
 *
 * @param path_out Buffer to store the created temp file path (must be at least 256 bytes)
 * @param path_size Size of path_out buffer
 * @param prefix Prefix for the temp file name (e.g., "asc_sig")
 * @param fd Output parameter: on Unix, receives the open file descriptor; on Windows, -1
 * @return 0 on success, -1 on error
 *
 * @note Caller must close fd on Unix and delete the file on both platforms when done
 *
 * @ingroup platform
 */
int platform_create_temp_file(char *path_out, size_t path_size, const char *prefix, int *fd);

/**
 * @brief Delete a temporary file
 *
 * @param path Path to the file to delete
 * @return 0 on success, -1 on error
 *
 * @ingroup platform
 */
int platform_delete_temp_file(const char *path);

// ============================================================================
// Key File Security
// ============================================================================

/**
 * @brief Validate that a cryptographic key file has appropriate permissions
 *
 * Ensures that only the file owner can read the key file, preventing
 * unauthorized access to private cryptographic material.
 *
 * Platform-specific validation:
 *   - POSIX: Checks file mode permissions and verifies group/other bits are 0
 *   - Windows: Checks ACL (Access Control List) to ensure only owner has read access
 *
 * @param key_path Path to the key file to validate
 * @return ASCIICHAT_OK if permissions are appropriate, error code if too permissive
 *
 * @ingroup platform
 */
asciichat_error_t platform_validate_key_file_permissions(const char *key_path);

#ifdef __cplusplus
}
#endif

/** @} */

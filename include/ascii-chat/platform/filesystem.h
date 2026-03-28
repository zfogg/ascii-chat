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
 * - Config file search across standard locations
 * - Home and configuration directory discovery
 * - Path utilities and normalization
 *
 * This header abstracts away platform differences in directory management, path
 * handling, and configuration file discovery. All operations are platform-aware
 * and handle differences like path separators, permission models, and standard
 * configuration directories transparently.
 *
 * **Platform-specific behavior:**
 * - **POSIX (Linux/macOS)**: Uses POSIX APIs (mkdir, stat, lstat, opendir)
 * - **Windows**: Uses Win32 API (CreateDirectoryA, GetFileAttributesEx, FindFile)
 * - **Path separators**: Automatically converted (/ on POSIX, \\ on Windows)
 * - **Permissions**: Unix mode parameter used on POSIX; ignored on Windows (ACL-based)
 * - **Symlinks**: All functions skip symlinks (lstat on POSIX, no-follow on Windows)
 * - **Configuration directories**: Follow XDG standard on Unix, APPDATA on Windows
 *
 * **Thread safety:**
 * Most operations are thread-safe under normal conditions. However:
 * - Config file discovery uses static buffers - safe to call from multiple threads
 * - Directory operations are safe as long as multiple threads don't create/delete
 *   the same directory simultaneously (standard filesystem semantics)
 * - Temporary file creation uses platform-specific atomic operations (mkstemp, etc.)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include "../asciichat_errno.h"

/* ============================================================================
 * Cross-platform I/O headers
 * ============================================================================
 * Consolidates file descriptor and I/O operations across platforms.
 */
#ifdef _WIN32
#include <io.h>    /* Windows I/O functions */
#include <fcntl.h> /* File control options */
#else
#include <unistd.h> /* POSIX standard I/O and file descriptor functions */
#include <fcntl.h>  /* POSIX file control options */
#endif

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
// Path Separator and Length Constants
// ============================================================================

/**
 * @brief Platform-specific path separator character
 *
 * - Windows: '\\' (backslash)
 * - Unix/POSIX: '/' (forward slash)
 *
 * Use this constant instead of hardcoding separators or using #ifdef _WIN32.
 *
 * @note For string literals, use PATH_SEPARATOR_STR instead.
 *
 * @ingroup platform
 */
#ifdef _WIN32
#define PATH_DELIM '\\'
#define PATH_SEPARATOR_STR "\\"
#else
#define PATH_DELIM '/'
#define PATH_SEPARATOR_STR "/"
#endif

/**
 * @brief Platform-specific PATH environment variable separator
 *
 * - Windows: ";" (semicolon)
 * - Unix/POSIX: ":" (colon)
 *
 * @ingroup platform
 */
#ifdef _WIN32
#define PATH_ENV_SEPARATOR ";"
#else
#define PATH_ENV_SEPARATOR ":"
#endif

/**
 * @brief Maximum path length supported by the operating system
 *
 * Platform-specific values:
 * - Windows: 32767 characters (extended-length path with \\?\ prefix)
 * - Linux: 4096 bytes (PATH_MAX from limits.h)
 * - macOS: 1024 bytes (PATH_MAX from sys/syslimits.h)
 *
 * @note Windows legacy MAX_PATH (260) is too restrictive for modern use.
 *       We use the extended-length limit instead.
 *
 * @ingroup platform
 */
#ifdef _WIN32
#define PLATFORM_MAX_PATH_LENGTH 32767
#elif defined(__linux__)
#ifndef PATH_MAX
#define PLATFORM_MAX_PATH_LENGTH 4096
#else
#define PLATFORM_MAX_PATH_LENGTH PATH_MAX
#endif
#elif defined(__APPLE__)
#ifndef PATH_MAX
#define PLATFORM_MAX_PATH_LENGTH 1024
#else
#define PLATFORM_MAX_PATH_LENGTH PATH_MAX
#endif
#else
#define PLATFORM_MAX_PATH_LENGTH 4096
#endif

/**
 * @brief Maximum environment variable value length
 *
 * Windows theoretically supports 32767 chars per env var value.
 * Unix systems typically have no hard limit per variable, but total
 * environment size is limited (usually ~128KB-2MB).
 *
 * We use 32KB as a reasonable maximum that handles Windows PATH
 * (which can easily exceed 4KB) while not being excessive.
 */
#define PLATFORM_MAX_ENV_VALUE_LENGTH 32768

// ============================================================================
// File Permission Constants
// ============================================================================

/**
 * @brief File permission: Private (owner read/write only)
 *
 * Octal mode 0600: rw-------
 * Used for sensitive files like private keys, log files, and configuration files.
 *
 * @note On Windows, this is a no-op (Windows uses ACLs instead of POSIX permissions)
 *
 * @ingroup platform
 */
#define FILE_PERM_PRIVATE 0600

/**
 * @brief Directory permission: Private (owner read/write/execute only)
 *
 * Octal mode 0700: rwx------
 * Used for private directories like ~/.ascii-chat
 *
 * @note On Windows, this is a no-op (Windows uses ACLs instead of POSIX permissions)
 *
 * @ingroup platform
 */
#define DIR_PERM_PRIVATE 0700

/**
 * @brief File permission: Public read, owner write
 *
 * Octal mode 0644: rw-r--r--
 * Used for files that should be readable by others but only writable by owner.
 *
 * @ingroup platform
 */
#define FILE_PERM_PUBLIC_READ 0644

/**
 * @brief Permission mask for all permissions
 *
 * Octal mode 0777: rwxrwxrwx
 * Used for masking permission bits (e.g., st_mode & 0777)
 *
 * @ingroup platform
 */
#define FILE_PERM_MASK 0777

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

/**
 * @brief Directory entry information for platform_dir_foreach()
 * @ingroup platform
 */
typedef struct {
  const char *name; /**< Entry name (filename only, not full path) */
  bool is_dir;      /**< true if entry is a directory */
} platform_dir_entry_t;

/**
 * @brief Callback for platform_dir_foreach()
 * @param entry Directory entry information
 * @param user_data User-provided context pointer
 * @return true to continue iteration, false to stop
 */
typedef bool (*platform_dir_foreach_cb)(const platform_dir_entry_t *entry, void *user_data);

/**
 * @brief Iterate over entries in a directory
 * @param path Directory path
 * @param callback Function called for each entry (excluding "." and "..")
 * @param user_data Opaque pointer passed to callback
 * @return ASCIICHAT_OK on success, error code if directory cannot be opened
 * @ingroup platform
 */
asciichat_error_t platform_dir_foreach(const char *path, platform_dir_foreach_cb callback, void *user_data);

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

/**
 * @brief Open a temporary file for writing
 *
 * Platform-aware wrapper that handles the differences between POSIX and Windows
 * temp file opening.
 *
 * Platform-specific behavior:
 *   - POSIX: Use fd from platform_create_temp_file() directly
 *   - Windows: Open the temp file created by platform_create_temp_file()
 *
 * @param path Path to the temporary file (from platform_create_temp_file)
 * @param fd_out Output parameter: file descriptor
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Caller must close fd when done
 * @note On Windows, platform_create_temp_file returns fd=-1, so this wrapper opens it
 * @note On POSIX, platform_create_temp_file already returns valid fd
 *
 * @ingroup platform
 */
asciichat_error_t platform_temp_file_open(const char *name, const char *path, int *fd_out);

// ============================================================================
// File Truncation
// ============================================================================

/**
 * @brief Truncate a file to a specific size
 *
 * Resizes a file to the specified size, removing data if truncating,
 * or padding with zeros if extending (platform-dependent).
 *
 * Platform-specific behavior:
 *   - Windows: Uses CreateFileA, SetFilePointerEx, SetEndOfFile
 *   - POSIX: Uses ftruncate() or truncate()
 *
 * @param path Path to the file to truncate
 * @param size New file size in bytes
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note File must be writable
 *
 * @ingroup platform
 */
asciichat_error_t platform_truncate_file(const char *path, size_t size);

// ============================================================================
// Path Utilities
// ============================================================================

/**
 * @brief Check if a path is absolute (not relative)
 *
 * Platform-specific logic:
 *   - Windows: Checks for drive letter (C:) or UNC path (\\server)
 *   - POSIX: Checks for leading slash (/)
 *
 * @param path Path string to check
 * @return true if path is absolute, false if relative or NULL
 *
 * @ingroup platform
 */
bool platform_path_is_absolute(const char *path);

/**
 * @brief Get the path separator character for current platform
 *
 * @return '\\' on Windows, '/' on POSIX
 *
 * @ingroup platform
 */
char platform_path_get_separator(void);

/**
 * @brief Normalize and validate a file path
 *
 * Converts path to platform-standard format with correct separators and normalization.
 *
 * @param input Input path string
 * @param output Output buffer for normalized path
 * @param output_size Size of output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup platform
 */
asciichat_error_t platform_path_normalize(const char *input, char *output, size_t output_size);

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

// ============================================================================
// Config File Search
// ============================================================================

/**
 * @brief Result of a config file search
 *
 * Represents a single matching config file in the search result list.
 * @ingroup platform
 */
typedef struct {
  char *path;            ///< Absolute path to config file (allocated, must be freed)
  uint8_t priority;      ///< Priority order (0 = highest, 255 = lowest)
  bool exists;           ///< True if file exists and is a regular file
  bool is_system_config; ///< True if from system directory (not user config)
} config_file_result_t;

/**
 * @brief List of config file search results
 *
 * Contains all matching config files in priority order (highest priority first).
 * @ingroup platform
 */
typedef struct {
  config_file_result_t *files; ///< Array of results (allocated, must be freed)
  size_t count;                ///< Number of results found
  size_t capacity;             ///< Allocated capacity
} config_file_list_t;

/**
 * @brief Find config file across multiple standard locations
 *
 * Searches for a config file across platform-specific standard locations
 * and returns ALL existing matches in priority order (highest first).
 *
 * This allows calling code to implement different merge strategies:
 * - Override: use first match (colors.toml)
 * - Cascade: load all in reverse order (config.toml)
 * - Append: search all for matching entries (known_hosts)
 *
 * **Search Order (Unix/macOS):**
 * 1. ~/.config/ascii-chat/filename (XDG user config)
 * 2. /opt/homebrew/etc/ascii-chat/filename (macOS Homebrew)
 * 3. /usr/local/etc/ascii-chat/filename (Unix local)
 * 4. /etc/ascii-chat/filename (system-wide)
 *
 * **Search Order (Windows):**
 * 1. %APPDATA%\ascii-chat\filename (user config)
 * 2. %PROGRAMDATA%\ascii-chat\filename (system-wide)
 *
 * @param filename Config filename (e.g., "config.toml", "colors.toml", "known_hosts")
 * @param list_out Pointer to config_file_list_t to populate
 * @return ASCIICHAT_OK on success (even if no files found), error code on failure
 *
 * @note Caller must free list_out with config_file_list_destroy()
 * @note Returns ASCIICHAT_OK even if no files are found (list_out->count == 0)
 * @note Files are checked for existence and regular file type
 *
 * @par Example (Override semantics - colors.toml):
 * @code{.c}
 * config_file_list_t list = {0};
 * if (platform_find_config_file("colors.toml", &list) == ASCIICHAT_OK) {
 *   if (list.count > 0) {
 *     // Use first match (highest priority)
 *     colors_load_from_file(list.files[0].path, scheme);
 *   }
 *   config_file_list_destroy(&list);
 * }
 * @endcode
 *
 * @par Example (Cascade semantics - config.toml):
 * @code{.c}
 * config_file_list_t list = {0};
 * if (platform_find_config_file("config.toml", &list) == ASCIICHAT_OK) {
 *   // Load configs in reverse order (lowest priority first)
 *   // This allows higher-priority configs to override
 *   for (size_t i = list.count; i > 0; i--) {
 *     config_load_and_apply(list.files[i - 1].path, opts);
 *   }
 *   config_file_list_destroy(&list);
 * }
 * @endcode
 *
 * @ingroup platform
 */
asciichat_error_t platform_find_config_file(const char *filename, config_file_list_t *list_out);

/**
 * @brief Free config file list resources
 *
 * Releases all allocated memory in a config file list result.
 * Safe to call with NULL or empty lists.
 *
 * @param list Pointer to config_file_list_t to free
 *
 * @note This function always succeeds; no error checking needed
 * @note Safe to call multiple times with the same list
 * @note Safe to call with list->count == 0
 *
 * @ingroup platform
 */
void config_file_list_destroy(config_file_list_t *list);

// ============================================================================
// Home and Config Directory Discovery
// ============================================================================

/**
 * @brief Get the user's home directory
 *
 * Platform-specific implementation:
 *   - POSIX: Returns HOME environment variable
 *   - Windows: Returns USERPROFILE environment variable (fallback to HOME)
 *
 * @return Pointer to home directory string (not allocated), or NULL if not found
 *
 * @note The returned pointer is managed by the system and should not be freed
 * @note The returned string is valid only until the next getenv() call
 *
 * @ingroup platform
 */
const char *platform_get_home_dir(void);

/**
 * @brief Get the application configuration directory
 *
 * Platform-specific implementation:
 *   - POSIX: Returns $XDG_CONFIG_HOME/ascii-chat/ (default: ~/.config/ascii-chat/)
 *   - Windows: Returns %APPDATA%\ascii-chat\
 *
 * @return Allocated string with config directory path (including trailing separator),
 *         or NULL on error. Caller must free with SAFE_FREE()
 *
 * @note The returned string includes a trailing path separator (/ or \)
 * @note Returns a freshly allocated string each time; caller must free it
 * @note Returns NULL if home directory cannot be determined
 *
 * @par Example:
 * @code{.c}
 * char *config_dir = platform_get_config_dir();
 * if (config_dir) {
 *   // config_dir = "/home/user/.config/ascii-chat/" on Linux
 *   // config_dir = "C:\\Users\\user\\AppData\\Roaming\\ascii-chat\\" on Windows
 *   SAFE_FREE(config_dir);
 * }
 * @endcode
 *
 * @ingroup platform
 */
char *platform_get_config_dir(void);

/**
 * @brief Get the application data directory
 *
 * Platform-specific implementation:
 *   - POSIX: Returns $XDG_DATA_HOME/ascii-chat/ (default: ~/.local/share/ascii-chat/)
 *   - Windows: Returns %APPDATA%\ascii-chat\
 *
 * @return Allocated string with data directory path (including trailing separator),
 *         or NULL on error. Caller must free() the returned string.
 *
 * @note Creates the directory if it doesn't exist
 * @note Thread-safe (returns newly allocated string each time)
 *
 * @par Example:
 * @code{.c}
 * char *data_dir = platform_get_data_dir();
 * if (data_dir) {
 *   // data_dir = "/home/user/.local/share/ascii-chat/" on Linux
 *   // data_dir = "C:\\Users\\user\\AppData\\Roaming\\ascii-chat\\" on Windows
 *   SAFE_FREE(data_dir);
 * }
 * @endcode
 *
 * @ingroup platform
 */
char *platform_get_data_dir(void);

// ============================================================================
// File I/O Operations
// ============================================================================

/**
 * @brief Safe file open (open replacement)
 * @param name Debug name for the file descriptor (required, e.g., "config_file")
 * @param pathname File path to open
 * @param flags Open flags (PLATFORM_O_RDONLY, PLATFORM_O_WRONLY, PLATFORM_O_RDWR, etc.)
 * @param ... Variable arguments (mode for PLATFORM_O_CREAT)
 * @return File descriptor, or -1 on error
 */
int platform_open(const char *name, const char *pathname, int flags, ...);

/**
 * @brief Safe file open stream (fopen replacement)
 * @param name Debug name for the file (required, e.g., "config_file")
 * @param filename File path to open
 * @param mode Open mode string (e.g., "r", "w", "rb")
 * @return FILE pointer, or NULL on error
 */
FILE *platform_fopen(const char *name, const char *filename, const char *mode);

/**
 * @brief Create a temporary file (tmpfile replacement)
 * @return FILE pointer, or NULL on error
 */
FILE *platform_tmpfile(void);

/**
 * @brief Convert file descriptor to stream (fdopen replacement)
 * @param name Debug name for the stream (required, e.g., "log_stream")
 * @param fd File descriptor
 * @param mode Open mode string for the stream
 * @return FILE pointer, or NULL on error
 */
FILE *platform_fdopen(const char *name, int fd, const char *mode);

/**
 * @brief Safe file read (read replacement)
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
ssize_t platform_read(int fd, void *buf, size_t count);

/**
 * @brief Safe file write (write replacement)
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
ssize_t platform_write(int fd, const void *buf, size_t count);

/**
 * @brief Safe file close (close replacement)
 * @param fd File descriptor to close
 * @return 0 on success, -1 on error
 */
int platform_close(int fd);

/**
 * @brief Delete/unlink file
 * @param pathname File path to delete
 * @return 0 on success, -1 on error
 */
int platform_unlink(const char *pathname);

/**
 * @brief Change file permissions/mode
 * @param pathname File path
 * @param mode New file mode (permissions)
 * @return 0 on success, -1 on error
 */
int platform_chmod(const char *pathname, int mode);

// ============================================================================
// Platform Path Utilities
// ============================================================================

/**
 * @brief Skip absolute path prefix (drive letter on Windows)
 *
 * Advances pointer past the absolute path prefix for the current platform.
 *
 * Platform-specific behavior:
 *   - Windows: Skips drive letter (e.g., "C:" in "C:\path")
 *   - Unix: Returns original pointer (no prefix to skip)
 *
 * @param path Path string to process (e.g., "C:\path" or "/path")
 * @return Pointer to first character after the prefix, or original path if no prefix
 *
 * @note Safe to call with NULL (returns NULL)
 * @note Safe to call with relative paths
 *
 * @ingroup platform
 */
const char *platform_path_skip_absolute_prefix(const char *path);

/**
 * @brief Normalize path separators for the current platform
 *
 * Converts all path separators to the preferred format for the current platform.
 *
 * Platform-specific behavior:
 *   - Windows: Converts forward slashes (/) to backslashes (\)
 *   - Unix: No-op (already uses forward slashes)
 *
 * @param path Path string to normalize (modified in-place)
 *
 * @note The path is modified in-place
 * @note Safe to call with NULL (no-op)
 *
 * @ingroup platform
 */
void platform_normalize_path_separators(char *path);

/**
 * @brief Platform-aware path string comparison
 *
 * Compares paths with platform-specific rules for case sensitivity.
 *
 * Platform-specific behavior:
 *   - Windows: Case-insensitive comparison
 *   - Unix: Case-sensitive comparison
 *
 * @param a First path string
 * @param b Second path string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if a<b, >0 if a>b
 *
 * @ingroup platform
 */
int platform_path_strcasecmp(const char *a, const char *b, size_t n);

/**
 * @brief Get human-readable error message for file read failure
 *
 * Checks errno and provides specific error messages for common cases:
 * - ENOENT: File does not exist
 * - EACCES: Permission denied
 * - EISDIR: Is a directory, not a file
 * - Other: Generic error with errno description
 *
 * @param path File path that failed to open
 * @return Static string describing the error (do not free)
 *
 * @ingroup platform
 */
const char *file_read_error_message(const char *path);

/**
 * @brief Get human-readable error message for file write failure
 *
 * Checks errno and provides specific error messages for common cases:
 * - ENOENT: Directory does not exist
 * - EACCES: Permission denied
 * - EROFS: Read-only filesystem
 * - ENOSPC: No space left on device
 * - EISDIR: Is a directory, not a file
 * - Other: Generic error with errno description
 *
 * @param path File path that failed to open
 * @return Static string describing the error (do not free)
 *
 * @ingroup platform
 */
const char *file_write_error_message(const char *path);

/**
 * @brief Check if file is readable
 *
 * Tests whether the file exists and can be read by the current process.
 *
 * @param path File path to check
 * @return true if file is readable, false otherwise
 *
 * @ingroup platform
 */
bool file_is_readable(const char *path);

/**
 * @brief Check if file is writable
 *
 * Tests whether the file can be written by the current process.
 * Returns true even if file doesn't exist (assumes directory is writable).
 *
 * @param path File path to check
 * @return true if file is writable, false otherwise
 *
 * @ingroup platform
 */
bool file_is_writable(const char *path);

/* ============================================================================
 * Path Utilities
 * ============================================================================ */

/**
 * @brief Get the path to the current executable
 *
 * Retrieves the full path to the currently running executable using
 * platform-specific methods.
 *
 * Platform-specific implementations:
 *   - Windows: GetModuleFileNameA()
 *   - Linux: readlink("/proc/self/exe")
 *   - macOS: _NSGetExecutablePath()
 *
 * @param exe_path Buffer to store the executable path
 * @param path_size Size of the buffer
 * @return true on success, false on failure
 *
 * @note Thread-safe
 * @note Buffer should be PLATFORM_MAX_PATH_LENGTH bytes to support all paths
 *
 * @par Example:
 * @code{.c}
 * char exe_path[PLATFORM_MAX_PATH_LENGTH];
 * if (platform_get_executable_path(exe_path, sizeof(exe_path))) {
 *   // Use exe_path
 * }
 * @endcode
 *
 * @ingroup platform
 */
bool platform_get_executable_path(char *exe_path, size_t path_size);

/**
 * @brief Get the system temporary directory path
 *
 * Retrieves the path to the system's temporary directory using
 * platform-specific methods. Verifies the directory exists and is writable.
 *
 * Platform-specific implementations:
 *   - Windows: %TEMP% or %TMP% environment variable, fallback to C:\Temp
 *   - Linux/macOS: /tmp
 *
 * @param temp_dir Buffer to store the temporary directory path
 * @param path_size Size of the buffer
 * @return true on success (directory exists and is writable), false on failure
 *
 * @note Thread-safe
 * @note Returned path does not include trailing directory separator
 * @note Buffer should be at least 256 bytes to support typical paths
 * @note Returns false if the directory doesn't exist or lacks write permission
 *
 * @par Example:
 * @code{.c}
 * char temp_dir[256];
 * if (platform_get_temp_dir(temp_dir, sizeof(temp_dir))) {
 *   // temp_dir is valid and writable
 *   char log_path[512];
 *   snprintf(log_path, sizeof(log_path), "%s/myapp.log", temp_dir);
 * }
 * @endcode
 *
 * @ingroup platform
 */
bool platform_get_temp_dir(char *temp_dir, size_t path_size);

/**
 * @brief Get the current working directory of the process.
 *
 * Normalizes the result using platform-specific semantics and does not append
 * a trailing directory separator.
 *
 * @param cwd Buffer to store the current working directory
 * @param path_size Size of the buffer in bytes
 * @return true on success, false on failure (buffer too small or API error)
 */
bool platform_get_cwd(char *cwd, size_t path_size);

/**
 * @brief Access modes for platform_access()
 *
 * @ingroup platform
 */
#define PLATFORM_ACCESS_EXISTS 0 ///< Check if file/directory exists
#define PLATFORM_ACCESS_WRITE 2  ///< Check if file/directory is writable
#define PLATFORM_ACCESS_READ 4   ///< Check if file/directory is readable

/**
 * @brief Check file/directory access permissions
 *
 * Platform-safe wrapper for access() / _access(). Tests whether the calling
 * process has the requested access to the specified path.
 *
 * Platform-specific implementations:
 *   - POSIX: Uses access() with F_OK, R_OK, W_OK, X_OK modes
 *   - Windows: Uses _access() with 0, 2, 4, 6 modes
 *
 * @param path File or directory path to check
 * @param mode Access mode to test (PLATFORM_ACCESS_EXISTS, PLATFORM_ACCESS_WRITE, PLATFORM_ACCESS_READ)
 * @return 0 on success (access permitted), -1 on failure (access denied or path doesn't exist)
 *
 * @note Thread-safe on all platforms
 * @note Does not follow symbolic links on POSIX (uses access() not faccessat())
 * @note Returns -1 if path is NULL
 *
 * @par Example:
 * @code{.c}
 * if (platform_access("/tmp", PLATFORM_ACCESS_WRITE) == 0) {
 *   // Directory is writable
 * }
 * @endcode
 *
 * @ingroup platform
 */
int platform_access(const char *path, int mode);

/**
 * @brief Check if a binary is available in the system PATH
 *
 * This function checks if the specified binary can be found in the PATH
 * by searching each directory in the PATH environment variable.
 * Results are cached to avoid repeated filesystem checks.
 *
 * On Windows: Automatically appends .exe if needed, checks with GetFileAttributesA
 * On Unix: Uses access() with X_OK to verify executable permission
 *
 * @param bin_name Base name of the binary (e.g., "ssh-keygen", "llvm-symbolizer")
 *                 On Windows, .exe extension is added automatically if not present
 * @return true if binary is in PATH and executable, false otherwise
 *
 * @note Thread-safe: Uses internal locking for cache access
 * @note First call for a binary checks filesystem, subsequent calls use cache
 * @note No external dependencies (doesn't spawn where/command -v)
 *
 * @par Example:
 * @code{.c}
 * if (platform_is_binary_in_path("ssh-keygen")) {
 *   // Use ssh-keygen
 * }
 * @endcode
 *
 * @ingroup platform
 */
bool platform_is_binary_in_path(const char *bin_name);

/**
 * @brief Cleanup the binary PATH cache
 *
 * Frees all cached binary PATH lookup results and destroys the cache.
 * Should be called during program cleanup (e.g., in platform_destroy()).
 *
 * @note Thread-safe: Uses internal locking
 * @note Safe to call even if cache was never initialized
 *
 * @ingroup platform
 */
void platform_cleanup_binary_path_cache(void);

/**
 * @brief Synchronize a file descriptor to disk
 * @param fd File descriptor to sync
 * @return 0 on success, non-zero on error
 *
 * Forces all buffered data for the file descriptor to be written to disk.
 *
 * @ingroup platform
 */
int platform_fsync(int fd);

/* ============================================================================
 * Platform-Specific Internal Functions (not part of public API)
 * ============================================================================ */

/**
 * @brief Check if a binary is in PATH (uncached, platform-specific implementation)
 *
 * Internal function called by platform_is_binary_in_path().
 * Defined in posix/filesystem.c (Unix/Linux/macOS) or windows/filesystem.c (Windows).
 *
 * @param bin_name Base name of the binary to search for
 * @return true if binary found and executable, false otherwise
 */
bool check_binary_in_path_uncached(const char *bin_name);

#ifdef __cplusplus
}
#endif

/** @} */

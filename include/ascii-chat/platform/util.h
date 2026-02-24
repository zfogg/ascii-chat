/**
 * @file platform/util.h
 * @brief Public platform utility API for string, memory, and file operations
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides the public API for platform-specific utility functions
 * that are needed by the main codebase. These utilities provide cross-platform
 * implementations of common operations with consistent behavior across Windows,
 * Linux, and macOS.
 *
 * DESIGN PHILOSOPHY:
 * ==================
 * This header is the ONLY way to access platform implementation details from
 * outside the platform/ directory. All platform-internal implementations remain
 * private to the platform/ directory via platform/internal.h.
 *
 * CORE FEATURES:
 * ==============
 * - String operations (duplication, comparison, formatting)
 * - Memory operations (aligned allocation, memory barriers)
 * - Error handling (cross-platform errno/GetLastError)
 * - File operations (safe wrappers around open/read/write/close)
 * - Type definitions (ssize_t on all platforms)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

// Define ssize_t for all platforms
#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h> // For ssize_t on POSIX systems
#endif

/* ============================================================================
 * String Operations
 * ============================================================================
 */

/**
 * @brief Safe string formatting (snprintf replacement)
 * @param str Destination buffer
 * @param size Size of destination buffer
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 * @return Number of characters written, or negative on error
 *
 * Cross-platform wrapper around snprintf that behaves consistently on
 * Windows and POSIX systems.
 *
 * @ingroup platform
 */
int platform_snprintf(char *str, size_t size, const char *format, ...);

/**
 * @brief Safe variable-argument string formatting
 * @param str Destination buffer
 * @param size Size of destination buffer
 * @param format Printf-style format string
 * @param ap Variable argument list
 * @return Number of characters written, or negative on error
 *
 * Variable-argument version of platform_snprintf.
 *
 * @ingroup platform
 */
int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap);

/**
 * @brief Duplicate string (strdup replacement)
 * @param s String to duplicate
 * @return Dynamically allocated copy of string, or NULL on error
 *
 * Cross-platform string duplication. Allocated memory should be freed
 * with free().
 *
 * @ingroup platform
 */
char *platform_strdup(const char *s);

/**
 * @brief Duplicate string with length limit (strndup replacement)
 * @param s String to duplicate
 * @param n Maximum number of characters to copy
 * @return Dynamically allocated copy of string (max n chars), or NULL on error
 *
 * Duplicates up to n characters from string. Allocated memory should be
 * freed with free().
 *
 * @ingroup platform
 */
char *platform_strndup(const char *s, size_t n);

/**
 * @brief Case-insensitive string comparison
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2 (case-insensitive)
 *
 * Compares two strings case-insensitively.
 *
 * @ingroup platform
 */
int platform_strcasecmp(const char *s1, const char *s2);

/**
 * @brief Case-insensitive string comparison with length limit
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2 (case-insensitive)
 *
 * Compares up to n characters of two strings case-insensitively.
 *
 * @ingroup platform
 */
int platform_strncasecmp(const char *s1, const char *s2, size_t n);

/**
 * @brief Thread-safe string tokenization (strtok_r replacement)
 * @param str String to tokenize (or NULL to continue tokenizing)
 * @param delim Delimiter characters
 * @param saveptr Pointer to save tokenization state
 * @return Pointer to next token, or NULL when no more tokens
 *
 * Thread-safe version of strtok. State is maintained in saveptr between calls.
 *
 * @ingroup platform
 */
char *platform_strtok_r(char *str, const char *delim, char **saveptr);

/**
 * @brief Safe string copy with size tracking (strlcpy)
 * @param dst Destination buffer
 * @param src Source string
 * @param size Size of destination buffer
 * @return Length of source string (before truncation)
 *
 * Safely copies string to destination, truncating if necessary. Always
 * null-terminates destination (if size > 0).
 *
 * @ingroup platform
 */
size_t platform_strlcpy(char *dst, const char *src, size_t size);

/**
 * @brief Safe string concatenation with size tracking (strlcat)
 * @param dst Destination buffer (must be null-terminated)
 * @param src Source string
 * @param size Size of destination buffer
 * @return Length of concatenated string (before truncation)
 *
 * Safely appends source to destination, truncating if necessary. Always
 * null-terminates destination (if size > 0).
 *
 * @ingroup platform
 */
size_t platform_strlcat(char *dst, const char *src, size_t size);

/**
 * @brief Safe string copy with explicit size bounds (strncpy replacement)
 * @param dst Destination buffer
 * @param dst_size Size of destination buffer
 * @param src Source string
 * @param count Maximum number of characters to copy
 * @return 0 on success, -1 on overflow/error
 *
 * Safely copies string with explicit destination size and character count limits.
 * Always null-terminates destination on success.
 *
 * @ingroup platform
 */
int platform_strncpy(char *dst, size_t dst_size, const char *src, size_t count);

/* ============================================================================
 * Memory Operations
 * ============================================================================
 */

/**
 * @brief Allocate aligned memory
 * @param alignment Required alignment (must be power of 2)
 * @param size Number of bytes to allocate
 * @return Pointer to aligned memory, or NULL on error
 *
 * Allocates memory with specified alignment. Should be freed with
 * platform_aligned_free().
 *
 * @ingroup platform
 */
void *platform_aligned_alloc(size_t alignment, size_t size);

/**
 * @brief Free aligned memory
 * @param ptr Pointer returned from platform_aligned_alloc()
 *
 * Frees memory previously allocated with platform_aligned_alloc().
 *
 * @ingroup platform
 */
void platform_aligned_free(void *ptr);

/**
 * @brief Perform memory barrier/fence operation
 *
 * Ensures all memory operations before this call are visible to other threads
 * before operations after this call. Platform-specific implementation using
 * atomic operations or memory barriers.
 *
 * @ingroup platform
 */
void platform_memory_barrier(void);

/* ============================================================================
 * Error Handling
 * ============================================================================
 */

/**
 * @brief Get thread-safe error string
 * @param errnum Error number (errno or GetLastError on Windows)
 * @return Pointer to error description string
 *
 * Returns string description of error code. Thread-safe.
 *
 * @ingroup platform
 */
const char *platform_strerror(int errnum);

/**
 * @brief Get last platform error code
 * @return Error code (errno on POSIX, GetLastError on Windows)
 *
 * Retrieves the last error that occurred. Equivalent to errno on POSIX.
 *
 * @ingroup platform
 */
int platform_get_last_error(void);

/**
 * @brief Set platform error code
 * @param error Error code to set
 *
 * Sets the current error code. Equivalent to errno on POSIX.
 *
 * @ingroup platform
 */
void platform_set_last_error(int error);

/* ============================================================================
 * File Operations
 * ============================================================================
 */

/**
 * @brief Safe file open (open replacement)
 * @param name Debug name for the file descriptor (required, e.g., "config_file")
 * @param pathname File path to open
 * @param flags Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
 * @param ... Variable arguments (mode for O_CREAT)
 * @return File descriptor, or -1 on error
 *
 * Cross-platform file opening with consistent behavior.
 * Use PLATFORM_O_* flags for portability.
 * The name parameter is required for debug tracking and validation.
 *
 * @ingroup platform
 */
int platform_open(const char *name, const char *pathname, int flags, ...);

/**
 * @brief Safe file open stream (fopen replacement)
 * @param name Debug name for the file (required, e.g., "config_file")
 * @param filename File path to open
 * @param mode Open mode string (e.g., "r", "w", "rb")
 * @return FILE pointer, or NULL on error
 *
 * Cross-platform file stream opening.
 * The name parameter is required for debug tracking and validation.
 *
 * @ingroup platform
 */
FILE *platform_fopen(const char *name, const char *filename, const char *mode);

/**
 * @brief Create a temporary file (tmpfile replacement)
 * @return FILE pointer, or NULL on error
 *
 * Uses tmpfile_s on Windows and tmpfile on POSIX.
 *
 * @ingroup platform
 */
FILE *platform_tmpfile(void);

/**
 * @brief Convert file descriptor to stream (fdopen replacement)
 * @param name Debug name for the stream (required, e.g., "log_stream")
 * @param fd File descriptor
 * @param mode Open mode string for the stream
 * @return FILE pointer, or NULL on error
 *
 * Associates a FILE stream with an existing file descriptor.
 * The name parameter is required for debug tracking and validation.
 *
 * @ingroup platform
 */
FILE *platform_fdopen(const char *name, int fd, const char *mode);

/**
 * @brief Safe file read (read replacement)
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or -1 on error
 *
 * Cross-platform file reading.
 *
 * @ingroup platform
 */
ssize_t platform_read(int fd, void *buf, size_t count);

/**
 * @brief Safe file write (write replacement)
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or -1 on error
 *
 * Cross-platform file writing.
 *
 * @ingroup platform
 */
ssize_t platform_write(int fd, const void *buf, size_t count);

/**
 * @brief Safe file close (close replacement)
 * @param fd File descriptor to close
 * @return 0 on success, -1 on error
 *
 * Cross-platform file closing.
 *
 * @ingroup platform
 */
int platform_close(int fd);

/**
 * @brief Delete/unlink file
 * @param pathname File path to delete
 * @return 0 on success, -1 on error
 *
 * Deletes a file. Cross-platform unlink replacement.
 *
 * @ingroup platform
 */
int platform_unlink(const char *pathname);

/**
 * @brief Change file permissions/mode
 * @param pathname File path
 * @param mode New file mode (permissions)
 * @return 0 on success, -1 on error
 *
 * Changes file permissions/mode. Cross-platform chmod replacement.
 *
 * @ingroup platform
 */
int platform_chmod(const char *pathname, int mode);

/* ============================================================================
 * File Open Flags (Cross-platform)
 * ============================================================================
 */

/**
 * @def PLATFORM_O_RDONLY
 * @brief Open file for reading only
 * @ingroup platform
 */

/**
 * @def PLATFORM_O_WRONLY
 * @brief Open file for writing only
 * @ingroup platform
 */

/**
 * @def PLATFORM_O_RDWR
 * @brief Open file for reading and writing
 * @ingroup platform
 */

/**
 * @def PLATFORM_O_CREAT
 * @brief Create file if it doesn't exist
 * @ingroup platform
 */

/**
 * @def PLATFORM_O_EXCL
 * @brief Fail if file already exists (with O_CREAT)
 * @ingroup platform
 */

/**
 * @def PLATFORM_O_TRUNC
 * @brief Truncate file to zero length if it exists
 * @ingroup platform
 */

/**
 * @def PLATFORM_O_APPEND
 * @brief Append to end of file
 * @ingroup platform
 */

/**
 * @def PLATFORM_O_BINARY
 * @brief Open file in binary mode (Windows)
 * @ingroup platform
 */

#ifdef _WIN32
#define PLATFORM_O_RDONLY _O_RDONLY
#define PLATFORM_O_WRONLY _O_WRONLY
#define PLATFORM_O_RDWR _O_RDWR
#define PLATFORM_O_CREAT _O_CREAT
#define PLATFORM_O_EXCL _O_EXCL
#define PLATFORM_O_TRUNC _O_TRUNC
#define PLATFORM_O_APPEND _O_APPEND
#define PLATFORM_O_BINARY _O_BINARY
#else
#define PLATFORM_O_RDONLY O_RDONLY
#define PLATFORM_O_WRONLY O_WRONLY
#define PLATFORM_O_RDWR O_RDWR
#define PLATFORM_O_CREAT O_CREAT
#define PLATFORM_O_EXCL O_EXCL
#define PLATFORM_O_TRUNC O_TRUNC
#define PLATFORM_O_APPEND O_APPEND
#define PLATFORM_O_BINARY 0 // Not needed on POSIX
#endif

/* ============================================================================
 * String Formatting
 * ============================================================================
 */

/**
 * @brief Cross-platform asprintf implementation
 * @param strp Output pointer for allocated string
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Allocates and formats a string like sprintf, but automatically determines
 * the required buffer size. Caller must free() the returned string.
 *
 * @ingroup platform
 */
int platform_asprintf(char **strp, const char *format, ...);

/**
 * @brief Cross-platform getline implementation
 * @param lineptr Pointer to buffer (can be NULL, will be allocated/reallocated)
 * @param n Pointer to buffer size
 * @param stream File stream to read from
 * @return Number of characters read (including newline), or -1 on error/EOF
 *
 * Reads an entire line from stream, allocating/reallocating the buffer as needed.
 * The buffer will include the newline character if present.
 * Caller must free() the buffer when done.
 *
 * @ingroup platform
 */
ssize_t platform_getline(char **lineptr, size_t *n, FILE *stream);

/** @} */

#pragma once

/**
 * @file platform/internal.h
 * @ingroup platform
 * @brief Internal implementation helpers for platform abstraction layer
 *
 * This header contains internal helpers and macros used by the platform
 * implementation files. Not for external use.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

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

// ============================================================================
// Platform-Internal Functions
// ============================================================================
// These functions are used internally by the platform abstraction layer
// and by the main codebase for platform-safe operations.

// ============================================================================
// String Operations
// ============================================================================

// Safe string formatting
int platform_snprintf(char *str, size_t size, const char *format, ...);
int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap);

// String duplication
char *platform_strdup(const char *s);
char *platform_strndup(const char *s, size_t n);

// Case-insensitive comparison
int platform_strcasecmp(const char *s1, const char *s2);
int platform_strncasecmp(const char *s1, const char *s2, size_t n);

// Thread-safe tokenization
char *platform_strtok_r(char *str, const char *delim, char **saveptr);

// Safe string copy (returns destination length)
size_t platform_strlcpy(char *dst, const char *src, size_t size);
size_t platform_strlcat(char *dst, const char *src, size_t size);

// Safe string copy with explicit size (for strncpy replacement)
int platform_strncpy(char *dst, size_t dst_size, const char *src, size_t count);

// ============================================================================
// Memory Operations
// ============================================================================

// Aligned memory allocation
void *platform_aligned_alloc(size_t alignment, size_t size);
void platform_aligned_free(void *ptr);

// Memory barriers
void platform_memory_barrier(void);

// ============================================================================
// Error Handling
// ============================================================================

// Thread-safe error string
const char *platform_strerror(int errnum);

// Last error code (errno on POSIX, GetLastError on Windows)
int platform_get_last_error(void);
void platform_set_last_error(int error);

// ============================================================================
// File Operations
// ============================================================================

// Safe file operations
int platform_open(const char *pathname, int flags, ...);
FILE *platform_fopen(const char *filename, const char *mode);
FILE *platform_fdopen(int fd, const char *mode);
ssize_t platform_read(int fd, void *buf, size_t count);
ssize_t platform_write(int fd, const void *buf, size_t count);
int platform_close(int fd);
int platform_unlink(const char *pathname);
int platform_chmod(const char *pathname, int mode);

// File mode helpers
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

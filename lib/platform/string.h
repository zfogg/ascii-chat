#pragma once

/**
 * @file string.h
 * @brief Cross-platform string manipulation interface for ASCII-Chat
 *
 * This header provides safe string manipulation functions with consistent
 * behavior across Windows and POSIX platforms.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

// ============================================================================
// Platform String & Memory Functions
// ============================================================================
// Cross-platform string and memory operations with consistent behavior
// across Windows, Linux, and macOS.

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

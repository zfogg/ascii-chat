/**
 * @file platform/posix/string.c
 * @brief POSIX implementation of safe string functions
 *
 * This file provides POSIX implementations of safe string functions
 * that satisfy clang-tidy cert-err33-c requirements.
 *
 * @author Assistant
 * @date December 2024
 */

#include "platform/string.h"
#include "common.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

int safe_snprintf(char *buffer, size_t buffer_size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsnprintf(buffer, buffer_size, format, args);
  va_end(args);

  // Ensure null termination
  if (buffer_size > 0) {
    buffer[buffer_size - 1] = '\0';
  }

  return result;
}

int safe_fprintf(FILE *stream, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfprintf(stream, format, args);
  va_end(args);

  return result;
}

char *platform_strcat(char *dest, size_t dest_size, const char *src) {
  if (!dest || !src || dest_size == 0) {
    return NULL;
  }

  // Calculate remaining space in dest buffer
  size_t dest_len = strlen(dest);
  size_t src_len = strlen(src);
  size_t remaining = dest_size - dest_len;

  if (remaining <= src_len) {
    return NULL; // Buffer overflow prevented
  }

  return strcat(dest, src);
}

// ============================================================================
// Platform String Functions (moved from system.c)
// ============================================================================

/**
 * @brief Platform-safe snprintf implementation
 * @param str Destination buffer
 * @param size Buffer size
 * @param format Format string
 * @return Number of characters written (excluding null terminator)
 */
int platform_snprintf(char *str, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsnprintf(str, size, format, args);
  va_end(args);
  return result;
}

/**
 * @brief Platform-safe vsnprintf implementation
 * @param str Destination buffer
 * @param size Buffer size
 * @param format Format string
 * @param ap Variable argument list
 * @return Number of characters written (excluding null terminator)
 */
int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  return vsnprintf(str, size, format, ap);
}

/**
 * @brief Duplicate a string
 * @param s Source string
 * @return Allocated copy of string, or NULL on failure
 */
char *platform_strdup(const char *s) {
  if (!s) {
    return NULL;
  }
  size_t len = strlen(s) + 1;
  char *result = SAFE_MALLOC(len, char *);
  if (result) {
    memcpy(result, s, len);
  }
  return result;
}

/**
 * @brief Duplicate up to n characters of a string
 * @param s Source string
 * @param n Maximum number of characters to copy
 * @return Allocated copy of string, or NULL on failure
 */
char *platform_strndup(const char *s, size_t n) {
#ifdef __APPLE__
  // macOS has strndup but it may not be declared in older SDKs
  size_t len = strnlen(s, n);
  char *result = SAFE_MALLOC(len + 1, char *);
  if (result) {
    memcpy(result, s, len);
    result[len] = '\0';
  }
  return result;
#else
  // Linux/BSD have strndup
  return strndup(s, n);
#endif
}

/**
 * @brief Case-insensitive string comparison
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int platform_strcasecmp(const char *s1, const char *s2) {
  return strcasecmp(s1, s2);
}

/**
 * @brief Case-insensitive string comparison with length limit
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int platform_strncasecmp(const char *s1, const char *s2, size_t n) {
  return strncasecmp(s1, s2, n);
}

/**
 * @brief Thread-safe string tokenization
 * @param str String to tokenize (NULL for continuation)
 * @param delim Delimiter string
 * @param saveptr Pointer to save state between calls
 * @return Pointer to next token, or NULL if no more tokens
 */
char *platform_strtok_r(char *str, const char *delim, char **saveptr) {
  return strtok_r(str, delim, saveptr);
}

/**
 * @brief Safe string copy with size limit
 * @param dst Destination buffer
 * @param src Source string
 * @param size Destination buffer size
 * @return Length of source string (excluding null terminator)
 */
size_t platform_strlcpy(char *dst, const char *src, size_t size) {
#ifdef __APPLE__
  // macOS has strlcpy
  return strlcpy(dst, src, size);
#else
  // Linux doesn't have strlcpy, implement it
  size_t src_len = strlen(src);
  if (size > 0) {
    size_t copy_len = (src_len >= size) ? size - 1 : src_len;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
  }
  return src_len;
#endif
}

/**
 * @brief Safe string concatenation with size limit
 * @param dst Destination buffer
 * @param src Source string
 * @param size Destination buffer size
 * @return Total length of resulting string
 */
size_t platform_strlcat(char *dst, const char *src, size_t size) {
#ifdef __APPLE__
  // macOS has strlcat
  return strlcat(dst, src, size);
#else
  // Linux doesn't have strlcat, implement it
  size_t dst_len = strnlen(dst, size);
  size_t src_len = strlen(src);

  if (dst_len == size) {
    return size + src_len;
  }

  size_t remain = size - dst_len - 1;
  size_t copy_len = (src_len > remain) ? remain : src_len;

  memcpy(dst + dst_len, src, copy_len);
  dst[dst_len + copy_len] = '\0';

  return dst_len + src_len;
#endif
}

/**
 * @brief Safe string copy with explicit size (for strncpy replacement)
 * @param dst Destination buffer
 * @param dst_size Destination buffer size
 * @param src Source string
 * @param count Maximum number of characters to copy
 * @return 0 on success, non-zero on error
 */
int platform_strncpy(char *dst, size_t dst_size, const char *src, size_t count) {
  if (!dst || !src || dst_size == 0) {
    return -1;
  }

  // Use the smaller of count and dst_size-1
  size_t copy_len = (count < dst_size - 1) ? count : dst_size - 1;

  // Also limit by source string length
  size_t src_len = strlen(src);
  if (copy_len > src_len) {
    copy_len = src_len;
  }

  memcpy(dst, src, copy_len);
  dst[copy_len] = '\0';

  return 0;
}

int safe_sscanf(const char *str, const char *format, ...) {
  if (!str || !format) {
    return -1;
  }

  va_list args;
  va_start(args, format);

  // Use standard vsscanf on POSIX systems
  int result = vsscanf(str, format, args);
  if (result == EOF) {
    va_end(args);
    return -1;
  }

  va_end(args);
  return result;
}

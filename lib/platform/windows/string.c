/**
 * @file platform/windows/string.c
 * @ingroup platform
 * @brief 🔤 Windows Secure CRT string functions satisfying clang-tidy cert-err33-c requirements
 *
 * IMPORTANT: This file uses Windows Secure CRT functions (strcpy_s, strcat_s,
 * etc.) which are ALWAYS available on Windows with MSVC or Clang, regardless
 * of C11 Annex K support. These are Microsoft-specific extensions, not the
 * optional C11 Annex K functions that require __STDC_LIB_EXT1__.
 *
 * @author Assistant
 * @date December 2024
 */

#include "common.h" // Must be first - defines errno constants for Windows
#include "platform/string.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include <corecrt.h>
#include <stdarg.h>
#include <stdio.h> // For vsscanf
#include <string.h>
#include <stdlib.h> // For malloc

int safe_snprintf(char *buffer, size_t buffer_size, const char *format, ...) {
  if (!buffer || buffer_size == 0 || !format) {
    return -1;
  }

  va_list args;
  va_start(args, format);

  // Use Windows _vsnprintf_s for enhanced security
  int result = _vsnprintf_s(buffer, buffer_size, _TRUNCATE, format, args);
  if (result == -1) {
    SET_ERRNO_SYS(ERROR_FORMAT, "vsnprintf_s failed");
    return -1;
  }
  va_end(args);

  // Ensure null termination even if truncated
  buffer[buffer_size - 1] = '\0';

  if (result < 0) {
    SET_ERRNO(ERROR_FORMAT, "vsnprintf_s failed: result=%d", result);
    return result;
  }
  if ((size_t)result >= buffer_size) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "vsnprintf_s failed (buffer overflow): result=%d, buffer_size=%zu", result,
              buffer_size);
    return result;
  }

  return result;
}

int safe_fprintf(FILE *stream, const char *format, ...) {
  if (!stream || !format) {
    return -1;
  }

  va_list args;
  va_start(args, format);

  // Use Windows vfprintf_s for enhanced security
  int result = vfprintf_s(stream, format, args);
  if (result == -1) {
    SET_ERRNO_SYS(ERROR_FORMAT, "vfprintf_s failed");
    return -1;
  }
  va_end(args);

  return result;
}

char *platform_strcat(char *dest, size_t dest_size, const char *src) {
  if (!dest || !src || dest_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "platform_strcat: invalid parameters");
    return NULL;
  }

  // Calculate remaining space in dest buffer
  size_t dest_len = strlen(dest);
  size_t src_len = strlen(src);
  size_t remaining = dest_size - dest_len;

  if (remaining <= src_len) {
    SET_ERRNO(ERROR_STRING,
              "platform_strcat: buffer overflow prevented (dest_len=%zu, src_len=%zu, remaining=%zu, dest_size=%zu)",
              dest_len, src_len, remaining, dest_size);
    return NULL;
  }

  errno_t err = strcat_s(dest, dest_size, src);
  if (err != 0) {
    SET_ERRNO_SYS(ERROR_STRING, "strcat_s failed");
    return NULL;
  }
  return dest;
}

/**
 * Platform-safe strcpy wrapper
 *
 * Uses strcpy_s on Windows (always available in MSVC/Clang Secure CRT).
 * Always null-terminates the destination string.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t platform_strcpy(char *dest, size_t dest_size, const char *src) {
  if (!dest || !src) {
    SET_ERRNO(ERROR_INVALID_PARAM, "platform_strcpy: invalid parameters");
    return ERROR_INVALID_PARAM;
  }
  if (dest_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "platform_strcpy: dest_size is zero");
    return ERROR_INVALID_PARAM;
  }

  size_t src_len = strlen(src);
  if (src_len >= dest_size) {
    SET_ERRNO(ERROR_STRING, "platform_strcpy: source string too long for destination buffer");
    return ERROR_STRING; // Not enough space including null terminator
  }

  // Windows always has strcpy_s via Secure CRT (works with both MSVC and GCC/Clang)
  errno_t err = strcpy_s(dest, dest_size, src);
  if (err != 0) {
    SET_ERRNO_SYS(ERROR_STRING, "strcpy_s failed");
    return ERROR_STRING;
  }
  return ASCIICHAT_OK;
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
  int result = vsnprintf_s(str, size, _TRUNCATE, format, args);
  if (result == -1) {
    SET_ERRNO_SYS(ERROR_FORMAT, "vsnprintf_s failed");
    return -1;
  }
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
  return vsnprintf_s(str, size, _TRUNCATE, format, ap);
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
  size_t len = strnlen(s, n);
  char *result = SAFE_MALLOC(len + 1, char *);
  if (result) {
    memcpy(result, s, len);
    result[len] = '\0';
  }
  return result;
}

/**
 * @brief Case-insensitive string comparison
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int platform_strcasecmp(const char *s1, const char *s2) {
  return _stricmp(s1, s2);
}

/**
 * @brief Case-insensitive string comparison with length limit
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int platform_strncasecmp(const char *s1, const char *s2, size_t n) {
  return _strnicmp(s1, s2, n);
}

/**
 * @brief Thread-safe string tokenization
 * @param str String to tokenize (NULL for continuation)
 * @param delim Delimiter string
 * @param saveptr Pointer to save state between calls
 * @return Pointer to next token, or NULL if no more tokens
 */
char *platform_strtok_r(char *str, const char *delim, char **saveptr) {
  return strtok_s(str, delim, saveptr);
}

/**
 * @brief Safe string copy with size limit
 * @param dst Destination buffer
 * @param src Source string
 * @param size Destination buffer size
 * @return Length of source string (excluding null terminator)
 */
size_t platform_strlcpy(char *dst, const char *src, size_t size) {
  if (size == 0)
    return strlen(src);

  errno_t err = strncpy_s(dst, size, src, _TRUNCATE);
  if (err != 0) {
    SET_ERRNO_SYS(ERROR_STRING, "strncpy_s failed");
    return (size_t)-1;
  }
  return strlen(src);
}

/**
 * @brief Safe string concatenation with size limit
 * @param dst Destination buffer
 * @param src Source string
 * @param size Destination buffer size
 * @return Total length of resulting string
 */
size_t platform_strlcat(char *dst, const char *src, size_t size) {
  size_t dst_len = strnlen(dst, size);
  if (dst_len == size)
    return size + strlen(src);

  errno_t err = strncat_s(dst, size, src, _TRUNCATE);
  if (err != 0) {
    SET_ERRNO_SYS(ERROR_STRING, "strncat_s failed");
    return (size_t)-1;
  }
  return dst_len + strlen(src);
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
    SET_ERRNO(ERROR_INVALID_PARAM, "platform_strncpy: invalid parameters");
    return -1;
  }

  // Use the smaller of count and dst_size-1
  size_t copy_len = (count < dst_size - 1) ? count : dst_size - 1;

  // strncpy_s: copies up to count characters, always null-terminates
  errno_t err = strncpy_s(dst, dst_size, src, copy_len);
  if (err != 0) {
    SET_ERRNO_SYS(ERROR_STRING, "strncpy_s failed");
    return -1;
  }

  return 0;
}

int safe_sscanf(const char *str, const char *format, ...) {
  if (!str || !format) {
    SET_ERRNO(ERROR_INVALID_PARAM, "safe_sscanf: invalid parameters");
    return -1;
  }

  va_list args;
  va_start(args, format);

  // Use Windows sscanf_s for enhanced security
  // Note: GCC/Clang on Windows support sscanf_s via mingw-w64's secure CRT implementation
  // vsscanf_s is not available in GCC, so we use sscanf_s with va_list manually
  // For GCC/Clang compatibility, we need to use a different approach
#ifdef __GNUC__
  // GCC/Clang: Use vsnprintf to format then sscanf, or use sscanf_s directly
  // For now, we'll use the standard sscanf which is safe with proper format strings
  // Note: This is less secure than sscanf_s but works with GCC
  int result = vsscanf(str, format, args);
#else
  // MSVC/Clang on Windows: Use vsscanf_s
  int result = vsscanf_s(str, format, args);
#endif
  if (result == EOF || result < 0) {
    SET_ERRNO_SYS(ERROR_FORMAT, "sscanf failed");
    va_end(args);
    return -1;
  }

  va_end(args);
  return result;
}

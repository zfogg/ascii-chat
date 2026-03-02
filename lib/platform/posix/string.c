/**
 * @file platform/posix/string.c
 * @brief POSIX string utilities
 * @ingroup platform
 */

#ifndef _WIN32

#include <ascii-chat/platform/string.h>
#include <ascii-chat/common.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

/* ============================================================================
 * String Formatting
 * ============================================================================
 */

int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  if (!str || !format || size == 0) {
    return -1;
  }
  return vsnprintf(str, size, format, ap);
}

int platform_vasprintf(char **strp, const char *format, va_list ap) {
  if (!strp || !format) {
    return -1;
  }
  return vasprintf(strp, format, ap);
}

int platform_asprintf(char **strp, const char *format, ...) {
  if (!strp || !format) {
    return -1;
  }
  va_list args;
  va_start(args, format);
  int ret = vasprintf(strp, format, args);
  va_end(args);
  return ret;
}

/* ============================================================================
 * String Operations
 * ============================================================================
 */

char *platform_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s) + 1;
  char *dup = SAFE_MALLOC(len, char *);
  if (dup)
    strcpy(dup, s);
  return dup;
}

int platform_strcasecmp(const char *s1, const char *s2) {
  if (!s1 || !s2)
    return s1 == s2 ? 0 : (s1 ? 1 : -1);
  while (*s1 && *s2) {
    int c1 = tolower((unsigned char)*s1);
    int c2 = tolower((unsigned char)*s2);
    if (c1 != c2)
      return c1 - c2;
    s1++;
    s2++;
  }
  return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int platform_strncasecmp(const char *s1, const char *s2, size_t n) {
  if (!s1 || !s2)
    return s1 == s2 ? 0 : (s1 ? 1 : -1);
  for (size_t i = 0; i < n; i++) {
    if (!s1[i] && !s2[i])
      return 0;
    int c1 = tolower((unsigned char)s1[i]);
    int c2 = tolower((unsigned char)s2[i]);
    if (c1 != c2)
      return c1 - c2;
  }
  return 0;
}

char *platform_strtok_r(char *str, const char *delim, char **saveptr) {
  return strtok_r(str, delim, saveptr);
}

size_t platform_strlcpy(char *dst, const char *src, size_t size) {
  if (!dst || !src)
    return 0;
  size_t src_len = strlen(src);
  if (size > 0) {
    size_t copy_len = src_len < size - 1 ? src_len : size - 1;
    strncpy(dst, src, copy_len);
    dst[copy_len] = '\0';
  }
  return src_len;
}

int platform_strncpy(char *dst, size_t dst_size, const char *src, size_t count) {
  if (!dst || !src || dst_size == 0)
    return -1;
  size_t src_len = strlen(src);
  if (src_len > count)
    return -1; /* Would be truncated */
  if (src_len >= dst_size)
    return -1; /* Doesn't fit in destination */
  strncpy(dst, src, count);
  dst[count] = '\0';
  return 0;
}

/* ============================================================================
 * String Reading
 * ============================================================================
 */

ssize_t platform_getline(char **lineptr, size_t *n, FILE *stream) {
  if (!lineptr || !n || !stream) {
    return -1;
  }
  return getline(lineptr, n, stream);
}

/* ============================================================================
 * Shell Path Escaping
 * ============================================================================
 */

/**
 * Escape path for shell (POSIX version)
 * Uses single quotes which prevent all expansion
 */
asciichat_error_t platform_escape_shell_path(const char *path, char *output, size_t output_size) {
  if (!path || !output || output_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments to platform_escape_shell_path");
  }

  size_t path_len = strlen(path);

  /* Single-quoted string: 'path' */
  /* Need 2 bytes for quotes + 1 for null terminator */
  if (output_size < path_len + 3) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Output buffer too small for escaped path");
  }

  output[0] = '\'';
  strcpy(output + 1, path);
  output[path_len + 1] = '\'';
  output[path_len + 2] = '\0';

  return ASCIICHAT_OK;
}

#endif

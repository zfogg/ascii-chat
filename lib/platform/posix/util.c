/**
 * @file platform/posix/util.c
 * @brief POSIX utility functions implementation
 * @ingroup platform
 */

#ifndef _WIN32

#include <ascii-chat/platform/util.h>
#include <ascii-chat/common.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

/* String operations */

int platform_snprintf(char *str, size_t size, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int ret = platform_vsnprintf(str, size, format, ap);
  va_end(ap);
  return ret;
}

int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  return vsnprintf(str, size, format, ap);
}

char *platform_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s) + 1;
  char *dup = (char *)malloc(len);
  if (dup)
    strcpy(dup, s);
  return dup;
}

char *platform_strndup(const char *s, size_t n) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  if (len > n)
    len = n;
  char *dup = (char *)malloc(len + 1);
  if (dup) {
    strncpy(dup, s, len);
    dup[len] = '\0';
  }
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

size_t platform_strlcat(char *dst, const char *src, size_t size) {
  if (!dst || !src)
    return 0;
  size_t dst_len = strlen(dst);
  size_t src_len = strlen(src);
  if (dst_len < size) {
    size_t remaining = size - dst_len - 1;
    if (remaining > 0) {
      strncat(dst, src, remaining);
    }
    dst[size - 1] = '\0';
  }
  return dst_len + src_len;
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

/* File operations */

ssize_t platform_getline(char **lineptr, size_t *n, FILE *stream) {
  if (!lineptr || !n || !stream) {
    return -1;
  }
  return getline(lineptr, n, stream);
}

int platform_asprintf(char **strp, const char *format, ...) {
  if (!strp)
    return -1;
  va_list ap;
  va_start(ap, format);
  int ret = vasprintf(strp, format, ap);
  va_end(ap);
  return ret;
}

#endif

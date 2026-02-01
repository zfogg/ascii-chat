/**
 * @file platform/windows/util.c
 * @brief Windows utility functions implementation
 * @ingroup platform
 */

#ifdef _WIN32

#include "../util.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

/* String operations */

int platform_snprintf(char *str, size_t size, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int ret = _vsnprintf_s(str, size, _TRUNCATE, format, ap);
  va_end(ap);
  return ret;
}

int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  return _vsnprintf_s(str, size, _TRUNCATE, format, ap);
}

char *platform_strdup(const char *s) {
  if (!s)
    return NULL;
  return _strdup(s);
}

char *platform_strndup(const char *s, size_t n) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  if (len > n)
    len = n;
  char *dup = (char *)malloc(len + 1);
  if (dup) {
    strncpy_s(dup, len + 1, s, len);
    dup[len] = '\0';
  }
  return dup;
}

int platform_strcasecmp(const char *s1, const char *s2) {
  if (!s1 || !s2)
    return s1 == s2 ? 0 : (s1 ? 1 : -1);
  return _stricmp(s1, s2);
}

int platform_strncasecmp(const char *s1, const char *s2, size_t n) {
  if (!s1 || !s2)
    return s1 == s2 ? 0 : (s1 ? 1 : -1);
  return _strnicmp(s1, s2, n);
}

char *platform_strtok_r(char *str, const char *delim, char **saveptr) {
  return strtok_s(str, delim, saveptr);
}

size_t platform_strlcpy(char *dst, const char *src, size_t size) {
  if (!dst || !src)
    return 0;
  size_t src_len = strlen(src);
  if (size > 0) {
    size_t copy_len = src_len < size - 1 ? src_len : size - 1;
    strncpy_s(dst, size, src, copy_len);
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
    size_t remaining = size - dst_len;
    strncat_s(dst, size, src, remaining - 1);
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
  strncpy_s(dst, dst_size, src, count);
  return 0;
}

/* File operations */

ssize_t platform_getline(char **lineptr, size_t *n, FILE *stream) {
  if (!lineptr || !n || !stream) {
    return -1;
  }

  /* Allocate initial buffer if needed */
  if (*lineptr == NULL) {
    *n = 128;
    *lineptr = (char *)malloc(*n);
    if (*lineptr == NULL) {
      return -1;
    }
  }

  size_t pos = 0;
  int c;

  while ((c = fgetc(stream)) != EOF) {
    /* Resize buffer if needed (leave room for null terminator) */
    if (pos + 2 > *n) {
      size_t new_size = *n * 2;
      char *new_ptr = (char *)realloc(*lineptr, new_size);
      if (new_ptr == NULL) {
        return -1;
      }
      *lineptr = new_ptr;
      *n = new_size;
    }

    (*lineptr)[pos++] = (char)c;

    /* Stop at newline */
    if (c == '\n') {
      break;
    }
  }

  /* Handle EOF or error */
  if (c == EOF && pos == 0) {
    return -1; /* EOF with no data read */
  }

  /* Null-terminate the string */
  (*lineptr)[pos] = '\0';

  return (ssize_t)pos;
}

int platform_asprintf(char **strp, const char *format, ...) {
  if (!strp)
    return -1;
  va_list ap;
  va_start(ap, format);

  /* Determine required size */
  int size = _vscprintf(format, ap);
  if (size < 0) {
    va_end(ap);
    return -1;
  }

  *strp = (char *)malloc(size + 1);
  if (*strp == NULL) {
    va_end(ap);
    return -1;
  }

  int ret = vsprintf_s(*strp, size + 1, format, ap);
  va_end(ap);

  return ret < 0 ? -1 : ret;
}

#endif

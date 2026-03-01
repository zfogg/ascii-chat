/**
 * @file platform/windows/string.c
 * @brief Windows string utilities
 * @ingroup platform
 */

#ifdef _WIN32

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
  // Handle NULL buffer case for size calculation (standard vsnprintf behavior).
  // _vsnprintf_s does NOT accept NULL buffer, so use _vscprintf for size calculation.
  if (str == NULL || size == 0) {
    return _vscprintf(format, ap);
  }
  return _vsnprintf_s(str, size, _TRUNCATE, format, ap);
}

int platform_vasprintf(char **strp, const char *format, va_list ap) {
  // On Windows, use _vscprintf to get size, then allocate and format
  if (!strp || !format) {
    return -1;
  }

  // Get required size (not including null terminator)
  int size = _vscprintf(format, ap);
  if (size < 0) {
    return -1;
  }

  // Allocate buffer (size + 1 for null terminator)
  *strp = (char *)malloc(size + 1);
  if (!*strp) {
    return -1;
  }

  // Format into buffer
  int ret = _vsnprintf_s(*strp, size + 1, size, format, ap);
  if (ret < 0) {
    free(*strp);
    *strp = NULL;
    return -1;
  }

  return ret;
}

int platform_asprintf(char **strp, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int ret = platform_vasprintf(strp, format, args);
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
    memcpy(dup, s, len);
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

/* ============================================================================
 * String Reading
 * ============================================================================
 */

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

/* ============================================================================
 * Shell Path Escaping
 * ============================================================================
 */

/**
 * Escape path for shell (Windows version)
 * Uses double quotes and escapes internal quotes
 */
asciichat_error_t platform_escape_shell_path(const char *path, char *output, size_t output_size) {
  if (!path || !output || output_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments to platform_escape_shell_path");
  }

  size_t path_len = strlen(path);
  size_t output_pos = 0;

  /* Count quotes in path to determine buffer requirement */
  size_t quote_count = 0;
  for (size_t i = 0; i < path_len; i++) {
    if (path[i] == '"') {
      quote_count++;
    }
  }

  /* Need: opening quote + path + escaped quotes + closing quote + null */
  if (output_size < 1 + path_len + quote_count + 1 + 1) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Output buffer too small for escaped path");
  }

  /* Opening quote */
  output[output_pos++] = '"';

  /* Copy path, escaping internal quotes */
  for (size_t i = 0; i < path_len; i++) {
    if (path[i] == '"') {
      /* Escape quote by doubling it: " -> "" */
      output[output_pos++] = '"';
      output[output_pos++] = '"';
    } else {
      output[output_pos++] = path[i];
    }
  }

  /* Closing quote */
  output[output_pos++] = '"';
  output[output_pos] = '\0';

  return ASCIICHAT_OK;
}

/**
 * Safe string copy with bounds checking (Windows implementation)
 */
asciichat_error_t platform_strcpy(char *dest, size_t dest_size, const char *src) {
  if (!dest || !src) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pointers for strcpy");
  }
  if (dest_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Destination buffer size is zero");
  }

  size_t src_len = strlen(src);
  if (src_len >= dest_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Source string too long for destination buffer: %zu >= %zu", src_len,
                     dest_size);
  }

  SAFE_STRNCPY(dest, src, dest_size - 1);
  dest[dest_size - 1] = '\0';
  return ASCIICHAT_OK;
}

#endif

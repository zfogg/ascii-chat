/**
 * @file platform/wasm/string.c
 * @brief String utility functions for WASM/Emscripten
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/asciichat_errno.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>

size_t platform_strlcpy(char *dst, const char *src, size_t size) {
  size_t src_len = strlen(src);

  if (size == 0) {
    return src_len;
  }

  size_t copy_len = (src_len >= size) ? size - 1 : src_len;
  memcpy(dst, src, copy_len);
  dst[copy_len] = '\0';

  return src_len;
}

size_t platform_strlcat(char *dst, const char *src, size_t size) {
  size_t dst_len = strlen(dst);
  size_t src_len = strlen(src);

  if (dst_len >= size) {
    return dst_len + src_len;
  }

  size_t remaining = size - dst_len;
  size_t copy_len = (src_len >= remaining) ? remaining - 1 : src_len;
  memcpy(dst + dst_len, src, copy_len);
  dst[dst_len + copy_len] = '\0';

  return dst_len + src_len;
}

int platform_strcasecmp(const char *s1, const char *s2) {
  // Use standard strcasecmp (available in WASM)
  return strcasecmp(s1, s2);
}

int platform_asprintf(char **strp, const char *fmt, ...) {
  // Use standard asprintf (available in WASM/Emscripten)
  va_list args;
  va_start(args, fmt);
  int ret = vasprintf(strp, fmt, args);
  va_end(args);
  return ret;
}

char *platform_strdup(const char *s) {
  // Use standard strdup (available in WASM)
  return strdup(s);
}

asciichat_error_t platform_memcpy(void *dest, size_t dest_size, const void *src, size_t count) {
  if (!dest || !src) {
    return ERROR_INVALID_PARAM;
  }
  if (count > dest_size) {
    return ERROR_INVALID_PARAM; // Buffer overflow protection
  }
  memcpy(dest, src, count);
  return ASCIICHAT_OK;
}

asciichat_error_t platform_memset(void *dest, size_t dest_size, int ch, size_t count) {
  if (!dest) {
    return ERROR_INVALID_PARAM;
  }
  if (count > dest_size) {
    return ERROR_INVALID_PARAM; // Buffer overflow protection
  }
  memset(dest, ch, count);
  return ASCIICHAT_OK;
}

int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  return vsnprintf(str, size, format, ap);
}

int platform_strncasecmp(const char *s1, const char *s2, size_t n) {
  return strncasecmp(s1, s2, n);
}

/**
 * @file platform/windows/string.c
 * @brief Windows implementation of safe string functions
 *
 * This file provides Windows implementations of safe string functions
 * that satisfy clang-tidy cert-err33-c requirements.
 *
 * @author Assistant
 * @date December 2024
 */

#include "platform/string.h"
#include <stdarg.h>
#include <string.h>
#include "common.h"

// safe_snprintf and safe_fprintf are implemented in system.c
// to avoid duplicate symbol errors

char *platform_strcat(char *dest, size_t dest_size, const char *src) {
  if (!dest || !src || dest_size == 0) {
    log_error("platform_strcat: invalid parameters");
    return NULL;
  }

  // Calculate remaining space in dest buffer
  size_t dest_len = strlen(dest);
  size_t src_len = strlen(src);
  size_t remaining = dest_size - dest_len;

  if (remaining <= src_len) {
    log_error("platform_strcat: buffer overflow prevented (dest_len=%zu, src_len=%zu, remaining=%zu, dest_size=%zu)",
              dest_len, src_len, remaining, dest_size);
    return NULL;
  }

  errno_t err = strcat_s(dest, dest_size, src);
  if (err != 0) {
    log_error("strcat_s failed: errno=%d (%s)", err, SAFE_STRERROR(err));
    return NULL;
  }
  return dest;
}

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

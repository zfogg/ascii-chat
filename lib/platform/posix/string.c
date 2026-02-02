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

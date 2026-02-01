/**
 * @file platform/windows/string.c
 * @brief Windows string utilities
 * @ingroup platform
 */

#ifdef _WIN32

#include "../string.h"
#include "../../common.h"
#include <string.h>
#include <stdio.h>

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

#endif

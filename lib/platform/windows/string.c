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

#include "util/string.h"
#include "common.h"

void escape_ascii(const char *str, const char *escape_char, char *out_buffer, size_t out_buffer_size) {
  if (escape_char == NULL || str == NULL || !out_buffer || out_buffer_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid string or escape character or output buffer");
    return;
  }

  // Count how many characters need escaping
  size_t escape_count = 0;
  for (size_t i = 0; i < strlen(str); i++) {
    for (size_t j = 0; j < strlen(escape_char); j++) {
      if (str[i] == escape_char[j]) {
        escape_count++;
        break;
      }
    }
  }

  size_t total_len = strlen(str) + escape_count;
  if (out_buffer_size < total_len) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Output buffer is too small");
    return;
  }

  size_t out_pos = 0;
  for (size_t i = 0; i < strlen(str); i++) {
    bool needs_escape = false;
    for (size_t j = 0; j < strlen(escape_char); j++) {
      if (str[i] == escape_char[j]) {
        needs_escape = true;
        break;
      }
    }

    if (needs_escape) {
      out_buffer[out_pos++] = '\\';
    }
    out_buffer[out_pos++] = str[i];
  }

  out_buffer[out_pos] = '\0';
}

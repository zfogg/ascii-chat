#include "parsing.h"
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include "../asciichat_errno.h"

asciichat_error_t safe_parse_size_message(const char *message, unsigned int *width, unsigned int *height) {
  if (!message || !width || !height) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for size message parsing");
  }

  // Check if message starts with "SIZE:"
  if (strncmp(message, "SIZE:", 5) != 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Message does not start with 'SIZE:'");
  }

  const char *ptr = message + 5; // Skip "SIZE:"
  char *endptr;

  // Parse first number (width)
  errno = 0;
  unsigned long w = strtoul(ptr, &endptr, 10);
  if (errno != 0 || endptr == ptr || w > UINT_MAX || w == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid width value in size message");
  }

  // Check for comma separator
  if (*endptr != ',') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing comma separator in size message");
  }
  ptr = endptr + 1;

  // Parse second number (height)
  errno = 0;
  unsigned long h = strtoul(ptr, &endptr, 10);
  if (errno != 0 || endptr == ptr || h > UINT_MAX || h == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid height value in size message");
  }

  // Should end with newline or null terminator
  if (*endptr != '\n' && *endptr != '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid format: size message should end with newline or null terminator");
  }

  // Additional bounds checking
  if (w > 65535 || h > 65535) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Size values too large (max 65535)");
  }

  *width = (unsigned int)w;
  *height = (unsigned int)h;
  return ASCIICHAT_OK;
}

asciichat_error_t safe_parse_audio_message(const char *message, unsigned int *num_samples) {
  if (!message || !num_samples) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for audio message parsing");
  }

  // Check if message starts with "AUDIO:"
  if (strncmp(message, "AUDIO:", 6) != 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Message does not start with 'AUDIO:'");
  }

  const char *ptr = message + 6; // Skip "AUDIO:"
  char *endptr;

  // Parse number
  errno = 0;
  unsigned long samples = strtoul(ptr, &endptr, 10);
  if (errno != 0 || endptr == ptr || samples > UINT_MAX || samples == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid sample count value in audio message");
  }

  // Should end with newline or null terminator
  if (*endptr != '\n' && *endptr != '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid format: audio message should end with newline or null terminator");
  }

  *num_samples = (unsigned int)samples;
  return ASCIICHAT_OK;
}

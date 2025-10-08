#include "parsing.h"
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

int safe_parse_size_message(const char *message, unsigned int *width, unsigned int *height) {
  if (!message || !width || !height) {
    return -1;
  }

  // Check if message starts with "SIZE:"
  if (strncmp(message, "SIZE:", 5) != 0) {
    return -1;
  }

  const char *ptr = message + 5; // Skip "SIZE:"
  char *endptr;

  // Parse first number (width)
  errno = 0;
  unsigned long w = strtoul(ptr, &endptr, 10);
  if (errno != 0 || endptr == ptr || w > UINT_MAX || w == 0) {
    return -1;
  }

  // Check for comma separator
  if (*endptr != ',') {
    return -1;
  }
  ptr = endptr + 1;

  // Parse second number (height)
  errno = 0;
  unsigned long h = strtoul(ptr, &endptr, 10);
  if (errno != 0 || endptr == ptr || h > UINT_MAX || h == 0) {
    return -1;
  }

  // Should end with newline or null terminator
  if (*endptr != '\n' && *endptr != '\0') {
    return -1;
  }

  // Additional bounds checking
  if (w > 65535 || h > 65535) {
    return -1;
  }

  *width = (unsigned int)w;
  *height = (unsigned int)h;
  return 0;
}

int safe_parse_audio_message(const char *message, unsigned int *num_samples) {
  if (!message || !num_samples) {
    return -1;
  }

  // Check if message starts with "AUDIO:"
  if (strncmp(message, "AUDIO:", 6) != 0) {
    return -1;
  }

  const char *ptr = message + 6; // Skip "AUDIO:"
  char *endptr;

  // Parse number
  errno = 0;
  unsigned long samples = strtoul(ptr, &endptr, 10);
  if (errno != 0 || endptr == ptr || samples > UINT_MAX || samples == 0) {
    return -1;
  }

  // Should end with newline or null terminator
  if (*endptr != '\n' && *endptr != '\0') {
    return -1;
  }

  *num_samples = (unsigned int)samples;
  return 0;
}

/**
 * @file util/parsing.c
 * @ingroup util
 * @brief 🔍 Safe string parsing utilities for integers, sizes, and protocol messages
 */

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

/* ============================================================================
 * Integer Parsing Functions
 * ============================================================================
 */

asciichat_error_t parse_long(const char *str, long *out_value, long min_value, long max_value) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  if (*str == '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty string cannot be parsed as integer");
  }

  // Clear errno before calling strtol to detect errors
  errno = 0;
  char *endptr = NULL;
  long value = strtol(str, &endptr, 10);

  // Check for conversion errors
  if (errno == ERANGE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value out of range: %s", str);
  }

  // Check that entire string was consumed (no extra characters)
  if (endptr == str || *endptr != '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid integer format: %s", str);
  }

  // Check user-specified range
  if (value < min_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value %ld is below minimum %ld", value, min_value);
  }

  if (value > max_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value %ld exceeds maximum %ld", value, max_value);
  }

  *out_value = value;
  return ASCIICHAT_OK;
}

asciichat_error_t parse_ulong(const char *str, unsigned long *out_value, unsigned long min_value,
                              unsigned long max_value) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  if (*str == '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty string cannot be parsed as integer");
  }

  // Clear errno before calling strtoul to detect errors
  errno = 0;
  char *endptr = NULL;
  unsigned long value = strtoul(str, &endptr, 10);

  // Check for conversion errors
  if (errno == ERANGE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value out of range: %s", str);
  }

  // Check that entire string was consumed (no extra characters)
  if (endptr == str || *endptr != '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid integer format: %s", str);
  }

  // Check user-specified range
  if (value < min_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value %lu is below minimum %lu", value, min_value);
  }

  if (value > max_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value %lu exceeds maximum %lu", value, max_value);
  }

  *out_value = value;
  return ASCIICHAT_OK;
}

asciichat_error_t parse_ulonglong(const char *str, unsigned long long *out_value, unsigned long long min_value,
                                  unsigned long long max_value) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  if (*str == '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty string cannot be parsed as integer");
  }

  // Clear errno before calling strtoull to detect errors
  errno = 0;
  char *endptr = NULL;
  unsigned long long value = strtoull(str, &endptr, 10);

  // Check for conversion errors
  if (errno == ERANGE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value out of range: %s", str);
  }

  // Check that entire string was consumed (no extra characters)
  if (endptr == str || *endptr != '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid integer format: %s", str);
  }

  // Check user-specified range
  if (value < min_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value too small (below minimum)");
  }

  if (value > max_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Integer value too large (exceeds maximum)");
  }

  *out_value = value;
  return ASCIICHAT_OK;
}

asciichat_error_t parse_port(const char *str, uint16_t *out_port) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_port) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  unsigned long port_ulong;
  asciichat_error_t err = parse_ulong(str, &port_ulong, 1, 65535);
  if (err != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port number: %s (must be 1-65535)", str);
  }

  *out_port = (uint16_t)port_ulong;
  return ASCIICHAT_OK;
}

asciichat_error_t parse_int32(const char *str, int32_t *out_value, int32_t min_value, int32_t max_value) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  long value;
  asciichat_error_t err = parse_long(str, &value, (long)min_value, (long)max_value);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  *out_value = (int32_t)value;
  return ASCIICHAT_OK;
}

asciichat_error_t parse_uint32(const char *str, uint32_t *out_value, uint32_t min_value, uint32_t max_value) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  unsigned long value;
  asciichat_error_t err = parse_ulong(str, &value, (unsigned long)min_value, (unsigned long)max_value);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  *out_value = (uint32_t)value;
  return ASCIICHAT_OK;
}

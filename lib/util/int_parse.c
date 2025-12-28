/**
 * @file util/int_parse.c
 * @ingroup util
 * @brief 🔢 Safe integer parsing with overflow detection and error handling
 */

#include "int_parse.h"
#include "common.h"
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

asciichat_error_t int_parse_long(const char *str, long *out_value, long min_value, long max_value) {
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

asciichat_error_t int_parse_ulong(const char *str, unsigned long *out_value, unsigned long min_value,
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

asciichat_error_t int_parse_ulonglong(const char *str, unsigned long long *out_value, unsigned long long min_value,
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

asciichat_error_t int_parse_port(const char *str, uint16_t *out_port) {
  if (!out_port) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  unsigned long port_ulong;
  asciichat_error_t err = int_parse_ulong(str, &port_ulong, 1, 65535);
  if (err != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port number: %s (must be 1-65535)", str);
  }

  *out_port = (uint16_t)port_ulong;
  return ASCIICHAT_OK;
}

asciichat_error_t int_parse_int32(const char *str, int32_t *out_value, int32_t min_value, int32_t max_value) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  long value;
  asciichat_error_t err = int_parse_long(str, &value, (long)min_value, (long)max_value);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  *out_value = (int32_t)value;
  return ASCIICHAT_OK;
}

asciichat_error_t int_parse_uint32(const char *str, uint32_t *out_value, uint32_t min_value, uint32_t max_value) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_value) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  unsigned long value;
  asciichat_error_t err = int_parse_ulong(str, &value, (unsigned long)min_value, (unsigned long)max_value);
  if (err != ASCIICHAT_OK) {
    return err;
  }

  *out_value = (uint32_t)value;
  return ASCIICHAT_OK;
}

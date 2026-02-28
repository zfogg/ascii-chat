/**
 * @file util/parsing.c
 * @ingroup util
 * @brief 🔍 Safe string parsing utilities for integers, sizes, and protocol messages
 */

#include <ascii-chat/util/parsing.h>
#include <ascii-chat/util/pcre2.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>

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

/**
 * @brief PCRE2 regex validator for port number parsing
 *
 * Validates port range 1-65535 atomically, rejecting leading zeros.
 * Thread-safe singleton with lazy initialization via atomic operations.
 */
static pcre2_singleton_t *g_port_regex = NULL;

/**
 * @brief Get compiled port regex (lazy initialization)
 * Returns NULL if compilation failed
 *
 * Pattern: Match port numbers 1-65535 (rejects leading zeros)
 * [1-9]\d{0,3}          = 1-9999
 * [1-5]\d{4}            = 10000-59999
 * 6[0-4]\d{3}           = 60000-64999
 * 65[0-4]\d{2}          = 65000-65499
 * 655[0-2]\d            = 65500-65529
 * 6553[0-5]             = 65530-65535
 */
static pcre2_code *port_regex_get(void) {
  if (g_port_regex == NULL) {
    const char *port_pattern = "^([1-9]\\d{0,3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])$";
    g_port_regex = asciichat_pcre2_singleton_compile(port_pattern, 0);
  }
  return asciichat_pcre2_singleton_get_code(g_port_regex);
}

asciichat_error_t parse_port(const char *str, uint16_t *out_port) {
  if (!str) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "String pointer is NULL");
  }

  if (!out_port) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output pointer is NULL");
  }

  if (*str == '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Empty string cannot be parsed as port");
  }

  pcre2_code *regex = port_regex_get();
  if (!regex) {
    /* Fallback: use original parse_ulong method if PCRE2 not available */
    unsigned long port_ulong;
    asciichat_error_t err = parse_ulong(str, &port_ulong, 0, 65535);
    if (err != ASCIICHAT_OK) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port number: %s (must be 0-65535)", str);
    }
    *out_port = (uint16_t)port_ulong;
    return ASCIICHAT_OK;
  }

  /* Validate port using PCRE2 regex */
  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    log_error("Failed to allocate match data for port validation");
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port number: %s (must be 0-65535)", str);
  }

  int match_result = pcre2_jit_match(regex, (PCRE2_SPTR8)str, strlen(str), 0, 0, match_data, NULL);
  pcre2_match_data_free(match_data);

  if (match_result < 0) {
    /* Format validation failed */
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port number: %s (must be 0-65535)", str);
  }

  /* Extract matched port string and convert to uint16_t */
  char *endptr = NULL;
  unsigned long port_value = strtoul(str, &endptr, 10);

  if (port_value < 0 || port_value > 65535) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Port out of range: %lu", port_value);
  }

  *out_port = (uint16_t)port_value;
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

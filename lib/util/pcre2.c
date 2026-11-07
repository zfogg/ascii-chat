/**
 * @file util/pcre2.c
 * @brief PCRE2 compilation and match helpers
 * @ingroup util
 */

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define UTIL_PCRE2_TEMP_BUFFER_SIZE 64

#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <string.h>
#include <errno.h>

pcre2_code *asciichat_pcre2_compile(const char *pattern, uint32_t flags) {
  int error;
  PCRE2_SIZE offset;
  return pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, flags, &error, &offset, NULL);
}

/**
 * @brief Extract named substring from PCRE2 match data
 *
 * Extracts a named capture group from match data and returns an allocated string.
 * The caller is responsible for freeing the returned string with SAFE_FREE().
 *
 * @param regex Compiled PCRE2 regex with named groups
 * @param match_data Match data from pcre2_match() or pcre2_jit_match()
 * @param group_name Name of the capture group to extract
 * @param subject Original subject string that was matched
 * @return Allocated string containing the matched substring, or NULL if group not matched
 */
char *asciichat_pcre2_extract_named_group(pcre2_code *regex, pcre2_match_data *match_data, const char *group_name,
                                          const char *subject) {
  if (!regex || !match_data || !group_name || !subject) {
    log_error("invalid parameters");
    return NULL;
  }

  /* Get group number from name */
  int group_number = pcre2_substring_number_from_name(regex, (PCRE2_SPTR)group_name);
  if (group_number < 0) {
    return NULL; /* Group doesn't exist */
  }

  /* Get ovector (output vector with match offsets) */
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
  PCRE2_SIZE start = ovector[2 * group_number];
  PCRE2_SIZE end = ovector[2 * group_number + 1];

  if (start == PCRE2_UNSET || end == PCRE2_UNSET) {
    return NULL; /* Group not matched */
  }

  /* Allocate and copy substring */
  size_t len = end - start;
  char *result = SAFE_MALLOC(len + 1, char *);
  if (!result) {
    log_error("failed to allocate %zu bytes", len + 1);
    return NULL;
  }

  memcpy(result, subject + start, len);
  result[len] = '\0';

  return result;
}

/**
 * @brief Extract numbered capture group as allocated string
 */
char *asciichat_pcre2_extract_group(pcre2_match_data *match_data, int group_num, const char *subject) {
  if (!match_data || !subject || group_num < 0) {
    return NULL;
  }

  /* Get ovector (output vector with match offsets) */
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
  PCRE2_SIZE start = ovector[2 * group_num];
  PCRE2_SIZE end = ovector[2 * group_num + 1];

  if (start == PCRE2_UNSET || end == PCRE2_UNSET) {
    return NULL; /* Group not matched */
  }

  /* Allocate and copy substring */
  size_t len = end - start;
  char *result = SAFE_MALLOC(len + 1, char *);
  if (!result) {
    log_error("failed to allocate %zu bytes", len + 1);
    return NULL;
  }

  memcpy(result, subject + start, len);
  result[len] = '\0';

  return result;
}

/**
 * @brief Extract numbered capture group as pointer into subject (non-allocating)
 */
const char *asciichat_pcre2_extract_group_ptr(pcre2_match_data *match_data, int group_num, const char *subject,
                                              size_t *out_len) {
  if (!match_data || !subject || !out_len || group_num < 0) {
    return NULL;
  }

  /* Get ovector (output vector with match offsets) */
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
  PCRE2_SIZE start = ovector[2 * group_num];
  PCRE2_SIZE end = ovector[2 * group_num + 1];

  if (start == PCRE2_UNSET || end == PCRE2_UNSET) {
    return NULL; /* Group not matched */
  }

  *out_len = end - start;
  return subject + start;
}

/**
 * @brief Extract numbered capture group and convert to unsigned long
 */
bool asciichat_pcre2_extract_group_ulong(pcre2_match_data *match_data, int group_num, const char *subject,
                                         unsigned long *out_value) {
  if (!match_data || !subject || !out_value || group_num < 0) {
    return false;
  }

  /* Get ovector (output vector with match offsets) */
  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
  PCRE2_SIZE start = ovector[2 * group_num];
  PCRE2_SIZE end = ovector[2 * group_num + 1];

  if (start == PCRE2_UNSET || end == PCRE2_UNSET) {
    return false; /* Group not matched */
  }

  /* Extract substring into temporary buffer */
  size_t len = end - start;
  if (len == 0 || len > 63) { /* Sanity check for reasonable number length */
    return false;
  }

  char temp[UTIL_PCRE2_TEMP_BUFFER_SIZE];
  memcpy(temp, subject + start, len);
  temp[len] = '\0';

  /* Parse as unsigned long */
  char *endptr;
  errno = 0;
  unsigned long value = strtoul(temp, &endptr, 10);

  if (errno != 0 || endptr == temp || *endptr != '\0') {
    return false; /* Parse failed */
  }

  *out_value = value;
  return true;
}

int asciichat_pcre2_match(const pcre2_code *code, PCRE2_SPTR subject, PCRE2_SIZE length, PCRE2_SIZE startoffset,
                          uint32_t options, pcre2_match_data *match_data, pcre2_match_context *context) {
  int result = pcre2_jit_match(code, subject, length, startoffset, options, match_data, context);
  if (result == PCRE2_ERROR_JIT_BADOPTION || result == PCRE2_ERROR_JIT_STACKLIMIT)
    return pcre2_match(code, subject, length, startoffset, options | PCRE2_NO_JIT, match_data, context);
  return result;
}

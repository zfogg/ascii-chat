/**
 * @file util/pcre2.h
 * @brief PCRE2 compilation and match helpers
 * @ingroup util
 *
 * Compiled patterns are owned and freed by their callers.
 */

#pragma once

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdbool.h>

/** Compile a pattern owned by the caller, who must call pcre2_code_free(). */
pcre2_code *asciichat_pcre2_compile(const char *pattern, uint32_t flags);

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
 *
 * Example:
 * @code
 * pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
 * int rc = pcre2_jit_match(regex, (PCRE2_SPTR)subject, strlen(subject), 0, 0, match_data, NULL);
 * if (rc >= 0) {
 *   char *value = asciichat_pcre2_extract_named_group(regex, match_data, "fieldname", subject);
 *   if (value) {
 *     // Use value...
 *     SAFE_FREE(value);
 *   }
 * }
 * pcre2_match_data_free(match_data);
 * @endcode
 */
char *asciichat_pcre2_extract_named_group(pcre2_code *regex, pcre2_match_data *match_data, const char *group_name,
                                          const char *subject);

/**
 * @brief Extract numbered capture group as allocated string
 *
 * Extracts a numbered capture group from match data and returns an allocated string.
 * The caller is responsible for freeing the returned string with SAFE_FREE().
 *
 * @param match_data Match data from pcre2_match() or pcre2_jit_match()
 * @param group_num Capture group number (1-based, 0 = entire match)
 * @param subject Original subject string that was matched
 * @return Allocated string containing the matched substring, or NULL if group not matched
 *
 * Example:
 * @code
 * char *foundation = asciichat_pcre2_extract_group(match_data, 1, line);
 * if (foundation) {
 *   // Use foundation...
 *   SAFE_FREE(foundation);
 * }
 * @endcode
 */
char *asciichat_pcre2_extract_group(pcre2_match_data *match_data, int group_num, const char *subject);

/**
 * @brief Extract numbered capture group as pointer into subject (non-allocating)
 *
 * Returns a pointer to the matched substring within the original subject string.
 * No memory allocation occurs - the returned pointer is only valid as long as
 * the subject string remains valid.
 *
 * @param match_data Match data from pcre2_match() or pcre2_jit_match()
 * @param group_num Capture group number (1-based, 0 = entire match)
 * @param subject Original subject string that was matched
 * @param out_len Output parameter for substring length
 * @return Pointer to matched substring within subject, or NULL if group not matched
 *
 * Example:
 * @code
 * size_t len;
 * const char *video_id = asciichat_pcre2_extract_group_ptr(match_data, 1, url, &len);
 * if (video_id) {
 *   printf("Video ID: %.*s\n", (int)len, video_id);
 * }
 * @endcode
 */
const char *asciichat_pcre2_extract_group_ptr(pcre2_match_data *match_data, int group_num, const char *subject,
                                              size_t *out_len);

/**
 * @brief Extract numbered capture group and convert to unsigned long
 *
 * Extracts a numbered capture group and converts it to an unsigned long integer.
 * Handles extraction and parsing in one step.
 *
 * @param match_data Match data from pcre2_match() or pcre2_jit_match()
 * @param group_num Capture group number (1-based, 0 = entire match)
 * @param subject Original subject string that was matched
 * @param out_value Output parameter for parsed integer value
 * @return true if extraction and parsing succeeded, false otherwise
 *
 * Example:
 * @code
 * unsigned long component_id;
 * if (asciichat_pcre2_extract_group_ulong(match_data, 2, line, &component_id)) {
 *   candidate->component_id = (uint32_t)component_id;
 * }
 * @endcode
 */
bool asciichat_pcre2_extract_group_ulong(pcre2_match_data *match_data, int group_num, const char *subject,
                                         unsigned long *out_value);

/**
 * Match with the JIT fast path, using the interpreter when JIT is unavailable.
 * Returns the native PCRE2 capture count or negative PCRE2 error code.
 */
int asciichat_pcre2_match(const pcre2_code *code, PCRE2_SPTR subject, PCRE2_SIZE length, PCRE2_SIZE startoffset,
                          uint32_t options, pcre2_match_data *match_data, pcre2_match_context *context);

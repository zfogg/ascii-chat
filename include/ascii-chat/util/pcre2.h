/**
 * @file util/pcre2.h
 * @brief 🔍 Centralized PCRE2 singleton pattern for efficient regex compilation
 * @ingroup util
 *
 * Provides thread-safe lazy initialization of PCRE2 compiled regexes with
 * automatic JIT compilation. Each regex pattern is compiled once per process
 * on first use, enabling safe concurrent reads with zero initialization
 * overhead for unused patterns.
 */

#pragma once

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdbool.h>

/**
 * @brief Opaque handle to a compiled PCRE2 regex singleton
 *
 * Represents a thread-safe compiled regex with JIT optimization.
 * Handles are returned by pcre2_singleton_compile() and are valid
 * for the lifetime of the program.
 */
typedef struct pcre2_singleton pcre2_singleton_t;

/**
 * @brief Compile and cache a PCRE2 regex pattern with thread-safe singleton semantics
 *
 * Allocates a singleton structure to store the pattern and flags. Actual compilation
 * happens lazily on first call to asciichat_pcre2_singleton_get_code().
 *
 * This function returns immediately without doing expensive regex compilation.
 * Compilation happens later when the code is first accessed.
 *
 * @param pattern PCRE2 regex pattern (e.g., "^[a-z]+$")
 * @param flags PCRE2 compile flags (e.g., PCRE2_CASELESS | PCRE2_MULTILINE)
 * @return Opaque singleton handle, or NULL on allocation failure
 *
 * Example:
 * @code
 * static pcre2_singleton_t *g_email_regex = NULL;
 *
 * void init_validators() {
 *   g_email_regex = asciichat_pcre2_singleton_compile(
 *       "^[a-z0-9._%+-]+@[a-z0-9.-]+\\.[a-z]{2,}$",
 *       PCRE2_CASELESS
 *   );
 * }
 * @endcode
 */
pcre2_singleton_t *asciichat_pcre2_singleton_compile(const char *pattern, uint32_t flags);

/**
 * @brief Get the compiled pcre2_code from a singleton handle
 *
 * Lazily compiles the regex on first call. Subsequent calls return the cached code.
 * Thread-safe: multiple threads can call this concurrently; compilation happens once.
 *
 * @param singleton Handle returned by asciichat_pcre2_singleton_compile()
 * @return Compiled regex code (read-only), or NULL if compilation failed
 */
pcre2_code *asciichat_pcre2_singleton_get_code(pcre2_singleton_t *singleton);

/**
 * @brief Check if a singleton was successfully initialized
 *
 * @param singleton Handle returned by asciichat_pcre2_singleton_compile()
 * @return true if regex compiled successfully, false otherwise
 */
bool asciichat_pcre2_singleton_is_initialized(pcre2_singleton_t *singleton);

/**
 * @brief Free a PCRE2 singleton and its resources
 *
 * Frees the compiled regex code, JIT stack, and singleton structure.
 * After calling this, the singleton pointer is invalid and should not be used.
 *
 * @param singleton Handle returned by asciichat_pcre2_singleton_compile()
 */
void asciichat_pcre2_singleton_free(pcre2_singleton_t *singleton);

/**
 * @brief Free all PCRE2 singletons
 *
 * Automatically frees all singletons created with asciichat_pcre2_singleton_compile().
 * Safe to call multiple times (idempotent). Should be called once during shutdown.
 */
void asciichat_pcre2_cleanup_all(void);

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

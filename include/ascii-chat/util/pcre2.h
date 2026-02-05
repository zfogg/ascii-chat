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

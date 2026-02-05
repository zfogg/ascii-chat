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
 * happens lazily on first call to pcre2_singleton_get_code().
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
 *   g_email_regex = pcre2_singleton_compile(
 *       "^[a-z0-9._%+-]+@[a-z0-9.-]+\\.[a-z]{2,}$",
 *       PCRE2_CASELESS
 *   );
 * }
 * @endcode
 */
pcre2_singleton_t *pcre2_singleton_compile(const char *pattern, uint32_t flags);

/**
 * @brief Get the compiled pcre2_code from a singleton handle
 *
 * Lazily compiles the regex on first call. Subsequent calls return the cached code.
 * Thread-safe: multiple threads can call this concurrently; compilation happens once.
 *
 * @param singleton Handle returned by pcre2_singleton_compile()
 * @return Compiled regex code (read-only), or NULL if compilation failed
 */
pcre2_code *pcre2_singleton_get_code(pcre2_singleton_t *singleton);

/**
 * @brief Check if a singleton was successfully initialized
 *
 * @param singleton Handle returned by pcre2_singleton_compile()
 * @return true if regex compiled successfully, false otherwise
 */
bool pcre2_singleton_is_initialized(pcre2_singleton_t *singleton);

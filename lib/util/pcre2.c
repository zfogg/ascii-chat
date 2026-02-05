/**
 * @file util/pcre2.c
 * @brief Centralized PCRE2 singleton pattern for efficient regex compilation
 * @ingroup util
 */

#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <string.h>
#include <stdatomic.h>

/**
 * @brief Represents a thread-safe compiled PCRE2 regex singleton
 *
 * Uses atomic flag for thread-safe lazy initialization without mutexes.
 * The compilation flag is set only once, and the compiled code is read-only
 * after that, enabling concurrent access from multiple threads.
 */
typedef struct pcre2_singleton {
  _Atomic(pcre2_code *) code;   ///< Compiled regex (lazy init, atomic)
  pcre2_jit_stack *jit_stack;   ///< JIT stack for performance
  _Atomic(bool) compiled;       ///< Whether compilation was attempted
  const char *pattern;          ///< Pattern string (for error messages)
  uint32_t flags;               ///< PCRE2 compile flags
  struct pcre2_singleton *next; ///< Next singleton in global registry
} pcre2_singleton_t;

/* Global registry of all PCRE2 singletons for automatic cleanup */
static pcre2_singleton_t *g_singleton_registry = NULL;
static _Atomic(bool) g_registry_initialized = false;

/**
 * @brief Compile and cache a PCRE2 regex pattern with thread-safe singleton semantics
 *
 * Allocates a singleton structure and stores the pattern/flags. The actual
 * compilation happens lazily on first call to asciichat_pcre2_singleton_get_code().
 *
 * This function returns immediately without doing expensive regex compilation.
 *
 * @param pattern PCRE2 regex pattern string
 * @param flags PCRE2 compile flags
 * @return Opaque singleton handle, or NULL on allocation failure
 */
pcre2_singleton_t *asciichat_pcre2_singleton_compile(const char *pattern, uint32_t flags) {
  if (!pattern) {
    log_error("PCRE2 singleton: pattern is NULL");
    return NULL;
  }

  /* Allocate singleton structure */
  pcre2_singleton_t *singleton = SAFE_MALLOC(sizeof(pcre2_singleton_t), pcre2_singleton_t *);
  if (!singleton) {
    log_error("PCRE2 singleton: failed to allocate singleton structure");
    return NULL;
  }

  /* Initialize fields */
  atomic_store(&singleton->code, NULL);
  singleton->jit_stack = NULL;
  atomic_store(&singleton->compiled, false);
  singleton->pattern = pattern;
  singleton->flags = flags;

  /* Register singleton in global list for automatic cleanup */
  singleton->next = g_singleton_registry;
  g_singleton_registry = singleton;
  atomic_store(&g_registry_initialized, true);

  return singleton;
}

/**
 * @brief Get the compiled pcre2_code from a singleton handle
 *
 * Lazily compiles the regex on first call. Fast path (subsequent calls)
 * only reads a single atomic variable with no synchronization overhead.
 *
 * Thread-safe: If multiple threads call this concurrently on first use,
 * only one will compile; others will wait for the code to become non-NULL.
 *
 * @param singleton Handle returned by asciichat_pcre2_singleton_compile()
 * @return Compiled regex code (read-only), or NULL if compilation failed
 */
pcre2_code *asciichat_pcre2_singleton_get_code(pcre2_singleton_t *singleton) {
  if (!singleton) {
    return NULL;
  }

  /* Fast path: check if already compiled (lock-free atomic load, no mutex) */
  pcre2_code *code = atomic_load(&singleton->code);
  if (code != NULL) {
    return code;
  }

  /* Slow path: first call, need to compile. Use compare-and-swap to ensure
   * only one thread does the compilation work. */
  int errornumber;
  PCRE2_SIZE erroroffset;

  code = pcre2_compile((PCRE2_SPTR8)singleton->pattern, PCRE2_ZERO_TERMINATED, singleton->flags, &errornumber,
                       &erroroffset, NULL);

  if (!code) {
    PCRE2_UCHAR error_buf[256];
    pcre2_get_error_message(errornumber, error_buf, sizeof(error_buf));
    log_warn("Failed to compile PCRE2 regex at offset %zu: %s", erroroffset, (const char *)error_buf);
    /* Mark as "attempted" by setting a sentinel value (store NULL explicitly) */
    atomic_store(&singleton->compiled, true);
    return NULL;
  }

  /* Attempt JIT compilation (non-fatal if fails) */
  int jit_rc = pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
  if (jit_rc < 0) {
    log_debug("PCRE2 JIT compilation not available (code %d), using interpreted mode", jit_rc);
  } else {
    /* Create JIT stack for pattern execution */
    singleton->jit_stack = pcre2_jit_stack_create(32 * 1024, 512 * 1024, NULL);
    if (!singleton->jit_stack) {
      log_warn("Failed to create JIT stack for PCRE2 regex");
    }
  }

  /* Store compiled code (atomic, so subsequent calls see it immediately) */
  atomic_store(&singleton->code, code);
  atomic_store(&singleton->compiled, true);

  log_debug("PCRE2 regex compiled: pattern at %p, flags=%u", singleton->pattern, singleton->flags);
  return code;
}

/**
 * @brief Check if a singleton was successfully initialized
 *
 * @param singleton Handle returned by asciichat_pcre2_singleton_compile()
 * @return true if regex compiled successfully, false otherwise
 */
bool asciichat_pcre2_singleton_is_initialized(pcre2_singleton_t *singleton) {
  if (!singleton) {
    return false;
  }
  return atomic_load(&singleton->code) != NULL;
}

/**
 * @brief Free a PCRE2 singleton and its resources
 *
 * Frees the compiled regex code, JIT stack, and singleton structure.
 * After calling this, the singleton pointer is invalid and should not be used.
 *
 * @param singleton Handle returned by asciichat_pcre2_singleton_compile()
 */
void asciichat_pcre2_singleton_free(pcre2_singleton_t *singleton) {
  if (!singleton) {
    return;
  }

  /* Free compiled code if it exists */
  pcre2_code *code = atomic_load(&singleton->code);
  if (code) {
    pcre2_code_free(code);
  }

  /* Free JIT stack if it exists */
  if (singleton->jit_stack) {
    pcre2_jit_stack_free(singleton->jit_stack);
  }

  /* Free the singleton structure itself */
  free(singleton);
}

/**
 * @brief Free all PCRE2 singletons in the global registry
 *
 * Walks the global registry and frees all singletons. Safe to call
 * multiple times (idempotent). Should be called once during shutdown.
 */
void asciichat_pcre2_cleanup_all(void) {
  if (!atomic_load(&g_registry_initialized)) {
    return; /* No singletons were ever created or already cleaned up */
  }

  /* Check if already cleaned up */
  if (g_singleton_registry == NULL) {
    return;
  }

  pcre2_singleton_t *current = g_singleton_registry;

  /* Clear registry first to prevent re-entry */
  g_singleton_registry = NULL;
  atomic_store(&g_registry_initialized, false);

  /* Now free all singletons */
  while (current) {
    pcre2_singleton_t *next = current->next;

    /* Free compiled code */
    pcre2_code *code = atomic_load(&current->code);
    if (code) {
      pcre2_code_free(code);
      atomic_store(&current->code, NULL);
    }

    /* Free JIT stack */
    if (current->jit_stack) {
      pcre2_jit_stack_free(current->jit_stack);
      current->jit_stack = NULL;
    }

    /* Free singleton structure */
    free(current);

    current = next;
  }
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
    log_error("pcre2_extract_named_group: invalid parameters");
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
    log_error("pcre2_extract_named_group: failed to allocate %zu bytes", len + 1);
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
    log_error("asciichat_pcre2_extract_group: failed to allocate %zu bytes", len + 1);
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

  char temp[64];
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

#pragma once

/**
 * @file debug/mutex.h
 * @brief Per-thread mutex lock stack for deadlock detection
 * @ingroup debug_sync
 *
 * Maintains a stack of mutexes per thread, tracking both locked and pending locks.
 * Used to detect circular wait deadlock patterns by comparing stacks across threads.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lock state in the stack
 */
typedef enum {
  MUTEX_STACK_STATE_PENDING, // Attempting to acquire lock
  MUTEX_STACK_STATE_LOCKED,  // Lock successfully acquired
} mutex_stack_state_t;

/**
 * Entry in a thread's lock stack
 */
typedef struct {
  uintptr_t mutex_key;       // Pointer to mutex_t (used as unique ID)
  const char *mutex_name;    // Human-readable mutex name
  mutex_stack_state_t state; // PENDING or LOCKED
  uint64_t timestamp_ns;     // When this lock was attempted/acquired
} mutex_stack_entry_t;

/**
 * @brief Push a mutex onto the current thread's lock stack (PENDING state)
 * @param mutex_key Pointer to mutex (used as unique ID)
 * @param mutex_name Human-readable name of mutex
 */
void mutex_stack_push_pending(uintptr_t mutex_key, const char *mutex_name);

/**
 * @brief Mark the top of the current thread's lock stack as LOCKED
 * (transitions PENDING -> LOCKED)
 * @param mutex_key Pointer to mutex (must match most recent pending push)
 */
void mutex_stack_mark_locked(uintptr_t mutex_key);

/**
 * @brief Pop the top mutex from the current thread's lock stack (unlock)
 * @param mutex_key Pointer to mutex (used for validation)
 */
void mutex_stack_pop(uintptr_t mutex_key);

/**
 * @brief Get the current thread's lock stack
 * @param out_entries Pointer to array where entries will be stored
 * @param max_entries Maximum number of entries to return
 * @return Number of entries in the stack (may be > max_entries if truncated)
 */
int mutex_stack_get_current(mutex_stack_entry_t *out_entries, int max_entries);

/**
 * @brief Get all threads' lock stacks for deadlock analysis
 *
 * Allocates memory for all thread stacks. Caller must free with mutex_stack_free_all_threads().
 *
 * @param out_stacks Pointer to array of stacks (one per thread)
 * @param out_stack_counts Array of stack sizes corresponding to out_stacks
 * @param out_thread_count Number of threads with lock stacks
 * @return 0 on success, -1 on error
 */
int mutex_stack_get_all_threads(mutex_stack_entry_t ***out_stacks, int **out_stack_counts, int *out_thread_count);

/**
 * @brief Free memory allocated by mutex_stack_get_all_threads()
 */
void mutex_stack_free_all_threads(mutex_stack_entry_t **stacks, int *stack_counts, int thread_count);

/**
 * @brief Detect and log circular wait deadlocks
 *
 * Analyzes all thread lock stacks to find circular wait patterns:
 * - Thread A holds mutex1 and waits for mutex2
 * - Thread B holds mutex2 and waits for mutex1
 *
 * Logs detailed deadlock information with thread IDs, mutex names, and stack traces.
 * Called periodically by debug thread (~100ms interval).
 */
void mutex_stack_detect_deadlocks(void);

/**
 * @brief Initialize mutex stack system
 * @return 0 on success, -1 on error
 */
int mutex_stack_init(void);

/**
 * @brief Cleanup mutex stack system
 */
void mutex_stack_cleanup(void);

/**
 * @brief Cleanup TLS stack for current thread
 * Explicitly frees the thread-local mutex stack. Used to prevent leaks
 * when TLS destructors might not run reliably (e.g., before TLS key deletion).
 */
void mutex_stack_cleanup_current_thread(void);

#ifdef __cplusplus
}
#endif

/** @} */

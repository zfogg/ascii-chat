/**
 * @file debug/atomic.h
 * @brief Debug tracking for atomic operations
 * @ingroup debug
 * @date March 2026
 *
 * Provides debug-only tracking for named atomics via the named.c registry.
 * Integrates with --sync-state output to display atomic timing and counts.
 */

#pragma once

#include <ascii-chat/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct atomic_t;
struct atomic_ptr;  // Forward declaration for atomic_ptr_t if needed

/**
 * @brief Register an atomic_t in the debug named registry
 * @param a Pointer to atomic_t to register
 * @param name Human-readable name
 *
 * Called automatically by atomic_init_impl() in debug builds.
 * Registers the atomic's address as a uintptr_t with the named registry.
 */
void debug_atomic_register(atomic_t *a, const char *name);

/**
 * @brief Unregister an atomic_t from the debug named registry
 * @param a Pointer to atomic_t to unregister
 *
 * Called automatically by atomic_destroy() in debug builds.
 */
void debug_atomic_unregister(atomic_t *a);

/**
 * @brief Register an _Atomic(void *) in the debug named registry
 * @param a Pointer to _Atomic(void *) to register
 * @param name Human-readable name
 */
void debug_atomic_ptr_register(_Atomic(void *) *a, const char *name);

/**
 * @brief Unregister an _Atomic(void *) from the debug named registry
 * @param a Pointer to _Atomic(void *) to unregister
 */
void debug_atomic_ptr_unregister(_Atomic(void *) *a);

/**
 * @brief Format timing info for an atomic_t to a buffer
 * @param atomic Pointer to atomic_t
 * @param buffer Output buffer for formatted timing info
 * @param size Buffer size
 * @return Number of bytes written (0 if never accessed)
 *
 * Used by --sync-state to display atomic operation timing and counts.
 * Only formats output if the atomic has been accessed.
 */
int debug_atomic_format_timing(const atomic_t *atomic, char *buffer, size_t size);

/**
 * @brief Format timing info for an atomic_ptr_t to a buffer
 * @param atomic Pointer to atomic_ptr_t
 * @param buffer Output buffer for formatted timing info
 * @param size Buffer size
 * @return Number of bytes written (0 if never accessed)
 *
 * Used by --sync-state to display atomic operation timing and counts.
 * Only formats output if the atomic has been accessed.
 */
int debug_atomic_ptr_format_timing(const atomic_ptr_t *atomic, char *buffer, size_t size);

/**
 * @brief Lifecycle initialization for debug atomic tracking
 */
void debug_atomic_init(void);

/**
 * @brief Lifecycle shutdown for debug atomic tracking
 */
void debug_atomic_shutdown(void);

/**
 * @brief Check if debug atomic tracking is initialized
 */
bool debug_atomic_is_initialized(void);

/**
 * @brief Print all named atomic operations state (integration with --sync-state)
 *
 * Called by debug_sync_print_state() to include atomic operations in sync state output.
 */
void debug_atomic_print_state(void);

#ifdef __cplusplus
}
#endif

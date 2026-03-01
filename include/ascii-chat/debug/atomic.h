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
struct atomic_ptr_t;

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
 * @brief Register an atomic_ptr_t in the debug named registry
 * @param a Pointer to atomic_ptr_t to register
 * @param name Human-readable name
 */
void debug_atomic_ptr_register(atomic_ptr_t *a, const char *name);

/**
 * @brief Unregister an atomic_ptr_t from the debug named registry
 * @param a Pointer to atomic_ptr_t to unregister
 */
void debug_atomic_ptr_unregister(atomic_ptr_t *a);

#ifdef __cplusplus
}
#endif

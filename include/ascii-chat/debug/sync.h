#pragma once

/**
 * @file sync.h
 * @brief 🔒 Synchronization primitive debugging (mutexes, rwlocks, condition variables)
 * @ingroup debug_sync
 * @addtogroup debug_sync
 * @{
 *
 * This module provides comprehensive synchronization debugging:
 * - Dynamic state inspection using named.c registry
 * - Timing information for all lock operations
 * - Zero-copy iteration through all registered primitives
 *
 * No internal collection overhead - queries named.c directly for current state.
 *
 * FEATURES:
 * =========
 * - Query timing info for any named mutex, rwlock, or cond
 * - Print all named mutexes with their timing state
 * - Print all named rwlocks with their timing state
 * - Print all named condition variables with their timing state
 * - Combined state view (debug_sync_print_state)
 *
 * USAGE:
 * ======
 * 1. Call debug_sync_print_mutex_state() to see all mutex timings
 * 2. Call debug_sync_print_rwlock_state() to see all rwlock timings
 * 3. Call debug_sync_print_cond_state() to see all cond timings
 * 4. Call debug_sync_print_state() to see everything at once
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Public API Functions - always available
// ============================================================================

/**
 * @brief Print timing state for all named mutexes
 * @ingroup debug_sync
 *
 * Iterates through named.c registry and prints timing information for each
 * registered mutex (last lock/unlock times).
 */
void debug_sync_print_mutex_state(void);

/**
 * @brief Print timing state for all named rwlocks
 * @ingroup debug_sync
 *
 * Iterates through named.c registry and prints timing information for each
 * registered rwlock (last read/write lock/unlock times).
 */
void debug_sync_print_rwlock_state(void);

/**
 * @brief Print timing state for all named condition variables
 * @ingroup debug_sync
 *
 * Iterates through named.c registry and prints timing information for each
 * registered condition variable (last wait/signal/broadcast times).
 */
void debug_sync_print_cond_state(void);

/**
 * @brief Print all synchronization primitive states (calls all three)
 * @ingroup debug_sync
 *
 * Comprehensive view: mutexes + rwlocks + condition variables
 */
void debug_sync_print_state(void);

// ============================================================================
// Legacy API - stubs for backward compatibility
// ============================================================================

/**
 * @brief Initialize debug synchronization system (no-op in new implementation)
 * @ingroup debug_sync
 */
int lock_debug_init(void);

/**
 * @brief Start debug thread (no-op in new implementation)
 * @ingroup debug_sync
 */
int lock_debug_start_thread(void);

/**
 * @brief Stop debug system (no-op in new implementation)
 * @ingroup debug_sync
 */
void lock_debug_destroy(void);

/**
 * @brief Clean up debug thread (no-op in new implementation)
 * @ingroup debug_sync
 */
void lock_debug_cleanup_thread(void);

/**
 * @brief Trigger lock state print (calls debug_sync_print_state)
 * @ingroup debug_sync
 */
void lock_debug_trigger_print(void);

/**
 * @brief Get lock statistics (no-op in new implementation)
 * @ingroup debug_sync
 */
void lock_debug_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held);

/**
 * @brief Print current lock state
 * @ingroup debug_sync
 */
void lock_debug_print_state(void);

#ifdef __cplusplus
}
#endif

/** @} */

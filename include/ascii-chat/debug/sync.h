#pragma once
#include <ascii-chat/common/error_codes.h>

/**
 * @file sync.h
 * @brief 🔒 Synchronization primitive debugging (mutexes, rwlocks, condition variables)
 * @ingroup debug_sync
 * @addtogroup debug_sync
 * @{
 *
 * This module provides comprehensive synchronization state inspection and debugging:
 * - Dynamic state inspection using named.c registry (see debug/named.h)
 * - Timing information for all lock operations (last lock/unlock times)
 * - Zero-copy iteration through all registered primitives
 * - Scheduled delayed printing for capture during critical sections
 *
 * No internal collection overhead - queries named.c directly for live state,
 * making it safe to call even in tight loops without performance penalty.
 *
 * ## Purpose
 *
 * Synchronization debugging answers critical questions:
 * - **Development**: Which locks are blocking? Which have stale holders?
 * - **Production**: Is a deadlock happening? How long are acquisitions taking?
 * - **Testing**: Are multiple threads contending properly? What's the lock order?
 *
 * Useful for:
 * - Detecting deadlocks and lock inversions
 * - Identifying contention bottlenecks
 * - Validating thread safety assumptions
 * - Production debugging with minimal overhead
 *
 * ## Key Features
 *
 * - **Zero-overhead queries**: No collection threads, direct state inspection
 * - **Named primitive support**: Works with any primitive registered via named.h
 * - **Timing snapshots**: Last lock/unlock times for contention analysis
 * - **Comprehensive views**: Print all mutexes, rwlocks, and condition variables
 * - **Delayed reporting**: Schedule state capture during specific execution phases
 * - **Thread-safe printing**: Can be called from signal handlers or debug threads
 *
 * ## Integration with Named Registry
 *
 * Synchronization primitives are identified by their names in the named.c registry:
 * @code
 * // In your code:
 * mutex_t recv_lock;
 * mutex_init(&recv_lock);
 * NAMED_REGISTER(&recv_lock, "recv", "mutex");  // See debug/named.h
 *
 * // Later, in debug output:
 * debug_sync_print_state();  // Prints "recv.1 (mutex)" with timing
 * @endcode
 *
 * @see debug/named.h - The registry that backs this module
 *
 * ## Usage Examples
 *
 * ### Development: Print Current Lock State
 *
 * @code
 * // When you suspect a deadlock or lock contention:
 * debug_sync_print_state();  // Prints all mutexes, rwlocks, conds with timing
 * @endcode
 *
 * ### Production: Periodic State Dumps
 *
 * @code
 * // In a signal handler (e.g., SIGUSR2):
 * void handle_debug_signal(int sig) {
 *     debug_stats_trigger_print();  // Schedules print on debug thread
 * }
 *
 * // In main:
 * debug_stats_init();
 * debug_sync_start_thread();
 * signal(SIGUSR2, handle_debug_signal);  // SIGUSR2 is mapped to sync state printing
 * // Now: kill -USR2 <pid> triggers state dump in logs
 * @endcode
 *
 * ### Testing: Capture State at Specific Moments
 *
 * @code
 * // Schedule a state dump after 100ms (during critical section):
 * debug_sync_print_state_delayed(100 * 1000000);  // 100ms in nanoseconds
 * // State will print automatically on debug thread
 * @endcode
 *
 * ### Backtrace on Lock Contention
 *
 * @code
 * // Combined with backtrace:
 * debug_sync_print_state();         // See which locks are held
 * debug_sync_print_backtrace_delayed(50 * 1000000);  // Get stacks after 50ms
 * @endcode
 *
 * ## Output Format
 *
 * Typical output:
 * ```
 * === Mutex State ===
 * recv.1 (mutex) @ lib/network/socket.c:42:socket_create()
 *   Last lock:   2026-02-23T17:45:23.123456789Z (123ms ago)
 *   Last unlock: 2026-02-23T17:45:23.200000000Z (45ms ago)
 *   Status: free
 *
 * send.2 (mutex) @ lib/network/socket.c:89:socket_send()
 *   Last lock:   2026-02-23T17:45:23.195000000Z (50ms ago)
 *   Last unlock: (never)
 *   Status: HELD (potential deadlock!)
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 *
 * @} */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Public API Functions - always available
// ============================================================================

/**
 * @brief Print timing state for all named mutexes to stdout/log
 * @ingroup debug_sync
 *
 * Queries the named.c registry and prints a table of all registered mutexes
 * with their timing information:
 * - Name and registration location
 * - Last lock time (with elapsed time)
 * - Last unlock time (with elapsed time)
 * - Current status (held/free)
 *
 * Useful for detecting:
 * - Mutex deadlocks (HELD locks that should be released)
 * - Lock starvation (locks never acquired)
 * - Excessive lock contention (very frequent lock/unlock cycles)
 *
 * @note Thread-safe: can be called from any thread or signal handler
 * @note No blocking: reads only, doesn't acquire locks
 * @note Zero overhead in release builds if no mutexes are named
 *
 * @see NAMED_REGISTER() in debug/named.h to register mutexes for tracking
 */
void debug_sync_print_mutex_state(void);

/**
 * @brief Print timing state for all named read-write locks to stdout/log
 * @ingroup debug_sync
 *
 * Queries the named.c registry and prints a table of all registered rwlocks
 * with their timing information:
 * - Name and registration location
 * - Last read-lock time (with elapsed time)
 * - Last write-lock time (with elapsed time)
 * - Last unlock time (with elapsed time)
 * - Current status (read-held/write-held/free)
 *
 * Useful for detecting:
 * - RWlock writer starvation (reads blocking writers)
 * - Asymmetric lock patterns (only readers or only writers)
 * - Writer lock deadlocks (HELD write locks)
 *
 * @note Thread-safe: can be called from any thread or signal handler
 * @note No blocking: reads only, doesn't acquire locks
 *
 * @see NAMED_REGISTER() in debug/named.h to register rwlocks for tracking
 */
void debug_sync_print_rwlock_state(void);

/**
 * @brief Print timing state for all named condition variables to stdout/log
 * @ingroup debug_sync
 *
 * Queries the named.c registry and prints a table of all registered condition
 * variables with their timing information:
 * - Name and registration location
 * - Last wait time (with elapsed time)
 * - Last signal time (with elapsed time)
 * - Last broadcast time (with elapsed time)
 *
 * Useful for detecting:
 * - Condition variable deadlocks (waiters never signaled)
 * - Signal storms (excessive signaling)
 * - Lost wakeups (signals without corresponding waiters)
 *
 * @note Thread-safe: can be called from any thread or signal handler
 * @note No blocking: reads only, doesn't acquire locks
 *
 * @see NAMED_REGISTER() in debug/named.h to register condition variables
 */
void debug_sync_print_cond_state(void);

/**
 * @brief Print all synchronization primitive states at once
 * @ingroup debug_sync
 *
 * Comprehensive debugging view that combines output from:
 * 1. debug_sync_print_mutex_state()
 * 2. debug_sync_print_rwlock_state()
 * 3. debug_sync_print_cond_state()
 *
 * This is the starting point for understanding overall lock contention and state.
 * Use individual _print_*_state() functions if you only need specific primitive types.
 *
 * ## Typical Workflow
 *
 * @code
 * // In debugger or signal handler:
 * debug_sync_print_state();  // See all sync primitives at once
 * // Then examine the output for:
 * // - Any HELD locks that should be free
 * // - Asymmetric patterns (locks acquired but never released)
 * // - Timestamps that suggest contention or deadlock
 * @endcode
 *
 * @note Thread-safe: can be called from any thread or signal handler
 * @note Useful in production with signal handlers for "give me the state now"
 *
 * @see debug_sync_print_mutex_state()
 * @see debug_sync_print_rwlock_state()
 * @see debug_sync_print_cond_state()
 */
void debug_sync_print_state(void);

#include <ascii-chat/debug/stats.h>

/**
 * @brief Get synchronization statistics
 * @param total_acquired Pointer to store total lock acquisitions (can be NULL)
 * @param total_released Pointer to store total lock releases (can be NULL)
 * @param currently_held Pointer to store currently held locks (can be NULL)
 * @ingroup debug_sync
 *
 * Retrieves global statistics about synchronization primitive usage.
 * Any parameter can be NULL if that statistic isn't needed.
 *
 * Useful for:
 * - Monitoring lock contention trends
 * - Validating symmetric lock/unlock behavior
 * - Detecting lock leaks (acquired != released)
 *
 * ## Example Usage
 *
 * @code
 * uint64_t acquired, released;
 * uint32_t held;
 *
 * debug_sync_get_stats(&acquired, &released, &held);
 *
 * if (acquired == released && held == 0) {
 *     log_info("No lock leaks: %lu acquisitions, all released", acquired);
 * } else if (held > 0) {
 *     log_warn("Possible deadlock: %u locks currently held", held);
 * }
 * @endcode
 *
 * @note Atomically reads global counters, no locking required
 * @note Statistics accumulate over program lifetime
 * @note Useful for periodic health checks or monitoring
 */
void debug_sync_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held);

/**
 * @brief Check all condition variables for potential deadlocks
 * @ingroup debug_sync
 *
 * Scans all registered condition variables and logs warnings for any that
 * have threads waiting without being signaled for longer than 5 seconds.
 *
 * For each stuck condition variable, logs:
 * - Number of waiting threads and elapsed wait time
 * - Callsite where wait was entered (file, line, function)
 * - Associated mutex status (held by whom, or free)
 *
 * Called periodically by the debug thread (every 100ms), so detection latency
 * is at most 5 seconds + 100ms for stuck conditions.
 *
 * ## Output Example
 *
 * @code
 * [WARN] Stuck cond 'audio_send_queue_cond.0': 1 thread(s) waiting 66s with no signal (most recent waiter:
 * audio_sender.0) [WARN]   wait entered at src/client/audio.c:291 audio_sender_thread_func() [WARN]   associated mutex
 * is FREE — producer is not calling cond_signal
 * @endcode
 *
 * @note Thread-safe: can be called from any thread
 * @note No blocking: reads only, doesn't acquire locks
 * @note Called automatically by debug thread; only call manually for immediate checks
 *
 * @see debug_sync_start_thread() to ensure periodic checking
 */
void debug_sync_check_cond_deadlocks(void);

// Independent worker for synchronization monitoring and reports.
asciichat_error_t debug_sync_init(void);
asciichat_error_t debug_sync_start_thread(void);
void debug_sync_cleanup_thread(void);
void debug_sync_destroy(void);
void debug_sync_final_cleanup(void);
bool debug_sync_is_cleanup_in_progress(void);
void debug_sync_trigger_print(void);
void debug_sync_print_state_delayed(uint64_t delay_ns);
void debug_sync_print_backtrace_delayed(uint64_t delay_ns);
void debug_sync_print(void);
void debug_sync_poll(void);


#ifdef __cplusplus
}
#endif

/** @} */

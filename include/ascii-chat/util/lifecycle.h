#ifndef ASCIICHAT_UTIL_LIFECYCLE_H
#define ASCIICHAT_UTIL_LIFECYCLE_H

#include <stdatomic.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file lifecycle.h
 * @brief Lock-free module lifecycle state machine using pure stdatomic.
 *
 * Provides a standardized API for module-level init/shutdown synchronization
 * across all modules. Uses a 4-state machine (UNINITIALIZED, INITIALIZING,
 * INITIALIZED, DEAD) to coordinate concurrent initialization and permanent
 * shutdown with zero mutex overhead.
 *
 * Typical usage (concurrent init):
 *   static lifecycle_t g_module_lc = LIFECYCLE_INIT;
 *
 *   bool module_init(void) {
 *       if (!lifecycle_init_once(&g_module_lc)) return false;
 *       // do actual init work
 *       if (init_failed) {
 *           lifecycle_init_abort(&g_module_lc);  // allow retry
 *           return false;
 *       }
 *       lifecycle_init_commit(&g_module_lc);  // mark ready
 *       return true;
 *   }
 *
 * Typical usage (non-concurrent, single-threaded startup):
 *   if (!lifecycle_init(&g_module_lc)) return false;  // already initialized
 *   // do actual init work
 */

typedef enum {
    LIFECYCLE_UNINITIALIZED = 0,   ///< Not yet initialized (zero = default)
    LIFECYCLE_INITIALIZING  = 1,   ///< init_once winner in progress; losers spin
    LIFECYCLE_INITIALIZED   = 2,   ///< Ready to use
    LIFECYCLE_DEAD          = 3,   ///< Permanently shut down; no re-init
} lifecycle_state_t;

/**
 * Lock-free module lifecycle state machine.
 * Single atomic int, zero-initializable.
 */
typedef struct {
    _Atomic int state;   ///< lifecycle_state_t enum value
} lifecycle_t;

/// Static initializer for module-global lifecycle variables
#define LIFECYCLE_INIT { .state = LIFECYCLE_UNINITIALIZED }

/**
 * Non-locking CAS-based initialization: UNINIT → INITIALIZED.
 *
 * @param lc lifecycle state
 * @return true if THIS caller won the race and should do init work
 * @return false if already INITIALIZED, INITIALIZING, or DEAD
 *
 * Suitable for single-threaded startup or contexts where the caller
 * guarantees serialization. No spinning, no mutex.
 */
bool lifecycle_init(lifecycle_t *lc);

/**
 * Lock-free concurrent initialization: CAS UNINIT → INITIALIZING.
 *
 * Winner receives true and must complete the two-phase sequence:
 *   1. Do actual init work
 *   2. Call lifecycle_init_commit() on success or lifecycle_init_abort() on failure
 *
 * Losing callers spin on the transient INITIALIZING state until the winner commits,
 * then return false without doing work.
 *
 * @param lc lifecycle state
 * @return true if THIS caller won the init race and should do work
 * @return false if init already happened (or is in progress) or module is DEAD
 *
 * Safe for concurrent callers. Exactly one caller gets true (the CAS winner).
 * Other concurrent callers spin until the winner moves to INITIALIZED or DEAD.
 */
bool lifecycle_init_once(lifecycle_t *lc);

/**
 * Commit successful initialization: INITIALIZING → INITIALIZED.
 * Call this after lifecycle_init_once() returns true and init work succeeds.
 *
 * @param lc lifecycle state
 *
 * Wakes all spinners waiting in lifecycle_init_once(). Must be called by the
 * lifecycle_init_once() winner after successful init work.
 */
void lifecycle_init_commit(lifecycle_t *lc);

/**
 * Abort failed initialization: INITIALIZING → UNINITIALIZED.
 * Call this if lifecycle_init_once() returns true but init work fails.
 *
 * @param lc lifecycle state
 *
 * Allows the next caller to retry initialization. Wakes spinners.
 */
void lifecycle_init_abort(lifecycle_t *lc);

/**
 * Regular shutdown: INITIALIZED → UNINITIALIZED.
 *
 * @param lc lifecycle state
 * @return true if THIS caller should do shutdown work
 * @return false if already UNINITIALIZED or DEAD
 *
 * Allows re-initialization after shutdown (unlike shutdown_forever).
 * Non-locking CAS operation.
 */
bool lifecycle_shutdown(lifecycle_t *lc);

/**
 * Permanent shutdown: any non-DEAD → DEAD.
 *
 * @param lc lifecycle state
 * @return true if module was INITIALIZED and caller should do shutdown work
 * @return false if already DEAD
 *
 * Once DEAD, future init/init_once calls always return false.
 * Spins if called while init_once is in progress (INITIALIZING state).
 * Used for modules that must never be re-initialized.
 */
bool lifecycle_shutdown_forever(lifecycle_t *lc);

/**
 * Query: is module in INITIALIZED state?
 *
 * @param lc lifecycle state
 * @return true if state == LIFECYCLE_INITIALIZED
 *
 * Load-only, no side effects. Safe to call from any thread.
 */
bool lifecycle_is_initialized(const lifecycle_t *lc);

/**
 * Query: is module in DEAD state?
 *
 * @param lc lifecycle state
 * @return true if state == LIFECYCLE_DEAD
 *
 * Load-only, no side effects. Safe to call from any thread.
 */
bool lifecycle_is_dead(const lifecycle_t *lc);

#ifdef __cplusplus
}
#endif

#endif /* ASCIICHAT_UTIL_LIFECYCLE_H */

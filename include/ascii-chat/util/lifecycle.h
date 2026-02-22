#ifndef ASCIICHAT_UTIL_LIFECYCLE_H
#define ASCIICHAT_UTIL_LIFECYCLE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <ascii-chat/platform/mutex.h>

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

/**
 * @defgroup lifecycle_sync Lifecycle with Sync Primitives
 * @brief Combined lifecycle + sync primitive initialization and shutdown.
 *
 * These functions handle both the lifecycle state machine AND the sync primitive
 * (mutex/rwlock) initialization/destruction in one atomic operation. This consolidates
 * init/shutdown logic into a single call.
 *
 * Example (mutex):
 *   static lifecycle_t g_lc = LIFECYCLE_INIT;
 *   static mutex_t g_mutex;
 *
 *   if (lifecycle_init_with_mutex(&g_lc, &g_mutex, "my_mutex")) {
 *       // do init work
 *   }
 *
 *   if (lifecycle_shutdown_with_mutex(&g_lc, &g_mutex)) {
 *       // do shutdown work
 *   }
 * @{
 */

/**
 * CAS-based initialization: init lifecycle and mutex together.
 *
 * @param lc lifecycle state
 * @param mutex sync primitive to initialize
 * @param name name tag for the mutex (for debugging)
 * @return true if THIS caller won and should do init work; mutex is initialized
 * @return false if already initialized or DEAD; mutex left untouched
 *
 * Atomically:
 *   1. Check if lifecycle needs init (CAS UNINIT → INITIALIZED)
 *   2. If winner: call mutex_init(mutex, name)
 *   3. Return true if init succeeded
 */
bool lifecycle_init_with_mutex(lifecycle_t *lc, mutex_t *mutex, const char *name);

/**
 * CAS-based shutdown: shutdown lifecycle and destroy mutex together.
 *
 * @param lc lifecycle state
 * @param mutex sync primitive to destroy
 * @return true if THIS caller won and should do shutdown work; mutex is destroyed
 * @return false if already shutdown or DEAD; mutex left untouched
 *
 * Atomically:
 *   1. Check if lifecycle needs shutdown (CAS INITIALIZED → UNINITIALIZED)
 *   2. If winner: call mutex_destroy(mutex)
 *   3. Return true if shutdown succeeded
 */
bool lifecycle_shutdown_with_mutex(lifecycle_t *lc, mutex_t *mutex);

/**
 * CAS-based initialization: init lifecycle and rwlock together.
 *
 * @param lc lifecycle state
 * @param rwlock sync primitive to initialize
 * @param name name tag for the rwlock (for debugging)
 * @return true if THIS caller won and should do init work; rwlock is initialized
 * @return false if already initialized or DEAD; rwlock left untouched
 */
bool lifecycle_init_with_rwlock(lifecycle_t *lc, rwlock_t *rwlock, const char *name);

/**
 * CAS-based shutdown: shutdown lifecycle and destroy rwlock together.
 *
 * @param lc lifecycle state
 * @param rwlock sync primitive to destroy
 * @return true if THIS caller won and should do shutdown work; rwlock is destroyed
 * @return false if already shutdown or DEAD; rwlock left untouched
 */
bool lifecycle_shutdown_with_rwlock(lifecycle_t *lc, rwlock_t *rwlock);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ASCIICHAT_UTIL_LIFECYCLE_H */

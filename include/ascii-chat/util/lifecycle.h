#pragma once

#include <ascii-chat/atomic.h>
#include <stdbool.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/rwlock.h>

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
  LIFECYCLE_UNINITIALIZED = 0, ///< Not yet initialized (zero = default)
  LIFECYCLE_INITIALIZING = 1,  ///< init_once winner in progress; losers spin
  LIFECYCLE_INITIALIZED = 2,   ///< Ready to use
  LIFECYCLE_DEAD = 3,          ///< Permanently shut down; no re-init
  LIFECYCLE_DESTROYING = 4,    ///< destroy_once winner in progress; losers skip
} lifecycle_state_t;

typedef enum {
  LIFECYCLE_SYNC_NONE = 0,   ///< No sync primitive
  LIFECYCLE_SYNC_MUTEX = 1,  ///< Contains mutex_t pointer
  LIFECYCLE_SYNC_RWLOCK = 2, ///< Contains rwlock_t pointer
} lifecycle_sync_type_t;

/**
 * Lock-free module lifecycle state machine with optional sync primitive.
 * Combines init/shutdown state with mutex or rwlock initialization.
 */
typedef struct {
  atomic_t state;               ///< lifecycle_state_t enum value
  lifecycle_sync_type_t sync_type; ///< Type of sync primitive (if any)
  union {
    mutex_t *mutex;   ///< Pointer to mutex (if sync_type == LIFECYCLE_SYNC_MUTEX)
    rwlock_t *rwlock; ///< Pointer to rwlock (if sync_type == LIFECYCLE_SYNC_RWLOCK)
  } sync;
} lifecycle_t;

/// Static initializer for module-global lifecycle variables (no sync primitive)
#define LIFECYCLE_INIT                                                                                                 \
  {                                                                                                                    \
    .state = {.impl = LIFECYCLE_UNINITIALIZED}, .sync_type = LIFECYCLE_SYNC_NONE, .sync = {0}                          \
  }

/// Static initializer for lifecycle with mutex
#define LIFECYCLE_INIT_MUTEX(m)                                                                                        \
  {                                                                                                                    \
    .state = {.impl = LIFECYCLE_UNINITIALIZED}, .sync_type = LIFECYCLE_SYNC_MUTEX, .sync = {.mutex = (m) }             \
  }

/// Static initializer for lifecycle with rwlock
#define LIFECYCLE_INIT_RWLOCK(r)                                                                                       \
  {                                                                                                                    \
    .state = {.impl = LIFECYCLE_UNINITIALIZED}, .sync_type = LIFECYCLE_SYNC_RWLOCK, .sync = {.rwlock = (r) }           \
  }

/**
 * CAS-based initialization: UNINIT → INITIALIZED.
 *
 * @param lc lifecycle state (may include sync_type and sync pointer)
 * @param name name tag for any sync primitive (for debugging), or NULL if no sync
 * @return true if THIS caller won the race and should do init work
 * @return false if already INITIALIZED, INITIALIZING, or DEAD
 *
 * Suitable for single-threaded startup or contexts where the caller
 * guarantees serialization. If lc->sync_type != SYNC_NONE, initializes the sync primitive.
 */
bool lifecycle_init(lifecycle_t *lc, const char *name);

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
 * @param lc lifecycle state (may include sync_type and sync pointer)
 * @return true if THIS caller should do shutdown work
 * @return false if already UNINITIALIZED or DEAD
 *
 * Allows re-initialization after shutdown (unlike shutdown_forever).
 * If lc->sync_type != SYNC_NONE, destroys the sync primitive.
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
 * Reset initialized module: INITIALIZED → UNINITIALIZED.
 *
 * @param lc lifecycle state (may include sync_type and sync pointer)
 * @return true if THIS caller should do reset work
 * @return false if not INITIALIZED or in DEAD state
 *
 * Allows re-initialization after reset (like shutdown, but keeps sync primitives).
 * If lc->sync_type != SYNC_NONE, destroys the sync primitive (will be recreated on next init).
 * Used for modules that support reset/reinit cycles (e.g., client crypto reconnect).
 */
bool lifecycle_reset(lifecycle_t *lc);

/**
 * Lock-free concurrent destruction: CAS INITIALIZED → DESTROYING.
 *
 * Winner receives true and must complete the two-phase sequence:
 *   1. Do actual destroy work
 *   2. Call lifecycle_destroy_commit() on completion
 *
 * Losing callers return false and should skip cleanup work. Once destruction begins,
 * no further operations are possible until the winner completes.
 *
 * @param lc lifecycle state
 * @return true if THIS caller won the destroy race and should do work
 * @return false if not INITIALIZED, already being destroyed, or in DEAD state
 *
 * Safe for concurrent callers. Exactly one caller gets true (the CAS winner).
 * Prevents double-join and other double-cleanup issues when multiple threads
 * call cleanup functions concurrently.
 */
bool lifecycle_destroy_once(lifecycle_t *lc);

/**
 * Commit successful destruction: DESTROYING → UNINITIALIZED.
 * Call this after lifecycle_destroy_once() returns true and destroy work completes.
 *
 * @param lc lifecycle state
 *
 * Returns the module to UNINITIALIZED state, allowing future re-initialization.
 */
void lifecycle_destroy_commit(lifecycle_t *lc);

/**
 * @defgroup lifecycle_sync Lifecycle with Sync Primitives
 * @brief Combined lifecycle + sync primitive initialization and shutdown.
 *
 * When lifecycle_t.sync_type is set (e.g., LIFECYCLE_SYNC_MUTEX), the lifecycle
 * functions automatically initialize/destroy the associated sync primitive.
 * No separate function calls needed - one if statement in one place handles all types.
 *
 * Example (mutex):
 *   static mutex_t g_mutex;
 *   static lifecycle_t g_lc = LIFECYCLE_INIT_MUTEX(&g_mutex);
 *
 *   if (lifecycle_init(&g_lc, "my_mutex")) {
 *       // Mutex is initialized, do init work
 *   }
 *
 *   if (lifecycle_shutdown(&g_lc)) {
 *       // Mutex is destroyed, do shutdown work
 *   }
 * @}
 */

#ifdef __cplusplus
}
#endif

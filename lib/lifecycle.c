/**
 * @file lifecycle.c
 * @ingroup core
 * @brief Lock-free module lifecycle state machine implementation
 *
 * Implements concurrent init/shutdown synchronization using atomic compare-and-swap.
 * See include/ascii-chat/util/lifecycle.h for API documentation and usage patterns.
 */

#include "ascii-chat/util/lifecycle.h"
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/atomic.h>

static _Thread_local bool g_lifecycle_log_in_progress = false;

#define lifecycle_log_dev(...)                                                                                         \
  do {                                                                                                                 \
    if (!g_lifecycle_log_in_progress) {                                                                                \
      g_lifecycle_log_in_progress = true;                                                                              \
      log_dev(__VA_ARGS__);                                                                                            \
      g_lifecycle_log_in_progress = false;                                                                             \
    }                                                                                                                  \
  } while (0)

bool lifecycle_init(lifecycle_t *lc, const char *name) {
  if (lc == NULL) {
    lifecycle_log_dev("NULL lifecycle pointer");
    return false;
  }
  uint64_t expected = LIFECYCLE_UNINITIALIZED;
  if (!atomic_cas_u64(&lc->state, &expected, LIFECYCLE_INITIALIZED)) {
    lifecycle_log_dev("%s already initialized (current state: %llu)", name ? name : "<unnamed>", expected);
    return false; // Already initialized or in INITIALIZING/DEAD state
  }

  /* Winner: initialize sync primitive if configured */
  if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
    lifecycle_log_dev("%s initializing mutex", name ? name : "<unnamed>");
    mutex_init(lc->sync.mutex, name);
  } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
    lifecycle_log_dev("%s initializing rwlock", name ? name : "<unnamed>");
    rwlock_init(lc->sync.rwlock, name);
  } else {
    lifecycle_log_dev("%s initialized (no sync primitive)", name ? name : "<unnamed>");
  }

  return true;
}

bool lifecycle_init_once(lifecycle_t *lc) {
  if (lc == NULL) {
    lifecycle_log_dev("NULL lifecycle pointer");
    return false;
  }

  uint64_t expected = LIFECYCLE_UNINITIALIZED;
  if (!atomic_cas_u64(&lc->state, &expected, LIFECYCLE_INITIALIZING)) {
    // If already initialized, just return false (no work needed)
    if (expected == LIFECYCLE_INITIALIZED) {
      lifecycle_log_dev("already initialized");
      return false;
    }

    // If dead, never allow re-init
    if (expected == LIFECYCLE_DEAD) {
      lifecycle_log_dev("module is dead, no re-init allowed");
      return false;
    }

    // If initializing, don't spin (caller may be retrying or it may complete asynchronously)
    // Just return false to indicate this thread doesn't need to do init work
    if (expected == LIFECYCLE_INITIALIZING) {
      lifecycle_log_dev("already initializing, skipping (will resolve asynchronously)");
      return false;
    }

    // Unexpected state
    lifecycle_log_dev("unexpected state: %d", expected);
    return false;
  }

  // Winner: state is now LIFECYCLE_INITIALIZING
  // Caller must call lifecycle_init_commit() or lifecycle_init_abort()
  lifecycle_log_dev("won CAS, transitioned to INITIALIZING");
  return true;
}

void lifecycle_init_commit(lifecycle_t *lc) {
  if (lc == NULL) {
    lifecycle_log_dev("NULL lifecycle pointer");
    return;
  }
  lifecycle_log_dev("transitioning INITIALIZING → INITIALIZED");
  atomic_store_u64(&lc->state, LIFECYCLE_INITIALIZED);
}

void lifecycle_init_abort(lifecycle_t *lc) {
  if (lc == NULL) {
    lifecycle_log_dev("NULL lifecycle pointer");
    return;
  }
  lifecycle_log_dev("transitioning INITIALIZING → UNINITIALIZED (retry allowed)");
  atomic_store_u64(&lc->state, LIFECYCLE_UNINITIALIZED);
}

bool lifecycle_shutdown(lifecycle_t *lc) {
  if (lc == NULL) {
    lifecycle_log_dev("NULL lifecycle pointer");
    return false;
  }
  uint64_t expected = LIFECYCLE_INITIALIZED;
  if (!atomic_cas_u64(&lc->state, &expected, LIFECYCLE_UNINITIALIZED)) {
    lifecycle_log_dev("not in INITIALIZED state (current: %d)", expected);
    return false; // Not initialized or in unexpected state
  }

  /* Winner: destroy sync primitive if configured */
  if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
    lifecycle_log_dev("destroying mutex");
    mutex_destroy(lc->sync.mutex);
  } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
    lifecycle_log_dev("destroying rwlock");
    rwlock_destroy(lc->sync.rwlock);
  } else {
    lifecycle_log_dev("completed (no sync primitive)");
  }

  return true;
}

bool lifecycle_shutdown_forever(lifecycle_t *lc) {
  if (lc == NULL) {
    log_dev("NULL lifecycle pointer");
    return false;
  }

  uint64_t current;
  do {
    current = atomic_load_u64(&lc->state);
    if (current == LIFECYCLE_DEAD) {
      log_dev("already DEAD");
      return false;
    }
    if (current == LIFECYCLE_INITIALIZING) {
      log_dev("spinning on INITIALIZING");
    }
  } while (current == LIFECYCLE_INITIALIZING || !atomic_cas_u64(&lc->state, &current, LIFECYCLE_DEAD));

  log_dev("transitioned to DEAD (was in state: %d)", current);
  return current == LIFECYCLE_INITIALIZED;
}

bool lifecycle_is_initialized(const lifecycle_t *lc) {
  if (lc == NULL)
    return false;
  return atomic_load_u64(&lc->state) == LIFECYCLE_INITIALIZED;
}

bool lifecycle_is_dead(const lifecycle_t *lc) {
  if (lc == NULL)
    return false;
  return atomic_load_u64(&lc->state) == LIFECYCLE_DEAD;
}

bool lifecycle_reset(lifecycle_t *lc) {
  if (lc == NULL) {
    log_dev("NULL lifecycle pointer");
    return false;
  }

  uint64_t expected = LIFECYCLE_INITIALIZED;
  if (!atomic_cas_u64(&lc->state, &expected, LIFECYCLE_UNINITIALIZED)) {
    log_dev("not in INITIALIZED state (current: %d)", expected);
    return false; // Not in INITIALIZED state
  }

  /* Winner: destroy sync primitive if configured (reset keeps same primitive) */
  if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
    log_dev("destroying and resetting mutex");
    mutex_destroy(lc->sync.mutex);
  } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
    log_dev("destroying and resetting rwlock");
    rwlock_destroy(lc->sync.rwlock);
  } else {
    log_dev("completed (no sync primitive, allows reinit)");
  }

  return true;
}

bool lifecycle_destroy_once(lifecycle_t *lc) {
  if (lc == NULL) {
    log_dev("NULL lifecycle pointer");
    return false;
  }

  uint64_t expected = LIFECYCLE_INITIALIZED;
  if (!atomic_cas_u64(&lc->state, &expected, LIFECYCLE_DESTROYING)) {
    // If not initialized, nothing to destroy
    if (expected == LIFECYCLE_UNINITIALIZED) {
      log_dev("already uninitialized, nothing to destroy");
      return false;
    }

    // If dead, never allow destruction (already permanently shut down)
    if (expected == LIFECYCLE_DEAD) {
      log_dev("module is dead, no destruction allowed");
      return false;
    }

    // If already destroying, don't duplicate work (return false, let first destroyer finish)
    if (expected == LIFECYCLE_DESTROYING) {
      log_dev("already destroying, skipping (first destroyer has priority)");
      return false;
    }

    // If initializing, skip destruction (init may still be in progress)
    if (expected == LIFECYCLE_INITIALIZING) {
      log_dev("still initializing, skipping destruction");
      return false;
    }

    // Unexpected state
    log_dev("unexpected state: %d", expected);
    return false;
  }

  // Winner: state is now LIFECYCLE_DESTROYING
  // Caller must call lifecycle_destroy_commit()
  log_dev("won CAS, transitioned to DESTROYING");
  return true;
}

void lifecycle_destroy_commit(lifecycle_t *lc) {
  if (lc == NULL) {
    log_dev("NULL lifecycle pointer");
    return;
  }
  log_dev("transitioning DESTROYING → UNINITIALIZED");
  atomic_store_u64(&lc->state, LIFECYCLE_UNINITIALIZED);
}

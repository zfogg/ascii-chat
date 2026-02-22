#include "ascii-chat/util/lifecycle.h"
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/log/logging.h>
#include <stdatomic.h>

bool lifecycle_init(lifecycle_t *lc, const char *name) {
    if (lc == NULL) {
        log_debug("[lifecycle] init: NULL lifecycle pointer");
        return false;
    }
    int expected = LIFECYCLE_UNINITIALIZED;
    if (!atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_INITIALIZED)) {
        log_debug("[lifecycle] init: %s already initialized (current state: %d)", name ? name : "<unnamed>", expected);
        return false; // Already initialized or in INITIALIZING/DEAD state
    }

    /* Winner: initialize sync primitive if configured */
    if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
        log_debug("[lifecycle] init: %s initializing mutex", name ? name : "<unnamed>");
        mutex_init(lc->sync.mutex, name);
    } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
        log_debug("[lifecycle] init: %s initializing rwlock", name ? name : "<unnamed>");
        rwlock_init(lc->sync.rwlock, name);
    } else {
        log_debug("[lifecycle] init: %s initialized (no sync primitive)", name ? name : "<unnamed>");
    }

    return true;
}

bool lifecycle_init_once(lifecycle_t *lc) {
    if (lc == NULL) {
        log_debug("[lifecycle] init_once: NULL lifecycle pointer");
        return false;
    }

    int expected = LIFECYCLE_UNINITIALIZED;
    if (!atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_INITIALIZING)) {
        // Spin until the INITIALIZING transient state resolves
        log_debug("[lifecycle] init_once: spinning on INITIALIZING state (current: %d)", expected);
        int s;
        do {
            s = atomic_load(&lc->state);
        } while (s == LIFECYCLE_INITIALIZING);
        log_debug("[lifecycle] init_once: spin complete, final state: %d", s);
        return false;
    }

    // Winner: state is now LIFECYCLE_INITIALIZING
    // Caller must call lifecycle_init_commit() or lifecycle_init_abort()
    log_debug("[lifecycle] init_once: won CAS, transitioned to INITIALIZING");
    return true;
}

void lifecycle_init_commit(lifecycle_t *lc) {
    if (lc == NULL) {
        log_debug("[lifecycle] init_commit: NULL lifecycle pointer");
        return;
    }
    log_debug("[lifecycle] init_commit: transitioning INITIALIZING → INITIALIZED");
    atomic_store(&lc->state, LIFECYCLE_INITIALIZED);
}

void lifecycle_init_abort(lifecycle_t *lc) {
    if (lc == NULL) {
        log_debug("[lifecycle] init_abort: NULL lifecycle pointer");
        return;
    }
    log_debug("[lifecycle] init_abort: transitioning INITIALIZING → UNINITIALIZED (retry allowed)");
    atomic_store(&lc->state, LIFECYCLE_UNINITIALIZED);
}

bool lifecycle_shutdown(lifecycle_t *lc) {
    if (lc == NULL) {
        log_debug("[lifecycle] shutdown: NULL lifecycle pointer");
        return false;
    }
    int expected = LIFECYCLE_INITIALIZED;
    if (!atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_UNINITIALIZED)) {
        log_debug("[lifecycle] shutdown: not in INITIALIZED state (current: %d)", expected);
        return false; // Not initialized or in unexpected state
    }

    /* Winner: destroy sync primitive if configured */
    if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
        log_debug("[lifecycle] shutdown: destroying mutex");
        mutex_destroy(lc->sync.mutex);
    } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
        log_debug("[lifecycle] shutdown: destroying rwlock");
        rwlock_destroy(lc->sync.rwlock);
    } else {
        log_debug("[lifecycle] shutdown: completed (no sync primitive)");
    }

    return true;
}

bool lifecycle_shutdown_forever(lifecycle_t *lc) {
    if (lc == NULL) {
        log_debug("[lifecycle] shutdown_forever: NULL lifecycle pointer");
        return false;
    }

    int current;
    do {
        current = atomic_load(&lc->state);
        if (current == LIFECYCLE_DEAD) {
            log_debug("[lifecycle] shutdown_forever: already DEAD");
            return false;
        }
        if (current == LIFECYCLE_INITIALIZING) {
            log_debug("[lifecycle] shutdown_forever: spinning on INITIALIZING");
        }
    } while (current == LIFECYCLE_INITIALIZING ||
             !atomic_compare_exchange_weak(&lc->state, &current, LIFECYCLE_DEAD));

    log_debug("[lifecycle] shutdown_forever: transitioned to DEAD (was in state: %d)", current);
    return current == LIFECYCLE_INITIALIZED;
}

bool lifecycle_is_initialized(const lifecycle_t *lc) {
    if (lc == NULL) return false;
    return atomic_load(&lc->state) == LIFECYCLE_INITIALIZED;
}

bool lifecycle_is_dead(const lifecycle_t *lc) {
    if (lc == NULL) return false;
    return atomic_load(&lc->state) == LIFECYCLE_DEAD;
}

bool lifecycle_reset(lifecycle_t *lc) {
    if (lc == NULL) {
        log_debug("[lifecycle] reset: NULL lifecycle pointer");
        return false;
    }

    int expected = LIFECYCLE_INITIALIZED;
    if (!atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_UNINITIALIZED)) {
        log_debug("[lifecycle] reset: not in INITIALIZED state (current: %d)", expected);
        return false; // Not in INITIALIZED state
    }

    /* Winner: destroy sync primitive if configured (reset keeps same primitive) */
    if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
        log_debug("[lifecycle] reset: destroying and resetting mutex");
        mutex_destroy(lc->sync.mutex);
    } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
        log_debug("[lifecycle] reset: destroying and resetting rwlock");
        rwlock_destroy(lc->sync.rwlock);
    } else {
        log_debug("[lifecycle] reset: completed (no sync primitive, allows reinit)");
    }

    return true;
}


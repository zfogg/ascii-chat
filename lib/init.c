#include "ascii-chat/util/lifecycle.h"
#include <ascii-chat/platform/mutex.h>
#include <stdatomic.h>

bool lifecycle_init(lifecycle_t *lc, const char *name) {
    if (lc == NULL) return false;
    int expected = LIFECYCLE_UNINITIALIZED;
    if (!atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_INITIALIZED)) {
        return false; // Already initialized or in INITIALIZING/DEAD state
    }

    /* Winner: initialize sync primitive if configured */
    if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
        mutex_init(lc->sync.mutex, name);
    } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
        rwlock_init(lc->sync.rwlock, name);
    }

    return true;
}

bool lifecycle_init_once(lifecycle_t *lc) {
    if (lc == NULL) return false;

    int expected = LIFECYCLE_UNINITIALIZED;
    if (!atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_INITIALIZING)) {
        // Spin until the INITIALIZING transient state resolves
        int s;
        do {
            s = atomic_load(&lc->state);
        } while (s == LIFECYCLE_INITIALIZING);
        return false;
    }

    // Winner: state is now LIFECYCLE_INITIALIZING
    // Caller must call lifecycle_init_commit() or lifecycle_init_abort()
    return true;
}

void lifecycle_init_commit(lifecycle_t *lc) {
    if (lc == NULL) return;
    atomic_store(&lc->state, LIFECYCLE_INITIALIZED);
}

void lifecycle_init_abort(lifecycle_t *lc) {
    if (lc == NULL) return;
    atomic_store(&lc->state, LIFECYCLE_UNINITIALIZED);
}

bool lifecycle_shutdown(lifecycle_t *lc) {
    if (lc == NULL) return false;
    int expected = LIFECYCLE_INITIALIZED;
    if (!atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_UNINITIALIZED)) {
        return false; // Not initialized or in unexpected state
    }

    /* Winner: destroy sync primitive if configured */
    if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
        mutex_destroy(lc->sync.mutex);
    } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
        rwlock_destroy(lc->sync.rwlock);
    }

    return true;
}

bool lifecycle_shutdown_forever(lifecycle_t *lc) {
    if (lc == NULL) return false;

    int current;
    do {
        current = atomic_load(&lc->state);
        if (current == LIFECYCLE_DEAD) return false;
    } while (current == LIFECYCLE_INITIALIZING ||
             !atomic_compare_exchange_weak(&lc->state, &current, LIFECYCLE_DEAD));

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
    if (lc == NULL) return false;

    int expected = LIFECYCLE_INITIALIZED;
    if (!atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_UNINITIALIZED)) {
        return false; // Not in INITIALIZED state
    }

    /* Winner: destroy sync primitive if configured (reset keeps same primitive) */
    if (lc->sync_type == LIFECYCLE_SYNC_MUTEX && lc->sync.mutex != NULL) {
        mutex_destroy(lc->sync.mutex);
    } else if (lc->sync_type == LIFECYCLE_SYNC_RWLOCK && lc->sync.rwlock != NULL) {
        rwlock_destroy(lc->sync.rwlock);
    }

    return true;
}


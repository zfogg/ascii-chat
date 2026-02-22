#include "ascii-chat/util/lifecycle.h"
#include <ascii-chat/platform/mutex.h>
#include <stdatomic.h>

bool lifecycle_init(lifecycle_t *lc) {
    if (lc == NULL) return false;
    int expected = LIFECYCLE_UNINITIALIZED;
    return atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_INITIALIZED);
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
    return atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_UNINITIALIZED);
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

/* ============================================================================
 * Lifecycle + Sync Primitive Wrappers
 * ============================================================================
 * These functions handle both the lifecycle state machine AND sync primitive
 * initialization/destruction in atomic operations.
 */

bool lifecycle_init_with_mutex(lifecycle_t *lc, mutex_t *mutex, const char *name) {
    if (lc == NULL || mutex == NULL) return false;

    if (lifecycle_init(lc)) {
        /* Winner of the CAS: initialize the mutex */
        mutex_init(mutex, name);
        return true;
    }

    /* Already initialized or DEAD: don't touch mutex */
    return false;
}

bool lifecycle_shutdown_with_mutex(lifecycle_t *lc, mutex_t *mutex) {
    if (lc == NULL || mutex == NULL) return false;

    if (lifecycle_shutdown(lc)) {
        /* Winner of the CAS: destroy the mutex */
        mutex_destroy(mutex);
        return true;
    }

    /* Already shutdown or DEAD: don't touch mutex */
    return false;
}

bool lifecycle_init_with_rwlock(lifecycle_t *lc, rwlock_t *rwlock, const char *name) {
    if (lc == NULL || rwlock == NULL) return false;

    if (lifecycle_init(lc)) {
        /* Winner of the CAS: initialize the rwlock */
        rwlock_init(rwlock, name);
        return true;
    }

    /* Already initialized or DEAD: don't touch rwlock */
    return false;
}

bool lifecycle_shutdown_with_rwlock(lifecycle_t *lc, rwlock_t *rwlock) {
    if (lc == NULL || rwlock == NULL) return false;

    if (lifecycle_shutdown(lc)) {
        /* Winner of the CAS: destroy the rwlock */
        rwlock_destroy(rwlock);
        return true;
    }

    /* Already shutdown or DEAD: don't touch rwlock */
    return false;
}

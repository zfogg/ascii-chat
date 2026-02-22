#include "ascii-chat/util/lifecycle.h"
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

#pragma once

/**
 * @file platform/init.h
 * @brief Platform initialization and static synchronization helpers
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides platform initialization functions and static initialization
 * helpers for synchronization primitives that need to work before main().
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include "platform/abstraction.h"

// ============================================================================
// Static Initialization Helpers
// ============================================================================

// Since Windows doesn't support static initialization of synchronization
// primitives the same way POSIX does, we need a different approach.
// We'll use a combination of static initialization and lazy initialization.

/**
 * @brief Static mutex structure for global mutexes requiring static initialization
 *
 * Provides lazy initialization for mutexes that need to work before main().
 * Windows doesn't support static initialization of synchronization primitives
 * the same way POSIX does, so this structure enables lazy initialization with
 * thread-safe detection.
 *
 * @note The initialized flag is checked atomically before first use to ensure
 *       the mutex is initialized exactly once.
 *
 * @ingroup platform
 */
typedef struct {
  /** @brief The actual mutex */
  mutex_t mutex;
#if PLATFORM_WINDOWS
  /** @brief Thread-safe initialization flag (Windows: LONG for InterlockedCompareExchange) */
  volatile LONG initialized;
#else
  /** @brief Thread-safe initialization flag (POSIX: int for atomic operations) */
  volatile int initialized;
#endif
} static_mutex_t;

/**
 * @brief Static reader-writer lock structure for global rwlocks requiring static initialization
 *
 * Provides lazy initialization for reader-writer locks that need to work before main().
 * Uses the same lazy initialization pattern as static_mutex_t for cross-platform
 * compatibility.
 *
 * @note The initialized flag is checked atomically before first use to ensure
 *       the rwlock is initialized exactly once.
 *
 * @ingroup platform
 */
typedef struct {
  /** @brief The actual reader-writer lock */
  rwlock_t lock;
#if PLATFORM_WINDOWS
  /** @brief Thread-safe initialization flag (Windows: LONG for InterlockedCompareExchange) */
  volatile LONG initialized;
#else
  /** @brief Thread-safe initialization flag (POSIX: int for atomic operations) */
  volatile int initialized;
#endif
} static_rwlock_t;

/**
 * @brief Static condition variable structure for global condition variables requiring static initialization
 *
 * Provides lazy initialization for condition variables that need to work before main().
 * Uses the same lazy initialization pattern as static_mutex_t for cross-platform
 * compatibility.
 *
 * @note The initialized flag is checked atomically before first use to ensure
 *       the condition variable is initialized exactly once.
 *
 * @ingroup platform
 */
typedef struct {
  /** @brief The actual condition variable */
  cond_t cond;
#if PLATFORM_WINDOWS
  /** @brief Thread-safe initialization flag (Windows: LONG for InterlockedCompareExchange) */
  volatile LONG initialized;
#else
  /** @brief Thread-safe initialization flag (POSIX: int for atomic operations) */
  volatile int initialized;
#endif
} static_cond_t;

// Initialization macros
// clang-format off
#if PLATFORM_WINDOWS
#define STATIC_MUTEX_INIT {{0}, 0}
#define STATIC_RWLOCK_INIT {{0}, 0}
#define STATIC_COND_INIT {{0}, 0}
#else
#define STATIC_MUTEX_INIT {PTHREAD_MUTEX_INITIALIZER, 1}
#define STATIC_RWLOCK_INIT {PTHREAD_RWLOCK_INITIALIZER, 1}
#define STATIC_COND_INIT {PTHREAD_COND_INITIALIZER, 1}
#endif
// clang-format on

// Lazy initialization functions
static inline void static_mutex_lock(static_mutex_t *m) {
#if PLATFORM_WINDOWS
  // Use InterlockedCompareExchange for thread-safe lazy init with proper synchronization
  // Pattern: First thread sets flag to 1, initializes, then sets to 2
  // Other threads spin until flag is 2 (fully initialized)
  LONG prev = InterlockedCompareExchange(&m->initialized, 1, 0);
  if (prev == 0) {
    // We won the race - initialize the mutex
    mutex_init(&m->mutex);
    // Memory barrier to ensure init is visible before flag update
    MemoryBarrier();
    InterlockedExchange(&m->initialized, 2);
  } else if (prev == 1) {
    // Another thread is initializing - spin until done
    while (InterlockedCompareExchange(&m->initialized, 2, 2) != 2) {
      YieldProcessor();
    }
  }
  // prev == 2 means already initialized, proceed directly
#endif
  mutex_lock(&m->mutex);
}

static inline void static_mutex_unlock(static_mutex_t *m) {
  mutex_unlock(&m->mutex);
}

static inline void static_rwlock_rdlock(static_rwlock_t *l) {
#if PLATFORM_WINDOWS
  LONG prev = InterlockedCompareExchange(&l->initialized, 1, 0);
  if (prev == 0) {
    rwlock_init(&l->lock);
    MemoryBarrier();
    InterlockedExchange(&l->initialized, 2);
  } else if (prev == 1) {
    while (InterlockedCompareExchange(&l->initialized, 2, 2) != 2) {
      YieldProcessor();
    }
  }
#endif
  rwlock_rdlock(&l->lock);
}

static inline void static_rwlock_wrlock(static_rwlock_t *l) {
#if PLATFORM_WINDOWS
  LONG prev = InterlockedCompareExchange(&l->initialized, 1, 0);
  if (prev == 0) {
    rwlock_init(&l->lock);
    MemoryBarrier();
    InterlockedExchange(&l->initialized, 2);
  } else if (prev == 1) {
    while (InterlockedCompareExchange(&l->initialized, 2, 2) != 2) {
      YieldProcessor();
    }
  }
#endif
  rwlock_wrlock(&l->lock);
}

static inline void static_cond_wait(static_cond_t *c, static_mutex_t *m) {
#if PLATFORM_WINDOWS
  LONG prev = InterlockedCompareExchange(&c->initialized, 1, 0);
  if (prev == 0) {
    cond_init(&c->cond);
    MemoryBarrier();
    InterlockedExchange(&c->initialized, 2);
  } else if (prev == 1) {
    while (InterlockedCompareExchange(&c->initialized, 2, 2) != 2) {
      YieldProcessor();
    }
  }
  if (InterlockedCompareExchange(&m->initialized, 1, 0) == 0) {
    mutex_init(&m->mutex);
  }
#endif
  cond_wait(&c->cond, &m->mutex);
}

static inline void static_cond_timedwait(static_cond_t *c, static_mutex_t *m, int timeout_ms) {
#if PLATFORM_WINDOWS
  LONG prev = InterlockedCompareExchange(&c->initialized, 1, 0);
  if (prev == 0) {
    cond_init(&c->cond);
    MemoryBarrier();
    InterlockedExchange(&c->initialized, 2);
  } else if (prev == 1) {
    while (InterlockedCompareExchange(&c->initialized, 2, 2) != 2) {
      YieldProcessor();
    }
  }
  if (InterlockedCompareExchange(&m->initialized, 1, 0) == 0) {
    mutex_init(&m->mutex);
  }
#endif
  cond_timedwait(&c->cond, &m->mutex, timeout_ms);
}

static inline void static_cond_signal(static_cond_t *c) {
#if PLATFORM_WINDOWS
  LONG prev = InterlockedCompareExchange(&c->initialized, 1, 0);
  if (prev == 0) {
    cond_init(&c->cond);
    MemoryBarrier();
    InterlockedExchange(&c->initialized, 2);
  } else if (prev == 1) {
    while (InterlockedCompareExchange(&c->initialized, 2, 2) != 2) {
      YieldProcessor();
    }
  }
#endif
  cond_signal(&c->cond);
}

static inline void static_cond_broadcast(static_cond_t *c) {
#if PLATFORM_WINDOWS
  LONG prev = InterlockedCompareExchange(&c->initialized, 1, 0);
  if (prev == 0) {
    cond_init(&c->cond);
    MemoryBarrier();
    InterlockedExchange(&c->initialized, 2);
  } else if (prev == 1) {
    while (InterlockedCompareExchange(&c->initialized, 2, 2) != 2) {
      YieldProcessor();
    }
  }
#endif
  cond_broadcast(&c->cond);
}

// ============================================================================
// Platform Initialization
// ============================================================================

// Call this once at program startup
asciichat_error_t platform_init(void);

// Call this at program exit
void platform_cleanup(void);

/** @} */

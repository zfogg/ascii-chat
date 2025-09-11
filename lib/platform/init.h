#ifndef PLATFORM_INIT_H
#define PLATFORM_INIT_H

#include "platform/abstraction.h"

// ============================================================================
// Static Initialization Helpers
// ============================================================================

// Since Windows doesn't support static initialization of synchronization
// primitives the same way POSIX does, we need a different approach.
// We'll use a combination of static initialization and lazy initialization.

// For global mutexes/locks that need static initialization
typedef struct {
  mutex_t mutex;
#if PLATFORM_WINDOWS
  volatile LONG initialized;
#else
  volatile int initialized;
#endif
} static_mutex_t;

typedef struct {
  rwlock_t lock;
#if PLATFORM_WINDOWS
  volatile LONG initialized;
#else
  volatile int initialized;
#endif
} static_rwlock_t;

typedef struct {
  cond_t cond;
#if PLATFORM_WINDOWS
  volatile LONG initialized;
#else
  volatile int initialized;
#endif
} static_cond_t;

// Initialization macros
#if PLATFORM_WINDOWS
#define STATIC_MUTEX_INIT {{0}, 0}
#define STATIC_RWLOCK_INIT {{0}, 0}
#define STATIC_COND_INIT {{0}, 0}
#else
#define STATIC_MUTEX_INIT {{PTHREAD_MUTEX_INITIALIZER}, 1}
#define STATIC_RWLOCK_INIT {{PTHREAD_RWLOCK_INITIALIZER}, 1}
#define STATIC_COND_INIT {{PTHREAD_COND_INITIALIZER}, 1}
#endif

// Lazy initialization functions
static inline void static_mutex_lock(static_mutex_t *m) {
#if PLATFORM_WINDOWS
  // Use InterlockedCompareExchange for thread-safe lazy init
  if (InterlockedCompareExchange(&m->initialized, 1, 0) == 0) {
    mutex_init(&m->mutex);
  }
#endif
  mutex_lock(&m->mutex);
}

static inline void static_mutex_unlock(static_mutex_t *m) {
  mutex_unlock(&m->mutex);
}

static inline void static_rwlock_rdlock(static_rwlock_t *l) {
#if PLATFORM_WINDOWS
  if (InterlockedCompareExchange(&l->initialized, 1, 0) == 0) {
    rwlock_init(&l->lock);
  }
#endif
  rwlock_rdlock(&l->lock);
}

static inline void static_rwlock_wrlock(static_rwlock_t *l) {
#if PLATFORM_WINDOWS
  if (InterlockedCompareExchange(&l->initialized, 1, 0) == 0) {
    rwlock_init(&l->lock);
  }
#endif
  rwlock_wrlock(&l->lock);
}

static inline void static_rwlock_unlock(static_rwlock_t *l) {
  rwlock_unlock(&l->lock);
}

static inline void static_cond_wait(static_cond_t *c, static_mutex_t *m) {
#if PLATFORM_WINDOWS
  if (InterlockedCompareExchange(&c->initialized, 1, 0) == 0) {
    cond_init(&c->cond);
  }
#endif
  cond_wait(&c->cond, &m->mutex);
}

static inline void static_cond_timedwait(static_cond_t *c, static_mutex_t *m, int timeout_ms) {
#if PLATFORM_WINDOWS
  if (InterlockedCompareExchange(&c->initialized, 1, 0) == 0) {
    cond_init(&c->cond);
  }
#endif
  cond_timedwait(&c->cond, &m->mutex, timeout_ms);
}

static inline void static_cond_signal(static_cond_t *c) {
#if PLATFORM_WINDOWS
  if (InterlockedCompareExchange(&c->initialized, 1, 0) == 0) {
    cond_init(&c->cond);
  }
#endif
  cond_signal(&c->cond);
}

static inline void static_cond_broadcast(static_cond_t *c) {
#if PLATFORM_WINDOWS
  if (InterlockedCompareExchange(&c->initialized, 1, 0) == 0) {
    cond_init(&c->cond);
  }
#endif
  cond_broadcast(&c->cond);
}

// ============================================================================
// Platform Initialization
// ============================================================================

// Call this once at program startup
static inline int platform_init(void) {
  int ret = 0;

#if PLATFORM_WINDOWS
  // Initialize Winsock
  ret = socket_init();
  if (ret != 0) {
    return ret;
  }

  // Enable ANSI escape sequences in console
  terminal_enable_ansi();

  // Set console to UTF-8
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#else
  // Install signal handlers for SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif

  return ret;
}

// Call this at program exit
static inline void platform_cleanup(void) {
#if PLATFORM_WINDOWS
  socket_cleanup();
#endif
}

#endif // PLATFORM_INIT_H
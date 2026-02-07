/**
 * @file platform/wasm/threading.c
 * @brief Threading abstraction for WASM/Emscripten using pthreads
 * @ingroup platform
 */

#include <ascii-chat/platform/abstraction.h>
#include <pthread.h>
#include <stdint.h>

// Emscripten provides pthread.h
// In single-threaded mode (USE_PTHREADS=0), these are no-ops
// In multi-threaded mode (USE_PTHREADS=1), these are real mutexes

int mutex_init(mutex_t *mutex) {
  return pthread_mutex_init(mutex, NULL);
}

int mutex_destroy(mutex_t *mutex) {
  return pthread_mutex_destroy(mutex);
}

int mutex_lock_impl(mutex_t *mutex) {
  return pthread_mutex_lock(mutex);
}

int mutex_trylock_impl(mutex_t *mutex) {
  return pthread_mutex_trylock(mutex);
}

int mutex_unlock_impl(mutex_t *mutex) {
  return pthread_mutex_unlock(mutex);
}

// Thread functions (not used in mirror mode, but provided for completeness)
int asciichat_thread_create(asciichat_thread_t *thread, void *(*start_routine)(void *), void *arg) {
  return pthread_create(thread, NULL, start_routine, arg);
}

int asciichat_thread_join(asciichat_thread_t *thread, void **retval) {
  return pthread_join(*thread, retval);
}

int asciichat_thread_detach(asciichat_thread_t *thread) {
  return pthread_detach(*thread);
}

asciichat_thread_t asciichat_thread_self(void) {
  return pthread_self();
}

int asciichat_thread_equal(asciichat_thread_t t1, asciichat_thread_t t2) {
  return pthread_equal(t1, t2);
}

// Read-write lock functions
int rwlock_init(rwlock_t *rwlock) {
  return pthread_rwlock_init(rwlock, NULL);
}

int rwlock_rdlock_impl(rwlock_t *rwlock) {
  return pthread_rwlock_rdlock(rwlock);
}

int rwlock_wrlock_impl(rwlock_t *rwlock) {
  return pthread_rwlock_wrlock(rwlock);
}

int rwlock_rdunlock_impl(rwlock_t *rwlock) {
  return pthread_rwlock_unlock(rwlock);
}

int rwlock_wrunlock_impl(rwlock_t *rwlock) {
  return pthread_rwlock_unlock(rwlock);
}

// Thread ID function
uint64_t asciichat_thread_current_id(void) {
  return (uint64_t)pthread_self();
}

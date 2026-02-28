/**
 * @file platform/wasm/threading.c
 * @brief Threading abstraction for WASM/Emscripten using pthreads
 * @ingroup platform
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/thread.h>
#include <pthread.h>
#include <stdint.h>

// Emscripten provides pthread.h but mutexes don't work correctly with pthreads in WASM
// The JS side is single-threaded, so we can safely make mutexes no-ops
// This avoids deadlocks from pthread_mutex_t not being properly initialized in WASM

int mutex_init(mutex_t *mutex, const char *name) {
  (void)mutex;
  (void)name;
  return 0; // Success
}

int mutex_destroy(mutex_t *mutex) {
  (void)mutex;
  return 0; // Success
}

int mutex_lock_impl(mutex_t *mutex) {
  (void)mutex;
  return 0; // Success - no-op
}

int mutex_trylock_impl(mutex_t *mutex) {
  (void)mutex;
  return 0; // Success - no-op
}

int mutex_unlock_impl(mutex_t *mutex) {
  (void)mutex;
  return 0; // Success - no-op
}

// Thread functions (not used in mirror mode, but provided for completeness)
int asciichat_thread_create(asciichat_thread_t *thread, const char *name, void *(*start_routine)(void *), void *arg) {
  (void)name;  // Unused in WASM - no thread registry
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
int rwlock_init(rwlock_t *rwlock, const char *name) {
  (void)name;
  return pthread_rwlock_init((pthread_rwlock_t *)rwlock, NULL);
}

int rwlock_rdlock_impl(rwlock_t *rwlock) {
  return pthread_rwlock_rdlock((pthread_rwlock_t *)rwlock);
}

int rwlock_wrlock_impl(rwlock_t *rwlock) {
  return pthread_rwlock_wrlock((pthread_rwlock_t *)rwlock);
}

int rwlock_rdunlock_impl(rwlock_t *rwlock) {
  return pthread_rwlock_unlock((pthread_rwlock_t *)rwlock);
}

int rwlock_wrunlock_impl(rwlock_t *rwlock) {
  return pthread_rwlock_unlock((pthread_rwlock_t *)rwlock);
}

// Thread ID function
uint64_t asciichat_thread_current_id(void) {
  return (uint64_t)pthread_self();
}

// Thread-local storage functions
int ascii_tls_key_create(tls_key_t *key, void (*destructor)(void *)) {
  return pthread_key_create(key, destructor);
}

int ascii_tls_key_delete(tls_key_t key) {
  return pthread_key_delete(key);
}

void *ascii_tls_get(tls_key_t key) {
  return pthread_getspecific(key);
}

int ascii_tls_set(tls_key_t key, void *value) {
  return pthread_setspecific(key, value);
}

/**
 * @file platform/wasm/stubs/threading.c
 * @brief Threading stubs for WASM
 * @ingroup platform
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>

int asciichat_thread_join_timeout(asciichat_thread_t *thread, void **retval, uint64_t timeout_ns) {
  (void)thread;
  (void)retval;
  (void)timeout_ns;
  return 0; // Success - no-op in WASM
}

bool asciichat_thread_is_initialized(asciichat_thread_t *thread) {
  (void)thread;
  return true;
}

/* Thread pool stubs */
typedef void thread_pool_t;

thread_pool_t *thread_pool_create(int num_threads) {
  (void)num_threads;
  return NULL;
}

void thread_pool_destroy(thread_pool_t *pool) {
  (void)pool;
}

/* Condition variable stubs */
int cond_init(cond_t *cond, const char *name) {
  (void)cond;
  (void)name;
  return 0;
}

int cond_destroy(cond_t *cond) {
  (void)cond;
  return 0;
}

int cond_signal(cond_t *cond) {
  (void)cond;
  return 0;
}

int cond_broadcast(cond_t *cond) {
  (void)cond;
  return 0;
}

int cond_wait_impl(cond_t *cond, mutex_t *mutex) {
  (void)cond;
  (void)mutex;
  return 0;
}

int cond_timedwait_impl(cond_t *cond, mutex_t *mutex, uint64_t timeout_ns) {
  (void)cond;
  (void)mutex;
  (void)timeout_ns;
  return 0;
}

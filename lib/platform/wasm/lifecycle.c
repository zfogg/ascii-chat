/**
 * @file platform/wasm/stubs/lifecycle.c
 * @brief WASM lifecycle management using atomic operations (no mutexes)
 */

#include <ascii-chat/util/lifecycle.h>
#include <stdatomic.h>
#include <stdbool.h>

bool lifecycle_init(lifecycle_t *lc, const char *name) {
  (void)name;
  if (!lc) return false;
  atomic_store(&lc->state, LIFECYCLE_INITIALIZED);
  return true;
}

bool lifecycle_init_once(lifecycle_t *lc) {
  if (!lc) return false;

  // Try to transition from UNINITIALIZED to INITIALIZING
  int expected = LIFECYCLE_UNINITIALIZED;
  return atomic_compare_exchange_strong(&lc->state, &expected, LIFECYCLE_INITIALIZING);
}

void lifecycle_init_commit(lifecycle_t *lc) {
  if (!lc) return;
  atomic_store(&lc->state, LIFECYCLE_INITIALIZED);
}

void lifecycle_init_abort(lifecycle_t *lc) {
  if (!lc) return;
  atomic_store(&lc->state, LIFECYCLE_UNINITIALIZED);
}

bool lifecycle_shutdown(lifecycle_t *lc) {
  if (!lc) return false;
  atomic_store(&lc->state, LIFECYCLE_UNINITIALIZED);
  return true;
}

bool lifecycle_shutdown_forever(lifecycle_t *lc) {
  return lifecycle_shutdown(lc);
}

bool lifecycle_is_initialized(const lifecycle_t *lc) {
  if (!lc) return false;
  return atomic_load(&((lifecycle_t *)lc)->state) == LIFECYCLE_INITIALIZED;
}

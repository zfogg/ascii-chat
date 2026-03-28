/**
 * @file platform/wasm/stubs/lifecycle.c
 * @brief WASM lifecycle management using atomic operations (no mutexes)
 */

#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/atomic.h>
#include <stdbool.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

bool lifecycle_init(lifecycle_t *lc, const char *name) {
  (void)name;
  if (!lc) return false;

  // Only initialize if not already initialized (matches native CAS behavior)
  int expected = LIFECYCLE_UNINITIALIZED;
  return atomic_cas_int(&lc->state, &expected, LIFECYCLE_INITIALIZED);
}

bool lifecycle_init_once(lifecycle_t *lc) {
  if (!lc) return false;

  // Try to transition from UNINITIALIZED to INITIALIZING
  int expected = LIFECYCLE_UNINITIALIZED;
  return atomic_cas_int(&lc->state, &expected, LIFECYCLE_INITIALIZING);
}

void lifecycle_init_commit(lifecycle_t *lc) {
  if (!lc) return;
  atomic_store_int(&lc->state, LIFECYCLE_INITIALIZED);
}

void lifecycle_init_abort(lifecycle_t *lc) {
  if (!lc) return;
  atomic_store_int(&lc->state, LIFECYCLE_UNINITIALIZED);
}

bool lifecycle_shutdown(lifecycle_t *lc) {
  if (!lc) return false;
  atomic_store_int(&lc->state, LIFECYCLE_UNINITIALIZED);
  return true;
}

bool lifecycle_shutdown_forever(lifecycle_t *lc) {
  return lifecycle_shutdown(lc);
}

bool lifecycle_is_initialized(const lifecycle_t *lc) {
  if (!lc) {
    return false;
  }

  // WASM: Use non-tracking atomic load to avoid time_get_ns() which can block
  // Simply read the state value without the atomic_on_load callback
  int state = (int)(int32_t)atomic_load((const _Atomic(uint64_t) *)&lc->state);
  bool result = state == LIFECYCLE_INITIALIZED;

  return result;
}

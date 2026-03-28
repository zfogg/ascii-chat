/**
 * @file platform/wasm/stubs/lifecycle.c
 * @brief WASM lifecycle management using atomic operations (no mutexes)
 */

#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/atomic.h>
#include <stdbool.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, lifecycle_js_debug_log, (const char *msg, int val), {
  console.error('[LIFECYCLE] ' + UTF8ToString(msg) + ' val=' + val);
});

EM_JS(void, lifecycle_js_debug_log_ptr, (const char *msg, uint32_t ptr), {
  console.error('[LIFECYCLE] ' + UTF8ToString(msg) + ' ptr=0x' + ptr.toString(16));
});
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
#ifdef __EMSCRIPTEN__
  lifecycle_js_debug_log("lifecycle_is_initialized: entry", 0);
  lifecycle_js_debug_log_ptr("lifecycle_is_initialized: lc pointer", (uint32_t)(uintptr_t)lc);
#endif

  if (!lc) {
#ifdef __EMSCRIPTEN__
    lifecycle_js_debug_log("lifecycle_is_initialized: lc is NULL", 0);
#endif
    return false;
  }

#ifdef __EMSCRIPTEN__
  lifecycle_js_debug_log("lifecycle_is_initialized: about to atomic_load", 0);
#endif

  // WASM: Use non-tracking atomic load to avoid time_get_ns() which can block
  // Simply read the state value without the atomic_on_load callback
  int state = (int)(int32_t)atomic_load((const _Atomic(uint64_t) *)&lc->state);

#ifdef __EMSCRIPTEN__
  lifecycle_js_debug_log("lifecycle_is_initialized: loaded state", state);
  lifecycle_js_debug_log("lifecycle_is_initialized: LIFECYCLE_INITIALIZED constant", LIFECYCLE_INITIALIZED);
  bool result = state == LIFECYCLE_INITIALIZED;
  lifecycle_js_debug_log("lifecycle_is_initialized: returning", result ? 1 : 0);
#else
  bool result = state == LIFECYCLE_INITIALIZED;
#endif

  return result;
}

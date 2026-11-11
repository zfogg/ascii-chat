/**
 * @file lib/app_callbacks.c
 * @brief Application callback registration system
 */

#include <ascii-chat/app_callbacks.h>
#include <libplatform/app_callbacks.h>
#include <stddef.h>
#include <stdatomic.h>

static _Atomic(const app_callbacks_t *) g_app_callbacks = NULL;

void app_callbacks_register(const app_callbacks_t *callbacks) {
  atomic_store_explicit(&g_app_callbacks, callbacks, memory_order_release);
  const libplatform_app_callbacks_t platform_callbacks = {
      .write_sync_state = callbacks ? callbacks->write_sync_state : NULL,
      .write_memory_report = callbacks ? callbacks->write_memory_report : NULL,
      .write_backtrace = callbacks ? callbacks->write_backtrace : NULL,
      .debug_context = callbacks ? callbacks->debug_context : NULL,
  };
  libplatform_app_callbacks_register(callbacks ? &platform_callbacks : NULL);
}

const app_callbacks_t *app_callbacks_get(void) {
  return atomic_load_explicit(&g_app_callbacks, memory_order_acquire);
}

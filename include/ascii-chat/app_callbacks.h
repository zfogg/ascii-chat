/**
 * @file app_callbacks.h
 * @brief Application-level callbacks for library code
 *
 * Allows lib/ code to call application-level functions without direct dependency.
 */

#ifndef APP_CALLBACKS_H
#define APP_CALLBACKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  // Exit signals
  bool (*should_exit)(void);
  void (*signal_exit)(void);

  // Platform timing / wait helpers
  uint64_t (*platform_get_monotonic_time_us)(void);
  void (*platform_sleep_ms)(unsigned int ms);
  void (*platform_pump_events)(void);

  // Audio
  void (*audio_stop_thread)(void);
} app_callbacks_t;

/**
 * Register application callbacks
 */
void app_callbacks_register(const app_callbacks_t *callbacks);

/**
 * Get registered callbacks (returns NULL if not registered)
 */
const app_callbacks_t *app_callbacks_get(void);

/* ============================================================================
 * Convenience Macros for Safe Callback Invocation
 * ============================================================================ */

/**
 * Call a void(void) callback safely
 * Usage: APP_CALLBACK_VOID(audio_stop_thread)
 */
#define APP_CALLBACK_VOID(callback_name)                                                                               \
  do {                                                                                                                 \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    if (__cb && __cb->callback_name) {                                                                                 \
      __cb->callback_name();                                                                                           \
    }                                                                                                                  \
  } while (0)

/**
 * Call a void(void) callback and return bool
 * Usage: bool result = APP_CALLBACK_BOOL(should_exit)
 */
#define APP_CALLBACK_BOOL(callback_name)                                                                               \
  ({                                                                                                                   \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    (__cb && __cb->callback_name) ? __cb->callback_name() : false;                                                     \
  })

/**
 * Call a uint64_t(void) callback
 * Usage: uint64_t now = APP_CALLBACK_UINT64(platform_get_monotonic_time_us)
 */
#define APP_CALLBACK_UINT64(callback_name)                                                                             \
  ({                                                                                                                   \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    (__cb && __cb->callback_name) ? __cb->callback_name() : 0ULL;                                                      \
  })

/**
 * Call a void(unsigned int) callback
 * Usage: APP_CALLBACK_VOID_UINT(platform_sleep_ms, ms)
 */
#define APP_CALLBACK_VOID_UINT(callback_name, value)                                                                   \
  do {                                                                                                                 \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    if (__cb && __cb->callback_name) {                                                                                 \
      __cb->callback_name(value);                                                                                      \
    }                                                                                                                  \
  } while (0)

#endif

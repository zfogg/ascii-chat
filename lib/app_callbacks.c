/**
 * @file lib/app_callbacks.c
 * @brief Application callback registration system
 */

#include <ascii-chat/app_callbacks.h>
#include <stddef.h>

static const app_callbacks_t *g_app_callbacks = NULL;

void app_callbacks_register(const app_callbacks_t *callbacks) {
  g_app_callbacks = callbacks;
}

const app_callbacks_t *app_callbacks_get(void) {
  return g_app_callbacks;
}

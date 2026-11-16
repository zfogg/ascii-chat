/**
 * @file platform/wasm/stubs/network.c
 * @brief Network function stubs for WASM
 * @ingroup platform
 */

#include <stdio.h>
#include <ascii-chat/asciichat_errno.h>
#include <libplatform/socket.h>

int platform_popen(const char *name, const char *command, const char *mode, FILE **out_stream) {
  (void)name;
  (void)command;
  (void)mode;
  (void)out_stream;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "not supported in WASM");
}

int platform_pclose(FILE **stream_ptr) {
  (void)stream_ptr;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "not supported in WASM");
}

typedef struct app_callbacks_t app_callbacks_t;
app_callbacks_t *app_callbacks_get(void) {
  return NULL;
}
typedef void app_callbacks_reg_t;
void app_callbacks_register(app_callbacks_reg_t *callbacks) {
  (void)callbacks;
}

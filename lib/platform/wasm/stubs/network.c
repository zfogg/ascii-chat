/**
 * @file platform/wasm/stubs/network.c
 * @brief Network function stubs for WASM
 * @ingroup platform
 */

#include <stdio.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/socket.h>

asciichat_error_t socket_sendv_all(socket_t sock, const socket_buffer_t *buffers, size_t count, uint64_t timeout_ns) {
  (void)sock;
  (void)buffers;
  (void)count;
  (void)timeout_ns;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "WASM uses browser transports instead of native sockets");
}

asciichat_error_t platform_popen(const char *name, const char *command, const char *mode, FILE **out_stream) {
  (void)name;
  (void)command;
  (void)mode;
  (void)out_stream;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "not supported in WASM");
}

asciichat_error_t platform_pclose(FILE **stream_ptr) {
  (void)stream_ptr;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "not supported in WASM");
}

int platform_execute_subprocess(const char *executable, const char **argv, char *output_buffer, size_t output_size) {
  (void)executable;
  (void)argv;
  (void)output_buffer;
  (void)output_size;
  return -1; // No subprocess support in WASM
}

typedef struct app_callbacks_t app_callbacks_t;
app_callbacks_t *app_callbacks_get(void) {
  return NULL;
}
typedef void app_callbacks_reg_t;
void app_callbacks_register(app_callbacks_reg_t *callbacks) {
  (void)callbacks;
}

/**
 * @file platform/wasm/stubs/network.c
 * @brief Network function stubs for WASM
 * @ingroup platform
 */

#include <stdio.h>
#include <ascii-chat/asciichat_errno.h>

asciichat_error_t platform_popen(const char *name, const char *command, const char *mode, FILE **out_stream) {
  (void)name;
  (void)command;
  (void)mode;
  (void)out_stream;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "platform_popen not supported in WASM");
}

asciichat_error_t platform_pclose(FILE **stream_ptr) {
  (void)stream_ptr;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "platform_pclose not supported in WASM");
}

int platform_execute_subprocess(const char *executable, const char **argv,
                                char *output_buffer, size_t output_size) {
  (void)executable;
  (void)argv;
  (void)output_buffer;
  (void)output_size;
  return -1; // No subprocess support in WASM
}

// Opaque forward declarations
typedef struct websocket_client_t websocket_client_t;
typedef struct tcp_client_t tcp_client_t;
typedef struct app_callbacks_t app_callbacks_t;

void websocket_client_destroy(websocket_client_t *client) {
  (void)client;
  // No-op - websocket client not used in WASM mirror mode
}

void tcp_client_destroy(tcp_client_t *client) {
  (void)client;
  // No-op - TCP client not used in WASM mirror mode
}

app_callbacks_t *app_callbacks_get(void) {
  return NULL; // No app callbacks in WASM
}

typedef void app_callbacks_reg_t;
typedef void app_client_t;
typedef void connection_attempt_context_t;

void app_callbacks_register(app_callbacks_reg_t *callbacks) {
  (void)callbacks;
}

app_client_t *app_client_create(void) {
  return NULL;
}

void app_client_destroy(app_client_t *client) {
  (void)client;
}

asciichat_error_t connection_context_init(connection_attempt_context_t *ctx) {
  (void)ctx;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "connection_context_init not supported in WASM");
}

void connection_context_cleanup(connection_attempt_context_t *ctx) {
  (void)ctx;
}

tcp_client_t *tcp_client_create(void) {
  return NULL;
}

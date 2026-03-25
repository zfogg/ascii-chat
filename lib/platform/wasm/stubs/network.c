/**
 * @file platform/wasm/stubs/network.c
 * @brief Network function stubs for WASM
 * @ingroup platform
 */

#include <stdio.h>

FILE *platform_popen(const char *cmd, const char *mode) {
  (void)cmd;
  (void)mode;
  return NULL; // No subprocess support in WASM
}

int platform_pclose(FILE *stream) {
  (void)stream;
  return -1;
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
typedef void connection_context_t;

void app_callbacks_register(app_callbacks_reg_t *callbacks) {
  (void)callbacks;
}

app_client_t *app_client_create(void) {
  return NULL;
}

void app_client_destroy(app_client_t *client) {
  (void)client;
}

connection_context_t *connection_context_init(void) {
  return NULL;
}

void connection_context_cleanup(connection_context_t *ctx) {
  (void)ctx;
}

tcp_client_t *tcp_client_create(void) {
  return NULL;
}

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
#include <ascii-chat/crypto/crypto.h>

typedef int socket_t;

typedef struct {
  // Exit signals
  bool (*should_exit)(void);
  void (*signal_exit)(void);

  // Server/client crypto setup
  void (*server_connection_set_ip)(const char *ip);
  void (*client_crypto_set_mode)(uint8_t mode);
  int (*client_crypto_init)(void);
  int (*client_crypto_handshake)(socket_t sockfd);
  bool (*crypto_client_is_ready)(void);
  const crypto_context_t *(*crypto_client_get_context)(void);

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
 * Call a void(void) callback and return int
 * Usage: int result = APP_CALLBACK_INT(client_crypto_init)
 */
#define APP_CALLBACK_INT(callback_name)                                                                                \
  ({                                                                                                                   \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    (__cb && __cb->callback_name) ? __cb->callback_name() : -1;                                                        \
  })

/**
 * Call a int(socket_t) callback
 * Usage: int result = APP_CALLBACK_INT_SOCKET(client_crypto_handshake, sockfd)
 */
#define APP_CALLBACK_INT_SOCKET(callback_name, sockfd)                                                                 \
  ({                                                                                                                   \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    (__cb && __cb->callback_name) ? __cb->callback_name(sockfd) : -1;                                                  \
  })

/**
 * Call a void(uint8_t) callback
 * Usage: APP_CALLBACK_VOID_UINT8(client_crypto_set_mode, mode)
 */
#define APP_CALLBACK_VOID_UINT8(callback_name, value)                                                                  \
  do {                                                                                                                 \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    if (__cb && __cb->callback_name) {                                                                                 \
      __cb->callback_name(value);                                                                                      \
    }                                                                                                                  \
  } while (0)

/**
 * Call a void(const char *) callback
 * Usage: APP_CALLBACK_VOID_STR(server_connection_set_ip, ip_addr)
 */
#define APP_CALLBACK_VOID_STR(callback_name, value)                                                                    \
  do {                                                                                                                 \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    if (__cb && __cb->callback_name) {                                                                                 \
      __cb->callback_name(value);                                                                                      \
    }                                                                                                                  \
  } while (0)

/**
 * Get a pointer return value from callback
 * Usage: const crypto_context_t *ctx = APP_CALLBACK_PTR(crypto_client_get_context)
 */
#define APP_CALLBACK_PTR(callback_name)                                                                                \
  ({                                                                                                                   \
    const app_callbacks_t *__cb = app_callbacks_get();                                                                 \
    (__cb && __cb->callback_name) ? __cb->callback_name() : NULL;                                                      \
  })

#endif

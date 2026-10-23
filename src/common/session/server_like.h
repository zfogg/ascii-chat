/** Shared server-mode orchestration. Networking is owned by network_server_t;
 * modes supply one callback set for TCP, WebSocket, and authenticated peers.
 * Listeners are constructed before init_fn and started only after it succeeds.
 * All connections drain before cleanup_fn releases mode dependencies.
 * The interrupt callback only publishes atomic shutdown requests.
 */
#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/network/nat/upnp.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/platform/api.h>
#include <ascii-chat/tui/status.h>
#include <stdbool.h>

typedef asciichat_error_t (*session_server_like_init_fn)(void *user_data);

typedef void (*session_server_like_interrupt_fn)(int sig);

typedef void (*session_server_like_cleanup_fn)(void *user_data);

typedef void (*session_server_like_status_fn)(void *user_data, ui_status_t *out_status);

typedef struct session_server_like_config {

  session_server_like_init_fn init_fn;
  void *init_user_data;

  session_server_like_interrupt_fn interrupt_fn;

  void (*tick_fn)(void *user_data);
  void *tick_user_data;

  session_server_like_cleanup_fn cleanup_fn;
  void *cleanup_user_data;

  session_server_like_status_fn status_fn;
  void *status_user_data;

  network_connection_callbacks_t connections;
  void *connection_user_data;
  bool websocket_enabled;
  size_t max_payload;
  uint64_t partial_timeout_ms, idle_timeout_ms;

  struct {

    bool enabled;

    const char *service_name;

    const char *service_type;
  } mdns;

  struct {

    bool enabled;

    const char *description;
  } upnp;

  bool raise_fd_limit;

  int fd_limit_target;

} session_server_like_config_t;

ASCIICHAT_API asciichat_error_t session_server_like_run(const session_server_like_config_t *config);

ASCIICHAT_API network_server_t *session_server_like_get_network_server(void);

ASCIICHAT_API asciichat_mdns_t *session_server_like_get_mdns_ctx(void);

ASCIICHAT_API nat_upnp_context_t *session_server_like_get_upnp_ctx(void);

ASCIICHAT_API asciichat_error_t session_server_like_mdns_advertise(const char *name, const char *type, uint16_t port,
                                                                   const char **txt, size_t txt_count);

ASCIICHAT_API bool session_server_like_shutdown_requested(void);

ASCIICHAT_API void session_server_like_request_shutdown(void);

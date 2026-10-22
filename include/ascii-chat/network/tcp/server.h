/** Internal TCP listener backend. Application connections belong to network_server_t. */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/platform/event.h>
#include "../../atomic.h"
#include "../../common.h"
#include "../../platform/socket.h"
#include "../../platform/abstraction.h"
#include <ascii-chat/uthash.h> // For UT_hash_handle

// Forward declarations
typedef struct tcp_client_entry tcp_client_entry_t;
typedef struct tcp_server tcp_server_t;

typedef struct {
  int port;                               ///< TCP listen port
  const char *ipv4_address;               ///< IPv4 bind address (NULL or empty = don't bind)
  const char *ipv6_address;               ///< IPv6 bind address (NULL or empty = don't bind)
  bool bind_ipv4;                         ///< Whether to bind IPv4 socket
  bool bind_ipv6;                         ///< Whether to bind IPv6 socket
  platform_event_accept_fn event_handler; ///< Required asynchronous accept handler
  void (*tick_fn)(void *user_data);       ///< Optional periodic callback on the tcp_server_run thread
  void *tick_user_data;
  void *user_data; ///< User data passed to each client handler
} tcp_server_config_t;

struct tcp_server {
  platform_event_loop_t *event_loop; ///< Owned while the asynchronous server runs
  socket_t listen_socket;            ///< IPv4 listen socket
  socket_t listen_socket6;           ///< IPv6 listen socket
  atomic_t running;                  ///< Server running flag (set false to shutdown)
  tcp_server_config_t config;        ///< Server configuration
};

asciichat_error_t tcp_server_init(tcp_server_t *server, const tcp_server_config_t *config);

asciichat_error_t tcp_server_run(tcp_server_t *server);

void tcp_server_destroy(tcp_server_t *server);

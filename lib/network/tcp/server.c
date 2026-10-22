/**
 * @file network/tcp_server.c
 * @brief 🌐 Generic TCP server with dual-stack IPv4/IPv6 support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>

/**
 * @brief Bind and listen on a TCP socket
 *
 * Helper function to create, bind, and listen on a TCP socket.
 * Extracted from common pattern in both ascii-chat server and acds.
 *
 * @param address IP address to bind (NULL or empty for INADDR_ANY/IN6ADDR_ANY)
 * @param family Address family (AF_INET or AF_INET6)
 * @param port Port number to bind
 * @return socket_t Socket descriptor on success, INVALID_SOCKET_VALUE on failure
 */
static socket_t bind_and_listen(const char *address, int family, int port) {
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

  char port_str[16];
  SAFE_SNPRINTF(port_str, sizeof(port_str), "%d", port);

  // Use provided address or NULL for wildcard
  const char *addr_str = (address && address[0] != '\0') ? address : NULL;

  int gai_result = getaddrinfo(addr_str, port_str, &hints, &res);
  if (gai_result != 0) {
    log_error("getaddrinfo failed for %s:%d: %s", addr_str ? addr_str : "(wildcard)", port, gai_strerror(gai_result));
    return INVALID_SOCKET_VALUE;
  }

  const char *socket_name = (family == AF_INET6) ? "server_ipv6" : "server_ipv4";
  socket_t server_socket = socket_create(socket_name, res->ai_family, res->ai_socktype, res->ai_protocol);
  if (server_socket == INVALID_SOCKET_VALUE) {
    log_error("Failed to create socket for %s:%d", addr_str ? addr_str : "(wildcard)", port);
    freeaddrinfo(res);
    return INVALID_SOCKET_VALUE;
  }

  // Enable SO_REUSEADDR to allow quick restarts
  int reuse = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0) {
    log_warn("Failed to set SO_REUSEADDR on %s:%d", addr_str ? addr_str : "(wildcard)", port);
  }

  // For IPv6, disable IPv4-mapped addresses (IPV6_V6ONLY=1) to support dual-stack
  if (family == AF_INET6) {
    int ipv6only = 1;
    if (setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&ipv6only, sizeof(ipv6only)) < 0) {
      log_warn("Failed to set IPV6_V6ONLY on [%s]:%d", addr_str ? addr_str : "::", port);
    }
  }

  // Bind socket
  if (bind(server_socket, res->ai_addr, (socklen_t)res->ai_addrlen) < 0) {
    log_error("Failed to bind %s:%d", addr_str ? addr_str : "(wildcard)", port);
    socket_close(server_socket);
    freeaddrinfo(res);
    return INVALID_SOCKET_VALUE;
  }

  freeaddrinfo(res);

  // Listen with backlog of 128 connections
  if (listen(server_socket, 128) < 0) {
    log_error("Failed to listen on %s:%d", addr_str ? addr_str : "(wildcard)", port);
    socket_close(server_socket);
    return INVALID_SOCKET_VALUE;
  }

  log_info("Listening on %s%s%s:%d (%s)", family == AF_INET6 ? "[" : "",
           addr_str ? addr_str : (family == AF_INET ? "0.0.0.0" : "::"), family == AF_INET6 ? "]" : "", port,
           family == AF_INET ? "IPv4" : "IPv6");

  return server_socket;
}

asciichat_error_t tcp_server_init(tcp_server_t *server, const tcp_server_config_t *config) {
  if (!server || !config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or config is NULL");
  }

  // Initialize server state
  memset(server, 0, sizeof(*server));
  server->listen_socket = INVALID_SOCKET_VALUE;
  server->listen_socket6 = INVALID_SOCKET_VALUE;
  atomic_store_bool(&server->running, true);
  server->config = *config; // Copy config

  // Determine which IP versions to bind
  bool should_bind_ipv4 = config->bind_ipv4;
  bool should_bind_ipv6 = config->bind_ipv6;

  // Bind IPv4 socket if requested
  if (should_bind_ipv4) {
    const char *ipv4_addr = (config->ipv4_address && config->ipv4_address[0] != '\0') ? config->ipv4_address : NULL;
    server->listen_socket = bind_and_listen(ipv4_addr, AF_INET, config->port);

    if (server->listen_socket == INVALID_SOCKET_VALUE) {
      log_warn("Failed to bind IPv4 socket");
    }
  }

  // Bind IPv6 socket if requested
  if (should_bind_ipv6) {
    const char *ipv6_addr = (config->ipv6_address && config->ipv6_address[0] != '\0') ? config->ipv6_address : NULL;
    server->listen_socket6 = bind_and_listen(ipv6_addr, AF_INET6, config->port);

    if (server->listen_socket6 == INVALID_SOCKET_VALUE) {
      log_warn("Failed to bind IPv6 socket");
    }
  }

  // Ensure at least one socket bound successfully
  if (server->listen_socket == INVALID_SOCKET_VALUE && server->listen_socket6 == INVALID_SOCKET_VALUE) {
    NAMED_UNREGISTER(&server->running);
    return SET_ERRNO(ERROR_NETWORK_BIND, "Failed to bind any sockets (IPv4 and IPv6 both failed)");
  }

  /* Register server with named registry, identified by port */
  char port_name[32];
  snprintf(port_name, sizeof(port_name), "server:%d", server->config.port);
  NAMED_REGISTER(server, port_name, "tcp_server", "0x%tx", NULL);

  /* Register server's sync primitives with hierarchical naming */
  NAMED_REGISTER_ATOMIC(&server->running, "is_running", (uintptr_t)(const void *)(server));

  return ASCIICHAT_OK;
}

static void tcp_event_tick(void *arg) {
  tcp_server_t *server = arg;
  if (!atomic_load_bool(&server->running)) {
    platform_event_loop_stop(server->event_loop);
    return;
  }
  if (server->config.tick_fn)
    server->config.tick_fn(server->config.tick_user_data);
}

static asciichat_error_t tcp_server_run_events(tcp_server_t *server) {
  asciichat_error_t result = platform_event_loop_create(&server->event_loop);
  if (result != ASCIICHAT_OK)
    return result;
  platform_event_listener_t *listener = NULL;
  if (server->listen_socket != INVALID_SOCKET_VALUE)
    result = platform_event_listen(server->event_loop, &server->listen_socket, server->config.event_handler,
                                   server->config.user_data, &listener);
  if (result == ASCIICHAT_OK && server->listen_socket6 != INVALID_SOCKET_VALUE)
    result = platform_event_listen(server->event_loop, &server->listen_socket6, server->config.event_handler,
                                   server->config.user_data, &listener);
  if (result == ASCIICHAT_OK)
    result = platform_event_loop_tick(server->event_loop, 50, tcp_event_tick, server);
  if (result != ASCIICHAT_OK)
    platform_event_loop_stop(server->event_loop);
  log_info("TCP event backend: %s", platform_event_backend());
  platform_event_loop_run(server->event_loop);
  asciichat_error_t cleanup = platform_event_loop_destroy(server->event_loop);
  server->event_loop = NULL;
  return result != ASCIICHAT_OK ? result : cleanup;
}

asciichat_error_t tcp_server_run(tcp_server_t *server) {
  if (!server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server is NULL");
  }

  if (!server->config.event_handler)
    return SET_ERRNO(ERROR_INVALID_PARAM, "TCP event handler is required");
  return tcp_server_run_events(server);
}

void tcp_server_destroy(tcp_server_t *server) {
  if (!server) {
    return;
  }

  log_debug("Shutting down TCP server...");

  // Signal server to stop
  atomic_store_bool(&server->running, false);

  // Close listen sockets
  if (server->listen_socket != INVALID_SOCKET_VALUE) {
    log_debug("Closing IPv4 listen socket");
    socket_close(server->listen_socket);
    server->listen_socket = INVALID_SOCKET_VALUE;
  }

  if (server->listen_socket6 != INVALID_SOCKET_VALUE) {
    log_debug("Closing IPv6 listen socket");
    socket_close(server->listen_socket6);
    server->listen_socket6 = INVALID_SOCKET_VALUE;
  }

  log_debug("TCP listener shutdown complete");
  NAMED_UNREGISTER(&server->running);
  NAMED_UNREGISTER(server);
}

/**
 * @file network/tcp_client.c
 * @brief TCP client implementation with connection lifecycle management
 *
 * Complete TCP client library providing:
 * - Connection establishment with timeout
 * - Reconnection with exponential backoff
 * - Thread-safe packet transmission
 * - Connection state management
 * - Socket lifecycle management
 *
 * This module consolidates all connection logic that was previously scattered
 * across src/client/server.c, making it reusable for any TCP client application.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 * @version 1.0
 */

#include <ascii-chat/network/tcp/client.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/debug/named.h>

#include <string.h>
#include <time.h>
#include <stdatomic.h>

#ifndef _WIN32
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
#include <ws2tcpip.h>
#include <process.h>
#define getpid _getpid
#endif

/* ============================================================================
 * Connection Lifecycle Management
 * ============================================================================ */

/** Maximum delay between reconnection attempts (nanoseconds) */
#define MAX_RECONNECT_DELAY (5LL * NS_PER_SEC_INT)

/**
 * @brief Calculate reconnection delay with exponential backoff
 *
 * Initial delay: 100ms, increases by 200ms per attempt, max 5 seconds (in nanoseconds)
 */
static uint64_t get_reconnect_delay(unsigned int attempt) {
  uint64_t delay_ns = (100LL * NS_PER_MS_INT) + ((uint64_t)(attempt - 1) * 200LL * NS_PER_MS_INT);
  return (delay_ns > MAX_RECONNECT_DELAY) ? MAX_RECONNECT_DELAY : delay_ns;
}

/**
 * @brief Close socket safely with platform-specific handling
 */
static int close_socket_safe(socket_t sockfd) {
  if (!socket_is_valid(sockfd)) {
    return 0; // Already closed
  }

  log_debug("Closing socket %d", sockfd);

  if (socket_close(sockfd) != 0) {
    log_error("Failed to close socket: %s", network_error_string());
    return -1;
  }

  // Small delay to ensure socket resources are released
  // Prevents WSA error 10038 on Windows
  platform_sleep_ns(50 * NS_PER_MS_INT); // 50ms

  return 0;
}

/**
 * @brief Create and initialize TCP client
 *
 * Allocates tcp_client_t and initializes all fields to safe defaults.
 */
tcp_client_t *tcp_client_create(void) {
  tcp_client_t *client = SAFE_MALLOC(sizeof(tcp_client_t), tcp_client_t *);
  if (!client) {
    log_error("Failed to allocate tcp_client_t");
    return NULL;
  }

  memset(client, 0, sizeof(*client));

  client->sockfd = INVALID_SOCKET_VALUE;
  atomic_store(&client->connection_active, false);
  atomic_store(&client->connection_lost, false);
  atomic_store(&client->should_reconnect, false);
  client->my_client_id = 0;
  memset(client->server_ip, 0, sizeof(client->server_ip));
  client->encryption_enabled = false;

  if (mutex_init(&client->send_mutex, "client_send")  != 0) {
    log_error("Failed to initialize send mutex");
    SAFE_FREE(client);
    return NULL;
  }

  NAMED_REGISTER_CLIENT(client, "tcp_client");

  log_debug("TCP client created successfully");
  return client;
}

/**
 * @brief Destroy TCP client and free resources
 *
 * Must be called AFTER all threads have been joined.
 */
void tcp_client_destroy(tcp_client_t **client_ptr) {
  if (!client_ptr || !*client_ptr) {
    return;
  }

  tcp_client_t *client = *client_ptr;

  if (socket_is_valid(client->sockfd)) {
    close_socket_safe(client->sockfd);
    client->sockfd = INVALID_SOCKET_VALUE;
  }

  NAMED_UNREGISTER(client);
  mutex_destroy(&client->send_mutex);
  SAFE_FREE(*client_ptr);

  log_debug("TCP client destroyed");
}

/* ============================================================================
 * Connection State Queries
 * ============================================================================ */

/**
 * @brief Check if connection is currently active
 */
bool tcp_client_is_active(const tcp_client_t *client) {
  if (!client)
    return false;
  return atomic_load(&client->connection_active);
}

/**
 * @brief Check if connection was lost
 */
bool tcp_client_is_lost(const tcp_client_t *client) {
  if (!client)
    return false;
  return atomic_load(&client->connection_lost);
}

/**
 * @brief Get current socket descriptor
 */
socket_t tcp_client_get_socket(const tcp_client_t *client) {
  return client ? client->sockfd : INVALID_SOCKET_VALUE;
}

/**
 * @brief Get client ID assigned by server
 */
uint32_t tcp_client_get_id(const tcp_client_t *client) {
  return client ? client->my_client_id : 0;
}

/* ============================================================================
 * Connection Control
 * ============================================================================ */

/**
 * @brief Signal that connection was lost (triggers reconnection)
 */
void tcp_client_signal_lost(tcp_client_t *client) {
  if (!client)
    return;

  if (!atomic_load(&client->connection_lost)) {
    atomic_store(&client->connection_lost, true);
    atomic_store(&client->connection_active, false);
    log_info("Connection lost signaled");
  }
}

/**
 * @brief Close connection gracefully
 */
void tcp_client_close(tcp_client_t *client) {
  if (!client)
    return;

  log_debug("Closing client connection");

  // Mark connection as inactive
  atomic_store(&client->connection_active, false);

  // Close socket
  if (socket_is_valid(client->sockfd)) {
    close_socket_safe(client->sockfd);
    client->sockfd = INVALID_SOCKET_VALUE;
  }

  // Reset client ID
  client->my_client_id = 0;
}

/**
 * @brief Shutdown connection forcefully (for signal handlers)
 */
void tcp_client_shutdown(tcp_client_t *client) {
  if (!client)
    return;

  atomic_store(&client->connection_active, false);

  // Shutdown socket for reading/writing to interrupt blocking calls
  if (socket_is_valid(client->sockfd)) {
    socket_shutdown(client->sockfd, SHUT_RDWR);
  }
}

/* ============================================================================
 * Thread-Safe Packet Transmission
 * ============================================================================ */

/**
 * @brief Send packet with thread-safe mutex protection
 *
 * All packet transmission goes through this function to ensure
 * packets aren't interleaved on the wire.
 */
int tcp_client_send_packet(tcp_client_t *client, packet_type_t type, const void *data, size_t len) {
  if (!client) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client");
  }

  if (!atomic_load(&client->connection_active)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  // Acquire send mutex for thread-safe transmission
  mutex_lock(&client->send_mutex);

  // Send packet without encryption (crypto is handled at app_client layer)
  asciichat_error_t result = (asciichat_error_t)send_packet(client->sockfd, type, data, len);

  mutex_unlock(&client->send_mutex);

  if (result != ASCIICHAT_OK) {
    log_debug("Failed to send packet type %d: %s", type, asciichat_error_string(result));
    return -1;
  }

  return 0;
}

/**
 * @brief Send ping packet
 */
int tcp_client_send_ping(tcp_client_t *client) {
  if (!client)
    return -1;
  return tcp_client_send_packet(client, PACKET_TYPE_PING, NULL, 0);
}

/**
 * @brief Send pong packet
 */
int tcp_client_send_pong(tcp_client_t *client) {
  if (!client)
    return -1;
  return tcp_client_send_packet(client, PACKET_TYPE_PONG, NULL, 0);
}

/* ============================================================================
 * Connection Establishment (to be implemented - migrated from server.c)
 * ============================================================================ */

/**
 * @brief Establish TCP connection to server
 *
 * Performs full connection lifecycle:
 * - DNS resolution with IPv4/IPv6 dual-stack support
 * - Socket creation and connection with timeout
 * - Crypto handshake (if enabled)
 * - Initial capability exchange
 * - Client ID assignment from local port
 *
 * @param client TCP client instance
 * @param address Server hostname or IP address
 * @param port Server port number
 * @param reconnect_attempt Current reconnection attempt (0 for first, 1+ for retries)
 * @param first_connection True if this is the very first connection since program start
 * @param has_ever_connected True if client has successfully connected at least once
 * @return 0 on success, negative on error
 */
int tcp_client_connect(tcp_client_t *client, const char *address, int port, int reconnect_attempt,
                       bool first_connection, bool has_ever_connected) {
  (void)first_connection;   // Currently unused
  (void)has_ever_connected; // Currently unused

  if (!client || !address || port <= 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client, address, or port");
  }

  // Close any existing connection
  if (socket_is_valid(client->sockfd)) {
    close_socket_safe(client->sockfd);
    client->sockfd = INVALID_SOCKET_VALUE;
  }

  // Apply reconnection delay if this is a retry
  if (reconnect_attempt > 0) {
    uint64_t delay_ns = get_reconnect_delay(reconnect_attempt);
    platform_sleep_ns(delay_ns);
  }

  // Resolve server address using getaddrinfo() for IPv4/IPv6 support
  // Special handling for localhost: ensure we try both IPv6 (::1) and IPv4 (127.0.0.1)
  bool is_localhost = (strcmp(address, "localhost") == 0 || is_localhost_ipv4(address) || is_localhost_ipv6(address));

  struct addrinfo hints, *res = NULL, *addr_iter;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;
  if (is_localhost) {
    hints.ai_flags = AI_NUMERICSERV; // Optimize for localhost
  }

  char port_str[16];
  SAFE_SNPRINTF(port_str, sizeof(port_str), "%d", port);

  // For localhost, try IPv6 loopback (::1) first, then fall back to IPv4
  if (is_localhost) {
    log_debug("Localhost detected - trying IPv6 loopback [::1]:%s first...", port_str);
    hints.ai_family = AF_INET6;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    int ipv6_result = getaddrinfo("::1", port_str, &hints, &res);
    if (ipv6_result == 0 && res != NULL) {
      // Try IPv6 loopback connection
      client->sockfd = socket_create("tcp_client_ipv6", res->ai_family, res->ai_socktype, res->ai_protocol);
      if (client->sockfd != INVALID_SOCKET_VALUE) {
        log_debug("Trying IPv6 loopback connection to [::1]:%s...", port_str);
        if (connect_with_timeout(client->sockfd, res->ai_addr, res->ai_addrlen, CONNECT_TIMEOUT)) {
          log_debug("Connection successful using IPv6 loopback");
          SAFE_STRNCPY(client->server_ip, "::1", sizeof(client->server_ip));
          freeaddrinfo(res);
          res = NULL; // Prevent double-free at connection_success label
          goto connection_success;
        }
        close_socket_safe(client->sockfd);
        client->sockfd = INVALID_SOCKET_VALUE;
      }
      freeaddrinfo(res);
      res = NULL;
    }

    // IPv6 failed, try IPv4 loopback (127.0.0.1)
    log_debug("IPv6 failed, trying IPv4 loopback 127.0.0.1:%s...", port_str);
    hints.ai_family = AF_INET;

    int ipv4_result = getaddrinfo("127.0.0.1", port_str, &hints, &res);
    if (ipv4_result == 0 && res != NULL) {
      client->sockfd = socket_create("tcp_client_ipv4", res->ai_family, res->ai_socktype, res->ai_protocol);
      if (client->sockfd != INVALID_SOCKET_VALUE) {
        log_debug("Trying IPv4 loopback connection to 127.0.0.1:%s...", port_str);
        if (connect_with_timeout(client->sockfd, res->ai_addr, res->ai_addrlen, CONNECT_TIMEOUT)) {
          log_debug("Connection successful using IPv4 loopback");
          SAFE_STRNCPY(client->server_ip, "127.0.0.1", sizeof(client->server_ip));
          freeaddrinfo(res);
          res = NULL;
          goto connection_success;
        }
        close_socket_safe(client->sockfd);
        client->sockfd = INVALID_SOCKET_VALUE;
      }
      freeaddrinfo(res);
      res = NULL;
    }

    // Both IPv6 and IPv4 loopback failed for localhost
    log_warn("Could not connect to localhost using either IPv6 or IPv4 loopback");
    return -1;
  }

  // For non-localhost addresses, use standard resolution
  log_debug("Resolving server address '%s' port %s...", address, port_str);
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = 0;
  int getaddr_result = getaddrinfo(address, port_str, &hints, &res);
  if (getaddr_result != 0) {
    log_error("Failed to resolve server address '%s': %s", address, gai_strerror(getaddr_result));
    return -1;
  }

  // Try each address returned by getaddrinfo() - prefer IPv6, fall back to IPv4
  for (int address_family = AF_INET6; address_family >= AF_INET; address_family -= (AF_INET6 - AF_INET)) {
    for (addr_iter = res; addr_iter != NULL; addr_iter = addr_iter->ai_next) {
      if (addr_iter->ai_family != address_family) {
        continue;
      }

      const char *socket_name = (addr_iter->ai_family == AF_INET6) ? "tcp_client_server_ipv6" : "tcp_client_server_ipv4";
      client->sockfd = socket_create(socket_name, addr_iter->ai_family, addr_iter->ai_socktype, addr_iter->ai_protocol);
      if (client->sockfd == INVALID_SOCKET_VALUE) {
        continue;
      }

      if (addr_iter->ai_family == AF_INET) {
        log_debug("Trying IPv4 connection...");
      } else if (addr_iter->ai_family == AF_INET6) {
        log_debug("Trying IPv6 connection...");
      }

      if (connect_with_timeout(client->sockfd, addr_iter->ai_addr, addr_iter->ai_addrlen, CONNECT_TIMEOUT)) {
        log_debug("Connection successful using %s", addr_iter->ai_family == AF_INET    ? "IPv4"
                                                    : addr_iter->ai_family == AF_INET6 ? "IPv6"
                                                                                       : "unknown protocol");

        // Extract server IP address for known_hosts
        if (format_ip_address(addr_iter->ai_family, addr_iter->ai_addr, client->server_ip, sizeof(client->server_ip)) ==
            ASCIICHAT_OK) {
          log_debug("Resolved server IP: %s", client->server_ip);
        } else {
          log_warn("Failed to format server IP address");
        }

        goto connection_success;
      }

      close_socket_safe(client->sockfd);
      client->sockfd = INVALID_SOCKET_VALUE;
    }
  }

connection_success:

  if (res) {
    freeaddrinfo(res);
  }

  // If we exhausted all addresses without success, fail
  if (client->sockfd == INVALID_SOCKET_VALUE) {
    log_warn("Could not connect to server %s:%d (tried all addresses)", address, port);
    return -1;
  }

  // Extract local port for client ID
  struct sockaddr_storage local_addr = {0};
  socklen_t addr_len = sizeof(local_addr);
  if (getsockname(client->sockfd, (struct sockaddr *)&local_addr, &addr_len) == -1) {
    log_error("Failed to get local socket address: %s", network_error_string());
    close_socket_safe(client->sockfd);
    client->sockfd = INVALID_SOCKET_VALUE;
    return -1;
  }

  // Extract port from either IPv4 or IPv6 address
  int local_port = 0;
  if (((struct sockaddr *)&local_addr)->sa_family == AF_INET) {
    local_port = NET_TO_HOST_U16(((struct sockaddr_in *)&local_addr)->sin_port);
  } else if (((struct sockaddr *)&local_addr)->sa_family == AF_INET6) {
    local_port = NET_TO_HOST_U16(((struct sockaddr_in6 *)&local_addr)->sin6_port);
  }
  client->my_client_id = (uint32_t)local_port;

  // Mark connection as active
  atomic_store(&client->connection_active, true);
  atomic_store(&client->connection_lost, false);
  atomic_store(&client->should_reconnect, false);

  // Initialize crypto (application must set crypto_initialized flag)
  // This is done outside this function by calling client_crypto_init()

  // Configure socket options
  if (socket_set_keepalive(client->sockfd, true) < 0) {
    log_warn("Failed to set socket keepalive: %s", network_error_string());
  }

  asciichat_error_t sock_config_result = socket_configure_buffers(client->sockfd);
  if (sock_config_result != ASCIICHAT_OK) {
    log_warn("Failed to configure socket: %s", network_error_string());
  }

  log_debug("Connection established successfully to %s:%d (client_id=%u)", address, port, client->my_client_id);
  return 0;
}

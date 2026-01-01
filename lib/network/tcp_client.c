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

#include "tcp_client.h"
#include "common.h"
#include "log/logging.h"
#include "platform/abstraction.h"
#include "platform/socket.h"
#include "platform/system.h"
#include "platform/terminal.h"
#include "network/packet.h"
#include "network/network.h"
#include "network/av.h"
#include "util/endian.h"
#include "util/ip.h"
#include "asciichat_errno.h"
#include "buffer_pool.h"
#include "options/options.h"
#include "crypto/handshake/common.h"

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

/** Maximum delay between reconnection attempts (microseconds) */
#define MAX_RECONNECT_DELAY (5 * 1000 * 1000)

/**
 * @brief Calculate reconnection delay with exponential backoff
 *
 * Initial delay: 100ms, increases by 200ms per attempt, max 5 seconds
 */
static unsigned int get_reconnect_delay(unsigned int attempt) {
  unsigned int delay_us = 100000 + (attempt - 1) * 200000;
  return (delay_us > MAX_RECONNECT_DELAY) ? MAX_RECONNECT_DELAY : delay_us;
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
  platform_sleep_usec(50000); // 50ms

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

  // Zero-initialize all fields
  memset(client, 0, sizeof(*client));

  /* Connection State */
  client->sockfd = INVALID_SOCKET_VALUE;
  atomic_store(&client->connection_active, false);
  atomic_store(&client->connection_lost, false);
  atomic_store(&client->should_reconnect, false);
  client->my_client_id = 0;
  memset(client->server_ip, 0, sizeof(client->server_ip));
  client->encryption_enabled = false;

  // Initialize send mutex
  if (mutex_init(&client->send_mutex) != 0) {
    log_error("Failed to initialize send mutex");
    SAFE_FREE(client);
    return NULL;
  }

  /* Audio State */
  memset(&client->audio_ctx, 0, sizeof(client->audio_ctx));
  memset(client->audio_send_queue, 0, sizeof(client->audio_send_queue));
  client->audio_send_queue_head = 0;
  client->audio_send_queue_tail = 0;
  client->audio_send_queue_initialized = false;
  atomic_store(&client->audio_sender_should_exit, false);

  // Initialize audio queue mutex and condition variable
  if (mutex_init(&client->audio_send_queue_mutex) != 0) {
    log_error("Failed to initialize audio queue mutex");
    mutex_destroy(&client->send_mutex);
    SAFE_FREE(client);
    return NULL;
  }

  if (cond_init(&client->audio_send_queue_cond) != 0) {
    log_error("Failed to initialize audio queue cond");
    mutex_destroy(&client->audio_send_queue_mutex);
    mutex_destroy(&client->send_mutex);
    SAFE_FREE(client);
    return NULL;
  }

  client->audio_capture_thread_created = false;
  client->audio_sender_thread_created = false;
  atomic_store(&client->audio_capture_thread_exited, false);

  /* Protocol State */
  client->data_thread_created = false;
  atomic_store(&client->data_thread_exited, false);
  client->last_active_count = 0;
  client->server_state_initialized = false;
  client->should_clear_before_next_frame = false;

  /* Capture State */
  client->capture_thread_created = false;
  atomic_store(&client->capture_thread_exited, false);

  /* Keepalive State */
  client->ping_thread_created = false;
  atomic_store(&client->ping_thread_exited, false);

  /* Display State */
  client->has_tty = false;
  atomic_store(&client->is_first_frame_of_connection, true);
  memset(&client->tty_info, 0, sizeof(client->tty_info));

  /* Crypto State */
  memset(&client->crypto_ctx, 0, sizeof(client->crypto_ctx));
  client->crypto_initialized = false;

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

#ifndef NDEBUG
  // Debug: verify all threads have exited
  if (client->audio_capture_thread_created && !atomic_load(&client->audio_capture_thread_exited)) {
    log_warn("Destroying client while audio capture thread may still be running");
  }
  if (client->data_thread_created && !atomic_load(&client->data_thread_exited)) {
    log_warn("Destroying client while data thread may still be running");
  }
  if (client->capture_thread_created && !atomic_load(&client->capture_thread_exited)) {
    log_warn("Destroying client while capture thread may still be running");
  }
  if (client->ping_thread_created && !atomic_load(&client->ping_thread_exited)) {
    log_warn("Destroying client while ping thread may still be running");
  }
#endif

  // Close socket if still open
  if (socket_is_valid(client->sockfd)) {
    close_socket_safe(client->sockfd);
    client->sockfd = INVALID_SOCKET_VALUE;
  }

  // Destroy synchronization primitives
  mutex_destroy(&client->send_mutex);
  mutex_destroy(&client->audio_send_queue_mutex);
  cond_destroy(&client->audio_send_queue_cond);

  // Free client structure
  SAFE_FREE(client);
  *client_ptr = NULL;

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

/**
 * @brief Cleanup connection resources
 */
void tcp_client_cleanup(tcp_client_t *client) {
  if (!client)
    return;

  // Close connection
  tcp_client_close(client);

  // Reset state flags
  atomic_store(&client->connection_lost, false);
  atomic_store(&client->should_reconnect, false);
  memset(client->server_ip, 0, sizeof(client->server_ip));
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

  // Determine if encryption should be used
  crypto_context_t *crypto_ctx = NULL;
  if (client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_ctx)) {
    crypto_ctx = crypto_handshake_get_context(&client->crypto_ctx);
  }

  // Send packet (encrypted if crypto context available)
  asciichat_error_t result = send_packet_secure(client->sockfd, type, data, len, crypto_ctx);

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
    unsigned int delay_us = get_reconnect_delay(reconnect_attempt);
    platform_sleep_usec(delay_us);
  }

  // Resolve server address using getaddrinfo() for IPv4/IPv6 support
  // Special handling for localhost: ensure we try both IPv6 (::1) and IPv4 (127.0.0.1)
  bool is_localhost =
      (strcmp(address, "localhost") == 0 || strcmp(address, "127.0.0.1") == 0 || strcmp(address, "::1") == 0);

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
      client->sockfd = socket_create(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (client->sockfd != INVALID_SOCKET_VALUE) {
        log_info("Trying IPv6 loopback connection to [::1]:%s...", port_str);
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
      client->sockfd = socket_create(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (client->sockfd != INVALID_SOCKET_VALUE) {
        log_info("Trying IPv4 loopback connection to 127.0.0.1:%s...", port_str);
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

      client->sockfd = socket_create(addr_iter->ai_family, addr_iter->ai_socktype, addr_iter->ai_protocol);
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

/* ============================================================================
 * Advanced Packet Sending Functions
 * ============================================================================ */

/**
 * @brief Send Opus-encoded audio frame
 *
 * @param client TCP client instance
 * @param opus_data Opus-encoded audio data
 * @param opus_size Size of encoded frame
 * @param sample_rate Sample rate in Hz
 * @param frame_duration Frame duration in milliseconds
 * @return 0 on success, negative on error
 */
int tcp_client_send_audio_opus(tcp_client_t *client, const uint8_t *opus_data, size_t opus_size, int sample_rate,
                               int frame_duration) {
  if (!client || !opus_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client or opus_data");
  }

  if (!atomic_load(&client->connection_active)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  mutex_lock(&client->send_mutex);

  // Recheck connection status inside mutex to prevent TOCTOU race
  if (!atomic_load(&client->connection_active) || client->sockfd == INVALID_SOCKET_VALUE) {
    mutex_unlock(&client->send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  // Get crypto context if encryption is enabled
  crypto_context_t *crypto_ctx = NULL;
  if (client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_ctx)) {
    crypto_ctx = crypto_handshake_get_context(&client->crypto_ctx);
  }

  // Build Opus packet with header
  size_t header_size = 16; // sample_rate (4), frame_duration (4), reserved (8)
  size_t total_size = header_size + opus_size;
  void *packet_data = buffer_pool_alloc(NULL, total_size);
  if (!packet_data) {
    mutex_unlock(&client->send_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for Opus packet: %zu bytes", total_size);
  }

  // Write header in network byte order
  uint8_t *buf = (uint8_t *)packet_data;
  uint32_t sr = HOST_TO_NET_U32((uint32_t)sample_rate);
  uint32_t fd = HOST_TO_NET_U32((uint32_t)frame_duration);
  memcpy(buf, &sr, 4);
  memcpy(buf + 4, &fd, 4);
  memset(buf + 8, 0, 8); // Reserved

  // Copy Opus data
  memcpy(buf + header_size, opus_data, opus_size);

  // Send packet with encryption if available
  asciichat_error_t result;
  if (crypto_ctx) {
    result = send_packet_secure(client->sockfd, PACKET_TYPE_AUDIO_OPUS, packet_data, total_size, crypto_ctx);
  } else {
    result = packet_send(client->sockfd, PACKET_TYPE_AUDIO_OPUS, packet_data, total_size);
  }

  buffer_pool_free(NULL, packet_data, total_size);
  mutex_unlock(&client->send_mutex);

  if (result != ASCIICHAT_OK) {
    tcp_client_signal_lost(client);
  }

  return result;
}

/**
 * @brief Send Opus audio batch packet
 *
 * @param client TCP client instance
 * @param opus_data Opus-encoded audio data (multiple frames)
 * @param opus_size Total size of Opus data
 * @param frame_sizes Array of individual frame sizes
 * @param frame_count Number of frames in batch
 * @return 0 on success, negative on error
 */
int tcp_client_send_audio_opus_batch(tcp_client_t *client, const uint8_t *opus_data, size_t opus_size,
                                     const uint16_t *frame_sizes, int frame_count) {
  if (!client || !opus_data || !frame_sizes) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client, opus_data, or frame_sizes");
  }

  if (!atomic_load(&client->connection_active)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  mutex_lock(&client->send_mutex);

  if (!atomic_load(&client->connection_active) || client->sockfd == INVALID_SOCKET_VALUE) {
    mutex_unlock(&client->send_mutex);
    return -1;
  }

  crypto_context_t *crypto_ctx = NULL;
  if (client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_ctx)) {
    crypto_ctx = crypto_handshake_get_context(&client->crypto_ctx);
  }

  // Opus uses 20ms frames at 48kHz
  int result =
      av_send_audio_opus_batch(client->sockfd, opus_data, opus_size, frame_sizes, 48000, 20, frame_count, crypto_ctx);

  mutex_unlock(&client->send_mutex);

  if (result < 0) {
    tcp_client_signal_lost(client);
  }

  return result;
}

/**
 * @brief Send terminal capabilities packet
 *
 * @param client TCP client instance
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 */
int tcp_client_send_terminal_capabilities(tcp_client_t *client, unsigned short width, unsigned short height) {
  if (!client) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client");
  }

  if (!atomic_load(&client->connection_active) || client->sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  // Get options from RCU state
  const options_t *opts = options_get();
  if (!opts) {
    log_error("Options not initialized");
    return -1;
  }

  // Detect terminal capabilities automatically
  terminal_capabilities_t caps = detect_terminal_capabilities();

  // Apply user's color mode override
  caps = apply_color_mode_override(caps);

  // Check if detection was reliable, use fallback only for auto-detection
  if (!caps.detection_reliable && (int)opts->color_mode == COLOR_MODE_AUTO) {
    log_warn("Terminal capability detection not reliable, using fallback");
    SAFE_MEMSET(&caps, sizeof(caps), 0, sizeof(caps));
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 2;
    caps.capabilities = 0;
    SAFE_STRNCPY(caps.term_type, "unknown", sizeof(caps.term_type));
    SAFE_STRNCPY(caps.colorterm, "", sizeof(caps.colorterm));
    caps.detection_reliable = 0;
  }

  // Convert to network packet format
  terminal_capabilities_packet_t net_packet;
  net_packet.capabilities = HOST_TO_NET_U32(caps.capabilities);
  net_packet.color_level = HOST_TO_NET_U32(caps.color_level);
  net_packet.color_count = HOST_TO_NET_U32(caps.color_count);
  net_packet.render_mode = HOST_TO_NET_U32(caps.render_mode);
  net_packet.width = HOST_TO_NET_U16(width);
  net_packet.height = HOST_TO_NET_U16(height);
  net_packet.palette_type = HOST_TO_NET_U32(opts->palette_type);
  net_packet.utf8_support = HOST_TO_NET_U32(caps.utf8_support ? 1 : 0);

  if (opts->palette_type == PALETTE_CUSTOM && opts->palette_custom_set) {
    SAFE_STRNCPY(net_packet.palette_custom, opts->palette_custom, sizeof(net_packet.palette_custom));
    net_packet.palette_custom[sizeof(net_packet.palette_custom) - 1] = '\0';
  } else {
    SAFE_MEMSET(net_packet.palette_custom, sizeof(net_packet.palette_custom), 0, sizeof(net_packet.palette_custom));
  }

  // Set desired FPS (from global g_max_fps if available, otherwise from caps)
  extern int g_max_fps; // Will be passed via options in future refactoring
  if (g_max_fps > 0) {
    net_packet.desired_fps = (uint8_t)(g_max_fps > 144 ? 144 : g_max_fps);
  } else {
    net_packet.desired_fps = caps.desired_fps;
  }

  if (net_packet.desired_fps == 0) {
    net_packet.desired_fps = DEFAULT_MAX_FPS;
  }

  SAFE_STRNCPY(net_packet.term_type, caps.term_type, sizeof(net_packet.term_type));
  net_packet.term_type[sizeof(net_packet.term_type) - 1] = '\0';

  SAFE_STRNCPY(net_packet.colorterm, caps.colorterm, sizeof(net_packet.colorterm));
  net_packet.colorterm[sizeof(net_packet.colorterm) - 1] = '\0';

  net_packet.detection_reliable = caps.detection_reliable;
  net_packet.utf8_support = opts->force_utf8 ? 1 : 0;

  SAFE_MEMSET(net_packet.reserved, sizeof(net_packet.reserved), 0, sizeof(net_packet.reserved));

  return tcp_client_send_packet(client, PACKET_TYPE_CLIENT_CAPABILITIES, &net_packet, sizeof(net_packet));
}

/**
 * @brief Send client join packet
 *
 * @param client TCP client instance
 * @param display_name Client display name
 * @param capabilities Client capability flags
 * @return 0 on success, negative on error
 */
int tcp_client_send_join(tcp_client_t *client, const char *display_name, uint32_t capabilities) {
  if (!client) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client");
  }

  if (!atomic_load(&client->connection_active) || client->sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  // Build CLIENT_JOIN packet
  client_info_packet_t join_packet;
  SAFE_MEMSET(&join_packet, sizeof(join_packet), 0, sizeof(join_packet));
  join_packet.client_id = HOST_TO_NET_U32(0); // Will be assigned by server
  SAFE_SNPRINTF(join_packet.display_name, MAX_DISPLAY_NAME_LEN, "%s", display_name ? display_name : "Unknown");
  join_packet.capabilities = HOST_TO_NET_U32(capabilities);

  int send_result = tcp_client_send_packet(client, PACKET_TYPE_CLIENT_JOIN, &join_packet, sizeof(join_packet));
  if (send_result == 0) {
    mutex_lock(&client->send_mutex);
    bool active = atomic_load(&client->connection_active);
    socket_t socket_snapshot = client->sockfd;
    crypto_context_t *crypto_ctx = NULL;
    if (client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_ctx)) {
      crypto_ctx = crypto_handshake_get_context(&client->crypto_ctx);
    }
    if (active && socket_snapshot != INVALID_SOCKET_VALUE) {
      (void)log_network_message(
          socket_snapshot, (const struct crypto_context_t *)crypto_ctx, LOG_INFO, REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER,
          "CLIENT_JOIN sent (display=\"%s\", capabilities=0x%x)", join_packet.display_name, capabilities);
    }
    mutex_unlock(&client->send_mutex);
  }
  return send_result;
}

/**
 * @brief Send stream start packet
 *
 * @param client TCP client instance
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 */
int tcp_client_send_stream_start(tcp_client_t *client, uint32_t stream_type) {
  if (!client) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client");
  }

  if (!atomic_load(&client->connection_active) || client->sockfd == INVALID_SOCKET_VALUE) {
    return -1;
  }

  uint32_t type_data = HOST_TO_NET_U32(stream_type);
  return tcp_client_send_packet(client, PACKET_TYPE_STREAM_START, &type_data, sizeof(type_data));
}

/**
 * @brief Send audio batch packet
 *
 * @param client TCP client instance
 * @param samples Audio sample buffer
 * @param num_samples Number of samples in buffer
 * @param batch_count Number of chunks in batch
 * @return 0 on success, negative on error
 */
int tcp_client_send_audio_batch(tcp_client_t *client, const float *samples, int num_samples, int batch_count) {
  if (!client || !samples) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client or samples");
  }

  if (!atomic_load(&client->connection_active)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  mutex_lock(&client->send_mutex);

  if (!atomic_load(&client->connection_active) || client->sockfd == INVALID_SOCKET_VALUE) {
    mutex_unlock(&client->send_mutex);
    return -1;
  }

  crypto_context_t *crypto_ctx = NULL;
  if (client->crypto_initialized && crypto_handshake_is_ready(&client->crypto_ctx)) {
    crypto_ctx = crypto_handshake_get_context(&client->crypto_ctx);
  }

  int result = send_audio_batch_packet(client->sockfd, samples, num_samples, batch_count, crypto_ctx);

  mutex_unlock(&client->send_mutex);

  if (result < 0) {
    tcp_client_signal_lost(client);
  }

  return result;
}

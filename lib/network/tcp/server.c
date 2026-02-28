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
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/thread_pool.h>
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

  // Note: client_handler is optional - some users may use tcp_server just for socket setup
  // and implement their own accept loop (like ascii-chat server with its cleanup logic)

  // Initialize server state
  memset(server, 0, sizeof(*server));
  server->listen_socket = INVALID_SOCKET_VALUE;
  server->listen_socket6 = INVALID_SOCKET_VALUE;
  atomic_store(&server->running, true);
  server->config = *config; // Copy config

  // Initialize client registry
  server->clients = NULL; // uthash starts with NULL
  server->cleanup_fn = NULL;
  if (rwlock_init(&server->clients_rwlock, "clients") != 0) {
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize clients read-write lock");
  }

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
    return SET_ERRNO(ERROR_NETWORK_BIND, "Failed to bind any sockets (IPv4 and IPv6 both failed)");
  }

  /* Register server with named registry, identified by port */
  char port_name[32];
  snprintf(port_name, sizeof(port_name), "server:%d", server->config.port);
  NAMED_REGISTER(server, port_name, "tcp_server", "0x%tx");

  return ASCIICHAT_OK;
}

asciichat_error_t tcp_server_run(tcp_server_t *server) {
  if (!server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server is NULL");
  }

  if (!server->config.client_handler) {
    return SET_ERRNO(ERROR_INVALID_PARAM,
                     "client_handler is required for tcp_server_run() - use custom accept loop if handler is NULL");
  }

  log_debug("TCP server starting accept loop...");

  while (atomic_load(&server->running)) {
    // Build fd_set for select()
    fd_set read_fds;
    socket_fd_zero(&read_fds);
    socket_t max_fd = 0;

    // Add IPv4 socket if available
    if (server->listen_socket != INVALID_SOCKET_VALUE) {
      socket_fd_set(server->listen_socket, &read_fds);
      max_fd = server->listen_socket > max_fd ? server->listen_socket : max_fd;
    }

    // Add IPv6 socket if available
    if (server->listen_socket6 != INVALID_SOCKET_VALUE) {
      socket_fd_set(server->listen_socket6, &read_fds);
      max_fd = server->listen_socket6 > max_fd ? server->listen_socket6 : max_fd;
    }

    // Use timeout from config (defaults to 1 second if not set)
    // Convert double seconds to tv_sec and tv_usec
    double timeout_sec_double = server->config.accept_timeout_sec > 0 ? server->config.accept_timeout_sec : 1.0;
    time_t timeout_sec = (time_t)timeout_sec_double;
    long timeout_usec = (long)((timeout_sec_double - timeout_sec) * (double)US_PER_SEC_INT);
    struct timeval timeout = {.tv_sec = timeout_sec, .tv_usec = timeout_usec};

    int select_result = socket_select((int)(max_fd + 1), &read_fds, NULL, NULL, &timeout);

    if (select_result < 0) {
      // Check if interrupted by signal (expected during shutdown)
      int err = socket_get_last_error();
      if (err == EINTR) {
        // Signal interrupt (e.g., SIGTERM, SIGINT) - check running flag and continue
        log_debug("select() interrupted by signal");
        continue;
      }
      // Actual error - socket_get_error_string() returns last socket error
      log_error("select() failed in accept loop: %s (errno=%d)", socket_get_error_string(), err);
      continue;
    }

    if (select_result == 0) {
      // Timeout - invoke status update callback if configured
      if (server->config.status_update_fn) {
        server->config.status_update_fn(server->config.status_update_data);
      }
      continue;
    }

    // Check which socket has incoming connection
    socket_t ready_socket = INVALID_SOCKET_VALUE;
    if (server->listen_socket != INVALID_SOCKET_VALUE && socket_fd_isset(server->listen_socket, &read_fds)) {
      ready_socket = server->listen_socket;
    } else if (server->listen_socket6 != INVALID_SOCKET_VALUE && socket_fd_isset(server->listen_socket6, &read_fds)) {
      ready_socket = server->listen_socket6;
    }

    if (ready_socket == INVALID_SOCKET_VALUE) {
      // Spurious wakeup
      continue;
    }

    // Accept connection
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    socket_t client_socket = accept(ready_socket, (struct sockaddr *)&client_addr, &client_addr_len);

    if (client_socket == INVALID_SOCKET_VALUE) {
      log_warn("Failed to accept connection");
      continue;
    }

    // Format client IP for logging
    char client_ip[INET6_ADDRSTRLEN];
    int addr_family = (client_addr.ss_family == AF_INET) ? AF_INET : AF_INET6;
    if (format_ip_address(addr_family, (struct sockaddr *)&client_addr, client_ip, sizeof(client_ip)) != ASCIICHAT_OK) {
      SAFE_STRNCPY(client_ip, "(unknown)", sizeof(client_ip));
    }

    log_debug("Accepted connection from %s", client_ip);

    // Allocate client context
    tcp_client_context_t *ctx = SAFE_MALLOC(sizeof(tcp_client_context_t), tcp_client_context_t *);
    if (!ctx) {
      log_error("Failed to allocate client context");
      socket_close(client_socket);
      continue;
    }

    ctx->client_socket = client_socket;
    ctx->addr = client_addr;
    ctx->addr_len = client_addr_len;
    ctx->user_data = server->config.user_data;

    // Spawn client handler thread
    // Handler is responsible for:
    // 1. Allocating client_data
    // 2. Calling tcp_server_add_client() to register
    // 3. Spawning additional worker threads via tcp_server_spawn_thread() if needed
    // 4. Processing client requests
    // 5. Calling tcp_server_remove_client() on disconnect
    // 6. Closing socket and freeing ctx
    asciichat_thread_t thread;
    if (asciichat_thread_create(&thread, "tcp_client", server->config.client_handler, ctx) != 0) {
      log_error("Failed to create client handler thread for %s", client_ip);
      SAFE_FREE(ctx);
      socket_close(client_socket);
      continue;
    }

    // Thread is detached (handler is responsible for cleanup)
    (void)thread; // Suppress unused warning
  }

  log_debug("TCP server accept loop exited");
  return ASCIICHAT_OK;
}

void tcp_server_destroy(tcp_server_t *server) {
  if (!server) {
    return;
  }

  log_debug("Shutting down TCP server...");

  // Signal server to stop
  atomic_store(&server->running, false);

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

  // Clean up client registry
  rwlock_wrlock(&server->clients_rwlock);

  tcp_client_entry_t *entry = NULL, *tmp = NULL;
  HASH_ITER(hh, server->clients, entry, tmp) {
    // Call cleanup callback if set
    if (server->cleanup_fn && entry->client_data) {
      server->cleanup_fn(entry->client_data);
    }

    // Destroy thread pool (stops all threads and frees resources)
    if (entry->threads) {
      thread_pool_destroy(entry->threads);
      entry->threads = NULL;
    }

    HASH_DEL(server->clients, entry);
    SAFE_FREE(entry);
  }
  server->clients = NULL;

  rwlock_wrunlock(&server->clients_rwlock);
  rwlock_destroy(&server->clients_rwlock);

  // Note: This function does NOT wait for client threads to exit
  // Caller is responsible for thread lifecycle management

  log_debug("TCP server shutdown complete");
}

// ============================================================================
// Client Management Functions
// ============================================================================

void tcp_server_set_cleanup_callback(tcp_server_t *server, tcp_client_cleanup_fn cleanup_fn) {
  if (!server) {
    return;
  }
  server->cleanup_fn = cleanup_fn;
}

asciichat_error_t tcp_server_add_client(tcp_server_t *server, socket_t socket, void *client_data) {
  if (!server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server is NULL");
  }

  if (socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "socket is invalid");
  }

  // Allocate new entry
  tcp_client_entry_t *entry = SAFE_MALLOC(sizeof(tcp_client_entry_t), tcp_client_entry_t *);
  if (!entry) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate client entry");
  }

  entry->socket = socket;
  entry->client_data = client_data;

  // Create thread pool for this client
  char pool_name[64];
  SAFE_SNPRINTF(pool_name, sizeof(pool_name), "client_%d", socket);
  entry->threads = thread_pool_create(pool_name);
  if (!entry->threads) {
    SAFE_FREE(entry);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create thread pool for client");
  }

  // Add to hash table (thread-safe with write lock)
  rwlock_wrlock(&server->clients_rwlock);
  HASH_ADD(hh, server->clients, socket, sizeof(socket_t), entry);
  rwlock_wrunlock(&server->clients_rwlock);

  log_debug("Added client socket=%d to registry", socket);
  return ASCIICHAT_OK;
}

asciichat_error_t tcp_server_remove_client(tcp_server_t *server, socket_t socket) {
  if (!server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server is NULL");
  }

  rwlock_wrlock(&server->clients_rwlock);

  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &socket, sizeof(socket_t), entry);

  if (!entry) {
    rwlock_wrunlock(&server->clients_rwlock);
    // Already removed (e.g., during shutdown) - this is fine
    log_debug("Client socket=%d already removed from registry", socket);
    return ASCIICHAT_OK;
  }

  // Call cleanup callback if set
  if (server->cleanup_fn && entry->client_data) {
    server->cleanup_fn(entry->client_data);
  }

  // Destroy thread pool (stops all threads and frees resources)
  if (entry->threads) {
    thread_pool_destroy(entry->threads);
    entry->threads = NULL;
  }

  HASH_DEL(server->clients, entry);
  SAFE_FREE(entry);

  rwlock_wrunlock(&server->clients_rwlock);

  log_debug("Removed client socket=%d from registry", socket);
  return ASCIICHAT_OK;
}

asciichat_error_t tcp_server_get_client(tcp_server_t *server, socket_t socket, void **out_data) {
  if (!server || !out_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or out_data is NULL");
  }

  rwlock_rdlock(&server->clients_rwlock);

  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &socket, sizeof(socket_t), entry);

  if (!entry) {
    *out_data = NULL;
    rwlock_rdunlock(&server->clients_rwlock);
    return SET_ERRNO(ERROR_INVALID_STATE, "Client socket=%d not in registry", socket);
  }

  *out_data = entry->client_data;
  rwlock_rdunlock(&server->clients_rwlock);

  return ASCIICHAT_OK;
}

void tcp_server_foreach_client(tcp_server_t *server, tcp_client_foreach_fn callback, void *user_arg) {
  if (!server || !callback) {
    return;
  }

  rwlock_rdlock(&server->clients_rwlock);

  tcp_client_entry_t *entry, *tmp;
  HASH_ITER(hh, server->clients, entry, tmp) {
    callback(entry->socket, entry->client_data, user_arg);
  }

  rwlock_rdunlock(&server->clients_rwlock);
}

size_t tcp_server_get_client_count(tcp_server_t *server) {
  if (!server) {
    return 0;
  }

  rwlock_rdlock(&server->clients_rwlock);
  size_t count = HASH_COUNT(server->clients);
  rwlock_rdunlock(&server->clients_rwlock);

  return count;
}

// ============================================================================
// Client Context Utilities
// ============================================================================

const char *tcp_client_context_get_ip(const tcp_client_context_t *ctx, char *buf, size_t len) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "ctx is NULL");
    return NULL;
  }
  if (!buf) {
    SET_ERRNO(ERROR_INVALID_PARAM, "buf is NULL");
    return NULL;
  }
  if (len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "len is 0");
    return NULL;
  }

  // Determine address family
  int addr_family = (ctx->addr.ss_family == AF_INET) ? AF_INET : AF_INET6;

  // Format IP address using existing utility
  if (format_ip_address(addr_family, (struct sockaddr *)&ctx->addr, buf, len) != 0) {
    return NULL;
  }

  return buf;
}

int tcp_client_context_get_port(const tcp_client_context_t *ctx) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "ctx is NULL");
    return -1;
  }

  // Extract port based on address family
  if (ctx->addr.ss_family == AF_INET) {
    struct sockaddr_in *addr_in = (struct sockaddr_in *)&ctx->addr;
    return ntohs(addr_in->sin_port);
  } else if (ctx->addr.ss_family == AF_INET6) {
    struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&ctx->addr;
    return ntohs(addr_in6->sin6_port);
  }

  SET_ERRNO(ERROR_INVALID_STATE, "Unknown address family: %d", ctx->addr.ss_family);
  return -1;
}

void tcp_server_reject_client(socket_t socket, const char *reason) {
  if (socket == INVALID_SOCKET_VALUE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "socket is INVALID_SOCKET_VALUE");
    return;
  }

  log_warn("Rejecting client connection: %s", reason ? reason : "unknown reason");
  socket_close(socket);
}

// ============================================================================
// Client Thread Pool Management
// ============================================================================

asciichat_error_t tcp_server_spawn_thread(tcp_server_t *server, socket_t client_socket, void *(*thread_func)(void *),
                                          void *thread_arg, int stop_id, const char *thread_name) {
  if (!server || !thread_func) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or thread_func is NULL");
  }

  // For WebRTC clients (no socket): create thread directly without TCP server thread pool tracking
  // This allows render threads to work for both TCP and WebRTC clients
  if (client_socket == INVALID_SOCKET_VALUE) {
    // Extract thread handle from thread_arg if it's a client_info_t
    // We need to store the thread handle somewhere, but for now just create the thread
    // The caller must manage the thread handle directly for WebRTC clients
    log_debug("Spawning standalone thread '%s' (no socket, WebRTC client)", thread_name ? thread_name : "unnamed");

    // For WebRTC clients, we can't use thread_pool since there's no socket entry
    // Instead, create the thread directly - the caller must handle the thread
    // This is a hack to allow reusing tcp_server_spawn_thread for WebRTC
    // In the future, consider a unified thread management system
    asciichat_thread_t temp_thread;
    asciichat_error_t result = asciichat_thread_create(&temp_thread, "tcp_worker", thread_func, thread_arg);
    if (result != ASCIICHAT_OK) {
      return result;
    }

    // Note: temp_thread handle is lost here, but the thread is running
    // The caller must manage thread lifecycle for WebRTC clients differently
    (void)temp_thread; // Suppress unused warning
    return ASCIICHAT_OK;
  }

  // Find client entry
  rwlock_rdlock(&server->clients_rwlock);
  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &client_socket, sizeof(socket_t), entry);

  if (!entry) {
    rwlock_rdunlock(&server->clients_rwlock);
    return SET_ERRNO(ERROR_NOT_FOUND, "Client socket=%d not in registry", client_socket);
  }

  // Spawn thread in client's thread pool
  asciichat_error_t result = thread_pool_spawn(entry->threads, thread_func, thread_arg, stop_id, thread_name);

  rwlock_rdunlock(&server->clients_rwlock);

  if (result != ASCIICHAT_OK) {
    return result;
  }

  size_t thread_count = thread_pool_get_count(entry->threads);
  log_debug("Spawned thread '%s' (stop_id=%d) for client socket=%d (total_threads=%zu)",
            thread_name ? thread_name : "unnamed", stop_id, client_socket, thread_count);

  return ASCIICHAT_OK;
}

asciichat_error_t tcp_server_stop_client_threads(tcp_server_t *server, socket_t client_socket) {
  if (!server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server is NULL");
  }

  if (client_socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "client_socket is invalid");
  }

  // Find client entry
  rwlock_rdlock(&server->clients_rwlock);
  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &client_socket, sizeof(socket_t), entry);

  if (!entry) {
    rwlock_rdunlock(&server->clients_rwlock);
    return SET_ERRNO(ERROR_NOT_FOUND, "Client socket=%d not in registry", client_socket);
  }

  // Stop all threads in client's thread pool (in stop_id order)
  asciichat_error_t result = ASCIICHAT_OK;
  if (entry->threads) {
    result = thread_pool_stop_all(entry->threads);
  }

  rwlock_rdunlock(&server->clients_rwlock);

  log_debug("All threads stopped for client socket=%d", client_socket);
  return result;
}

asciichat_error_t tcp_server_get_thread_count(tcp_server_t *server, socket_t client_socket, size_t *count) {
  if (!server || !count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or count is NULL");
  }

  if (client_socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "client_socket is invalid");
  }

  *count = 0;

  // Find client entry
  rwlock_rdlock(&server->clients_rwlock);
  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &client_socket, sizeof(socket_t), entry);

  if (!entry) {
    rwlock_rdunlock(&server->clients_rwlock);
    return SET_ERRNO(ERROR_NOT_FOUND, "Client socket=%d not in registry", client_socket);
  }

  // Get thread count from pool
  if (entry->threads) {
    *count = thread_pool_get_count(entry->threads);
  }

  rwlock_rdunlock(&server->clients_rwlock);

  return ASCIICHAT_OK;
}

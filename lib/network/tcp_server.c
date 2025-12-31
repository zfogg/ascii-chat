/**
 * @file network/tcp_server.c
 * @brief 🌐 Generic TCP server with dual-stack IPv4/IPv6 support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <netdb.h>

#include "network/tcp_server.h"
#include "common.h"
#include "log/logging.h"
#include "platform/socket.h"
#include "platform/thread.h"
#include "util/ip.h"

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

  socket_t server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
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
  if (mutex_init(&server->clients_mutex) != 0) {
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize clients mutex");
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

  log_info("TCP server starting accept loop...");

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
    int timeout_sec = server->config.accept_timeout_sec > 0 ? server->config.accept_timeout_sec : 1;
    struct timeval timeout = {.tv_sec = timeout_sec, .tv_usec = 0};

    int select_result = socket_select((int)(max_fd + 1), &read_fds, NULL, NULL, &timeout);

    if (select_result < 0) {
      log_error("select() failed in accept loop");
      continue;
    }

    if (select_result == 0) {
      // Timeout - check running flag and continue
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

    log_info("Accepted connection from %s", client_ip);

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
    asciithread_t thread;
    if (ascii_thread_create(&thread, server->config.client_handler, ctx) != 0) {
      log_error("Failed to create client handler thread for %s", client_ip);
      SAFE_FREE(ctx);
      socket_close(client_socket);
      continue;
    }

    // Thread is detached (handler is responsible for cleanup)
    (void)thread; // Suppress unused warning
  }

  log_info("TCP server accept loop exited");
  return ASCIICHAT_OK;
}

void tcp_server_shutdown(tcp_server_t *server) {
  if (!server) {
    return;
  }

  log_info("Shutting down TCP server...");

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
  mutex_lock(&server->clients_mutex);

  tcp_client_entry_t *entry, *tmp;
  HASH_ITER(hh, server->clients, entry, tmp) {
    // Call cleanup callback if set
    if (server->cleanup_fn && entry->client_data) {
      server->cleanup_fn(entry->client_data);
    }

    // Clean up thread pool
    mutex_lock(&entry->threads_mutex);
    tcp_client_thread_t *thread = entry->threads;
    while (thread) {
      tcp_client_thread_t *next = thread->next;
      SAFE_FREE(thread);
      thread = next;
    }
    mutex_unlock(&entry->threads_mutex);
    mutex_destroy(&entry->threads_mutex);

    HASH_DEL(server->clients, entry);
    SAFE_FREE(entry);
  }
  server->clients = NULL;

  mutex_unlock(&server->clients_mutex);
  mutex_destroy(&server->clients_mutex);

  // Note: This function does NOT wait for client threads to exit
  // Caller is responsible for thread lifecycle management

  log_info("TCP server shutdown complete");
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
  entry->threads = NULL;
  entry->thread_count = 0;

  // Initialize thread pool mutex
  if (mutex_init(&entry->threads_mutex) != 0) {
    SAFE_FREE(entry);
    return SET_ERRNO(ERROR_INTERNAL, "Failed to initialize thread pool mutex");
  }

  // Add to hash table (thread-safe)
  mutex_lock(&server->clients_mutex);
  HASH_ADD(hh, server->clients, socket, sizeof(socket_t), entry);
  mutex_unlock(&server->clients_mutex);

  log_debug("Added client socket=%d to registry", socket);
  return ASCIICHAT_OK;
}

asciichat_error_t tcp_server_remove_client(tcp_server_t *server, socket_t socket) {
  if (!server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server is NULL");
  }

  mutex_lock(&server->clients_mutex);

  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &socket, sizeof(socket_t), entry);

  if (!entry) {
    mutex_unlock(&server->clients_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Client socket=%d not in registry", socket);
  }

  // Call cleanup callback if set
  if (server->cleanup_fn && entry->client_data) {
    server->cleanup_fn(entry->client_data);
  }

  // Clean up thread pool
  mutex_lock(&entry->threads_mutex);
  tcp_client_thread_t *thread = entry->threads;
  while (thread) {
    tcp_client_thread_t *next = thread->next;
    SAFE_FREE(thread);
    thread = next;
  }
  entry->threads = NULL;
  entry->thread_count = 0;
  mutex_unlock(&entry->threads_mutex);

  // Destroy thread pool mutex
  mutex_destroy(&entry->threads_mutex);

  HASH_DEL(server->clients, entry);
  SAFE_FREE(entry);

  mutex_unlock(&server->clients_mutex);

  log_debug("Removed client socket=%d from registry", socket);
  return ASCIICHAT_OK;
}

asciichat_error_t tcp_server_get_client(tcp_server_t *server, socket_t socket, void **out_data) {
  if (!server || !out_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or out_data is NULL");
  }

  mutex_lock(&server->clients_mutex);

  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &socket, sizeof(socket_t), entry);

  if (!entry) {
    *out_data = NULL;
    mutex_unlock(&server->clients_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "Client socket=%d not in registry", socket);
  }

  *out_data = entry->client_data;
  mutex_unlock(&server->clients_mutex);

  return ASCIICHAT_OK;
}

void tcp_server_foreach_client(tcp_server_t *server, tcp_client_foreach_fn callback, void *user_arg) {
  if (!server || !callback) {
    return;
  }

  mutex_lock(&server->clients_mutex);

  tcp_client_entry_t *entry, *tmp;
  HASH_ITER(hh, server->clients, entry, tmp) {
    callback(entry->socket, entry->client_data, user_arg);
  }

  mutex_unlock(&server->clients_mutex);
}

size_t tcp_server_get_client_count(tcp_server_t *server) {
  if (!server) {
    return 0;
  }

  mutex_lock(&server->clients_mutex);
  size_t count = HASH_COUNT(server->clients);
  mutex_unlock(&server->clients_mutex);

  return count;
}

// ============================================================================
// Client Thread Pool Management
// ============================================================================

asciichat_error_t tcp_server_spawn_thread(tcp_server_t *server, socket_t client_socket, void *(*thread_func)(void *),
                                          void *thread_arg, int stop_id, const char *thread_name) {
  if (!server || !thread_func) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or thread_func is NULL");
  }

  if (client_socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "client_socket is invalid");
  }

  // Find client entry
  mutex_lock(&server->clients_mutex);
  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &client_socket, sizeof(socket_t), entry);

  if (!entry) {
    mutex_unlock(&server->clients_mutex);
    return SET_ERRNO(ERROR_NOT_FOUND, "Client socket=%d not in registry", client_socket);
  }

  // Allocate thread entry
  tcp_client_thread_t *thread_entry = SAFE_MALLOC(sizeof(tcp_client_thread_t), tcp_client_thread_t *);
  if (!thread_entry) {
    mutex_unlock(&server->clients_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate thread entry");
  }

  thread_entry->stop_id = stop_id;
  thread_entry->thread_func = thread_func;
  thread_entry->thread_arg = thread_arg;
  thread_entry->next = NULL;

  // Copy thread name (truncate if necessary)
  if (thread_name) {
    SAFE_STRNCPY(thread_entry->name, thread_name, sizeof(thread_entry->name));
  } else {
    SAFE_SNPRINTF(thread_entry->name, sizeof(thread_entry->name), "worker-%d", stop_id);
  }

  // Create thread
  if (ascii_thread_create(&thread_entry->thread, thread_func, thread_arg) != 0) {
    SAFE_FREE(thread_entry);
    mutex_unlock(&server->clients_mutex);
    return SET_ERRNO(ERROR_INTERNAL, "Failed to create thread '%s' for client socket=%d", thread_entry->name,
                     client_socket);
  }

  // Add to thread list (sorted by stop_id)
  mutex_lock(&entry->threads_mutex);

  if (!entry->threads || entry->threads->stop_id > stop_id) {
    // Insert at head
    thread_entry->next = entry->threads;
    entry->threads = thread_entry;
  } else {
    // Find insertion point (maintain sorted order by stop_id)
    tcp_client_thread_t *prev = entry->threads;
    while (prev->next && prev->next->stop_id <= stop_id) {
      prev = prev->next;
    }
    thread_entry->next = prev->next;
    prev->next = thread_entry;
  }

  entry->thread_count++;
  mutex_unlock(&entry->threads_mutex);
  mutex_unlock(&server->clients_mutex);

  log_debug("Spawned thread '%s' (stop_id=%d) for client socket=%d (total_threads=%zu)", thread_entry->name, stop_id,
            client_socket, entry->thread_count);

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
  mutex_lock(&server->clients_mutex);
  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &client_socket, sizeof(socket_t), entry);

  if (!entry) {
    mutex_unlock(&server->clients_mutex);
    return SET_ERRNO(ERROR_NOT_FOUND, "Client socket=%d not in registry", client_socket);
  }

  // Stop threads in stop_id order (already sorted)
  mutex_lock(&entry->threads_mutex);

  log_debug("Stopping %zu threads for client socket=%d in stop_id order", entry->thread_count, client_socket);

  tcp_client_thread_t *thread = entry->threads;
  while (thread) {
    log_debug("Joining thread '%s' (stop_id=%d) for client socket=%d", thread->name, thread->stop_id, client_socket);

    // Join thread (wait for it to exit)
    if (ascii_thread_join(&thread->thread, NULL) != 0) {
      log_warn("Failed to join thread '%s' for client socket=%d", thread->name, client_socket);
    }

    tcp_client_thread_t *next = thread->next;
    SAFE_FREE(thread);
    thread = next;
  }

  entry->threads = NULL;
  entry->thread_count = 0;

  mutex_unlock(&entry->threads_mutex);
  mutex_unlock(&server->clients_mutex);

  log_debug("All threads stopped for client socket=%d", client_socket);
  return ASCIICHAT_OK;
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
  mutex_lock(&server->clients_mutex);
  tcp_client_entry_t *entry = NULL;
  HASH_FIND(hh, server->clients, &client_socket, sizeof(socket_t), entry);

  if (!entry) {
    mutex_unlock(&server->clients_mutex);
    return SET_ERRNO(ERROR_NOT_FOUND, "Client socket=%d not in registry", client_socket);
  }

  mutex_lock(&entry->threads_mutex);
  *count = entry->thread_count;
  mutex_unlock(&entry->threads_mutex);
  mutex_unlock(&server->clients_mutex);

  return ASCIICHAT_OK;
}

/**
 * @file network/parallel_connect.c
 * @brief Parallel IPv4/IPv6 connection implementation
 */

#include "parallel_connect.h"
#include "log/logging.h"
#include "platform/abstraction.h"
#include <netdb.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#endif

typedef struct {
  socket_t socket;
  struct sockaddr_storage addr;
  socklen_t addr_len;
  int family;
  const char *family_name;
  uint32_t timeout_ms;
  volatile bool connected;
  volatile bool done;
  volatile socket_t *winner_socket;
  volatile bool *winner_found;
  mutex_t *lock;
  cond_t *signal;

  // Optional exit callback for graceful shutdown
  parallel_connect_should_exit_fn should_exit_callback;
  void *callback_data;
} connection_attempt_t;

static void *attempt_connection_thread(void *arg) {
  connection_attempt_t *attempt = (connection_attempt_t *)arg;
  if (!attempt)
    return NULL;

  log_debug("PCONN: [%s] Starting connection attempt", attempt->family_name);

  attempt->socket = socket(attempt->family, SOCK_STREAM, 0);
  if (attempt->socket == INVALID_SOCKET_VALUE) {
    log_debug("PCONN: [%s] Failed to create socket", attempt->family_name);
    goto done;
  }

  // Check if winner already found
  mutex_lock(attempt->lock);
  if (*attempt->winner_found) {
    log_debug("PCONN: [%s] Winner already found, aborting", attempt->family_name);
    mutex_unlock(attempt->lock);
    socket_close(attempt->socket);
    attempt->socket = INVALID_SOCKET_VALUE;
    goto done;
  }
  mutex_unlock(attempt->lock);

  // Set socket to non-blocking
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(attempt->socket, FIONBIO, &mode);
#else
  int flags = fcntl(attempt->socket, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(attempt->socket, F_SETFL, flags | O_NONBLOCK);
  }
#endif

  // Attempt non-blocking connect
  log_debug("PCONN: [%s] Attempting connect with %dms timeout", attempt->family_name, attempt->timeout_ms);
  int connect_result = connect(attempt->socket, (struct sockaddr *)&attempt->addr, attempt->addr_len);

#ifdef _WIN32
  int connect_error = WSAGetLastError();
  bool is_in_progress = (connect_error == WSAEWOULDBLOCK);
#else
  int connect_error = errno;
  bool is_in_progress = (connect_error == EINPROGRESS);
#endif

  if (connect_result == 0) {
    // Immediate success (rare)
    log_debug("PCONN: [%s] Connected immediately", attempt->family_name);
    attempt->connected = true;
  } else if (is_in_progress) {
    // Wait with select() using short timeouts so we can check for winner, exit callback, and exit early
    uint32_t elapsed_ms = 0;
    uint32_t check_interval_ms = 100; // Check every 100ms if winner found or exit requested

    while (elapsed_ms < attempt->timeout_ms) {
      // Check if caller requested shutdown/exit (e.g., SIGTERM signal handler set flag)
      if (attempt->should_exit_callback && attempt->should_exit_callback(attempt->callback_data)) {
        log_debug("PCONN: [%s] Exit requested via callback, aborting connection", attempt->family_name);
        break;
      }

      // Check if winner was already found (allow loser to exit early)
      mutex_lock(attempt->lock);
      bool should_exit = *attempt->winner_found;
      mutex_unlock(attempt->lock);

      if (should_exit) {
        log_debug("PCONN: [%s] Winner already found, exiting early", attempt->family_name);
        break;
      }

      fd_set writefds;
      FD_ZERO(&writefds);
      FD_SET(attempt->socket, &writefds);

      struct timeval tv;
      tv.tv_sec = check_interval_ms / 1000;
      tv.tv_usec = (check_interval_ms % 1000) * 1000;

      int select_result = select((int)attempt->socket + 1, NULL, &writefds, NULL, &tv);

      if (select_result > 0) {
        // Check if connection actually succeeded
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(attempt->socket, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);

        if (so_error == 0) {
          log_debug("PCONN: [%s] Connection succeeded", attempt->family_name);
          attempt->connected = true;
        } else {
          log_debug("PCONN: [%s] Connection failed: %d", attempt->family_name, so_error);
        }
        break;
      } else if (select_result < 0) {
        log_debug("PCONN: [%s] Select error", attempt->family_name);
        break;
      }

      elapsed_ms += check_interval_ms;
    }

    if (elapsed_ms >= attempt->timeout_ms && !attempt->connected) {
      log_debug("PCONN: [%s] Connection timeout after %dms", attempt->family_name, attempt->timeout_ms);
    }
  } else {
    log_debug("PCONN: [%s] Connect failed immediately: %d", attempt->family_name, connect_error);
  }

  // Signal winner if this succeeded
  if (attempt->connected) {
    mutex_lock(attempt->lock);
    if (!*attempt->winner_found) {
      log_info("PCONN: [%s] Won the race! Setting as winner", attempt->family_name);
      *attempt->winner_socket = attempt->socket;
      *attempt->winner_found = true;
      attempt->socket = INVALID_SOCKET_VALUE; // Don't close it - caller owns it
      cond_signal(attempt->signal);
    } else {
      log_debug("PCONN: [%s] Connected but another won already, closing", attempt->family_name);
      socket_close(attempt->socket);
      attempt->socket = INVALID_SOCKET_VALUE;
    }
    mutex_unlock(attempt->lock);
  } else {
    // Signal that we're done trying
    mutex_lock(attempt->lock);
    cond_signal(attempt->signal);
    mutex_unlock(attempt->lock);
  }

done:
  attempt->done = true;
  return NULL;
}

asciichat_error_t parallel_connect(const parallel_connect_config_t *config, socket_t *out_socket) {
  if (!config || !out_socket) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "config or out_socket is NULL");
  }

  *out_socket = INVALID_SOCKET_VALUE;

  // Resolve hostname
  log_debug("PCONN: Resolving %s:%d", config->hostname, config->port);

  struct addrinfo hints;
  struct addrinfo *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", config->port);

  int gai_result = getaddrinfo(config->hostname, port_str, &hints, &result);
  if (gai_result != 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to resolve %s: %s", config->hostname, gai_strerror(gai_result));
  }

  // Find first IPv4 and IPv6 addresses
  struct addrinfo *ipv4_addr = NULL;
  struct addrinfo *ipv6_addr = NULL;

  for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET && !ipv4_addr) {
      ipv4_addr = rp;
      log_debug("PCONN: Found IPv4 address");
    } else if (rp->ai_family == AF_INET6 && !ipv6_addr) {
      ipv6_addr = rp;
      log_debug("PCONN: Found IPv6 address");
    }
    if (ipv4_addr && ipv6_addr)
      break;
  }

  // Need at least one address
  if (!ipv4_addr && !ipv6_addr) {
    freeaddrinfo(result);
    return SET_ERRNO(ERROR_NETWORK, "No IPv4 or IPv6 addresses found for %s", config->hostname);
  }

  // Shared state
  mutex_t lock;
  cond_t signal;
  mutex_init(&lock);
  cond_init(&signal);

  volatile socket_t winner_socket = INVALID_SOCKET_VALUE;
  volatile bool winner_found = false;

  connection_attempt_t ipv4_attempt = {0};
  connection_attempt_t ipv6_attempt = {0};
  asciichat_thread_t ipv4_thread;
  asciichat_thread_t ipv6_thread;

  asciichat_thread_init(&ipv4_thread);
  asciichat_thread_init(&ipv6_thread);

  // Start IPv4 attempt if available
  if (ipv4_addr) {
    ipv4_attempt.family = AF_INET;
    ipv4_attempt.family_name = "IPv4";
    ipv4_attempt.timeout_ms = config->timeout_ms;
    ipv4_attempt.winner_socket = &winner_socket;
    ipv4_attempt.winner_found = &winner_found;
    ipv4_attempt.lock = &lock;
    ipv4_attempt.signal = &signal;
    ipv4_attempt.should_exit_callback = config->should_exit_callback;
    ipv4_attempt.callback_data = config->callback_data;
    memcpy(&ipv4_attempt.addr, ipv4_addr->ai_addr, ipv4_addr->ai_addrlen);
    ipv4_attempt.addr_len = ipv4_addr->ai_addrlen;

    asciichat_thread_create(&ipv4_thread, attempt_connection_thread, &ipv4_attempt);
  }

  // Start IPv6 attempt if available
  if (ipv6_addr) {
    ipv6_attempt.family = AF_INET6;
    ipv6_attempt.family_name = "IPv6";
    ipv6_attempt.timeout_ms = config->timeout_ms;
    ipv6_attempt.winner_socket = &winner_socket;
    ipv6_attempt.winner_found = &winner_found;
    ipv6_attempt.lock = &lock;
    ipv6_attempt.signal = &signal;
    ipv6_attempt.should_exit_callback = config->should_exit_callback;
    ipv6_attempt.callback_data = config->callback_data;
    memcpy(&ipv6_attempt.addr, ipv6_addr->ai_addr, ipv6_addr->ai_addrlen);
    ipv6_attempt.addr_len = ipv6_addr->ai_addrlen;

    asciichat_thread_create(&ipv6_thread, attempt_connection_thread, &ipv6_attempt);
  }

  // Wait for winner or both to finish
  uint32_t max_wait_ms = config->timeout_ms + 1000;
  uint32_t elapsed_ms = 0;

  mutex_lock(&lock);
  while (!winner_found && elapsed_ms < max_wait_ms) {
    cond_timedwait(&signal, &lock, 100);
    elapsed_ms += 100;

    // Check if both are done
    if (ipv4_addr && ipv6_addr) {
      if (ipv4_attempt.done && ipv6_attempt.done) {
        break;
      }
    } else if (ipv4_addr && ipv4_attempt.done) {
      break;
    } else if (ipv6_addr && ipv6_attempt.done) {
      break;
    }
  }
  mutex_unlock(&lock);

  // Join threads (must wait for both to complete before returning)
  // The threads access shared state on our stack, so we cannot return until they're done
  if (asciichat_thread_is_initialized(&ipv4_thread)) {
    asciichat_thread_join(&ipv4_thread, NULL);
  }
  if (asciichat_thread_is_initialized(&ipv6_thread)) {
    asciichat_thread_join(&ipv6_thread, NULL);
  }

  // Close any remaining sockets
  if (ipv4_attempt.socket != INVALID_SOCKET_VALUE) {
    socket_close(ipv4_attempt.socket);
  }
  if (ipv6_attempt.socket != INVALID_SOCKET_VALUE) {
    socket_close(ipv6_attempt.socket);
  }

  // Cleanup
  mutex_destroy(&lock);
  cond_destroy(&signal);
  freeaddrinfo(result);

  // Check result
  if (winner_socket == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to connect to %s:%d (both IPv4 and IPv6 failed)", config->hostname,
                     config->port);
  }

  *out_socket = (socket_t)winner_socket;
  log_info("PCONN: Successfully connected to %s:%d", config->hostname, config->port);
  return ASCIICHAT_OK;
}

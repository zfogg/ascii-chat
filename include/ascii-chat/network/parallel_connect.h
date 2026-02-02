/**
 * @file network/parallel_connect.h
 * @brief Parallel IPv4/IPv6 connection with race-to-connect semantics
 * @ingroup network
 *
 * Attempts IPv4 and IPv6 connections in parallel and returns the first
 * successful socket, closing the loser. Handles unreachable addresses
 * gracefully without blocking on timeouts.
 */

#ifndef PARALLEL_CONNECT_H
#define PARALLEL_CONNECT_H

#include "../platform/socket.h"
#include "../common.h"

/**
 * @brief Callback type to check if connection should be abandoned
 *
 * Called periodically by connection threads to allow graceful shutdown.
 * Should return true if connection attempts should stop immediately.
 *
 * @param user_data Opaque pointer provided by caller
 * @return true to abandon connection attempts, false to continue
 */
typedef bool (*parallel_connect_should_exit_fn)(void *user_data);

typedef struct {
  const char *hostname;
  uint16_t port;
  uint32_t timeout_ms; // Per-attempt timeout

  // Optional: callback to check if connection should be abandoned (e.g., shutdown signal)
  // Called periodically from connection threads (~100ms intervals)
  parallel_connect_should_exit_fn should_exit_callback;
  void *callback_data;
} parallel_connect_config_t;

/**
 * Connect to hostname with parallel IPv4/IPv6 attempts
 *
 * Resolves hostname and attempts IPv4 and IPv6 connections in parallel.
 * Returns the first successful socket; the losing connection is closed.
 * Each connection attempt is non-blocking with select() timeout.
 *
 * @param config Connection configuration
 * @param out_socket Pointer to receive winning socket
 * @return ASCIICHAT_OK on success, ERROR_* on failure
 *
 * @note Caller is responsible for closing the returned socket
 * @note Timeout applies to each connection attempt independently
 */
asciichat_error_t parallel_connect(const parallel_connect_config_t *config, socket_t *out_socket);

#endif // PARALLEL_CONNECT_H

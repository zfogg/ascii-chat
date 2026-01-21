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

#include "platform/socket.h"
#include "common.h"

typedef struct {
  const char *hostname;
  uint16_t port;
  uint32_t timeout_ms; // Per-attempt timeout
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

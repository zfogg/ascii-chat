/**
 * @file network/connection_endpoint.h
 * @brief Shared endpoint normalization for TCP and WebSocket connections
 *
 * This helper removes repeated URL/scheme parsing across client and discovery
 * connection setup code. Callers get one normalized endpoint description and
 * can then choose the appropriate transport path.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../asciichat_errno.h"

typedef enum {
  CONNECTION_ENDPOINT_TCP = 0,
  CONNECTION_ENDPOINT_WEBSOCKET = 1,
} connection_endpoint_protocol_t;

typedef struct {
  connection_endpoint_protocol_t protocol;
  char input[512];
  char host[256];
  uint16_t port;
  bool has_explicit_port;
} connection_endpoint_t;

/**
 * Resolve an input string into a normalized TCP or WebSocket endpoint.
 *
 * Supported inputs:
 * - ws://host[:port]
 * - wss://host[:port]
 * - tcp://host[:port]
 * - bare hostnames or host:port pairs
 *
 * Bare hostnames are treated as TCP endpoints and use default_port when the
 * input does not specify a port.
 */
asciichat_error_t connection_endpoint_resolve(const char *input, uint16_t default_port,
                                               connection_endpoint_t *endpoint_out);

const char *connection_endpoint_protocol_name(connection_endpoint_protocol_t protocol);

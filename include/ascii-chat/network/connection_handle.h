/**
 * @file connection_handle.h
 * @brief Shared connection ownership handle for TCP and WebSocket clients
 */
#pragma once

#include <stdbool.h>

#include <ascii-chat/network/acip/transport.h>

struct tcp_client;
struct websocket_client;

typedef enum {
  CONNECTION_HANDLE_NONE = 0,
  CONNECTION_HANDLE_TCP = 1,
  CONNECTION_HANDLE_WEBSOCKET = 2,
} connection_handle_backend_t;

typedef struct {
  acip_transport_t *transport;
  acip_transport_type_t transport_type;
  connection_handle_backend_t backend;
  bool owns_client_owner;
  void *client_owner;
} connection_handle_t;

void connection_handle_init(connection_handle_t *handle);
void connection_handle_cleanup(connection_handle_t *handle);

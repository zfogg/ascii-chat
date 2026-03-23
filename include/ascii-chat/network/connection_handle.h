/**
 * @file connection_handle.h
 * @brief Shared connection ownership handle for TCP and WebSocket clients
 */
#pragma once

#include <stdbool.h>

#include <ascii-chat/network/acip/transport.h>

typedef struct {
  acip_transport_t *transport;
  acip_transport_type_t transport_type;
  void *owner;
  void (*owner_destroy)(void *owner);
} connection_handle_t;

void connection_handle_init(connection_handle_t *handle);
void connection_handle_cleanup(connection_handle_t *handle);

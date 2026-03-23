/**
 * @file connection_handle.c
 * @brief Shared connection ownership cleanup for TCP and WebSocket clients
 */

#include <ascii-chat/network/connection_handle.h>
#include <ascii-chat/network/tcp/client.h>
#include <ascii-chat/network/websocket/client.h>

void connection_handle_init(connection_handle_t *handle) {
  if (!handle) {
    return;
  }

  *handle = (connection_handle_t){0};
  handle->transport_type = ACIP_TRANSPORT_TCP;
  handle->backend = CONNECTION_HANDLE_NONE;
  handle->owns_client_owner = false;
}

void connection_handle_cleanup(connection_handle_t *handle) {
  if (!handle) {
    return;
  }

  if (handle->transport) {
    acip_transport_close(handle->transport);
    acip_transport_destroy(handle->transport);
    handle->transport = NULL;
  }

  if (handle->backend == CONNECTION_HANDLE_WEBSOCKET && handle->client_owner && handle->owns_client_owner) {
    websocket_client_t *ws_client = (websocket_client_t *)handle->client_owner;
    websocket_client_destroy(&ws_client);
  } else if (handle->backend == CONNECTION_HANDLE_TCP && handle->client_owner && handle->owns_client_owner) {
    tcp_client_t *tcp_client = (tcp_client_t *)handle->client_owner;
    tcp_client_destroy(&tcp_client);
  }

  handle->client_owner = NULL;
  handle->backend = CONNECTION_HANDLE_NONE;
  handle->transport_type = ACIP_TRANSPORT_TCP;
  handle->owns_client_owner = false;
}

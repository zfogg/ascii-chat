/**
 * @file connection_handle.c
 * @brief Shared connection ownership cleanup for TCP and WebSocket clients
 */

#include <ascii-chat/network/connection_handle.h>
void connection_handle_init(connection_handle_t *handle) {
  if (!handle) {
    return;
  }

  *handle = (connection_handle_t){0};
  handle->transport_type = ACIP_TRANSPORT_TCP;
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

  if (handle->owner && handle->owner_destroy) {
    handle->owner_destroy(handle->owner);
  }

  handle->owner = NULL;
  handle->owner_destroy = NULL;
  handle->transport_type = ACIP_TRANSPORT_TCP;
}

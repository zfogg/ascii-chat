#pragma once
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/acip/server.h>
#include "client.h"
#include <ascii-chat/network/negotiation.h>
typedef struct {
  network_negotiation_t negotiation;
  atomic_ptr_t client;
  /**
   * @brief For server connection state, this is the live record that ties `negotiation`, `client` together. The owning
   * code creates it, updates it while work is active, and releases its handles and pointed-to storage before freeing
   * the record.
   */
} server_connection_state_t;
extern const network_connection_callbacks_t server_network_callbacks;
asciichat_error_t server_dispatch_packet(client_info_t *, packet_type_t, const void *, size_t);
asciichat_error_t server_send_state(acip_transport_t *);
void server_send_video(client_info_t *, acip_frame_workspace_t *, const char *, size_t, uint32_t, uint32_t, int,
                       const acip_render_settings_t *);
asciichat_error_t server_send_audio(client_info_t *, const void *, size_t);

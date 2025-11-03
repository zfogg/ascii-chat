/**
 * @file server/protocol.h
 * @ingroup server_protocol
 * @brief Server packet processing and protocol implementation
 */
#pragma once

#include "client.h"

// Packet handling functions
void handle_image_frame_packet(client_info_t *client, void *data, size_t len);
void handle_audio_batch_packet(client_info_t *client, const void *data, size_t len);
void handle_audio_packet(client_info_t *client, const void *data, size_t len);
void handle_client_capabilities_packet(client_info_t *client, const void *data, size_t len);
void handle_size_packet(client_info_t *client, const void *data, size_t len);
void handle_ping_packet(client_info_t *client);
void handle_client_join_packet(client_info_t *client, const void *data, size_t len);
void handle_stream_start_packet(client_info_t *client, const void *data, size_t len);
void handle_stream_stop_packet(client_info_t *client, const void *data, size_t len);
void handle_client_leave_packet(client_info_t *client);

// Protocol utility functions
int send_server_state_to_client(client_info_t *client);
void broadcast_clear_console_to_all_clients(void);

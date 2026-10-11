#pragma once
#include <ascii-chat/platform/event.h>
#include <ascii-chat/network/acip/server.h>
#include "client.h"

void server_event_accept(platform_event_stream_t *, void *);
asciichat_error_t server_event_dispatch(client_info_t *, packet_type_t, const void *, size_t);
void server_event_video(client_info_t *, acip_frame_workspace_t *, const char *, size_t, uint32_t, uint32_t, int);
asciichat_error_t server_event_audio(client_info_t *, const void *, size_t);

/**
 * @file server/render.h
 * @ingroup server_render
 * @brief Per-client rendering threads with rate limiting
 */
#pragma once

#include "client.h"
#include <ascii-chat/common/limits.h>

// Per-client render thread functions
void *client_video_render_thread(void *arg);
void *client_audio_render_thread(void *arg);

// Render thread lifecycle management
int create_client_render_threads(server_context_t *server_ctx, client_info_t *client);
asciichat_error_t stop_client_render_threads(client_info_t *client);

// Render timing control - match platform-specific client FPS for optimal performance
#define VIDEO_RENDER_FPS DEFAULT_MAX_FPS
// Audio mixing runs at 100 Hz, while each outgoing 960-sample Opus frame at
// 48 kHz is queued every 20 ms.
#define AUDIO_PACKET_FPS 50

/**
 * @file server/render.h
 * @ingroup server_render
 * @brief Per-client rendering threads with rate limiting
 */
#pragma once

#include "client.h"

// Per-client render thread functions
void *client_video_render_thread(void *arg);
void *client_audio_render_thread(void *arg);

// Render thread lifecycle management
int create_client_render_threads(server_context_t *server_ctx, client_info_t *client);
void stop_client_render_threads(client_info_t *client);

// Render timing control - match platform-specific client FPS for optimal performance
#ifdef _WIN32
#define VIDEO_RENDER_FPS 60 // Windows with timeBeginPeriod(1) can handle 60 FPS
#else
#define VIDEO_RENDER_FPS 60 // Linux/macOS can handle higher rates
#endif
// Audio mixing runs at 100 Hz, while each outgoing 960-sample Opus frame at
// 48 kHz is queued every 20 ms.
#define AUDIO_PACKET_FPS 50

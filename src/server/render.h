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
int create_client_render_threads(client_info_t *client);
void stop_client_render_threads(client_info_t *client);

// Render timing control - match platform-specific client FPS for optimal performance
#ifdef _WIN32
#define VIDEO_RENDER_FPS 60 // Windows with timeBeginPeriod(1) can handle 60 FPS
#else
#define VIDEO_RENDER_FPS 60 // Linux/macOS can handle higher rates
#endif
// Audio render rate: 480 samples per iteration, 10ms interval = 100 FPS
// This gives 48,000 samples/sec which matches real-time playback rate
#define AUDIO_RENDER_FPS 100 // 10ms interval for real-time rate

// Render utilities
void calculate_render_interval(int target_fps, struct timespec *interval);
void wait_for_next_render(struct timespec *last_render_time, const struct timespec *interval);

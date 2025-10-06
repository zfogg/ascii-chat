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
#define VIDEO_RENDER_FPS 30 // Match Windows client FPS to prevent frame buildup
#else
#define VIDEO_RENDER_FPS 60 // Linux/macOS can handle higher rates
#endif
#define AUDIO_RENDER_FPS 172 // ~5.8ms interval for smooth audio

// Render utilities
void calculate_render_interval(int target_fps, struct timespec *interval);
void wait_for_next_render(struct timespec *last_render_time, const struct timespec *interval);

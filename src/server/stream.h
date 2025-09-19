#pragma once

#include "client.h"
#include "network.h"
#include <stdatomic.h>

// Global shutdown flag
extern atomic_bool g_should_exit;

// Stream mixing and sending functions
char *create_mixed_ascii_frame_for_client(uint32_t target_client_id, unsigned short width, unsigned short height,
                                          bool wants_stretch, size_t *out_size);

// Queue management for streaming (video now uses double buffer directly)
int queue_audio_for_client(client_info_t *client, const void *audio_data, size_t data_size);

// Legacy frame cache removed - using video_frame.c double buffering only

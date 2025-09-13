#pragma once

#include "client.h"
#include "network.h"
#include <stdatomic.h>

// Global shutdown flag
extern atomic_bool g_should_exit;

// Stream mixing and sending functions
char *create_mixed_ascii_frame_for_client(uint32_t target_client_id, unsigned short width, unsigned short height,
                                          bool wants_stretch, size_t *frame_size);

// Queue management for streaming
int queue_ascii_frame_for_client(client_info_t *client, const char *ascii_frame, size_t frame_size);
int queue_audio_for_client(client_info_t *client, const void *audio_data, size_t data_size);

// Global frame cache management
extern mutex_t g_frame_cache_mutex;

// Frame cache entry structure
typedef struct {
  uint32_t client_id;
  unsigned short width;
  unsigned short height;
  char *frame_data;
  size_t frame_size;
  struct timespec timestamp;
} frame_cache_entry_t;

extern frame_cache_entry_t g_frame_cache;

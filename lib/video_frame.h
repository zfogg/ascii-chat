#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "platform/abstraction.h"

// =============================================================================
// Video Frame Constants
// =============================================================================

// Video frame buffer sizes
#define VIDEO_FRAME_BUFFER_SIZE (2 * 1024 * 1024) // 2MB per frame buffer
#define MAX_FRAME_BUFFERS 2                       // Double buffering

/**
 * Modern video frame management - inspired by WebRTC/Zoom/Meet architecture
 *
 * This implements a lock-free double-buffering system with atomic pointer swaps,
 * similar to how professional video streaming services handle frames.
 *
 * Key principles:
 * - Zero-copy: Never copy frame data, only swap pointers
 * - Lock-free reads: Reader thread never blocks writer
 * - Latest-frame-wins: Old frames are dropped, not queued
 * - Metrics tracking: Monitor quality for adaptive streaming
 */

typedef struct {
  // Frame data
  void *data;
  size_t size;

  // Frame dimensions
  uint32_t width;
  uint32_t height;

  // Timing information
  uint64_t capture_timestamp_us; // When frame was captured
  uint64_t receive_timestamp_us; // When frame was received
  uint64_t sequence_number;      // For detecting drops

  // Quality metrics
  uint32_t encoding_time_us; // How long to encode/decode
  bool is_keyframe;          // Important frames
} video_frame_t;

typedef struct {
  // Double buffering with atomic swaps
  video_frame_t *front_buffer; // Being displayed (reader owns)
  video_frame_t *back_buffer;  // Being written (writer owns)

  // Synchronization (lock-free where possible)
  atomic_bool new_frame_available; // Signal from writer to reader
  mutex_t swap_mutex;              // Brief mutex for pointer swap only

  // Frame allocation
  video_frame_t frames[2]; // Pre-allocated, reused forever

  // Statistics for quality monitoring
  atomic_ullong total_frames_received;
  atomic_ullong total_frames_dropped;
  atomic_ullong last_frame_sequence;

  // Timing for adaptive quality
  atomic_uint avg_decode_time_us;
  atomic_uint avg_render_time_us;

  // Client info
  uint32_t client_id;
  bool active;

  // Buffer management
  size_t allocated_buffer_size; // Size of allocated buffers (for cleanup)
} video_frame_buffer_t;

// API functions

/**
 * Create a double-buffered video frame manager
 * Pre-allocates buffers for zero-allocation operation
 */
video_frame_buffer_t *video_frame_buffer_create(uint32_t client_id);

/**
 * Destroy frame buffer and free resources
 */
void video_frame_buffer_destroy(video_frame_buffer_t *vfb);

/**
 * Writer API: Start writing a new frame
 * Returns pointer to back buffer for writing
 * MUST call video_frame_commit() when done
 */
video_frame_t *video_frame_begin_write(video_frame_buffer_t *vfb);

/**
 * Writer API: Commit the frame and swap buffers
 * Makes frame available to reader atomically
 */
void video_frame_commit(video_frame_buffer_t *vfb);

/**
 * Reader API: Get latest frame if available
 * Returns NULL if no new frame since last read
 * Lock-free operation - never blocks writer
 */
const video_frame_t *video_frame_get_latest(video_frame_buffer_t *vfb);

/**
 * Get frame statistics for quality monitoring
 */
typedef struct {
  uint64_t total_frames;
  uint64_t dropped_frames;
  float drop_rate;
  uint32_t avg_decode_time_us;
  uint32_t avg_render_time_us;
} video_frame_stats_t;

void video_frame_get_stats(video_frame_buffer_t *vfb, video_frame_stats_t *stats);

/**
 * Simple atomic frame pointer for even simpler cases
 * This is what many services use when they don't need stats
 */
typedef struct {
  atomic_uintptr_t current_frame; // Just an atomic pointer
  video_frame_t frame_a;
  video_frame_t frame_b;
  atomic_bool use_frame_a; // Which frame to write to next
} simple_frame_swap_t;

// Even simpler API for basic use cases
simple_frame_swap_t *simple_frame_swap_create(void);
void simple_frame_swap_destroy(simple_frame_swap_t *sfs);
void simple_frame_swap_update(simple_frame_swap_t *sfs, const void *data, size_t size);
const video_frame_t *simple_frame_swap_get(simple_frame_swap_t *sfs);

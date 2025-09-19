#include "video_frame.h"
#include "common.h"
#include "buffer_pool.h"
#include "platform/abstraction.h"
#include <string.h>
#include <stdlib.h>

video_frame_buffer_t *video_frame_buffer_create(uint32_t client_id) {
  video_frame_buffer_t *vfb = (video_frame_buffer_t *)calloc(1, sizeof(video_frame_buffer_t));
  if (!vfb)
    return NULL;

  vfb->client_id = client_id;
  vfb->active = true;

  // Initialize double buffers
  vfb->front_buffer = &vfb->frames[0];
  vfb->back_buffer = &vfb->frames[1];

  // Pre-allocate frame data buffers (2MB each for HD video)
  const size_t frame_size = (size_t)2 * 1024 * 1024;
  data_buffer_pool_t *pool = data_buffer_pool_get_global();

  // Initialize frames - size starts at 0 until actual data is written!
  vfb->frames[0].size = 0; // Start with 0 - will be set when data is written
  vfb->frames[1].size = 0; // Start with 0 - will be set when data is written
  vfb->frames[0].data = NULL;
  vfb->frames[1].data = NULL;

  // Store the allocated buffer size for cleanup (different from data size!)
  vfb->allocated_buffer_size = frame_size;

  if (pool) {
    vfb->frames[0].data = data_buffer_pool_alloc(pool, frame_size);
    vfb->frames[1].data = data_buffer_pool_alloc(pool, frame_size);
  }

  if (!vfb->frames[0].data || !vfb->frames[1].data) {
    // Fallback to malloc if pool is exhausted or not available
    if (!vfb->frames[0].data)
      vfb->frames[0].data = malloc(frame_size);
    if (!vfb->frames[1].data)
      vfb->frames[1].data = malloc(frame_size);
  }

  // Initialize synchronization
  mutex_init(&vfb->swap_mutex);
  atomic_store(&vfb->new_frame_available, false);

  // Initialize statistics
  atomic_store(&vfb->total_frames_received, 0);
  atomic_store(&vfb->total_frames_dropped, 0);
  atomic_store(&vfb->last_frame_sequence, 0);

  log_debug("Created video frame buffer for client %u with double buffering", client_id);
  return vfb;
}

void video_frame_buffer_destroy(video_frame_buffer_t *vfb) {
  if (!vfb)
    return;

  vfb->active = false;

  // Free frame data
  data_buffer_pool_t *pool = data_buffer_pool_get_global();
  if (vfb->frames[0].data) {
    if (pool) {
      data_buffer_pool_free(pool, vfb->frames[0].data, vfb->allocated_buffer_size);
    } else {
      free(vfb->frames[0].data);
    }
  }
  if (vfb->frames[1].data) {
    if (pool) {
      data_buffer_pool_free(pool, vfb->frames[1].data, vfb->allocated_buffer_size);
    } else {
      free(vfb->frames[1].data);
    }
  }

  mutex_destroy(&vfb->swap_mutex);
  free(vfb);
}

video_frame_t *video_frame_begin_write(video_frame_buffer_t *vfb) {
  if (!vfb || !vfb->active)
    return NULL;

  // Writer always owns the back buffer
  return vfb->back_buffer;
}

void video_frame_commit(video_frame_buffer_t *vfb) {
  if (!vfb || !vfb->active)
    return;

  // Check if reader has consumed the previous frame
  if (atomic_load(&vfb->new_frame_available)) {
    // Reader hasn't consumed yet - we're dropping a frame
    // This is EXPECTED behavior in a double-buffer system when producer/consumer rates differ
    atomic_fetch_add(&vfb->total_frames_dropped, 1);
    // Don't log these drops - they're normal operation when rates differ
  }

  // Atomic pointer swap - this is the key operation
  mutex_lock(&vfb->swap_mutex);
  video_frame_t *temp = vfb->front_buffer;
  vfb->front_buffer = vfb->back_buffer;
  vfb->back_buffer = temp;
  mutex_unlock(&vfb->swap_mutex);

  // Signal reader that new frame is available
  atomic_store(&vfb->new_frame_available, true);
  atomic_fetch_add(&vfb->total_frames_received, 1);
}

const video_frame_t *video_frame_get_latest(video_frame_buffer_t *vfb) {
  if (!vfb || !vfb->active)
    return NULL;

  // Mark that we've consumed any new frame
  atomic_exchange(&vfb->new_frame_available, false);

  // Always return the front buffer (last valid frame)
  // This prevents flickering - we keep showing the last frame
  // until a new one arrives
  return vfb->front_buffer;
}

void video_frame_get_stats(video_frame_buffer_t *vfb, video_frame_stats_t *stats) {
  if (!vfb || !stats)
    return;

  stats->total_frames = atomic_load(&vfb->total_frames_received);
  stats->dropped_frames = atomic_load(&vfb->total_frames_dropped);
  stats->drop_rate = (stats->total_frames > 0) ? (float)stats->dropped_frames / (float)stats->total_frames : 0.0f;
  stats->avg_decode_time_us = atomic_load(&vfb->avg_decode_time_us);
  stats->avg_render_time_us = atomic_load(&vfb->avg_render_time_us);
}

// Simple frame swap implementation for basic cases
simple_frame_swap_t *simple_frame_swap_create(void) {
  simple_frame_swap_t *sfs = (simple_frame_swap_t *)calloc(1, sizeof(simple_frame_swap_t));
  if (!sfs)
    return NULL;

  // Pre-allocate both frames
  const size_t frame_size = (size_t)2 * 1024 * 1024;
  sfs->frame_a.data = malloc(frame_size);
  sfs->frame_b.data = malloc(frame_size);

  atomic_store(&sfs->current_frame, (uintptr_t)&sfs->frame_a);
  atomic_store(&sfs->use_frame_a, false); // Next write goes to frame_b

  return sfs;
}

void simple_frame_swap_destroy(simple_frame_swap_t *sfs) {
  if (!sfs)
    return;
  free(sfs->frame_a.data);
  free(sfs->frame_b.data);
  free(sfs);
}

void simple_frame_swap_update(simple_frame_swap_t *sfs, const void *data, size_t size) {
  if (!sfs || !data)
    return;

  // Determine which frame to write to
  bool use_a = atomic_load(&sfs->use_frame_a);
  video_frame_t *write_frame = use_a ? &sfs->frame_a : &sfs->frame_b;

  // Copy data to write frame
  if (size <= (size_t)2 * 1024 * 1024) {
    SAFE_MEMCPY(write_frame->data, size, data, size);
    write_frame->size = size;
    write_frame->capture_timestamp_us = (uint64_t)time(NULL) * 1000000;

    // Atomically update current frame pointer
    atomic_store(&sfs->current_frame, (uintptr_t)write_frame);

    // Toggle for next write
    atomic_store(&sfs->use_frame_a, !use_a);
  }
}

const video_frame_t *simple_frame_swap_get(simple_frame_swap_t *sfs) {
  if (!sfs)
    return NULL;
  uintptr_t frame_ptr = atomic_load(&sfs->current_frame);
  return (const video_frame_t *)(void *)frame_ptr; // NOLINT(bugprone-casting-through-void,performance-no-int-to-ptr)
}
#include "video_frame.h"
#include "common.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "buffer_pool.h"
#include <string.h>
#include <stdlib.h>

video_frame_buffer_t *video_frame_buffer_create(uint32_t client_id) {
  if (client_id == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client ID is 0");
    return NULL;
  }

  video_frame_buffer_t *vfb = SAFE_CALLOC(1, sizeof(video_frame_buffer_t), video_frame_buffer_t *);

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
    // Fallback to aligned malloc if pool is exhausted or not available
    // 64-byte cache-line alignment improves performance for large video frames
    if (!vfb->frames[0].data)
      vfb->frames[0].data = SAFE_MALLOC_ALIGNED(frame_size, 64, void *);
    if (!vfb->frames[1].data)
      vfb->frames[1].data = SAFE_MALLOC_ALIGNED(frame_size, 64, void *);
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
  if (!vfb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Video frame buffer is NULL");
    return;
  }

  vfb->active = false;

  // Free frame data
  data_buffer_pool_t *pool = data_buffer_pool_get_global();
  if (vfb->frames[0].data) {
    if (pool) {
      data_buffer_pool_free(pool, vfb->frames[0].data, vfb->allocated_buffer_size);
    } else {
      SAFE_FREE(vfb->frames[0].data);
    }
  }
  if (vfb->frames[1].data) {
    if (pool) {
      data_buffer_pool_free(pool, vfb->frames[1].data, vfb->allocated_buffer_size);
    } else {
      SAFE_FREE(vfb->frames[1].data);
    }
  }

  mutex_destroy(&vfb->swap_mutex);
  SAFE_FREE(vfb);
}

video_frame_t *video_frame_begin_write(video_frame_buffer_t *vfb) {
  if (!vfb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Video frame buffer is NULL");
    return NULL;
  }
  if (!vfb->active) {
    SET_ERRNO(ERROR_INVALID_STATE, "vfb->active is not true");
    return NULL;
  }

  // Writer always owns the back buffer
  return vfb->back_buffer;
}

void video_frame_commit(video_frame_buffer_t *vfb) {
  if (!vfb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Video frame buffer or active is NULL");
    return;
  }
  if (!vfb->active) {
    SET_ERRNO(ERROR_INVALID_STATE, "vfb->active is not true");
    return;
  }

  // Check if reader has consumed the previous frame
  if (atomic_load(&vfb->new_frame_available)) {
    // Reader hasn't consumed yet - we're dropping a frame
    uint64_t drops = atomic_fetch_add(&vfb->total_frames_dropped, 1) + 1;
    // Throttle drop logging - only log every 100 drops to avoid spam
    if (drops == 1 || drops % 100 == 0) {
      log_debug("Dropping frame for client %u (reader too slow, total drops: %llu)", vfb->client_id,
                (unsigned long long)drops);
    }
  }

  // Atomic pointer swap - NO MUTEX NEEDED since video_frame_commit() is only called by one thread (the receive thread)
  video_frame_t *temp = vfb->front_buffer;
  vfb->front_buffer = vfb->back_buffer;
  vfb->back_buffer = temp;

  // Signal reader that new frame is available
  atomic_store(&vfb->new_frame_available, true);
  atomic_fetch_add(&vfb->total_frames_received, 1);
}

const video_frame_t *video_frame_get_latest(video_frame_buffer_t *vfb) {
  if (!vfb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Video frame buffer is not active");
    return NULL;
  }
  if (!vfb->active) {
    SET_ERRNO(ERROR_INVALID_STATE, "vfb->active is not true");
    return NULL;
  }

  // Mark that we've consumed any new frame
  atomic_exchange(&vfb->new_frame_available, false);

  // Always return the front buffer (last valid frame)
  // This prevents flickering - we keep showing the last frame
  // until a new one arrives
  return vfb->front_buffer;
}

void video_frame_get_stats(video_frame_buffer_t *vfb, video_frame_stats_t *stats) {
  if (!vfb || !stats) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Video frame buffer or stats is NULL");
    return;
  }
  if (!vfb->active) {
    SET_ERRNO(ERROR_INVALID_STATE, "vfb->active is not true");
    return;
  }

  stats->total_frames = atomic_load(&vfb->total_frames_received);
  stats->dropped_frames = atomic_load(&vfb->total_frames_dropped);
  stats->drop_rate = (stats->total_frames > 0) ? (float)stats->dropped_frames / (float)stats->total_frames : 0.0f;
  stats->avg_decode_time_us = atomic_load(&vfb->avg_decode_time_us);
  stats->avg_render_time_us = atomic_load(&vfb->avg_render_time_us);
}

// Simple frame swap implementation for basic cases
simple_frame_swap_t *simple_frame_swap_create(void) {
  simple_frame_swap_t *sfs = SAFE_CALLOC(1, sizeof(simple_frame_swap_t), simple_frame_swap_t *);

  // Pre-allocate both frames
  const size_t frame_size = (size_t)2 * 1024 * 1024;
  sfs->frame_a.data = SAFE_MALLOC(frame_size, void *);
  sfs->frame_b.data = SAFE_MALLOC(frame_size, void *);

  atomic_store(&sfs->current_frame, (uintptr_t)&sfs->frame_a);
  atomic_store(&sfs->use_frame_a, false); // Next write goes to frame_b

  return sfs;
}

void simple_frame_swap_destroy(simple_frame_swap_t *sfs) {
  if (!sfs) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Simple frame swap is NULL");
    return;
  }
  SAFE_FREE(sfs->frame_a.data);
  SAFE_FREE(sfs->frame_b.data);
  SAFE_FREE(sfs);
}

void simple_frame_swap_update(simple_frame_swap_t *sfs, const void *data, size_t size) {
  if (!sfs || !data) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Simple frame swap or data is NULL");
    return;
  }

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
  if (!sfs) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Simple frame swap is NULL");
    return NULL;
  }
  uintptr_t frame_ptr = atomic_load(&sfs->current_frame);
  return (const video_frame_t *)(void *)frame_ptr; // NOLINT(bugprone-casting-through-void,performance-no-int-to-ptr)
}

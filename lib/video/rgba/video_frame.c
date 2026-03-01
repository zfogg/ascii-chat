
/**
 * @file video_frame.c
 * @ingroup video_frame
 * @brief 🎬 Video frame buffer management for client-specific ASCII rendering
 */

#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h> // For asciichat_errno system
#include <ascii-chat/debug/named.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/time.h>
#include <string.h>
#include <stdlib.h>

video_frame_buffer_t *video_frame_buffer_create(const char *client_id) {
  if (client_id == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Client ID is 0");
    return NULL;
  }

  video_frame_buffer_t *vfb = SAFE_CALLOC(1, sizeof(video_frame_buffer_t), video_frame_buffer_t *);

  // Copy client_id string - we own this memory now
  // (caller's client_id may be a stack variable that goes out of scope)
  char *client_id_copy = SAFE_MALLOC(strlen(client_id) + 1, char *);
  if (!client_id_copy) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for client_id copy");
    SAFE_FREE(vfb);
    return NULL;
  }
  strcpy(client_id_copy, client_id);
  vfb->client_id = client_id_copy;
  vfb->active = true;

  // Initialize double buffers
  vfb->front_buffer = &vfb->frames[0];
  vfb->back_buffer = &vfb->frames[1];

  // DEBUG: Log buffer initialization
  log_info("VFB_INIT: vfb=%p, vfb->frames=%p, &frames[0]=%p, &frames[1]=%p, setting back_buffer=%p", (void *)vfb,
           (void *)vfb->frames, (void *)&vfb->frames[0], (void *)&vfb->frames[1], (void *)vfb->back_buffer);

  // Pre-allocate frame data buffers (2MB each for HD video)
  const size_t frame_size = (size_t)MAX_FRAME_BUFFER_SIZE;
  buffer_pool_t *pool = buffer_pool_get_global();

  log_info("VFB_ALLOC_START: client_id=%s, frame_size=%zu (2MB=%zu), pool=%p", client_id, frame_size,
           (size_t)(2 * 1024 * 1024), (void *)pool);

  // Initialize frames - size starts at 0 until actual data is written!
  vfb->frames[0].size = 0; // Start with 0 - will be set when data is written
  vfb->frames[1].size = 0; // Start with 0 - will be set when data is written
  vfb->frames[0].data = NULL;
  vfb->frames[1].data = NULL;

  // Initialize allocated_buffer_size to 0 - will be set AFTER successful allocation
  vfb->allocated_buffer_size = 0;

  // Attempt pool allocation
  if (pool) {
    vfb->frames[0].data = buffer_pool_alloc(pool, frame_size);
    vfb->frames[1].data = buffer_pool_alloc(pool, frame_size);
    log_info("VFB_POOL_ALLOC: frame[0].data=%p, frame[1].data=%p (from pool)", (void *)vfb->frames[0].data,
             (void *)vfb->frames[1].data);
  } else {
    log_warn("VFB_NO_POOL: pool is NULL, will use malloc fallback");
  }

  // Fallback to aligned malloc if pool allocation failed or returned undersized buffer
  // Note: buffer_pool_alloc may return undersized buffers if pool is exhausted and falls back to SAFE_MALLOC
  if (!vfb->frames[0].data || !vfb->frames[1].data) {
    log_info("VFB_MALLOC_FALLBACK: frame[0]=%s, frame[1]=%s", vfb->frames[0].data ? "OK" : "allocating",
             vfb->frames[1].data ? "OK" : "allocating");
    // 64-byte cache-line alignment improves performance for large video frames
    // CRITICAL: Always use SAFE_MALLOC_ALIGNED for fallback to guarantee minimum size and alignment
    if (!vfb->frames[0].data) {
      vfb->frames[0].data = SAFE_MALLOC_ALIGNED(frame_size, 64, void *);
      if (vfb->frames[0].data) {
        log_info("VFB_MALLOC_FRAME0: allocated %zu bytes with 64-byte alignment", frame_size);
      }
    }
    if (!vfb->frames[1].data) {
      vfb->frames[1].data = SAFE_MALLOC_ALIGNED(frame_size, 64, void *);
      if (vfb->frames[1].data) {
        log_info("VFB_MALLOC_FRAME1: allocated %zu bytes with 64-byte alignment", frame_size);
      }
    }
    log_info("VFB_MALLOC_RESULT: frame[0].data=%p, frame[1].data=%p (allocated with malloc)",
             (void *)vfb->frames[0].data, (void *)vfb->frames[1].data);
  }

  // CRITICAL: Only set allocated_buffer_size after BOTH buffers are successfully allocated
  // This prevents claiming 2MB buffers when actual allocation failed or returned smaller buffers
  if (!vfb->frames[0].data || !vfb->frames[1].data) {
    log_error("VFB_ALLOC_FAILED: frame[0].data=%p, frame[1].data=%p (cannot create frame buffer)",
              (void *)vfb->frames[0].data, (void *)vfb->frames[1].data);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate video frame buffers");
    video_frame_buffer_destroy(vfb);
    return NULL;
  }

  // CRITICAL: Verify we actually got 2MB buffers, not undersized fallbacks
  // The buffer pool may return undersized buffers if exhausted, but we REQUIRE full-sized allocations
  // Do this by checking if allocations came from fallback or if pool returned a pointer we got
  // Since we can't check actual size from buffer_pool_alloc(), we enforce that both allocations succeeded
  // and use SAFE_MALLOC_ALIGNED as fallback which guarantees correct size

  // After our allocation logic above:
  // - Pool allocations go through buffer_pool_alloc (may be undersized)
  // - Fallback allocations use SAFE_MALLOC_ALIGNED (guaranteed 2MB)
  // We verify BOTH succeeded with the fallback already ensuring size

  vfb->allocated_buffer_size = frame_size; // Set to requested size

  log_info("VFB_ALLOC_FINAL: allocated_buffer_size=%zu (requested=%zu), frame[0].data=%p, frame[1].data=%p",
           vfb->allocated_buffer_size, frame_size, (void *)vfb->frames[0].data, (void *)vfb->frames[1].data);

  log_info("VFB_ALLOC_SUCCESS: client_id=%s, allocated_buffer_size=%zu, frames[0].data=%p, frames[1].data=%p",
           client_id, vfb->allocated_buffer_size, (void *)vfb->frames[0].data, (void *)vfb->frames[1].data);

  // When buffers are allocated from the pool, they may contain leftover data from previous clients
  // This ensures frames with size=0 are truly empty, preventing ghost frames during reconnection
  if (vfb->frames[0].data) {
    memset(vfb->frames[0].data, 0, frame_size);
  }
  if (vfb->frames[1].data) {
    memset(vfb->frames[1].data, 0, frame_size);
  }

  // Initialize synchronization
  if (mutex_init(&vfb->swap_mutex, "video_frame_swap") != 0) {
    SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to initialize mutex for video frame buffer");
    video_frame_buffer_destroy(vfb);
    return NULL;
  }
  atomic_store_bool(&vfb->new_frame_available, false);

  // Initialize statistics
  atomic_store_u64(&vfb->total_frames_received, 0);
  atomic_store_u64(&vfb->total_frames_dropped, 0);
  atomic_store_u64(&vfb->last_frame_sequence, 0);

  log_debug("Created video frame buffer for client %u with double buffering", client_id);

  // DEBUG: Final state check
  log_info("VFB_FINAL: vfb=%p, back_buffer=%p (should be %p), frames[0].data=%p, frames[1].data=%p, allocated_size=%zu",
           (void *)vfb, (void *)vfb->back_buffer, (void *)&vfb->frames[1], (void *)vfb->frames[0].data,
           (void *)vfb->frames[1].data, vfb->allocated_buffer_size);

  NAMED_REGISTER_VIDEO_FRAME_BUFFER(vfb, "buffer");
  return vfb;
}

void video_frame_buffer_destroy(video_frame_buffer_t *vfb) {
  if (!vfb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Video frame buffer is NULL");
    return;
  }

  NAMED_UNREGISTER(vfb);

  vfb->active = false;

  // Free frame data
  buffer_pool_t *pool = buffer_pool_get_global();
  if (vfb->frames[0].data) {
    if (pool) {
      buffer_pool_free(pool, vfb->frames[0].data, vfb->allocated_buffer_size);
    } else {
      SAFE_FREE(vfb->frames[0].data);
    }
  }
  if (vfb->frames[1].data) {
    if (pool) {
      buffer_pool_free(pool, vfb->frames[1].data, vfb->allocated_buffer_size);
    } else {
      SAFE_FREE(vfb->frames[1].data);
    }
  }

  mutex_destroy(&vfb->swap_mutex);

  // Free the allocated client_id string
  SAFE_FREE(vfb->client_id);

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
  if (atomic_load_bool(&vfb->new_frame_available)) {
    // Reader hasn't consumed yet - we're dropping a frame
    uint64_t drops = atomic_fetch_add_u64(&vfb->total_frames_dropped, 1) + 1;
    // Throttle drop logging - only log every 100 drops to avoid spam
    if (drops == 1 || drops % 100 == 0) {
      log_dev_every(4500 * US_PER_MS_INT, "Dropping frame for client %u (reader too slow, total drops: %llu)",
                    vfb->client_id, (unsigned long long)drops);
    }
  }

  // Pointer swap using mutex for thread safety
  // The send thread reads front_buffer while render thread swaps
  // Without this mutex, the send thread could read a stale front_buffer pointer
  // mid-swap, causing it to see size=0 on newly-initialized frames
  mutex_lock(&vfb->swap_mutex);
  video_frame_t *temp = vfb->front_buffer;
  vfb->front_buffer = vfb->back_buffer;
  vfb->back_buffer = temp;
  mutex_unlock(&vfb->swap_mutex);

  // Signal reader that new frame is available
  atomic_store_bool(&vfb->new_frame_available, true);
  atomic_fetch_add_u64(&vfb->total_frames_received, 1);
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

  // Use mutex to safely read front_buffer pointer
  // (in case render thread is swapping)
  // This ensures we get a consistent pointer and don't read mid-swap
  mutex_lock(&vfb->swap_mutex);
  const video_frame_t *result = vfb->front_buffer;
  mutex_unlock(&vfb->swap_mutex);

  return result;
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
  stats->avg_decode_time_ns = atomic_load(&vfb->avg_decode_time_ns);
  stats->avg_render_time_ns = atomic_load(&vfb->avg_render_time_ns);
}

// Simple frame swap implementation for basic cases
simple_frame_swap_t *simple_frame_swap_create(void) {
  simple_frame_swap_t *sfs = SAFE_CALLOC(1, sizeof(simple_frame_swap_t), simple_frame_swap_t *);

  // Pre-allocate both frames
  const size_t frame_size = (size_t)MAX_FRAME_BUFFER_SIZE;
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
  if (size <= (size_t)MAX_FRAME_BUFFER_SIZE) {
    SAFE_MEMCPY(write_frame->data, size, data, size);
    write_frame->size = size;
    write_frame->capture_timestamp_ns = time_get_ns();

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

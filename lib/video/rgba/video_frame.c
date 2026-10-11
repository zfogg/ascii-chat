#include <ascii-chat/stats/runtime.h>

/**
 * @file video_frame.c
 * @ingroup video_frame
 * @brief 🎬 Video frame buffer management for client-specific ASCII rendering
 */

#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h> // For asciichat_errno system
#include <ascii-chat/debug/named.h>
#include <ascii-chat/buffer.h>
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
  size_t client_id_len = strlen(client_id) + 1;
  char *client_id_copy = SAFE_MALLOC(client_id_len, char *);
  if (!client_id_copy) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for client_id copy");
    SAFE_FREE(vfb);
    return NULL;
  }
  asciichat_error_t strcpy_result = SAFE_STRCPY(client_id_copy, client_id_len, client_id);
  if (strcpy_result != ASCIICHAT_OK) {
    log_error("Failed to copy client_id: %s", asciichat_error_string(strcpy_result));
    SAFE_FREE(client_id_copy);
    SAFE_FREE(vfb);
    return NULL;
  }
  vfb->client_id = client_id_copy;
  vfb->active = true;

  // Initialize double buffers
  vfb->front_buffer = &vfb->frames[0];
  vfb->back_buffer = &vfb->frames[1];

  const size_t frame_size = (size_t)MAX_FRAME_BUFFER_SIZE;
  vfb->frames[0].data = buffer_alloc(NULL, frame_size);
  vfb->frames[1].data = buffer_alloc(NULL, frame_size);
  if (!vfb->frames[0].data || !vfb->frames[1].data) {
    buffer_release(vfb->frames[0].data);
    buffer_release(vfb->frames[1].data);
    SAFE_FREE(vfb->client_id);
    SAFE_FREE(vfb);
    SET_ERRNO(ERROR_MEMORY, "Video frame buffers exceed memory budget");
    return NULL;
  }
  vfb->allocated_buffer_size = frame_size;
  memset(vfb->frames[0].data, 0, frame_size);
  memset(vfb->frames[1].data, 0, frame_size);

  // Initialize synchronization
  log_info("VFB_MUTEX_INIT_START: About to init mutex for client_id=%s", client_id);
  if (mutex_init(&vfb->swap_mutex, "video_frame_swap") != 0) {
    log_error("VFB_MUTEX_INIT_FAILED: mutex_init returned error");
    SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to initialize mutex for video frame buffer");
    buffer_release(vfb->frames[0].data);
    buffer_release(vfb->frames[1].data);
    SAFE_FREE(vfb->client_id);
    SAFE_FREE(vfb);
    return NULL;
  }
  log_info("VFB_MUTEX_INIT_DONE: mutex initialized successfully");
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

  NAMED_REGISTER_VIDEO_FRAME_BUFFER(vfb, "buffer", NULL);

  // Register atomic fields with descriptive names - vfb is their parent
  NAMED_REGISTER_ATOMIC(&vfb->new_frame_available, "new_frame_available_flag", (uintptr_t)(const void *)(vfb));
  NAMED_REGISTER_ATOMIC(&vfb->total_frames_received, "total_frames_received_counter", (uintptr_t)(const void *)(vfb));
  NAMED_REGISTER_ATOMIC(&vfb->total_frames_dropped, "total_frames_dropped_counter", (uintptr_t)(const void *)(vfb));
  NAMED_REGISTER_ATOMIC(&vfb->last_frame_sequence, "last_frame_sequence_number", (uintptr_t)(const void *)(vfb));

  return vfb;
}

void video_frame_buffer_destroy(video_frame_buffer_t *vfb) {
  if (!vfb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Video frame buffer is NULL");
    return;
  }

  NAMED_UNREGISTER(vfb);

  // Unregister atomic fields
  NAMED_UNREGISTER(&vfb->new_frame_available);
  NAMED_UNREGISTER(&vfb->total_frames_received);
  NAMED_UNREGISTER(&vfb->total_frames_dropped);
  NAMED_UNREGISTER(&vfb->last_frame_sequence);

  vfb->active = false;

  buffer_release(vfb->frames[0].data);
  buffer_release(vfb->frames[1].data);

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

  // Keep the unread check, pointer swap, and publication atomic with respect
  // to readers. Checking front_buffer before taking this mutex races with a
  // reader consuming the previous frame and with another commit swapping it.
  mutex_lock(&vfb->swap_mutex);
  if (atomic_load_bool(&vfb->new_frame_available) && vfb->front_buffer->size > 0) {
    uint64_t drops = atomic_fetch_add_u64(&vfb->total_frames_dropped, 1) + 1;
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_DROPPED, 1);
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_QUEUE_DROPS, 1);
    if (drops == 1 || drops % 100 == 0) {
      log_dev_every(4500 * US_PER_MS_INT, "Dropping frame for client %u (reader too slow, total drops: %llu)",
                    vfb->client_id, (unsigned long long)drops);
    }
  }
  video_frame_t *temp = vfb->front_buffer;
  vfb->front_buffer = vfb->back_buffer;
  vfb->back_buffer = temp;
  atomic_store_bool(&vfb->new_frame_available, true);
  mutex_unlock(&vfb->swap_mutex);

  atomic_fetch_add_u64(&vfb->total_frames_received, 1);
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_FRAMES_ENQUEUED, 1);
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

  mutex_lock(&vfb->swap_mutex);
  if (!atomic_load_bool(&vfb->new_frame_available)) {
    mutex_unlock(&vfb->swap_mutex);
    return NULL;
  }
  atomic_store_bool(&vfb->new_frame_available, false);
  const video_frame_t *result = vfb->front_buffer;
  mutex_unlock(&vfb->swap_mutex);

  return result;
}

const video_frame_t *video_frame_peek_latest(video_frame_buffer_t *vfb) {
  if (!vfb || !vfb->active)
    return NULL;
  mutex_lock(&vfb->swap_mutex);
  const video_frame_t *frame = vfb->front_buffer;
  const video_frame_t *result = frame && frame->size > 0 ? frame : NULL;
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

  stats->total_frames = atomic_load_u64(&vfb->total_frames_received);
  stats->dropped_frames = atomic_load_u64(&vfb->total_frames_dropped);
  stats->drop_rate = (stats->total_frames > 0) ? (float)stats->dropped_frames / (float)stats->total_frames : 0.0f;
  stats->avg_decode_time_ns = atomic_load_u64(&vfb->avg_decode_time_ns);
  stats->avg_render_time_ns = atomic_load_u64(&vfb->avg_render_time_ns);
}

// Simple frame swap implementation for basic cases
simple_frame_swap_t *simple_frame_swap_create(void) {
  simple_frame_swap_t *sfs = SAFE_CALLOC(1, sizeof(simple_frame_swap_t), simple_frame_swap_t *);

  // Pre-allocate both frames
  const size_t frame_size = (size_t)MAX_FRAME_BUFFER_SIZE;
  sfs->frame_a.data = SAFE_MALLOC(frame_size, void *);
  sfs->frame_b.data = SAFE_MALLOC(frame_size, void *);

  atomic_store_u64(&sfs->current_frame, 0);
  atomic_store_bool(&sfs->use_frame_a, false); // Next write goes to frame_b

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
  bool use_a = atomic_load_bool(&sfs->use_frame_a);
  video_frame_t *write_frame = use_a ? &sfs->frame_a : &sfs->frame_b;

  // Copy data to write frame
  if (size <= (size_t)MAX_FRAME_BUFFER_SIZE) {
    SAFE_MEMCPY(write_frame->data, size, data, size);
    write_frame->size = size;
    write_frame->capture_timestamp_ns = time_get_ns();

    // Atomically update current frame pointer
    atomic_store_u64(&sfs->current_frame, (uintptr_t)write_frame);

    // Toggle for next write
    atomic_store_bool(&sfs->use_frame_a, !use_a);
  }
}

const video_frame_t *simple_frame_swap_get(simple_frame_swap_t *sfs) {
  if (!sfs) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Simple frame swap is NULL");
    return NULL;
  }
  uintptr_t frame_ptr = atomic_load_u64(&sfs->current_frame);
  return (const video_frame_t *)(void *)frame_ptr; // NOLINT(bugprone-casting-through-void,performance-no-int-to-ptr)
}

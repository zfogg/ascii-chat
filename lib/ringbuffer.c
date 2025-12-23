/**
 * @file ringbuffer.c
 * @ingroup ringbuffer
 * @brief 🎯 Lock-free circular buffer for audio streaming with atomic operations
 */

#include "ringbuffer.h"
#include "common.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "buffer_pool.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

static inline bool __attribute__((unused)) is_power_of_two(size_t n) {
  return n && !(n & (n - 1));
}

static inline size_t next_power_of_two(size_t n) {
  if (n == 0)
    return 1;
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  if (sizeof(size_t) > 4) {
    n |= n >> 32;
  }
  return n + 1;
}

/* ============================================================================
 * Ring Buffer Implementation
 * ============================================================================
 *
 * THREAD SAFETY NOTE: This ring buffer is designed for single-producer,
 * single-consumer (SPSC) use only. The atomic operations provide memory
 * ordering guarantees but do NOT support concurrent writes from multiple
 * producers. For multi-writer scenarios, external synchronization is required.
 */

ringbuffer_t *ringbuffer_create(size_t element_size, size_t capacity) {
  if (element_size == 0 || capacity == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ring buffer parameters: element_size=%zu, capacity=%zu", element_size,
              capacity);
    return NULL;
  }

  ringbuffer_t *rb;
  rb = SAFE_CALLOC(1, sizeof(ringbuffer_t), ringbuffer_t *);

  /* Round capacity up to power of 2 for optimization */
  size_t actual_capacity = next_power_of_two(capacity);

  rb->buffer = SAFE_CALLOC(actual_capacity, element_size, char *);

  rb->element_size = element_size;
  rb->capacity = actual_capacity;
  rb->is_power_of_two = true;
  rb->capacity_mask = actual_capacity - 1;
  atomic_init(&rb->head, 0);
  atomic_init(&rb->tail, 0);
  atomic_init(&rb->size, 0);

  return rb;
}

void ringbuffer_destroy(ringbuffer_t *rb) {
  if (rb) {
    SAFE_FREE(rb->buffer);
    SAFE_FREE(rb);
  }
}

bool ringbuffer_write(ringbuffer_t *rb, const void *data) {
  if (!rb || !data)
    return false;

  size_t current_size = atomic_load(&rb->size);
  if (current_size >= rb->capacity) {
    return false; /* Buffer full */
  }

  size_t head = atomic_load(&rb->head);
  size_t next_head = (head + 1) & rb->capacity_mask;

  /* Copy data */
  SAFE_MEMCPY(rb->buffer + (head * rb->element_size), rb->element_size, data, rb->element_size);

  /* Update head and size atomically */
  atomic_store(&rb->head, next_head);
  atomic_fetch_add(&rb->size, 1);

  return true;
}

bool ringbuffer_read(ringbuffer_t *rb, void *data) {
  if (!rb || !data)
    return false;

  size_t current_size = atomic_load(&rb->size);
  if (current_size == 0) {
    return false; /* Buffer empty */
  }

  size_t tail = atomic_load(&rb->tail);
  size_t next_tail = (tail + 1) & rb->capacity_mask;

  /* Copy data */
  SAFE_MEMCPY(data, rb->element_size, rb->buffer + (tail * rb->element_size), rb->element_size);

  /* Update tail and size atomically */
  atomic_store(&rb->tail, next_tail);
  atomic_fetch_sub(&rb->size, 1);

  return true;
}

bool ringbuffer_peek(ringbuffer_t *rb, void *data) {
  if (!rb || !data)
    return false;

  size_t current_size = atomic_load(&rb->size);
  if (current_size == 0) {
    return false; /* Buffer empty */
  }

  size_t tail = atomic_load(&rb->tail);

  /* Copy data without updating tail */
  SAFE_MEMCPY(data, rb->element_size, rb->buffer + (tail * rb->element_size), rb->element_size);

  return true;
}

size_t ringbuffer_size(const ringbuffer_t *rb) {
  return rb ? atomic_load(&rb->size) : 0;
}

bool ringbuffer_is_empty(const ringbuffer_t *rb) {
  return ringbuffer_size(rb) == 0;
}

bool ringbuffer_is_full(const ringbuffer_t *rb) {
  return rb ? ringbuffer_size(rb) >= rb->capacity : true;
}

void ringbuffer_clear(ringbuffer_t *rb) {
  if (rb) {
    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
    atomic_store(&rb->size, 0);
  }
}

/* ============================================================================
 * Frame Buffer Implementation
 * ============================================================================
 */

framebuffer_t *framebuffer_create(size_t capacity) {
  if (capacity == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid frame buffer parameters");
    return NULL;
  }

  framebuffer_t *fb;
  fb = SAFE_CALLOC(1, sizeof(framebuffer_t), framebuffer_t *);

  // Initialize mutex for thread-safe access
  if (mutex_init(&fb->mutex) != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize framebuffer mutex");
    SAFE_FREE(fb);
    return NULL;
  }

  // Create ringbuffer to store frame_t structs
  fb->rb = ringbuffer_create(sizeof(frame_t), capacity);
  if (!fb->rb) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate frame buffer");
    mutex_destroy(&fb->mutex);
    SAFE_FREE(fb);
    return NULL;
  }

  return fb;
}

framebuffer_t *framebuffer_create_multi(size_t capacity) {
  if (capacity == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid capacity: %zu", capacity);
    return NULL;
  }

  framebuffer_t *fb;
  fb = SAFE_CALLOC(1, sizeof(framebuffer_t), framebuffer_t *);

  // Initialize mutex for thread-safe access
  if (mutex_init(&fb->mutex) != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize framebuffer mutex");
    SAFE_FREE(fb);
    return NULL;
  }

  // Create ringbuffer to store multi_source_frame_t structs
  fb->rb = ringbuffer_create(sizeof(multi_source_frame_t), capacity);

  if (!fb->rb) {
    mutex_destroy(&fb->mutex);
    SAFE_FREE(fb);
    return NULL;
  }

  return fb;
}

void framebuffer_destroy(framebuffer_t *fb) {
  if (!fb)
    return;

  // Add magic number check to detect double-free using rb pointer
  if (fb->rb == (ringbuffer_t *)0xDEADBEEF) {
    SET_ERRNO(ERROR_INVALID_STATE, "DOUBLE-FREE DETECTED: framebuffer %p already destroyed!", fb);
    return;
  }

  framebuffer_clear(fb);
  ringbuffer_destroy(fb->rb);
  mutex_destroy(&fb->mutex);

  // Mark as destroyed before freeing
  fb->rb = (ringbuffer_t *)0xDEADBEEF;
  SAFE_FREE(fb);
}

bool framebuffer_write_frame(framebuffer_t *fb, const char *frame_data, size_t frame_size) {
  if (!fb || !frame_data || frame_size == 0)
    return false;

  // Validate frame size to prevent overflow
  if (frame_size > 10 * 1024 * 1024) { // 10MB max for ASCII frames
    SET_ERRNO(ERROR_INVALID_PARAM, "Rejecting oversized frame: %zu bytes", frame_size);
    return false;
  }

  // Allocate a copy of the frame data using buffer pool for better performance
  // Do this BEFORE acquiring the mutex to minimize lock hold time
  char *frame_copy = (char *)buffer_pool_alloc(frame_size + 1);
  if (!frame_copy) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate %zu bytes from buffer pool for frame", frame_size + 1);
    return false;
  }

  SAFE_MEMCPY(frame_copy, frame_size, frame_data, frame_size);
  frame_copy[frame_size] = '\0'; // Ensure null termination

  // Create a frame_t struct with the copy (store allocated size for proper cleanup)
  frame_t frame = {.magic = FRAME_MAGIC, .size = frame_size + 1, .data = frame_copy};

  // BUGFIX: Thread-safe access to framebuffer (was missing, causing race conditions)
  mutex_lock(&fb->mutex);

  // Check if buffer is full - if so, we need to drop the oldest frame
  if (ringbuffer_size(fb->rb) >= fb->rb->capacity) {
    // Buffer is full, read and free the oldest frame before writing new one
    frame_t old_frame;
    if (ringbuffer_read(fb->rb, &old_frame)) {
      if (old_frame.magic == FRAME_MAGIC && old_frame.data) {
        old_frame.magic = FRAME_FREED;
        // BUG FIX: Use buffer_pool_free since data was allocated with buffer_pool_alloc
        buffer_pool_free(old_frame.data, old_frame.size);
      } else if (old_frame.magic != FRAME_MAGIC) {
        SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Invalid old frame magic 0x%x when dropping", old_frame.magic);
      }
    }
  }

  bool result = ringbuffer_write(fb->rb, &frame);

  mutex_unlock(&fb->mutex);

  if (!result) {
    // If we still couldn't write to ringbuffer, return the buffer to pool
    buffer_pool_free(frame_copy, frame_size + 1);
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to write frame to ringbuffer even after dropping oldest");
  }

  return result;
}

bool framebuffer_read_frame(framebuffer_t *fb, frame_t *frame) {
  if (!fb || !frame) {
    return false;
  }

  // Initialize frame to safe values
  frame->magic = 0;
  frame->data = NULL;
  frame->size = 0;

  // BUGFIX: Thread-safe access to framebuffer (was missing, causing race conditions)
  mutex_lock(&fb->mutex);

  bool result = ringbuffer_read(fb->rb, frame);

  // Validate the frame we just read
  if (result) {
    if (frame->magic != FRAME_MAGIC) {
      SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Invalid frame magic 0x%x (expected 0x%x)", frame->magic, FRAME_MAGIC);
      frame->data = NULL;
      frame->size = 0;
      mutex_unlock(&fb->mutex);
      return false;
    }

    if (frame->magic == FRAME_FREED) {
      SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Reading already-freed frame!");
      frame->data = NULL;
      frame->size = 0;
      mutex_unlock(&fb->mutex);
      return false;
    }

    if (frame->size > 10 * 1024 * 1024) {
      SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Frame size too large: %zu", frame->size);
      SAFE_FREE(frame->data);
      frame->data = NULL;
      frame->size = 0;
      mutex_unlock(&fb->mutex);
      return false;
    }
  }

  mutex_unlock(&fb->mutex);
  return result;
}

void framebuffer_clear(framebuffer_t *fb) {
  if (!fb || !fb->rb)
    return;

  // BUGFIX: Thread-safe access to framebuffer (was missing, causing race conditions)
  mutex_lock(&fb->mutex);

  // Check the element size to determine frame type
  if (fb->rb->element_size == sizeof(multi_source_frame_t)) {
    // Multi-source frame buffer - read and free multi_source_frame_t
    multi_source_frame_t multi_frame;
    while (ringbuffer_read(fb->rb, &multi_frame)) {
      if (multi_frame.magic == FRAME_MAGIC && multi_frame.data) {
        multi_frame.magic = FRAME_FREED; // Mark as freed to detect use-after-free
        buffer_pool_free(multi_frame.data, multi_frame.size);
      } else if (multi_frame.magic != FRAME_MAGIC && multi_frame.magic != 0) {
        SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Invalid multi-source frame magic 0x%x during clear",
                  multi_frame.magic);
      }
    }
  } else if (fb->rb->element_size == sizeof(frame_t)) {
    // Single-source frame buffer - read and free frame_t
    frame_t frame;
    while (ringbuffer_read(fb->rb, &frame)) {
      if (frame.magic == FRAME_MAGIC && frame.data) {
        frame.magic = FRAME_FREED; // Mark as freed to detect use-after-free
        buffer_pool_free(frame.data, frame.size);
      } else if (frame.magic != FRAME_MAGIC && frame.magic != 0) {
        SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Invalid frame magic 0x%x during clear", frame.magic);
      }
    }
  } else {
    SET_ERRNO(ERROR_INVALID_STATE, "Unknown frame buffer type with element size %zu", fb->rb->element_size);
  }

  // Clear the ringbuffer indices
  ringbuffer_clear(fb->rb);

  // Zero out the entire buffer to prevent any dangling pointers
  if (fb->rb->buffer) {
    SAFE_MEMSET(fb->rb->buffer, fb->rb->capacity * fb->rb->element_size, 0, fb->rb->capacity * fb->rb->element_size);
  }

  mutex_unlock(&fb->mutex);
}

// Multi-source frame functions for multi-user support

bool framebuffer_write_multi_frame(framebuffer_t *fb, const char *frame_data, size_t frame_size,
                                   uint32_t source_client_id, uint32_t frame_sequence, uint32_t timestamp) {
  if (!fb || !fb->rb || !frame_data || frame_size == 0) {
    return false;
  }

  // Allocate memory for frame data using buffer pool for better performance
  char *data_copy = (char *)buffer_pool_alloc(frame_size);
  if (!data_copy) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate %zu bytes from buffer pool for multi-source frame", frame_size);
    return false;
  }

  // Copy frame data
  SAFE_MEMCPY(data_copy, frame_size, frame_data, frame_size);

  // Create multi-source frame
  multi_source_frame_t multi_frame = {.magic = FRAME_MAGIC,
                                      .source_client_id = source_client_id,
                                      .frame_sequence = frame_sequence,
                                      .timestamp = timestamp,
                                      .size = frame_size,
                                      .data = data_copy};

  // Thread-safe access to framebuffer
  mutex_lock(&fb->mutex);

  // Try to write to ring buffer
  bool success = ringbuffer_write(fb->rb, &multi_frame);
  if (!success) {
    // Buffer full, return the buffer to pool
    buffer_pool_free(data_copy, frame_size);
    log_debug("Frame buffer full, dropping multi-source frame from client %u", source_client_id);
  }

  mutex_unlock(&fb->mutex);
  return success;
}

bool framebuffer_read_multi_frame(framebuffer_t *fb, multi_source_frame_t *frame) {
  if (!fb || !fb->rb || !frame) {
    return false;
  }

  // Thread-safe access to framebuffer
  mutex_lock(&fb->mutex);

  bool result = ringbuffer_read(fb->rb, frame);

  if (result) {
    // Validate frame magic
    if (frame->magic != FRAME_MAGIC) {
      SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Invalid multi-source frame magic 0x%x (expected 0x%x)", frame->magic,
                FRAME_MAGIC);
      frame->data = NULL;
      frame->size = 0;
      frame->source_client_id = 0;
      mutex_unlock(&fb->mutex);
      return false;
    }

    // Additional validation
    if (frame->size == 0 || !frame->data) {
      SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Invalid multi-source frame data (size=%zu, data=%p)", frame->size,
                frame->data);
      mutex_unlock(&fb->mutex);
      return false;
    }
  }

  mutex_unlock(&fb->mutex);
  return result;
}

bool framebuffer_peek_latest_multi_frame(framebuffer_t *fb, multi_source_frame_t *frame) {
  if (!fb || !fb->rb || !frame) {
    return false;
  }

  // Thread-safe access to framebuffer
  mutex_lock(&fb->mutex);

  // Use ringbuffer_peek to get the frame without consuming it
  bool result = ringbuffer_peek(fb->rb, frame);

  if (result) {
    // Validate frame magic
    if (frame->magic != FRAME_MAGIC) {
      SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Invalid multi-source frame magic 0x%x (expected 0x%x) in peek",
                frame->magic, FRAME_MAGIC);
      frame->data = NULL;
      frame->size = 0;
      frame->source_client_id = 0;
      mutex_unlock(&fb->mutex);
      return false;
    }

    // Additional validation
    if (frame->size == 0 || !frame->data) {
      SET_ERRNO(ERROR_INVALID_STATE, "CORRUPTION: Invalid multi-source frame data (size=%zu, data=%p) in peek",
                frame->size, frame->data);
      mutex_unlock(&fb->mutex);
      return false;
    }

    // IMPORTANT: We need to make a copy of the data since we're not consuming the frame
    // The original data pointer will remain valid in the ring buffer
    // Caller is responsible for freeing this copy
    char *data_copy;
    data_copy = SAFE_MALLOC(frame->size, char *);
    if (!data_copy) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for frame data copy in peek");
      mutex_unlock(&fb->mutex);
      return false;
    }
    SAFE_MEMCPY(data_copy, frame->size, frame->data, frame->size);
    frame->data = data_copy;
  }

  mutex_unlock(&fb->mutex);
  return result;
}

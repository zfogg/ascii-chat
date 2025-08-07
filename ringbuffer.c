#include "ringbuffer.h"
#include "common.h"
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
 */

ringbuffer_t *ringbuffer_create(size_t element_size, size_t capacity) {
  if (element_size == 0 || capacity == 0) {
    log_error("Invalid ring buffer parameters: element_size=%zu, capacity=%zu", element_size, capacity);
    return NULL;
  }

  ringbuffer_t *rb = (ringbuffer_t *)calloc(1, sizeof(ringbuffer_t));
  if (!rb) {
    log_error("Failed to allocate ring buffer structure");
    return NULL;
  }

  /* Round capacity up to power of 2 for optimization */
  size_t actual_capacity = next_power_of_two(capacity);

  rb->buffer = (char *)calloc(actual_capacity, element_size);
  if (!rb->buffer) {
    log_error("Failed to allocate ring buffer memory: %zu bytes", actual_capacity * element_size);
    free(rb);
    return NULL;
  }

  rb->element_size = element_size;
  rb->capacity = actual_capacity;
  rb->is_power_of_two = true;
  rb->capacity_mask = actual_capacity - 1;
  atomic_init(&rb->head, 0);
  atomic_init(&rb->tail, 0);
  atomic_init(&rb->size, 0);

  log_debug("Created ring buffer: capacity=%zu, element_size=%zu", actual_capacity, element_size);

  return rb;
}

void ringbuffer_destroy(ringbuffer_t *rb) {
  if (rb) {
    free(rb->buffer);
    free(rb);
    log_debug("Destroyed ring buffer");
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
  memcpy(rb->buffer + (head * rb->element_size), data, rb->element_size);

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
  memcpy(data, rb->buffer + (tail * rb->element_size), rb->element_size);

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
  memcpy(data, rb->buffer + (tail * rb->element_size), rb->element_size);

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
    log_debug("Cleared ring buffer");
  }
}

/* ============================================================================
 * Frame Buffer Implementation
 * ============================================================================
 */

framebuffer_t *framebuffer_create(size_t capacity) {
  if (capacity == 0) {
    log_error("Invalid frame buffer parameters");
    return NULL;
  }

  framebuffer_t *fb = (framebuffer_t *)calloc(1, sizeof(framebuffer_t));
  if (!fb) {
    log_error("Failed to allocate frame buffer structure");
    return NULL;
  }

  // Create ringbuffer to store frame_t structs
  fb->rb = ringbuffer_create(sizeof(frame_t), capacity);

  if (!fb->rb) {
    free(fb);
    return NULL;
  }

  log_info("Created frame buffer: capacity=%zu frames", capacity);

  return fb;
}

void framebuffer_destroy(framebuffer_t *fb) {
  if (fb) {
    // Use framebuffer_clear to properly clean up all frames
    framebuffer_clear(fb);

    ringbuffer_destroy(fb->rb);
    free(fb);
    log_info("Destroyed frame buffer");
  }
}

bool framebuffer_write_frame(framebuffer_t *fb, const char *frame_data, size_t frame_size) {
  if (!fb || !frame_data || frame_size == 0)
    return false;

  // Validate frame size to prevent overflow
  if (frame_size > 10 * 1024 * 1024) { // 10MB max for ASCII frames
    log_error("Rejecting oversized frame: %zu bytes", frame_size);
    return false;
  }

  // Check if buffer is full - if so, we need to drop the oldest frame
  if (ringbuffer_size(fb->rb) >= fb->rb->capacity) {
    // Buffer is full, read and free the oldest frame before writing new one
    frame_t old_frame;
    if (ringbuffer_read(fb->rb, &old_frame)) {
      if (old_frame.magic == FRAME_MAGIC && old_frame.data) {
        old_frame.magic = FRAME_FREED;
        free(old_frame.data);
      } else if (old_frame.magic != FRAME_MAGIC) {
        log_error("CORRUPTION: Invalid old frame magic 0x%x when dropping", old_frame.magic);
      }
    }
  }

  // Allocate a copy of the frame data that will be owned by the ringbuffer
  char *frame_copy = (char *)malloc(frame_size + 1);
  if (!frame_copy) {
    log_error("Failed to allocate memory for frame copy");
    return false;
  }

  memcpy(frame_copy, frame_data, frame_size);
  frame_copy[frame_size] = '\0'; // Ensure null termination

  // Create a frame_t struct with the copy
  frame_t frame = {.magic = FRAME_MAGIC, .size = frame_size, .data = frame_copy};

  bool result = ringbuffer_write(fb->rb, &frame);

  if (!result) {
    // If we still couldn't write to ringbuffer, free the copy
    free(frame_copy);
    log_error("Failed to write frame to ringbuffer even after dropping oldest");
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

  bool result = ringbuffer_read(fb->rb, frame);

  // Validate the frame we just read
  if (result) {
    if (frame->magic != FRAME_MAGIC) {
      log_error("CORRUPTION: Invalid frame magic 0x%x (expected 0x%x)", frame->magic, FRAME_MAGIC);
      frame->data = NULL;
      frame->size = 0;
      return false;
    }

    if (frame->magic == FRAME_FREED) {
      log_error("CORRUPTION: Reading already-freed frame!");
      frame->data = NULL;
      frame->size = 0;
      return false;
    }

    if (frame->size > 10 * 1024 * 1024) {
      log_error("CORRUPTION: Frame size too large: %zu", frame->size);
      free(frame->data);
      frame->data = NULL;
      frame->size = 0;
      return false;
    }

    // Additional check - validate pointer is not obviously bad
    // NOTE: unreliable across platforms
    // if ((uintptr_t)frame->data < 0x1000) {
    //   log_error("CORRUPTION: Invalid frame data pointer: %p", frame->data);
    //   frame->data = NULL;
    //   frame->size = 0;
    //   return false;
    // }
  }

  return result;
}

void framebuffer_clear(framebuffer_t *fb) {
  if (!fb || !fb->rb)
    return;

  // Read and free all frames
  frame_t frame;
  while (ringbuffer_read(fb->rb, &frame)) {
    if (frame.magic == FRAME_MAGIC && frame.data) {
      frame.magic = FRAME_FREED; // Mark as freed to detect use-after-free
      free(frame.data);
      frame.data = NULL;
    } else if (frame.magic != FRAME_MAGIC && frame.magic != 0) {
      log_error("CORRUPTION: Invalid frame magic 0x%x during clear", frame.magic);
    }
  }

  // Clear the ringbuffer indices
  ringbuffer_clear(fb->rb);

  // Zero out the entire buffer to prevent any dangling pointers
  if (fb->rb->buffer) {
    memset(fb->rb->buffer, 0, fb->rb->capacity * fb->rb->element_size);
  }
}
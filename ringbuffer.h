#ifndef ASCII_CHAT_RINGBUFFER_H
#define ASCII_CHAT_RINGBUFFER_H

#include "common.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Lock-Free Ring Buffer for Frame Management
 *
 * This ring buffer uses atomic operations to provide thread-safe access
 * without locks, improving performance for producer-consumer scenarios.
 * ============================================================================
 */

typedef struct {
  char *buffer;         /* The actual buffer memory */
  size_t element_size;  /* Size of each element */
  size_t capacity;      /* Number of elements that can be stored */
  _Atomic size_t head;  /* Write position (producer) */
  _Atomic size_t tail;  /* Read position (consumer) */
  _Atomic size_t size;  /* Current number of elements */
  bool is_power_of_two; /* Whether capacity is power of 2 (for optimization) */
  size_t capacity_mask; /* Mask for fast modulo when power of 2 */
} ringbuffer_t;

/* ============================================================================
 * Ring Buffer API
 * ============================================================================
 */

/**
 * Create a new ring buffer
 * @param element_size Size of each element in bytes
 * @param capacity Number of elements (will be rounded up to power of 2)
 * @return Pointer to ring buffer or NULL on failure
 */
ringbuffer_t *ringbuffer_create(size_t element_size, size_t capacity);

/**
 * Destroy a ring buffer and free its memory
 * @param rb Ring buffer to destroy
 */
void ringbuffer_destroy(ringbuffer_t *rb);

/**
 * Try to write an element to the ring buffer (non-blocking)
 * @param rb Ring buffer
 * @param data Data to write
 * @return true if successful, false if buffer is full
 */
bool ringbuffer_write(ringbuffer_t *rb, const void *data);

/**
 * Try to read an element from the ring buffer (non-blocking)
 * @param rb Ring buffer
 * @param data Buffer to read into
 * @return true if successful, false if buffer is empty
 */
bool ringbuffer_read(ringbuffer_t *rb, void *data);

/**
 * Peek at the next element without removing it
 * @param rb Ring buffer
 * @param data Buffer to peek into
 * @return true if successful, false if buffer is empty
 */
bool ringbuffer_peek(ringbuffer_t *rb, void *data);

/**
 * Get current number of elements in the buffer
 * @param rb Ring buffer
 * @return Number of elements
 */
size_t ringbuffer_size(const ringbuffer_t *rb);

/**
 * Check if buffer is empty
 * @param rb Ring buffer
 * @return true if empty
 */
bool ringbuffer_is_empty(const ringbuffer_t *rb);

/**
 * Check if buffer is full
 * @param rb Ring buffer
 * @return true if full
 */
bool ringbuffer_is_full(const ringbuffer_t *rb);

/**
 * Clear all elements from the buffer
 * @param rb Ring buffer
 */
void ringbuffer_clear(ringbuffer_t *rb);

/* ============================================================================
 * Frame Buffer Specific
 * ============================================================================
 */

typedef struct {
  ringbuffer_t *rb;
  size_t max_frame_size;
} framebuffer_t;

/**
 * Create a frame buffer for ASCII frames
 * @param capacity Number of frames to buffer
 * @param max_frame_size Maximum size of a single frame
 * @return Frame buffer or NULL on failure
 */
framebuffer_t *framebuffer_create(size_t capacity, size_t max_frame_size);

/**
 * Destroy a frame buffer
 * @param fb Frame buffer to destroy
 */
void framebuffer_destroy(framebuffer_t *fb);

/**
 * Write a frame to the buffer
 * @param fb Frame buffer
 * @param frame Frame data (null-terminated string)
 * @return true if successful, false if buffer is full
 */
bool framebuffer_write_frame(framebuffer_t *fb, const char *frame);

/**
 * Read a frame from the buffer
 * @param fb Frame buffer
 * @param frame Buffer to read frame into (must be at least max_frame_size)
 * @return true if successful, false if buffer is empty
 */
bool framebuffer_read_frame(framebuffer_t *fb, char *frame);

#endif /* ASCII_CHAT_RINGBUFFER_H */
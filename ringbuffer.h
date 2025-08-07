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

/**
 * Frame structure that stores both data and actual size
 * This eliminates the need for fixed-size buffers and null-terminator scanning
 */
typedef struct {
  uint32_t magic; /* Magic number to detect corruption */
  size_t size;    /* Actual size of frame data */
  char *data;     /* Pointer to frame data (not owned by this struct) */
} frame_t;

#define FRAME_MAGIC 0xDEADBEEF
#define FRAME_FREED 0xFEEDFACE

typedef struct {
  ringbuffer_t *rb;
} framebuffer_t;

/**
 * Create a frame buffer for ASCII frames
 * @param capacity Number of frames to buffer
 * @return Frame buffer or NULL on failure
 */
framebuffer_t *framebuffer_create(size_t capacity);

/**
 * Destroy a frame buffer
 * @param fb Frame buffer to destroy
 */
void framebuffer_destroy(framebuffer_t *fb);

/**
 * Write a frame to the buffer
 * @param fb Frame buffer
 * @param frame_data Frame data
 * @param frame_size Actual size of frame data
 * @return true if successful, false if buffer is full
 */
bool framebuffer_write_frame(framebuffer_t *fb, const char *frame_data, size_t frame_size);

/**
 * Read a frame from the buffer
 * @param fb Frame buffer
 * @param frame Pointer to frame_t struct to be filled
 * @return true if successful, false if buffer is empty
 */
bool framebuffer_read_frame(framebuffer_t *fb, frame_t *frame);

/**
 * Clear all frames from the buffer, freeing their data
 * @param fb Frame buffer
 */
void framebuffer_clear(framebuffer_t *fb);

#endif /* ASCII_CHAT_RINGBUFFER_H */
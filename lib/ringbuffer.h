#pragma once

/**
 * @file ringbuffer.h
 * @brief Lock-Free Ring Buffer and Frame Buffer Management
 * @ingroup ringbuffer
 * @addtogroup ringbuffer
 * @{
 *
 * This header provides lock-free ring buffer implementations for high-performance
 * producer-consumer scenarios in ascii-chat. The system includes both generic
 * lock-free ring buffers and specialized audio and frame buffers for media streaming.
 *
 * CORE FEATURES:
 * ==============
 * - Lock-free ring buffers using atomic operations
 * - Thread-safe producer-consumer data transfer
 * - Power-of-2 capacity optimization for fast modulo
 * - Specialized audio ring buffers with jitter buffering
 * - Frame buffer management for video frames
 * - Multi-source frame support for multi-client scenarios
 *
 * RING BUFFER ARCHITECTURE:
 * ========================
 * The lock-free ring buffer uses atomic operations for:
 * - Lock-free writes (producer never blocks consumer)
 * - Lock-free reads (consumer never blocks producer)
 * - Atomic size tracking for fast emptiness checks
 * - Power-of-2 capacity enables bit masking instead of modulo
 *
 * AUDIO RING BUFFER:
 * ==================
 * Specialized audio ring buffer with:
 * - Fixed-size circular buffer for audio samples
 * - Jitter buffer threshold for network latency compensation
 * - Mutex-protected operations for audio thread safety
 * - Optimized for real-time audio streaming
 *
 * FRAME BUFFER:
 * =============
 * Frame buffer system for ASCII video frames:
 * - Generic frame buffers for single-source scenarios
 * - Multi-source frame buffers for multi-client support
 * - Magic number validation for corruption detection
 * - Automatic frame data management
 *
 * PERFORMANCE BENEFITS:
 * =====================
 * - Lock-free operations minimize latency
 * - Power-of-2 capacity enables fast bit masking
 * - Zero-allocation pattern after initialization
 * - Efficient producer-consumer data transfer
 *
 * THREAD SAFETY:
 * ==============
 * - Generic ring buffers: Lock-free using atomic operations
 * - Audio ring buffers: Mutex-protected for thread safety
 * - Frame buffers: Mutex-protected for concurrent access
 *
 * @note Ring buffers are ideal for producer-consumer scenarios where
 *       one thread produces data and another consumes it.
 * @note The lock-free ring buffer requires power-of-2 capacity for
 *       optimal performance (uses bit masking instead of modulo).
 * @note Audio ring buffers use jitter buffering to compensate for
 *       network latency and packet timing variations.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "platform/mutex.h"

/**
 * @brief Lock-free ring buffer structure
 *
 * Implements a lock-free circular buffer using atomic operations for
 * thread-safe producer-consumer data transfer without locking overhead.
 *
 * @ingroup ringbuffer
 */
typedef struct {
  /** @brief The actual buffer memory */
  char *buffer;
  /** @brief Size of each element in bytes */
  size_t element_size;
  /** @brief Number of elements that can be stored */
  size_t capacity;
  /** @brief Write position (producer) - atomic for lock-free operations */
  _Atomic size_t head;
  /** @brief Read position (consumer) - atomic for lock-free operations */
  _Atomic size_t tail;
  /** @brief Current number of elements - atomic for fast size checks */
  _Atomic size_t size;
  /** @brief Whether capacity is power of 2 (enables bit masking optimization) */
  bool is_power_of_two;
  /** @brief Mask for fast modulo when capacity is power of 2 */
  size_t capacity_mask;
} ringbuffer_t;

/* ============================================================================
 * Audio Ring Buffer - Simple ring buffer for audio samples
 * ============================================================================
 */

/**
 * @name Audio Ring Buffer Constants
 * @ingroup ringbuffer
 * @{
 */

/** @brief Audio ring buffer size in samples (~1.7s @ 48kHz, 4x Opus batch) */
#define AUDIO_RING_BUFFER_SIZE (960 * 20 * 4)

/** @brief Jitter buffer threshold (wait for one full Opus batch = 400ms before starting playback) */
#define AUDIO_JITTER_BUFFER_THRESHOLD (960 * 20)

/** @} */

/**
 * @brief Audio ring buffer for real-time audio streaming
 *
 * Specialized ring buffer for audio samples with jitter buffering to
 * compensate for network latency and packet timing variations. Uses
 * mutex-protected operations for thread-safe audio access.
 *
 * @ingroup ringbuffer
 */
typedef struct audio_ring_buffer {
  /** @brief Audio sample data buffer */
  float data[AUDIO_RING_BUFFER_SIZE];
  /** @brief Write index (producer position) */
  volatile int write_index;
  /** @brief Read index (consumer position) */
  volatile int read_index;
  /** @brief True after initial jitter buffer fill */
  volatile bool jitter_buffer_filled;
  /** @brief Mutex for thread-safe operations */
  mutex_t mutex;
} audio_ring_buffer_t;

/* ============================================================================
 * Ring Buffer API
 * ============================================================================
 */

/**
 * @brief Create a new ring buffer
 * @param element_size Size of each element in bytes
 * @param capacity Number of elements (will be rounded up to power of 2)
 * @return Pointer to ring buffer or NULL on failure
 *
 * Creates a new lock-free ring buffer with the specified element size and
 * capacity. The capacity is automatically rounded up to the next power of 2
 * to enable fast bit masking optimizations.
 *
 * @ingroup ringbuffer
 */
ringbuffer_t *ringbuffer_create(size_t element_size, size_t capacity);

/**
 * @brief Destroy a ring buffer and free its memory
 * @param rb Ring buffer to destroy (must not be NULL)
 *
 * Frees all memory associated with the ring buffer. The buffer must not
 * be in use by any threads when this function is called.
 *
 * @ingroup ringbuffer
 */
void ringbuffer_destroy(ringbuffer_t *rb);

/**
 * @brief Try to write an element to the ring buffer (non-blocking)
 * @param rb Ring buffer (must not be NULL)
 * @param data Data to write (must not be NULL, must be element_size bytes)
 * @return true if successful, false if buffer is full
 *
 * Attempts to write an element to the ring buffer. This is a non-blocking
 * operation that uses lock-free atomic operations. Returns false immediately
 * if the buffer is full.
 *
 * @note SPSC only: Single Producer, Single Consumer. NOT thread-safe for
 *       multiple producers - use external synchronization if needed.
 *
 * @ingroup ringbuffer
 */
bool ringbuffer_write(ringbuffer_t *rb, const void *data);

/**
 * @brief Try to read an element from the ring buffer (non-blocking)
 * @param rb Ring buffer (must not be NULL)
 * @param data Buffer to read into (must not be NULL, must be element_size bytes)
 * @return true if successful, false if buffer is empty
 *
 * Attempts to read an element from the ring buffer. This is a non-blocking
 * operation that uses lock-free atomic operations. Returns false immediately
 * if the buffer is empty.
 *
 * @note SPSC only: Single Producer, Single Consumer. NOT thread-safe for
 *       multiple consumers - use external synchronization if needed.
 *
 * @ingroup ringbuffer
 */
bool ringbuffer_read(ringbuffer_t *rb, void *data);

/**
 * @brief Peek at the next element without removing it
 * @param rb Ring buffer (must not be NULL)
 * @param data Buffer to peek into (must not be NULL, must be element_size bytes)
 * @return true if successful, false if buffer is empty
 *
 * Examines the next element without removing it from the buffer. This is
 * useful for previewing data before committing to reading it.
 *
 * @note SPSC only: Single Producer, Single Consumer. NOT thread-safe for
 *       multiple concurrent peek operations.
 *
 * @ingroup ringbuffer
 */
bool ringbuffer_peek(ringbuffer_t *rb, void *data);

/**
 * @brief Get current number of elements in the buffer
 * @param rb Ring buffer (must not be NULL)
 * @return Number of elements currently in the buffer
 *
 * Returns the current size of the buffer. This is a snapshot value that
 * may change immediately after return due to concurrent operations.
 *
 * @note Thread-safe: Uses atomic operations for lock-free size check.
 *
 * @ingroup ringbuffer
 */
size_t ringbuffer_size(const ringbuffer_t *rb);

/**
 * @brief Check if buffer is empty
 * @param rb Ring buffer (must not be NULL)
 * @return true if buffer is empty, false otherwise
 *
 * Performs a fast check to determine if the buffer contains any elements.
 * More efficient than checking if size() == 0.
 *
 * @note Thread-safe: Uses atomic operations for lock-free check.
 *
 * @ingroup ringbuffer
 */
bool ringbuffer_is_empty(const ringbuffer_t *rb);

/**
 * @brief Check if buffer is full
 * @param rb Ring buffer (must not be NULL)
 * @return true if buffer is full, false otherwise
 *
 * Performs a fast check to determine if the buffer has reached capacity.
 * More efficient than checking if size() == capacity.
 *
 * @note Thread-safe: Uses atomic operations for lock-free check.
 *
 * @ingroup ringbuffer
 */
bool ringbuffer_is_full(const ringbuffer_t *rb);

/**
 * @brief Clear all elements from the buffer
 * @param rb Ring buffer (must not be NULL)
 *
 * Removes all elements from the buffer and resets it to empty state.
 * This operation is not atomic with respect to concurrent read/write
 * operations - use with caution in multi-threaded scenarios.
 *
 * @note Thread-safe: Uses atomic operations, but concurrent operations
 *       may still occur during clearing.
 *
 * @ingroup ringbuffer
 */
void ringbuffer_clear(ringbuffer_t *rb);

/* ============================================================================
 * Frame Buffer Specific
 * ============================================================================
 */

/**
 * @brief Frame structure that stores both data and actual size
 *
 * Stores frame data along with its actual size, eliminating the need for
 * fixed-size buffers and null-terminator scanning. Uses magic number
 * validation for corruption detection.
 *
 * @ingroup ringbuffer
 */
typedef struct {
  /** @brief Magic number to detect corruption (FRAME_MAGIC when valid) */
  uint32_t magic;
  /** @brief Actual size of frame data in bytes */
  size_t size;
  /** @brief Pointer to frame data (not owned by this struct) */
  char *data;
} frame_t;

/**
 * @brief Multi-source frame structure for multi-user support
 *
 * Tracks which client sent the frame along with metadata including
 * sequence numbers and timestamps for proper frame ordering and display.
 *
 * @ingroup ringbuffer
 */
typedef struct {
  /** @brief Magic number to detect corruption (FRAME_MAGIC when valid) */
  uint32_t magic;
  /** @brief Client ID that sent this frame */
  uint32_t source_client_id;
  /** @brief Frame sequence number for ordering */
  uint32_t frame_sequence;
  /** @brief Timestamp when frame was captured */
  uint32_t timestamp;
  /** @brief Actual size of frame data in bytes */
  size_t size;
  /** @brief Pointer to frame data (not owned by this struct) */
  char *data;
} multi_source_frame_t;

/**
 * @name Frame Magic Numbers
 * @ingroup ringbuffer
 * @{
 */

/** @brief Magic number indicating a valid frame (0xDEADBEEF) */
#define FRAME_MAGIC 0xDEADBEEF
/** @brief Magic number indicating a freed frame (0xFEEDFACE) */
#define FRAME_FREED 0xFEEDFACE

/** @} */

/**
 * @brief Frame buffer structure for managing video frames
 *
 * Wraps a ring buffer with mutex protection for thread-safe frame management.
 * Used for both single-source and multi-source frame scenarios.
 *
 * @ingroup ringbuffer
 */
typedef struct {
  /** @brief Underlying ring buffer for frame storage */
  ringbuffer_t *rb;
  /** @brief Mutex for thread-safe access to framebuffer operations */
  mutex_t mutex;
} framebuffer_t;

/**
 * @brief Create a frame buffer for ASCII frames
 * @param capacity Number of frames to buffer
 * @return Frame buffer or NULL on failure
 *
 * Creates a new frame buffer for single-source frame scenarios. The capacity
 * is automatically rounded up to the next power of 2 for optimal performance.
 *
 * @ingroup ringbuffer
 */
framebuffer_t *framebuffer_create(size_t capacity);

/**
 * @brief Create a multi-source frame buffer for multi-user support
 * @param capacity Number of frames to buffer
 * @return Frame buffer or NULL on failure
 *
 * Creates a new frame buffer for multi-client frame scenarios. Supports
 * tracking frame sources, sequence numbers, and timestamps for proper
 * multi-user frame display.
 *
 * @ingroup ringbuffer
 */
framebuffer_t *framebuffer_create_multi(size_t capacity);

/**
 * @brief Destroy a frame buffer
 * @param fb Frame buffer to destroy (must not be NULL)
 *
 * Frees all memory associated with the frame buffer, including any
 * frames still in the buffer. The buffer must not be in use by any
 * threads when this function is called.
 *
 * @ingroup ringbuffer
 */
void framebuffer_destroy(framebuffer_t *fb);

/**
 * @brief Write a frame to the buffer
 * @param fb Frame buffer (must not be NULL)
 * @param frame_data Frame data (must not be NULL)
 * @param frame_size Actual size of frame data in bytes
 * @return true if successful, false if buffer is full
 *
 * Writes a single-source frame to the buffer. The frame data is copied
 * into the buffer, so the caller retains ownership of the original data.
 *
 * @note Thread-safe: Protected by mutex for concurrent access.
 *
 * @ingroup ringbuffer
 */
bool framebuffer_write_frame(framebuffer_t *fb, const char *frame_data, size_t frame_size);

/**
 * @brief Read a frame from the buffer
 * @param fb Frame buffer (must not be NULL)
 * @param frame Pointer to frame_t struct to be filled (must not be NULL)
 * @return true if successful, false if buffer is empty
 *
 * Reads a single-source frame from the buffer. The frame data pointer
 * points to data owned by the buffer until the frame is cleared or
 * the buffer is destroyed.
 *
 * @note Thread-safe: Protected by mutex for concurrent access.
 *
 * @note The caller must not free the frame data pointer - it's owned
 *       by the buffer until the frame is consumed.
 *
 * @ingroup ringbuffer
 */
bool framebuffer_read_frame(framebuffer_t *fb, frame_t *frame);

/**
 * @brief Clear all frames from the buffer, freeing their data
 * @param fb Frame buffer (must not be NULL)
 *
 * Removes all frames from the buffer and frees their associated data.
 * This operation is thread-safe but should be used with caution if
 * other threads may be reading frames.
 *
 * @note Thread-safe: Protected by mutex for concurrent access.
 *
 * @ingroup ringbuffer
 */
void framebuffer_clear(framebuffer_t *fb);

/**
 * @brief Write a multi-source frame to the buffer (for multi-user support)
 * @param fb Frame buffer (must not be NULL)
 * @param frame_data Frame data (must not be NULL)
 * @param frame_size Actual size of frame data in bytes
 * @param source_client_id Client ID that sent this frame
 * @param frame_sequence Frame sequence number for ordering
 * @param timestamp Timestamp when frame was captured
 * @return true if successful, false if buffer is full
 *
 * Writes a multi-source frame to the buffer with associated metadata.
 * The frame data is copied into the buffer, so the caller retains
 * ownership of the original data.
 *
 * @note Thread-safe: Protected by mutex for concurrent access.
 *
 * @note The frame buffer must have been created with framebuffer_create_multi()
 *       for multi-source support.
 *
 * @ingroup ringbuffer
 */
bool framebuffer_write_multi_frame(framebuffer_t *fb, const char *frame_data, size_t frame_size,
                                   uint32_t source_client_id, uint32_t frame_sequence, uint32_t timestamp);

/**
 * @brief Read a multi-source frame from the buffer
 * @param fb Frame buffer (must not be NULL)
 * @param frame Pointer to multi_source_frame_t struct to be filled (must not be NULL)
 * @return true if successful, false if buffer is empty
 *
 * Reads a multi-source frame from the buffer along with its metadata.
 * The frame data pointer points to data owned by the buffer until the
 * frame is cleared or the buffer is destroyed.
 *
 * @note Thread-safe: Protected by mutex for concurrent access.
 *
 * @note The caller must not free the frame data pointer - it's owned
 *       by the buffer until the frame is consumed.
 *
 * @ingroup ringbuffer
 */
bool framebuffer_read_multi_frame(framebuffer_t *fb, multi_source_frame_t *frame);

/**
 * @brief Peek at the latest multi-source frame without removing it
 * @param fb Frame buffer (must not be NULL)
 * @param frame Pointer to multi_source_frame_t struct to be filled (must not be NULL)
 * @return true if successful, false if buffer is empty
 *
 * Examines the latest multi-source frame without removing it from the buffer.
 * Useful for previewing frames or checking frame metadata before reading.
 *
 * @note Thread-safe: Protected by mutex for concurrent access.
 *
 * @note The frame data pointer is valid only until the buffer is modified.
 *       Do not use the data pointer after subsequent buffer operations.
 *
 * @ingroup ringbuffer
 */
bool framebuffer_peek_latest_multi_frame(framebuffer_t *fb, multi_source_frame_t *frame);

/** @} */
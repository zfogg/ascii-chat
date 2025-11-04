#pragma once

/**
 * @file video_frame.h
 * @defgroup video_frame Video Frames
 * @ingroup video_frame
 * @brief High-performance video frame management with double-buffering and lock-free operations
 *
 * This header provides a high-performance video frame management system
 * inspired by WebRTC, Zoom, and Google Meet architectures. It implements a
 * lock-free double-buffering system with atomic pointer swaps for zero-copy
 * frame transfer.
 *
 * CORE FEATURES:
 * ==============
 * - Zero-copy frame transfer (pointer swaps only)
 * - Lock-free read operations (reader never blocks writer)
 * - Latest-frame-wins semantics (old frames are dropped, not queued)
 * - Double buffering with atomic pointer swaps
 * - Comprehensive quality metrics tracking
 * - Pre-allocated buffers for zero-allocation operation
 * - Frame sequence numbering for drop detection
 *
 * ARCHITECTURE:
 * =============
 * The system uses a double-buffering pattern:
 * - Front Buffer: Currently being displayed (reader owns)
 * - Back Buffer: Currently being written (writer owns)
 * - Atomic swap: Makes new frame available without blocking
 *
 * WRITER API:
 * ===========
 * 1. Call video_frame_begin_write() to get back buffer pointer
 * 2. Write frame data directly to back buffer
 * 3. Call video_frame_commit() to atomically swap buffers
 *
 * READER API:
 * ===========
 * 1. Call video_frame_get_latest() to get current frame
 * 2. Returns NULL if no new frame since last read
 * 3. Lock-free operation never blocks writer
 *
 * PERFORMANCE BENEFITS:
 * =====================
 * - Zero-copy eliminates memcpy overhead
 * - Lock-free reads minimize latency
 * - Pre-allocated buffers avoid runtime allocations
 * - Latest-frame-wins prevents buffer buildup
 * - Metrics enable adaptive quality control
 *
 * QUALITY METRICS:
 * ================
 * The system tracks:
 * - Total frames received
 * - Total frames dropped
 * - Frame drop rate
 * - Average decode time
 * - Average render time
 *
 * These metrics enable adaptive streaming quality based on performance.
 *
 * @note Frame data is not owned by the frame structure - it points to
 *       pre-allocated buffers managed by the frame buffer.
 * @note The writer must call video_frame_commit() after writing each frame
 *       to make it available to readers.
 * @note Readers should check for NULL returns from video_frame_get_latest()
 *       to handle cases where no new frame is available.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "platform/abstraction.h"

/* ============================================================================
 * Video Frame Constants
 * ============================================================================
 */

/** @brief Video frame buffer size (2MB per frame) */
#define VIDEO_FRAME_BUFFER_SIZE (2 * 1024 * 1024)
/** @brief Maximum number of frame buffers (double buffering) */
#define MAX_FRAME_BUFFERS 2

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

/**
 * @brief Video frame structure
 *
 * Represents a single video frame with data, dimensions, timing, and quality
 * metrics. Frame data points to pre-allocated buffers managed by the frame
 * buffer system.
 *
 * @note Frame data is not owned by this structure - it points to pre-allocated
 *       buffers that are reused across frames.
 * @note Sequence numbers are used to detect dropped frames (gaps in sequence).
 * @note Timestamps are in microseconds for high precision timing.
 *
 * @ingroup video_frame
 */
typedef struct {
  /** @name Frame Data
   * @{
   */
  void *data;  ///< Frame data pointer (points to pre-allocated buffer)
  size_t size; ///< Size of frame data in bytes
  /** @} */

  /** @name Frame Dimensions
   * @{
   */
  uint32_t width;  ///< Frame width in pixels
  uint32_t height; ///< Frame height in pixels
  /** @} */

  /** @name Timing Information
   * @{
   */
  uint64_t capture_timestamp_us; ///< Timestamp when frame was captured (microseconds)
  uint64_t receive_timestamp_us; ///< Timestamp when frame was received (microseconds)
  uint64_t sequence_number;      ///< Frame sequence number (for drop detection)
  /** @} */

  /** @name Quality Metrics
   * @{
   */
  uint32_t encoding_time_us; ///< Time taken to encode/decode frame (microseconds)
  bool is_keyframe;          ///< True if this is a keyframe (important frame)
  /** @} */
} video_frame_t;

/**
 * @brief Video frame buffer manager
 *
 * Implements double-buffered frame management with atomic pointer swaps for
 * zero-copy frame transfer. Writer thread writes to back buffer while reader
 * thread reads from front buffer. Atomic swap makes new frames available
 * without blocking.
 *
 * DOUBLE BUFFERING ARCHITECTURE:
 * - front_buffer: Currently displayed frame (reader owns)
 * - back_buffer: Currently written frame (writer owns)
 * - Atomic swap: Swaps front/back when writer commits frame
 *
 * SYNCHRONIZATION:
 * - Atomic bool flag signals when new frame is available
 * - Brief mutex for pointer swap (minimal contention)
 * - Lock-free reads (reader never blocks writer)
 *
 * STATISTICS TRACKING:
 * - Total frames received/dropped
 * - Frame drop rate
 * - Average decode/render times
 * - Quality metrics for adaptive streaming
 *
 * @note Pre-allocates buffers at creation time for zero-allocation operation.
 * @note Latest-frame-wins semantics (old frames are dropped, not queued).
 * @note Thread-safe: Writer and reader can operate concurrently.
 *
 * @ingroup video_frame
 */
typedef struct {
  /** @name Double Buffering
   * @{
   */
  video_frame_t *front_buffer; ///< Currently displayed frame (reader owns)
  video_frame_t *back_buffer;  ///< Currently written frame (writer owns)
  /** @} */

  /** @name Synchronization
   * @{
   */
  atomic_bool new_frame_available; ///< Atomic flag: true when new frame available
  mutex_t swap_mutex;              ///< Brief mutex for pointer swap only
  /** @} */

  /** @name Frame Allocation
   * @{
   */
  video_frame_t frames[2]; ///< Pre-allocated frame structures (reused forever)
  /** @} */

  /** @name Statistics
   * @{
   */
  atomic_ullong total_frames_received; ///< Total frames received (atomic counter)
  atomic_ullong total_frames_dropped;  ///< Total frames dropped (atomic counter)
  atomic_ullong last_frame_sequence;   ///< Last frame sequence number (atomic)
  /** @} */

  /** @name Quality Metrics
   * @{
   */
  atomic_uint avg_decode_time_us; ///< Average decode time in microseconds (atomic)
  atomic_uint avg_render_time_us; ///< Average render time in microseconds (atomic)
  /** @} */

  /** @name Client Info
   * @{
   */
  uint32_t client_id; ///< Client ID this buffer belongs to
  bool active;        ///< True if buffer is active (receiving frames)
  /** @} */

  /** @name Buffer Management
   * @{
   */
  size_t allocated_buffer_size; ///< Size of allocated data buffers (for cleanup)
  /** @} */
} video_frame_buffer_t;

/**
 * @brief Frame statistics structure
 *
 * Contains aggregated statistics for video frame quality monitoring.
 * Used for adaptive streaming quality control based on performance metrics.
 *
 * @ingroup video_frame
 */
typedef struct {
  /** @brief Total frames received since creation */
  uint64_t total_frames;
  /** @brief Total frames dropped (due to buffer full or errors) */
  uint64_t dropped_frames;
  /** @brief Frame drop rate (dropped_frames / total_frames, 0.0-1.0) */
  float drop_rate;
  /** @brief Average frame decode time in microseconds */
  uint32_t avg_decode_time_us;
  /** @brief Average frame render time in microseconds */
  uint32_t avg_render_time_us;
} video_frame_stats_t;

/**
 * @brief Simple atomic frame swap structure
 *
 * Simplified frame management for basic use cases that don't need statistics.
 * Uses atomic pointer swaps for lock-free frame updates. This is the pattern
 * used by many video streaming services for simple frame delivery.
 *
 * @note This is a lighter-weight alternative to video_frame_buffer_t when
 *       statistics tracking is not needed.
 * @note Uses two pre-allocated frames that alternate on each update.
 * @note Atomic pointer swap enables lock-free operation.
 *
 * @ingroup video_frame
 */
typedef struct {
  atomic_uintptr_t current_frame; ///< Atomic pointer to current frame
  video_frame_t frame_a;          ///< First pre-allocated frame
  video_frame_t frame_b;          ///< Second pre-allocated frame
  atomic_bool use_frame_a;        ///< Atomic flag: true = use frame_a next, false = use frame_b
} simple_frame_swap_t;

/* ============================================================================
 * Video Frame Buffer API
 * @{
 */

/**
 * @brief Create a double-buffered video frame manager
 * @param client_id Client ID for this frame buffer
 * @return Pointer to newly created frame buffer, or NULL on failure
 *
 * Creates a new video frame buffer manager with double buffering. Pre-allocates
 * all buffers (frame structures and data buffers) for zero-allocation operation.
 * Initializes atomic counters and synchronization primitives.
 *
 * @note Pre-allocates 2MB × 2 buffers = 4MB total per frame buffer.
 * @note Frame buffers are reused forever (no runtime allocations).
 * @note Thread-safe: Can be used concurrently by writer and reader threads.
 *
 * @warning Must call video_frame_buffer_destroy() to free resources.
 *
 * @ingroup video_frame
 */
video_frame_buffer_t *video_frame_buffer_create(uint32_t client_id);

/**
 * @brief Destroy frame buffer and free all resources
 * @param vfb Frame buffer to destroy (can be NULL)
 *
 * Destroys the frame buffer and frees all pre-allocated buffers and internal
 * structures. Safe to call multiple times or with NULL pointer.
 *
 * @note All frame data and statistics are lost after destruction.
 *
 * @ingroup video_frame
 */
void video_frame_buffer_destroy(video_frame_buffer_t *vfb);

/**
 * @brief Writer API: Start writing a new frame
 * @param vfb Frame buffer manager (must not be NULL)
 * @return Pointer to back buffer for writing, or NULL on error
 *
 * Gets a pointer to the back buffer for writing a new frame. Writer thread
 * can write frame data directly to this buffer. MUST call video_frame_commit()
 * after writing to make the frame available to readers.
 *
 * @note Writer should fill in all frame fields (data, size, width, height, etc.).
 * @note Back buffer is exclusive to writer until video_frame_commit() is called.
 * @note If previous frame wasn't committed, it will be lost (latest-frame-wins).
 *
 * @warning Must call video_frame_commit() after writing to make frame available.
 *
 * @ingroup video_frame
 */
video_frame_t *video_frame_begin_write(video_frame_buffer_t *vfb);

/**
 * @brief Writer API: Commit the frame and swap buffers
 * @param vfb Frame buffer manager (must not be NULL)
 *
 * Commits the current back buffer and atomically swaps it with the front buffer.
 * Makes the new frame available to readers immediately. This is a brief critical
 * section (uses mutex), but readers continue reading from old front buffer during
 * swap, so they never block.
 *
 * @note After this call, the previously written frame becomes the front buffer.
 * @note Atomic flag is set to signal readers that a new frame is available.
 * @note Frame statistics are updated (total_frames_received, etc.).
 *
 * @note Thread-safe: Writer can commit while reader is reading (no blocking).
 *
 * @ingroup video_frame
 */
void video_frame_commit(video_frame_buffer_t *vfb);

/**
 * @brief Reader API: Get latest frame if available
 * @param vfb Frame buffer manager (must not be NULL)
 * @return Pointer to latest frame, or NULL if no new frame available
 *
 * Gets a pointer to the latest frame if one is available. Returns NULL if no
 * new frame has been committed since the last call. This is a lock-free operation
 * that never blocks the writer thread.
 *
 * @note Frame pointer is valid until next video_frame_commit() call.
 * @note Lock-free read operation (uses atomic flag check).
 * @note Reader should check for NULL returns and handle accordingly.
 *
 * @note Thread-safe: Reader can read while writer is writing (no blocking).
 *
 * @ingroup video_frame
 */
const video_frame_t *video_frame_get_latest(video_frame_buffer_t *vfb);

/**
 * @brief Get frame statistics for quality monitoring
 * @param vfb Frame buffer manager (must not be NULL)
 * @param stats Output structure for statistics (must not be NULL)
 *
 * Retrieves aggregated statistics from the frame buffer including total frames,
 * dropped frames, drop rate, and average processing times. Useful for adaptive
 * streaming quality control.
 *
 * @note All statistics are calculated atomically from atomic counters.
 * @note Drop rate is calculated as dropped_frames / total_frames.
 *
 * @ingroup video_frame
 */
void video_frame_get_stats(video_frame_buffer_t *vfb, video_frame_stats_t *stats);

/** @} */

/* ============================================================================
 * Simple Frame Swap API (Lightweight Alternative)
 * @{
 */

/**
 * @brief Create a simple atomic frame swap
 * @return Pointer to newly created frame swap, or NULL on failure
 *
 * Creates a lightweight frame swap structure for basic use cases that don't
 * need statistics tracking. Uses atomic pointer swaps for lock-free operation.
 *
 * @note This is a simpler alternative to video_frame_buffer_t.
 * @note Pre-allocates two frame structures that alternate on each update.
 * @note No statistics tracking (lighter weight than full frame buffer).
 *
 * @warning Must call simple_frame_swap_destroy() to free resources.
 *
 * @ingroup video_frame
 */
simple_frame_swap_t *simple_frame_swap_create(void);

/**
 * @brief Destroy simple frame swap and free resources
 * @param sfs Frame swap to destroy (can be NULL)
 *
 * Destroys the simple frame swap and frees all associated resources. Safe to
 * call multiple times or with NULL pointer.
 *
 * @ingroup video_frame
 */
void simple_frame_swap_destroy(simple_frame_swap_t *sfs);

/**
 * @brief Update frame data in simple frame swap
 * @param sfs Frame swap (must not be NULL)
 * @param data Frame data pointer (must not be NULL)
 * @param size Size of frame data in bytes
 *
 * Updates the current frame with new data. Atomically swaps the active frame
 * pointer to make new data available to readers. This is a zero-copy operation
 * (only pointer is updated, data is not copied).
 *
 * @note Frame data should remain valid until next update or destruction.
 * @note Atomic pointer swap ensures readers see consistent frame pointer.
 * @note Alternates between frame_a and frame_b on each update.
 *
 * @warning Frame data pointer must remain valid until next update.
 *
 * @ingroup video_frame
 */
void simple_frame_swap_update(simple_frame_swap_t *sfs, const void *data, size_t size);

/**
 * @brief Get current frame from simple frame swap
 * @param sfs Frame swap (must not be NULL)
 * @return Pointer to current frame, or NULL if no frame available
 *
 * Gets a pointer to the current frame. This is a lock-free read operation that
 * reads the atomic frame pointer. Returns NULL if no frame has been updated yet.
 *
 * @note Frame pointer is valid until next simple_frame_swap_update() call.
 * @note Lock-free operation (uses atomic pointer read).
 *
 * @ingroup video_frame
 */
const video_frame_t *simple_frame_swap_get(simple_frame_swap_t *sfs);

/** @} */

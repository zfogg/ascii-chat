/**
 * @file fps.h
 * @brief ⏱️ FPS tracking utility for monitoring frame throughput across all threads
 * @ingroup common
 * @addtogroup common
 * @{
 *
 * Provides a reusable FPS tracking mechanism for monitoring real-time
 * frame delivery rates across all client threads (data reception, webcam capture,
 * audio capture, keepalive). Tracks frame intervals, detects lag conditions,
 * and generates periodic FPS reports every 7-15 seconds.
 *
 * USAGE:
 * ------
 * ```c
 * fps_t tracker;
 * fps_init(&tracker, 60, "WEBCAM");  // 60 FPS expected
 *
 * // In your frame processing loop
 * struct timespec now;
 * clock_gettime(CLOCK_MONOTONIC, &now);
 * fps_frame(&tracker, &now, "frame received");
 * ```
 *
 * FEATURES:
 * ---------
 * - Automatic frame counting and interval measurement
 * - Lag detection (when frames arrive late)
 * - Periodic FPS report generation (configurable, default 10 seconds)
 * - Configurable expected FPS and reporting interval
 * - Thread-safe (caller must synchronize if needed)
 * - Integrated across all major threads (data reception, webcam, audio, keepalive)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 * @version 2.0 (Refactored with unified thread tracking)
 */

#pragma once

#include <time.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief FPS tracking state
 *
 * Maintains timing information for FPS calculation and lag detection.
 * Should be zero-initialized on creation.
 */
typedef struct {
  uint64_t frame_count;            // Frames counted since last report
  struct timespec last_fps_report; // Last time FPS was reported
  struct timespec last_frame_time; // Timestamp of last processed frame
  int expected_fps;                // Expected FPS (e.g., 60)
  uint64_t report_interval_us;     // Report interval in microseconds (default 10s)
  const char *tracker_name;        // Name for logging (e.g., "CLIENT", "WEBCAM", "AUDIO", "KEEPALIVE")
} fps_t;

/**
 * @brief Initialize FPS tracker
 *
 * Sets up the tracker with expected FPS and default reporting interval (10 seconds).
 * Should be called once before using the tracker.
 *
 * @param tracker Pointer to uninitialized fps_t (should be zero-initialized)
 * @param expected_fps Expected frame rate in FPS (e.g., 60, 30, 20)
 * @param name Display name for logging (e.g., "CLIENT", "WEBCAM", "AUDIO", "KEEPALIVE")
 */
void fps_init(fps_t *tracker, int expected_fps, const char *name);

/**
 * @brief Initialize FPS tracker with custom report interval
 *
 * Sets up the tracker with expected FPS and custom reporting interval.
 * Useful for different threads that may need different reporting frequencies.
 *
 * @param tracker Pointer to uninitialized fps_t (should be zero-initialized)
 * @param expected_fps Expected frame rate in FPS
 * @param name Display name for logging
 * @param report_interval_us Custom report interval in microseconds
 */
void fps_init_with_interval(fps_t *tracker, int expected_fps, const char *name, uint64_t report_interval_us);

/**
 * @brief Track a frame and detect lag conditions
 *
 * Call this function when a frame is processed. Automatically detects
 * when frames arrive late and generates periodic FPS reports every report_interval_us.
 *
 * @param tracker FPS tracker state
 * @param current_time Timestamp of this frame (CLOCK_MONOTONIC)
 * @param context Optional context string for lag logging (e.g., "ASCII frame received")
 */
void fps_frame(fps_t *tracker, const struct timespec *current_time, const char *context);

/** @} */

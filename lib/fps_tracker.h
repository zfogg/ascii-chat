/**
 * @file fps_tracker.h
 * @brief ⏱️ FPS tracking utility for monitoring frame throughput
 * @ingroup common
 * @addtogroup common
 * @{
 *
 * Provides a reusable FPS tracking mechanism for monitoring real-time
 * frame delivery rates. Tracks frame intervals, detects lag conditions,
 * and generates periodic FPS reports.
 *
 * USAGE:
 * ------
 * ```c
 * fps_tracker_t tracker;
 * fps_tracker_init(&tracker, 60, "CLIENT");  // 60 FPS expected
 *
 * // In your frame processing loop
 * struct timespec now;
 * clock_gettime(CLOCK_MONOTONIC, &now);
 * fps_tracker_frame(&tracker, &now, "frame_context");
 * ```
 *
 * FEATURES:
 * ---------
 * - Automatic frame counting and interval measurement
 * - Lag detection (when frames arrive late)
 * - Periodic FPS report generation (every 5 seconds by default)
 * - Configurable expected FPS and reporting interval
 * - Thread-safe (caller must synchronize if needed)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 * @version 1.0 (Initial refactoring from duplicated code)
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
  uint64_t report_interval_us;     // Report interval in microseconds (default 5s)
  const char *tracker_name;        // Name for logging (e.g., "CLIENT", "SERVER")
} fps_tracker_t;

/**
 * @brief Initialize FPS tracker
 *
 * Sets up the tracker with expected FPS and default reporting interval.
 * Should be called once before using the tracker.
 *
 * @param tracker Pointer to uninitialized fps_tracker_t (should be zero-initialized)
 * @param expected_fps Expected frame rate in FPS (e.g., 60)
 * @param name Display name for logging (e.g., "CLIENT", "SERVER")
 */
void fps_tracker_init(fps_tracker_t *tracker, int expected_fps, const char *name);

/**
 * @brief Track a frame and detect lag conditions
 *
 * Call this function when a frame is processed. Automatically detects
 * when frames arrive late and generates periodic FPS reports.
 *
 * @param tracker FPS tracker state
 * @param current_time Timestamp of this frame (CLOCK_MONOTONIC)
 * @param context Optional context string for lag logging (e.g., "ASCII frame received")
 */
void fps_tracker_frame(fps_tracker_t *tracker, const struct timespec *current_time, const char *context);

/** @} */

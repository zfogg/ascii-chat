/**
 * @file ui/fps_counter.h
 * @brief ⏱️ FPS counter for measuring output write throughput
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * Provides a rolling-window FPS counter that measures the actual throughput
 * of frame output operations (platform_write_all). Maintains a circular buffer
 * of timestamps to compute stable FPS readings.
 *
 * USAGE:
 * ------
 * ```c
 * fps_counter_t *fps = fps_counter_create();
 *
 * // After each frame write to terminal
 * fps_counter_tick(fps);
 *
 * // Get current FPS for display
 * float current_fps = fps_counter_get(fps);
 *
 * fps_counter_destroy(fps);
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Opaque FPS counter handle
 *
 * Maintains a rolling window of frame timestamps for FPS calculation.
 */
typedef struct fps_counter_s fps_counter_t;

/**
 * @brief Create a new FPS counter
 * @return Pointer to counter, or NULL on failure
 *
 * Creates and initializes a rolling-window FPS counter with a buffer
 * for 30 frame timestamps.
 *
 * @note Call fps_counter_destroy() to free resources when done.
 */
fps_counter_t *fps_counter_create(void);

/**
 * @brief Destroy FPS counter and free resources
 * @param counter Counter to destroy (can be NULL)
 */
void fps_counter_destroy(fps_counter_t *counter);

/**
 * @brief Record a frame timestamp
 * @param counter FPS counter (must not be NULL)
 *
 * Call this immediately after a frame write completes to record
 * the output throughput. Uses nanosecond-precision timing.
 */
void fps_counter_tick(fps_counter_t *counter);

/**
 * @brief Get the current FPS value
 * @param counter FPS counter (can be NULL)
 * @return Current FPS as a float, or 0.0 if counter is NULL or insufficient data
 *
 * Returns a rolling-window average FPS based on the last 30 frames.
 * Stable FPS reading after at least 2 frames have been recorded.
 */
float fps_counter_get(fps_counter_t *counter);

/** @} */

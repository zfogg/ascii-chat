/**
 * @file fps_tracker.h
 * @brief FPS tracker utility for managing frame rate monitoring across threads
 *
 * This module provides a simplified wrapper around the fps_t structure to
 * eliminate duplicate static initialization and tracking patterns that appear
 * in multiple client threads (audio, capture, protocol handlers, etc.).
 *
 * Instead of:
 * @code
 *   static fps_t fps_tracker = {0};
 *   static bool fps_tracker_initialized = false;
 *   if (!fps_tracker_initialized) {
 *       fps_init(&fps_tracker, fps_value, "LABEL");
 *       fps_tracker_initialized = true;
 *   }
 *   struct timespec current_time;
 *   (void)clock_gettime(CLOCK_MONOTONIC, &current_time);
 *   fps_frame(&fps_tracker, &current_time, "message");
 * @endcode
 *
 * Use:
 * @code
 *   static fps_tracker_t *tracker = fps_tracker_create(30, "LABEL");
 *   fps_tracker_record_frame(tracker);
 * @endcode
 *
 * @ingroup util
 */

#ifndef LIB_UTIL_FPS_TRACKER_H
#define LIB_UTIL_FPS_TRACKER_H

#include <time.h>
#include "lib/fps.h"

/**
 * @brief Opaque FPS tracker instance
 *
 * Encapsulates an fps_t structure along with initialization state.
 * Created and destroyed via fps_tracker_create()/fps_tracker_free().
 */
typedef struct fps_tracker_state fps_tracker_t;

/**
 * @brief Create and initialize an FPS tracker
 *
 * Allocates memory for a new FPS tracker and initializes it with the given
 * expected FPS and label. This eliminates the need for static variable
 * initialization patterns scattered throughout the codebase.
 *
 * @param expected_fps Expected frames per second (e.g., 30, 60, 144)
 * @param label Human-readable label for logging (e.g., "AUDIO_TX", "WEBCAM_RX")
 *
 * @return Pointer to tracker instance, or NULL on allocation failure
 *
 * @note The returned pointer must be freed with fps_tracker_free()
 * @note Thread-safe: each thread should have its own tracker instance
 *
 * @ingroup util
 */
fps_tracker_t *fps_tracker_create(int expected_fps, const char *label);

/**
 * @brief Record a frame with automatic timestamp
 *
 * Records the current frame with the current monotonic clock time.
 * Replaces the pattern of getting current_time and calling fps_frame().
 *
 * @param tracker FPS tracker instance created by fps_tracker_create()
 * @param message Optional debug message (can be NULL)
 *
 * @note This is the preferred method for tracking frame timing
 * @note Message is passed to fps_frame() for rate-limited logging
 *
 * @ingroup util
 */
void fps_tracker_record_frame(fps_tracker_t *tracker, const char *message);

/**
 * @brief Record a frame with explicit timestamp
 *
 * Records a frame with a specific timestamp. Useful when the frame time
 * comes from a source other than the current system clock.
 *
 * @param tracker FPS tracker instance
 * @param ts Pointer to timespec with frame timestamp
 * @param message Optional debug message (can be NULL)
 *
 * @ingroup util
 */
void fps_tracker_record_frame_at(fps_tracker_t *tracker, const struct timespec *ts, const char *message);

/**
 * @brief Get underlying fps_t structure for direct access if needed
 *
 * In rare cases where you need direct access to the underlying fps_t,
 * this provides it. Prefer fps_tracker_record_frame() for most cases.
 *
 * @param tracker FPS tracker instance
 * @return Pointer to underlying fps_t structure, or NULL if tracker is NULL
 *
 * @ingroup util
 */
fps_t *fps_tracker_get_fps(fps_tracker_t *tracker);

/**
 * @brief Free an FPS tracker instance
 *
 * Deallocates memory associated with the tracker. Must be called to avoid
 * memory leaks when tracker is no longer needed.
 *
 * @param tracker FPS tracker instance (safe to pass NULL)
 *
 * @note Safe to call with NULL pointer
 *
 * @ingroup util
 */
void fps_tracker_free(fps_tracker_t *tracker);

#endif // LIB_UTIL_FPS_TRACKER_H

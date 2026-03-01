/**
 * @file ui/fps_counter.c
 * @brief FPS counter implementation using rolling-window timestamp buffer
 * @ingroup session
 */

#include <ascii-chat/ui/fps_counter.h>
#include <ascii-chat/common.h>
#include <string.h>

/* ============================================================================
 * FPS Counter Implementation
 * ============================================================================ */

#define FPS_WINDOW_SIZE 30 ///< Number of frames to average over

/**
 * @brief FPS counter state
 *
 * Maintains a circular buffer of frame timestamps for rolling-window
 * FPS calculation.
 */
struct fps_counter_s {
  uint64_t frame_times[FPS_WINDOW_SIZE]; ///< Circular buffer of timestamps (ns)
  int head;                              ///< Current write position (0 to FPS_WINDOW_SIZE-1)
  int count;                             ///< Number of valid entries in buffer (0 to FPS_WINDOW_SIZE)
};

/* ============================================================================
 * Lifecycle Functions
 * ============================================================================ */

fps_counter_t *fps_counter_create(void) {
  fps_counter_t *counter = SAFE_CALLOC(1, sizeof(fps_counter_t), fps_counter_t *);
  if (!counter) {
    return NULL;
  }

  // Initialize circular buffer pointers
  counter->head = 0;
  counter->count = 0;
  memset(counter->frame_times, 0, sizeof(counter->frame_times));

  return counter;
}

void fps_counter_destroy(fps_counter_t *counter) {
  if (counter) {
    SAFE_FREE(counter);
  }
}

/* ============================================================================
 * FPS Measurement Functions
 * ============================================================================ */

void fps_counter_tick(fps_counter_t *counter) {
  if (!counter) {
    return;
  }

  // Record current time at head position
  counter->frame_times[counter->head] = time_get_ns();

  // Advance head pointer with wraparound
  counter->head = (counter->head + 1) % FPS_WINDOW_SIZE;

  // Track how many valid entries we have (up to FPS_WINDOW_SIZE)
  if (counter->count < FPS_WINDOW_SIZE) {
    counter->count++;
  }
}

float fps_counter_get(fps_counter_t *counter) {
  if (!counter || counter->count < 2) {
    return 0.0f;
  }

  // Find indices of oldest and newest timestamps in the buffer
  int oldest_idx = (counter->head - counter->count + FPS_WINDOW_SIZE) % FPS_WINDOW_SIZE;
  int newest_idx = (counter->head - 1 + FPS_WINDOW_SIZE) % FPS_WINDOW_SIZE;

  // Get the elapsed time between oldest and newest frames
  uint64_t oldest_time = counter->frame_times[oldest_idx];
  uint64_t newest_time = counter->frame_times[newest_idx];
  uint64_t elapsed_ns = newest_time - oldest_time;

  // Avoid division by zero
  if (elapsed_ns == 0) {
    return 0.0f;
  }

  // FPS = (number of frames) / elapsed_time
  // We have (count - 1) frames across the elapsed time
  // (count - 1) frames means count timestamps, so count-1 intervals
  return (float)(counter->count - 1) * 1e9f / (float)elapsed_ns;
}

/**
 * @file ui/fps_counter.c
 * @brief FPS counter implementation using rolling-window timestamp buffer
 * @ingroup session
 */

#include <ascii-chat/ui/fps_counter.h>
#include <ascii-chat/common.h>
#include <ascii-chat/ui/controller.h>
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
  bool visible;
  int last_fps;
  bool measuring;
  bool written;
  bool failed;
  uint64_t write_start;
  uint64_t frame_write_ns;
  uint64_t report_start;
  uint64_t report_writes;
  uint64_t report_frames;
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

  counter->last_fps = -1;
  counter->report_start = time_get_ns();

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
  fps_counter_tick_at(counter, time_get_ns());
}

void fps_counter_reset(fps_counter_t *counter) {
  if (counter) {
    counter->head = counter->count = 0;
    memset(counter->frame_times, 0, sizeof(counter->frame_times));
  }
}

void fps_counter_tick_at(fps_counter_t *counter, uint64_t now) {
  if (!counter) {
    return;
  }

  if (counter->count) {
    uint64_t previous = counter->frame_times[(counter->head + FPS_WINDOW_SIZE - 1) % FPS_WINDOW_SIZE];
    if (now <= previous || now - previous >= 2000000000ULL)
      fps_counter_reset(counter);
  }

  // Record current time at head position
  counter->frame_times[counter->head] = now;

  // Advance head pointer with wraparound
  counter->head = (counter->head + 1) % FPS_WINDOW_SIZE;

  // Track how many valid entries we have (up to FPS_WINDOW_SIZE)
  if (counter->count < FPS_WINDOW_SIZE) {
    counter->count++;
  }
}

float fps_counter_get(fps_counter_t *counter) {
  return fps_counter_get_at(counter, time_get_ns());
}

float fps_counter_get_at(fps_counter_t *counter, uint64_t now) {
  if (!counter || counter->count < 2) {
    return 0.0f;
  }

  // Find indices of oldest and newest timestamps in the buffer
  int oldest_idx = (counter->head - counter->count + FPS_WINDOW_SIZE) % FPS_WINDOW_SIZE;
  int newest_idx = (counter->head - 1 + FPS_WINDOW_SIZE) % FPS_WINDOW_SIZE;

  // Get the elapsed time between oldest and newest frames
  uint64_t oldest_time = counter->frame_times[oldest_idx];
  uint64_t newest_time = counter->frame_times[newest_idx];
  if (now < newest_time || now - newest_time >= 2000000000ULL)
    return 0.0f;
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


bool fps_counter_set_visible(fps_counter_t *counter, bool visible) {
  if (!counter)
    return false;
  bool changed = counter->visible != visible;
  counter->visible = visible;
  if (changed)
    counter->last_fps = -1;
  return changed;
}

void fps_counter_frame_begin(fps_counter_t *counter, bool measure) {
  if (!counter)
    return;
  counter->measuring = measure;
  counter->written = counter->failed = false;
  counter->frame_write_ns = 0;
}

void fps_counter_write_begin(fps_counter_t *counter) {
  if (counter && counter->measuring)
    counter->write_start = time_get_ns();
}

void fps_counter_write_end(fps_counter_t *counter, bool complete) {
  if (!counter || !counter->measuring)
    return;
  counter->frame_write_ns += time_get_ns() - counter->write_start;
  counter->failed |= !complete;
  counter->written |= complete;
}

void fps_counter_frame_end(fps_counter_t *counter, uint64_t now) {
  if (!counter)
    return;
  if (counter->measuring && counter->written && !counter->failed) {
    fps_counter_tick_at(counter, now);
    counter->report_writes += counter->frame_write_ns;
    counter->report_frames++;
  }
  counter->measuring = false;
}

void fps_counter_render(fps_counter_t *counter, int fd, int columns, bool redrawn) {
  if (!counter)
    return;
  uint64_t now = time_get_ns();
  float measured = fps_counter_get_at(counter, now);
  int value = measured >= 999.0f ? 999 : (int)(measured + 0.5f);
  if (counter->visible && columns >= 7 && (redrawn || value != counter->last_fps)) {
    // Restore the cursor so the right-margin overlay cannot wrap the next write.
    if (ui_controller_printf(fd, "\0337\033[1;%dH\033[0;7mFPS:%3d\033[0m\0338", columns - 6, value) == ASCIICHAT_OK)
      counter->last_fps = value;
  }
  if (now - counter->report_start >= 3 * NS_PER_SEC_INT) {
    if (counter->report_frames)
      log_debug("FPS_OUTPUT: frames=%llu elapsed_ms=%.3f write_ms=%.3f",
                (unsigned long long)counter->report_frames, (double)(now - counter->report_start) / NS_PER_MS_INT,
                (double)counter->report_writes / NS_PER_MS_INT);
    counter->report_start = now;
    counter->report_writes = counter->report_frames = 0;
  }
}

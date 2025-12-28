/**
 * @file fps_tracker.c
 * @brief ⏱️ FPS tracking utility implementation
 * @ingroup common
 */

#include "fps_tracker.h"
#include "logging.h"
#include "common.h"

/**
 * @brief Convert timespec to microseconds
 */
static uint64_t timespec_to_us(const struct timespec *ts) {
  return (uint64_t)ts->tv_sec * 1000000ULL + (uint64_t)ts->tv_nsec / 1000;
}

void fps_tracker_init(fps_tracker_t *tracker, int expected_fps, const char *name) {
  if (!tracker) {
    return;
  }

  tracker->frame_count = 0;
  tracker->expected_fps = expected_fps > 0 ? expected_fps : 60;
  tracker->tracker_name = name ? name : "FPS";
  tracker->report_interval_us = 5000000ULL;  // 5 seconds

  // Initialize timestamps to zero (will be set on first frame)
  tracker->last_fps_report.tv_sec = 0;
  tracker->last_fps_report.tv_nsec = 0;
  tracker->last_frame_time.tv_sec = 0;
  tracker->last_frame_time.tv_nsec = 0;

  log_debug("%s FPS TRACKER: Initialized with %d FPS target", tracker->tracker_name, tracker->expected_fps);
}

void fps_tracker_frame(fps_tracker_t *tracker, const struct timespec *current_time, const char *context) {
  if (!tracker || !current_time) {
    return;
  }

  // Initialize on first frame
  if (tracker->last_fps_report.tv_sec == 0) {
    tracker->last_fps_report = *current_time;
    tracker->last_frame_time = *current_time;
  }

  tracker->frame_count++;

  // Calculate time since last frame
  uint64_t current_us = timespec_to_us(current_time);
  uint64_t last_frame_us = timespec_to_us(&tracker->last_frame_time);
  uint64_t frame_interval_us = current_us - last_frame_us;
  tracker->last_frame_time = *current_time;

  // Expected frame interval in microseconds
  uint64_t expected_interval_us = 1000000ULL / (uint64_t)tracker->expected_fps;
  uint64_t lag_threshold_us = expected_interval_us + (expected_interval_us / 2);  // 50% over expected

  // Log error if frame arrived too late
  if (tracker->frame_count > 1 && frame_interval_us > lag_threshold_us) {
    const char *context_str = context ? context : "Frame";
    log_error("%s FPS LAG: %s received %.2lfms late (expected %.2lfms, got %.2lfms, actual fps: %.2lf)",
              tracker->tracker_name, context_str, (double)(frame_interval_us - expected_interval_us) / 1000.0,
              (double)expected_interval_us / 1000.0, (double)frame_interval_us / 1000.0,
              1000000.0 / (double)frame_interval_us);
  }

  // Report FPS every report_interval_us (default 5 seconds)
  uint64_t report_us = timespec_to_us(&tracker->last_fps_report);
  uint64_t elapsed_us = current_us - report_us;

  if (elapsed_us >= tracker->report_interval_us) {
    double elapsed_seconds = (double)elapsed_us / 1000000.0;
    double actual_fps = (double)tracker->frame_count / elapsed_seconds;

    char duration_str[32];
    format_duration_s(elapsed_seconds, duration_str, sizeof(duration_str));
    log_debug("%s FPS: %.1f fps (%llu frames in %s)", tracker->tracker_name, actual_fps, tracker->frame_count,
              duration_str);

    // Reset counters for next interval
    tracker->frame_count = 0;
    tracker->last_fps_report = *current_time;
  }
}

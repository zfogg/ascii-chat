/**
 * @file fps.c
 * @brief ⏱️ FPS tracking utility implementation
 * @ingroup common
 */

#include "fps.h"
#include "common.h"
#include "util/time.h"
#include "log/logging.h"

/**
 * @brief Convert timespec to microseconds
 */
static uint64_t timespec_to_us(const struct timespec *ts) {
  return (uint64_t)ts->tv_sec * 1000000ULL + (uint64_t)ts->tv_nsec / 1000;
}

/**
 * @brief Format and log FPS report in a single line
 */
static void log_fps_report(const char *tracker_name, double actual_fps, uint64_t frame_count, double elapsed_seconds) {
  log_info_every(1, "[%s] %.1f fps (%llu frames in %.1fs)", tracker_name, actual_fps, (unsigned long long)frame_count,
                 elapsed_seconds);
}

/**
 * @brief Format and log a single lag event
 */
static void log_lag_event(const char *tracker_name, const char *context, double late_ms, double expected_ms,
                          double actual_ms, double actual_fps) {
  log_error("[%s] LAG: %s late by %.1fms (expected %.1fms, got %.1fms, %.2f fps)", tracker_name, context, late_ms,
            expected_ms, actual_ms, actual_fps);
}

void fps_init(fps_t *tracker, int expected_fps, const char *name) {
  fps_init_with_interval(tracker, expected_fps, name, 10000000ULL); // 10 seconds default
}

void fps_init_with_interval(fps_t *tracker, int expected_fps, const char *name, uint64_t report_interval_us) {
  if (!tracker) {
    return;
  }

  tracker->frame_count = 0;
  tracker->expected_fps = expected_fps > 0 ? expected_fps : 60;
  tracker->tracker_name = name ? name : "FPS";
  tracker->report_interval_us = report_interval_us;

  // Initialize timestamps to zero (will be set on first frame)
  tracker->last_fps_report.tv_sec = 0;
  tracker->last_fps_report.tv_nsec = 0;
  tracker->last_frame_time.tv_sec = 0;
  tracker->last_frame_time.tv_nsec = 0;
}

void fps_frame(fps_t *tracker, const struct timespec *current_time, const char *context) {
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
  uint64_t lag_threshold_us = expected_interval_us + (expected_interval_us / 2); // 50% over expected

  // Log error if frame arrived too late
  if (tracker->frame_count > 1 && frame_interval_us > lag_threshold_us) {
    const char *context_str = context ? context : "Frame";
    double late_ms = (double)(frame_interval_us - expected_interval_us) / 1000.0;
    double expected_ms = (double)expected_interval_us / 1000.0;
    double actual_ms = (double)frame_interval_us / 1000.0;
    double actual_fps = 1000000.0 / (double)frame_interval_us;
    log_lag_event(tracker->tracker_name, context_str, late_ms, expected_ms, actual_ms, actual_fps);
  }

  // Report FPS every report_interval_us (default 10 seconds)
  uint64_t report_us = timespec_to_us(&tracker->last_fps_report);
  uint64_t elapsed_us = current_us - report_us;

  if (elapsed_us >= tracker->report_interval_us) {
    double elapsed_seconds = (double)elapsed_us / 1000000.0;
    double actual_fps = (double)tracker->frame_count / elapsed_seconds;
    log_fps_report(tracker->tracker_name, actual_fps, tracker->frame_count, elapsed_seconds);

    // Reset counters for next interval
    tracker->frame_count = 0;
    tracker->last_fps_report = *current_time;
  }
}

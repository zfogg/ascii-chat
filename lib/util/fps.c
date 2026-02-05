/**
 * @file fps.c
 * @brief ⏱️ FPS tracking utility implementation (nanosecond-precision)
 * @ingroup common
 */

#include <ascii-chat/util/fps.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/logging.h>

/**
 * @brief Format and log FPS report in a single line
 */
static void log_fps_report(const char *tracker_name, double actual_fps, uint64_t frame_count, double elapsed_seconds) {
  log_info_every(1, "[%s] %.1f fps (%llu frames in %.1fs)", tracker_name, actual_fps, (unsigned long long)frame_count,
                 elapsed_seconds);
}

/**
 * @brief Format and log a single lag event
 *
 * Logs lag events with millisecond precision in the display output
 */
static void log_lag_event(const char *tracker_name, const char *context, double late_ms, double expected_ms,
                          double actual_ms, double actual_fps) {
  // Rate limit to once per second to avoid log spam
  log_error_every(1000000, "[%s] LAG: %s late by %.1fms (expected %.1fms, got %.1fms, %.2f fps)", tracker_name, context,
                  late_ms, expected_ms, actual_ms, actual_fps);
}

void fps_init(fps_t *tracker, int expected_fps, const char *name) {
  // Default report interval: 1 second
  fps_init_with_interval(tracker, expected_fps, name, 1 * NS_PER_SEC_INT);
}

void fps_init_with_interval(fps_t *tracker, int expected_fps, const char *name, uint64_t report_interval_ns) {
  if (!tracker) {
    return;
  }

  tracker->frame_count = 0;
  tracker->expected_fps = expected_fps > 0 ? expected_fps : 60;
  tracker->tracker_name = name ? name : "FPS";
  tracker->report_interval_ns = report_interval_ns;

  // Initialize timestamps to zero (will be set on first frame)
  tracker->last_fps_report_ns = 0;
  tracker->last_frame_time_ns = 0;
}

void fps_frame_ns(fps_t *tracker, uint64_t current_time_ns, const char *context) {
  if (!tracker) {
    return;
  }

  // Initialize on first frame
  if (tracker->last_fps_report_ns == 0) {
    tracker->last_fps_report_ns = current_time_ns;
    tracker->last_frame_time_ns = current_time_ns;
  }

  tracker->frame_count++;

  // Calculate time since last frame in nanoseconds
  uint64_t frame_interval_ns = time_elapsed_ns(tracker->last_frame_time_ns, current_time_ns);
  tracker->last_frame_time_ns = current_time_ns;

  // Expected frame interval in nanoseconds
  // For 60 FPS: 1 second / 60 = 16,666,666 ns per frame
  uint64_t expected_interval_ns = NS_PER_SEC_INT / (uint64_t)tracker->expected_fps;
  uint64_t lag_threshold_ns = expected_interval_ns + (expected_interval_ns / 2); // 50% over expected

  // Log error if frame arrived too late (only after first frame)
  if (tracker->frame_count > 1 && frame_interval_ns > lag_threshold_ns) {
    const char *context_str = context ? context : "Frame";

    // Convert to milliseconds for display (but calculations stay in nanoseconds)
    double late_ms = time_ns_to_us(frame_interval_ns - expected_interval_ns) / 1000.0;
    double expected_ms = time_ns_to_us(expected_interval_ns) / 1000.0;
    double actual_ms = time_ns_to_us(frame_interval_ns) / 1000.0;
    double actual_fps = 1e9 / (double)frame_interval_ns;

    log_lag_event(tracker->tracker_name, context_str, late_ms, expected_ms, actual_ms, actual_fps);
  }

  // Report FPS every report_interval_ns
  uint64_t elapsed_ns = time_elapsed_ns(tracker->last_fps_report_ns, current_time_ns);

  if (elapsed_ns >= tracker->report_interval_ns) {
    double elapsed_seconds = time_ns_to_s(elapsed_ns);
    double actual_fps = (double)tracker->frame_count / elapsed_seconds;
    log_fps_report(tracker->tracker_name, actual_fps, tracker->frame_count, elapsed_seconds);

    // Reset counters for next interval
    tracker->frame_count = 0;
    tracker->last_fps_report_ns = current_time_ns;
  }
}

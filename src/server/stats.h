#pragma once

/**
 * @file server/stats.h
 * @ingroup server_stats
 * @brief Server performance statistics tracking
 *
 * This header provides server-side performance statistics tracking including
 * frame capture/send rates, bandwidth metrics, and performance counters.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdint.h>
#include "platform/mutex.h"

/**
 * @brief Server performance statistics structure
 *
 * Tracks comprehensive performance metrics for the ASCII-Chat server including
 * frame processing rates, bandwidth usage, and performance counters.
 *
 * STATISTICS TRACKED:
 * ===================
 * - frames_captured: Total frames captured from webcam/sources
 * - frames_sent: Total frames sent to all clients
 * - frames_dropped: Total frames dropped (backpressure, timeout, etc.)
 * - bytes_sent: Total bytes transmitted (network bandwidth)
 * - avg_capture_fps: Average frame capture rate (frames per second)
 * - avg_send_fps: Average frame send rate (frames per second)
 *
 * PERFORMANCE METRICS:
 * ====================
 * These statistics enable:
 * - Performance monitoring and analysis
 * - Bottleneck identification
 * - Capacity planning
 * - Quality of service metrics
 *
 * @note Statistics are updated atomically for thread-safe access.
 * @note Rates (avg_capture_fps, avg_send_fps) are calculated over time windows.
 * @note Frame drop rate = frames_dropped / frames_captured (when frames_captured > 0)
 *
 * @ingroup server_stats
 */
typedef struct {
  uint64_t frames_captured;
  uint64_t frames_sent;
  uint64_t frames_dropped;
  uint64_t bytes_sent;
  double avg_capture_fps;
  double avg_send_fps;
} server_stats_t;

// Statistics thread function
void *stats_logger_thread(void *arg);

// Global statistics
extern server_stats_t g_stats;
extern mutex_t g_stats_mutex;

// Statistics functions
int stats_init(void);
void stats_cleanup(void);
void update_server_stats(void);
void log_server_stats(void);

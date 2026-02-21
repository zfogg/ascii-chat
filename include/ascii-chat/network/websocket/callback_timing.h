/**
 * @file network/websocket/callback_timing.h
 * @brief WebSocket callback timing instrumentation
 * @ingroup network
 *
 * Tracks timing and frequency of libwebsockets callbacks to diagnose
 * performance issues (Issue #305: WebSocket FPS bug).
 *
 * @author Claude (Anthropic)
 * @date February 2026
 */

#ifndef WEBSOCKET_CALLBACK_TIMING_H
#define WEBSOCKET_CALLBACK_TIMING_H

#include <stdint.h>
#include <time.h>
#include <stdatomic.h>

/**
 * @brief Per-callback timing statistics
 *
 * Tracks invocation counts, timestamps, and durations for a single callback type.
 */
typedef struct {
  _Atomic uint64_t count;           // Total invocations
  _Atomic uint64_t last_ns;         // Last callback timestamp (nanoseconds)
  _Atomic uint64_t min_interval_ns; // Minimum interval between callbacks
  _Atomic uint64_t max_interval_ns; // Maximum interval between callbacks
  _Atomic uint64_t total_duration_ns; // Cumulative callback duration
} websocket_callback_stats_t;

/**
 * @brief WebSocket callback timing data for protocol lifecycle
 *
 * Aggregates statistics for PROTOCOL_INIT and PROTOCOL_DESTROY callbacks.
 */
typedef struct {
  websocket_callback_stats_t protocol_init;
  websocket_callback_stats_t protocol_destroy;
  websocket_callback_stats_t server_writeable;
  websocket_callback_stats_t receive;
} websocket_callback_timing_t;

/**
 * @brief Global timing tracker
 *
 * Shared across all WebSocket connections to profile callback efficiency.
 */
extern websocket_callback_timing_t g_ws_callback_timing;

/**
 * @brief Record callback start time
 *
 * Returns nanosecond timestamp for later duration calculation.
 *
 * @return Current time in nanoseconds
 */
static inline uint64_t websocket_callback_timing_start(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Record callback invocation
 *
 * Updates statistics for the given callback type with timing information.
 *
 * @param stats Pointer to callback statistics structure
 * @param start_ns Start timestamp from websocket_callback_timing_start()
 * @param end_ns End timestamp (typically current time)
 */
void websocket_callback_timing_record(websocket_callback_stats_t *stats, uint64_t start_ns, uint64_t end_ns);

/**
 * @brief Log current callback statistics
 *
 * Prints aggregate statistics for all tracked callbacks to help diagnose
 * callback frequency and performance issues.
 */
void websocket_callback_timing_log_stats(void);

/**
 * @brief Reset timing statistics
 *
 * Clears all counters and timing information. Useful for baseline measurements.
 */
void websocket_callback_timing_reset(void);

#endif // WEBSOCKET_CALLBACK_TIMING_H

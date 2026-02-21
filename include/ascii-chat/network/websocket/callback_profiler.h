/**
 * @file network/websocket/callback_profiler.h
 * @brief ⏱️ Profiling instrumentation for libwebsockets callbacks
 *
 * Tracks callback frequency, execution time, and data throughput for
 * performance analysis and optimization.
 *
 * Features:
 * - Per-callback statistics (execution time, frequency, data processed)
 * - Thread-safe counters with minimal overhead
 * - Min/max/average timing aggregation
 * - JSON reporting for analysis
 *
 * @author Claude (asciichat/polecats/furiosa)
 * @date February 2026
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Callback profiling statistics for a single LWS callback reason
 */
typedef struct {
  uint64_t invocation_count;      ///< Number of times callback was invoked
  uint64_t total_time_ns;         ///< Total accumulated execution time in nanoseconds
  uint64_t min_time_ns;           ///< Minimum single callback execution time
  uint64_t max_time_ns;           ///< Maximum single callback execution time
  uint64_t bytes_processed;       ///< Total data processed by this callback
  uint64_t last_invocation_ns;    ///< Timestamp of last invocation (wall clock)
} lws_callback_stats_t;

/**
 * @brief Initialize the callback profiler
 *
 * Must be called once at application startup before any callbacks are profiled.
 * Thread-safe operation is ensured via internal mutex.
 *
 * @return true on success, false on failure
 */
bool lws_profiler_init(void);

/**
 * @brief Cleanup the callback profiler
 *
 * Frees all profiling structures and destroys mutex.
 * Called at application exit or when stopping profiling.
 */
void lws_profiler_destroy(void);

/**
 * @brief Record a callback invocation with timing
 *
 * Call this when entering a callback to start timing.
 * Returns an opaque handle used to stop timing.
 *
 * @param callback_reason LWS callback reason enum (as integer)
 * @param client_id Optional client identifier for per-client analysis
 * @return Opaque timing handle (non-zero if profiler is initialized)
 */
uint64_t lws_profiler_start(int callback_reason, uint32_t client_id);

/**
 * @brief Stop timing a callback invocation
 *
 * Call this when exiting a callback to record execution time.
 * Updates min/max/average statistics and invocation count.
 *
 * @param handle Timing handle returned by lws_profiler_start()
 * @param bytes_processed Bytes processed by this callback (0 if not applicable)
 */
void lws_profiler_stop(uint64_t handle, size_t bytes_processed);

/**
 * @brief Get statistics for a specific callback
 *
 * Retrieves accumulated statistics for analysis and logging.
 *
 * @param callback_reason LWS callback reason enum (as integer)
 * @param stats Output structure (caller-allocated)
 * @return true if statistics exist, false if callback never invoked
 */
bool lws_profiler_get_stats(int callback_reason, lws_callback_stats_t *stats);

/**
 * @brief Reset all profiling statistics
 *
 * Clears counters and timing data. Useful for collecting data over
 * specific time windows.
 */
void lws_profiler_reset(void);

/**
 * @brief Print profiling report to logs
 *
 * Generates a human-readable summary of callback statistics including:
 * - Invocation frequency per callback type
 * - Average/min/max execution times
 * - Total data processed
 * - Efficiency metrics
 */
void lws_profiler_dump_report(void);

/**
 * @brief Export statistics as JSON
 *
 * Returns a JSON string with complete profiling data.
 * Caller must free the returned string.
 *
 * @return Allocated JSON string, or NULL on failure
 */
char *lws_profiler_export_json(void);

/**
 * @brief Check if profiler is initialized
 *
 * @return true if profiler is active, false otherwise
 */
bool lws_profiler_is_initialized(void);

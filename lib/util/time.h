#pragma once

/**
 * @file util/time.h
 * @brief ⏱️ High-precision timing utilities using sokol_time.h and uthash
 * @ingroup module_utilities
 * @addtogroup module_utilities
 * @{
 *
 * This module provides a simple timing API for performance measurement:
 * - START_TIMER("name", ...) - Begin timing with formatted message
 * - STOP_TIMER("name") - End timing, log result, return elapsed time
 *
 * Features:
 * - Cross-platform high-resolution timing (sokol_time.h)
 * - Automatic hashtable management for named timers
 * - Thread-safe operation with mutex protection
 * - Formatted timer names with printf-style arguments
 * - Zero overhead when timing is disabled
 *
 * Example usage:
 * ```c
 * START_TIMER("process_frame_%d", frame_num);
 * // ... do work ...
 * double elapsed_ns = STOP_TIMER("process_frame_%d", frame_num);
 * // Use format_duration_ns() for human-readable output
 * char duration_str[32];
 * format_duration_ns(elapsed_ns, duration_str, sizeof(duration_str));
 * log_info("Frame took %s", duration_str);
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// ============================================================================
// sokol_time.h Integration
// ============================================================================

// Include sokol_time.h declarations (WITHOUT implementation)
// SOKOL_IMPL is defined only in time.c to avoid duplicate symbols
#include <ascii-chat-deps/sokol/sokol_time.h>

// Include uthash wrapper for UBSan-safe hash functions
// Headers can include this even before common.h is fully processed
#include "uthash/uthash.h"

// ============================================================================
// Core Monotonic Timing API (Nanosecond Precision)
// ============================================================================

/**
 * @brief Get current monotonic time in nanoseconds
 *
 * Returns high-precision monotonic time that never goes backwards.
 * Suitable for measuring elapsed time, FPS tracking, and performance metrics.
 *
 * @return Current time in nanoseconds (monotonic, never decreases)
 * @ingroup module_utilities
 *
 * Usage:
 * ```c
 * uint64_t start = time_get_ns();
 * // ... do work ...
 * uint64_t elapsed_ns = time_elapsed_ns(start, time_get_ns());
 * ```
 */
uint64_t time_get_ns(void);

/**
 * @brief Get current wall-clock (real) time in nanoseconds
 *
 * Returns high-precision wall-clock time that can jump forwards or backwards
 * when system time is adjusted. Use this for timestamps, database records,
 * and user-facing time displays.
 *
 * @return Current wall-clock time in nanoseconds (CLOCK_REALTIME)
 * @ingroup module_utilities
 *
 * Note: This time can jump backward if system clock is adjusted. For measuring
 * elapsed time, use time_get_ns() instead.
 */
uint64_t time_get_realtime_ns(void);

/**
 * @brief Sleep for specified nanoseconds
 *
 * Sleeps the current thread for at least the specified number of nanoseconds.
 * On some systems, actual sleep time may be slightly longer due to scheduler
 * granularity.
 *
 * @param ns Number of nanoseconds to sleep
 * @ingroup module_utilities
 */
void time_sleep_ns(uint64_t ns);

/**
 * @brief Calculate elapsed time with wraparound safety
 *
 * Computes the difference between two time values, handling potential
 * wraparound of uint64_t (which won't happen in practice - uint64_t
 * wraps after ~584 years at nanosecond resolution, but this is defensive).
 *
 * @param start_ns Start time in nanoseconds
 * @param end_ns End time in nanoseconds
 * @return Elapsed time in nanoseconds (end - start)
 * @ingroup module_utilities
 *
 * Usage:
 * ```c
 * uint64_t start = time_get_ns();
 * // ... do work ...
 * uint64_t elapsed = time_elapsed_ns(start, time_get_ns());
 * ```
 */
uint64_t time_elapsed_ns(uint64_t start_ns, uint64_t end_ns);

// ============================================================================
// Time Unit Constants (moved before inline functions that use them)
// ============================================================================

// Floating-point versions (for format_duration functions)
#define NS_PER_US 1000.0
#define NS_PER_MS 1000000.0
#define NS_PER_SEC 1000000000.0
#define NS_PER_MIN (NS_PER_SEC * 60.0)
#define NS_PER_HOUR (NS_PER_MIN * 60.0)
#define NS_PER_DAY (NS_PER_HOUR * 24.0)
#define NS_PER_YEAR (NS_PER_DAY * 365.25) // Account for leap years

// Integer versions (for comparing uint64_t nanosecond values)
#define NS_PER_US_INT 1000ULL
#define NS_PER_MS_INT 1000000ULL
#define NS_PER_SEC_INT 1000000000ULL

// ============================================================================
// Inline Time Conversion Helpers
// ============================================================================

/**
 * @brief Convert nanoseconds to microseconds (inline)
 * @param ns Time in nanoseconds
 * @return Time in microseconds
 * @ingroup module_utilities
 */
static inline uint64_t time_ns_to_us(uint64_t ns) {
  return ns / NS_PER_US_INT;
}

/**
 * @brief Convert nanoseconds to milliseconds (inline)
 * @param ns Time in nanoseconds
 * @return Time in milliseconds
 * @ingroup module_utilities
 */
static inline uint64_t time_ns_to_ms(uint64_t ns) {
  return ns / NS_PER_MS_INT;
}

/**
 * @brief Convert nanoseconds to seconds (inline, returns double)
 * @param ns Time in nanoseconds
 * @return Time in seconds
 * @ingroup module_utilities
 */
static inline double time_ns_to_s(uint64_t ns) {
  return (double)ns / 1e9;
}

/**
 * @brief Convert microseconds to nanoseconds (inline)
 * @param us Time in microseconds
 * @return Time in nanoseconds
 * @ingroup module_utilities
 */
static inline uint64_t time_us_to_ns(uint64_t us) {
  return us * NS_PER_US_INT;
}

/**
 * @brief Convert milliseconds to nanoseconds (inline)
 * @param ms Time in milliseconds
 * @return Time in nanoseconds
 * @ingroup module_utilities
 */
static inline uint64_t time_ms_to_ns(uint64_t ms) {
  return ms * NS_PER_MS_INT;
}

/**
 * @brief Convert seconds to nanoseconds (inline)
 * @param s Time in seconds (double)
 * @return Time in nanoseconds
 * @ingroup module_utilities
 */
static inline uint64_t time_s_to_ns(double s) {
  return (uint64_t)(s * 1e9);
}

/**
 * @brief Convert struct timespec to nanoseconds (inline)
 * @param ts Pointer to timespec structure
 * @return Time in nanoseconds
 * @ingroup module_utilities
 *
 * Useful for converting CLOCK_MONOTONIC or CLOCK_REALTIME readings to nanoseconds.
 */
static inline uint64_t time_timespec_to_ns(const struct timespec *ts) {
  if (!ts) return 0;
  return (uint64_t)ts->tv_sec * NS_PER_SEC_INT + (uint64_t)ts->tv_nsec;
}

/**
 * @brief Convert nanoseconds to struct timespec (inline)
 * @param ns Time in nanoseconds
 * @param ts Pointer to timespec structure (output)
 * @ingroup module_utilities
 *
 * Useful for nanosleep() or other system calls that require struct timespec.
 */
static inline void time_ns_to_timespec(uint64_t ns, struct timespec *ts) {
  if (!ts) return;
  ts->tv_sec = (time_t)(ns / NS_PER_SEC_INT);
  ts->tv_nsec = (long)(ns % NS_PER_SEC_INT);
}

// ============================================================================
// Timer Record Structure
// ============================================================================

/**
 * @brief Individual timer record for a named timing operation
 * @ingroup module_utilities
 */
typedef struct timer_record {
  char *name;           ///< Timer name (heap-allocated, unique key)
  uint64_t start_ticks; ///< Start time in sokol ticks
  UT_hash_handle hh;    ///< uthash handle (makes this hashable by name)
} timer_record_t;

// ============================================================================
// Timer System API
// ============================================================================

/**
 * @brief Initialize the timing system
 *
 * Must be called before using any timer functions.
 * Automatically calls stm_setup() for sokol_time initialization.
 *
 * @return true on success, false on failure
 * @ingroup module_utilities
 */
bool timer_system_init(void);

/**
 * @brief Cleanup the timing system
 *
 * Frees all timer records and destroys mutex.
 * Should be called at program exit.
 *
 * @ingroup module_utilities
 */
void timer_system_cleanup(void);

/**
 * @brief Start a named timer
 *
 * Creates a new timer record and stores the start time.
 * If a timer with the same name exists, it will be overwritten.
 *
 * @param name Timer name (must be unique, will be copied)
 * @return true on success, false on failure
 * @ingroup module_utilities
 */
bool timer_start(const char *name);

/**
 * @brief Stop a named timer and return elapsed time
 *
 * Looks up the timer by name, calculates elapsed time, logs it,
 * and removes the timer from the hashtable.
 *
 * @param name Timer name to stop
 * @return Elapsed time in nanoseconds, or -1.0 if timer not found
 * @ingroup module_utilities
 */
double timer_stop(const char *name);

/**
 * @brief Check if timing system is initialized
 *
 * @return true if initialized, false otherwise
 * @ingroup module_utilities
 */
bool timer_is_initialized(void);

// ============================================================================
// Convenience Macros
// ============================================================================

/**
 * @brief Start a timer with formatted name
 *
 * The timer name is created by formatting the name_fmt string with the provided arguments.
 * This complete formatted string becomes the unique key in the hashtable.
 *
 * IMPORTANT: Timer names must be unique. If a timer with the same formatted name already
 * exists, timer_start() will return false and log an error.
 *
 * Usage:
 *   START_TIMER("lock_%p", lock_ptr);           // Unique per lock address
 *   START_TIMER("process_frame_%d", frame_id);  // Unique per frame
 *   START_TIMER("client_%u_decode", client_id); // Unique per client
 *
 * @param name_fmt Printf-style format string for timer name
 * @param ... Format arguments to create unique key
 * @ingroup module_utilities
 */
#define START_TIMER(name_fmt, ...)                                                                                     \
  do {                                                                                                                 \
    if (timer_is_initialized()) {                                                                                      \
      char _timer_name_buf[256];                                                                                       \
      snprintf(_timer_name_buf, sizeof(_timer_name_buf), name_fmt, ##__VA_ARGS__);                                     \
      (void)timer_start(_timer_name_buf);                                                                              \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Stop a timer with formatted name and return elapsed time
 *
 * The timer name must match exactly (including all format arguments) with the name
 * used in START_TIMER(). The formatted string is used as the hashtable key lookup.
 *
 * Usage:
 *   double ns = STOP_TIMER("lock_%p", lock_ptr);
 *   double ns = STOP_TIMER("process_frame_%d", frame_id);
 *
 * @param name_fmt Printf-style format string for timer name (must match START_TIMER)
 * @param ... Format arguments (must match START_TIMER to create same key)
 * @return Elapsed time in nanoseconds, or -1.0 if timer not found
 * @ingroup module_utilities
 */
#define STOP_TIMER(name_fmt, ...)                                                                                      \
  ({                                                                                                                   \
    double _elapsed = -1.0;                                                                                            \
    if (timer_is_initialized()) {                                                                                      \
      char _timer_name_buf[256];                                                                                       \
      snprintf(_timer_name_buf, sizeof(_timer_name_buf), name_fmt, ##__VA_ARGS__);                                     \
      _elapsed = timer_stop(_timer_name_buf);                                                                          \
    }                                                                                                                  \
    _elapsed;                                                                                                          \
  })

// ============================================================================
// Time Formatting API
// ============================================================================

/**
 * @brief Format milliseconds as human-readable duration string
 * @param milliseconds Duration in milliseconds
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Formats a duration in milliseconds into a human-readable string. The function
 * automatically selects the most appropriate units and precision based on the
 * duration magnitude.
 *
 * Format rules:
 * - For sub-millisecond durations: shows ns or µs
 * - For sub-second durations: shows ms
 * - For sub-minute durations: shows seconds with decimal
 * - For sub-hour durations: shows minutes and seconds (e.g., "1m30s")
 * - For sub-day durations: shows hours, minutes, and seconds (e.g., "5h30m0s")
 * - For sub-year durations: shows days, hours, minutes, and seconds (e.g., "1d4h48m0s")
 * - For year+ durations: shows years with decimal
 *
 * @note Buffer should be at least 32 bytes for all possible outputs
 * @note Thread-safe (no global state)
 *
 * @ingroup module_utilities
 */
int format_duration_ms(double milliseconds, char *buffer, size_t buffer_size);

/**
 * @brief Format nanoseconds as human-readable duration string
 * @param nanoseconds Duration in nanoseconds
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Similar to format_duration_ms() but accepts nanosecond precision input.
 * Useful for high-precision timing measurements.
 *
 * @note Buffer should be at least 32 bytes for all possible outputs
 * @note Thread-safe (no global state)
 *
 * @ingroup module_utilities
 */
int format_duration_ns(double nanoseconds, char *buffer, size_t buffer_size);

/**
 * @brief Format seconds as human-readable duration string
 * @param seconds Duration in seconds
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Similar to format_duration_ms() but accepts seconds as input.
 * Useful for timing measurements already in seconds.
 *
 * @note Buffer should be at least 32 bytes for all possible outputs
 * @note Thread-safe (no global state)
 *
 * @ingroup module_utilities
 */
int format_duration_s(double seconds, char *buffer, size_t buffer_size);

/**
 * @brief Stop a timer and log the result with a custom message
 *
 * Combines STOP_TIMER() with logging. The timer is stopped, elapsed time is retrieved,
 * and a log message is generated with the elapsed time appended in human-readable format.
 *
 * Usage:
 *   STOP_TIMER_AND_LOG("client_handshake", log_info, "Crypto handshake completed successfully");
 *   STOP_TIMER_AND_LOG("process_frame_%d", log_debug, "Frame %d processed", frame_id, frame_id);
 *
 * The macro will append " in X.XXms" (or appropriate unit) to your message automatically.
 *
 * @param timer_name Timer name (must match START_TIMER call)
 * @param log_func Logging function to use (log_info, log_debug, etc.)
 * @param msg_fmt Printf-style format string for the message
 * @param ... Format arguments for both timer name and message
 * @ingroup module_utilities
 */
#define STOP_TIMER_AND_LOG(timer_name, log_func, msg_fmt, ...)                                                         \
  do {                                                                                                                 \
    double _elapsed_ns = STOP_TIMER(timer_name, ##__VA_ARGS__);                                                        \
    if (_elapsed_ns >= 0.0) {                                                                                          \
      char _duration_str[32];                                                                                          \
      format_duration_ns(_elapsed_ns, _duration_str, sizeof(_duration_str));                                           \
      log_func(msg_fmt " in %s", ##__VA_ARGS__, _duration_str);                                                        \
    }                                                                                                                  \
  } while (0)

// ============================================================================
// Adaptive Sleep System
// ============================================================================

/**
 * @brief Configuration for adaptive sleep behavior
 *
 * Defines how a thread should adjust its sleep time based on workload.
 * Threads can speed up (sleep less) when queues build up and slow down
 * (sleep more) when queues are empty.
 *
 * @ingroup module_utilities
 */
typedef struct {
  uint64_t baseline_sleep_ns;  ///< Normal sleep time in nanoseconds (when queue is at target)
  double min_speed_multiplier; ///< Minimum speed (max sleep) - usually 1.0 (baseline speed)
  double max_speed_multiplier; ///< Maximum speed (min sleep) - e.g., 4.0 = process 4x faster
  double speedup_rate;         ///< Ramp-up rate when queue builds (0.0-1.0, higher = faster ramp)
  double slowdown_rate;        ///< Ramp-down rate when queue empties (0.0-1.0, higher = faster ramp)
} adaptive_sleep_config_t;

/**
 * @brief Runtime state for adaptive sleep
 *
 * Tracks current speed multiplier and last calculated sleep time.
 * Should be initialized once and updated each iteration.
 *
 * @ingroup module_utilities
 */
typedef struct {
  adaptive_sleep_config_t config;  ///< Configuration (copied, not referenced)
  double current_speed_multiplier; ///< Current speed state (1.0 = baseline, >1.0 = faster)
  uint64_t last_sleep_ns;          ///< Last calculated sleep time (for debugging)
} adaptive_sleep_state_t;

/**
 * @brief Initialize adaptive sleep state with configuration
 *
 * Sets up an adaptive sleep state structure with the provided configuration.
 * The configuration is copied internally, so the caller doesn't need to keep
 * the config structure alive.
 *
 * @param state Pointer to adaptive sleep state to initialize
 * @param config Pointer to configuration (will be copied)
 * @ingroup module_utilities
 */
void adaptive_sleep_init(adaptive_sleep_state_t *state, const adaptive_sleep_config_t *config);

/**
 * @brief Calculate adaptive sleep time based on queue depth
 *
 * Evaluates current queue depth against target and adjusts the speed multiplier:
 * - If queue_depth > target_depth: speed up (reduce sleep, drain queue faster)
 * - If queue_depth < target_depth: slow down (increase sleep back to baseline)
 * - Speed multiplier changes gradually based on speedup_rate/slowdown_rate
 *
 * The actual sleep time is calculated as: baseline_sleep_ns / current_speed_multiplier
 *
 * Example usage:
 * ```c
 * adaptive_sleep_state_t sleep_state;
 * adaptive_sleep_config_t config = {
 *   .baseline_sleep_ns = 16666667,  // ~60 FPS (16.67ms)
 *   .min_speed_multiplier = 1.0,    // Never slower than baseline
 *   .max_speed_multiplier = 4.0,    // Can process up to 4x faster (240 FPS)
 *   .speedup_rate = 0.1,            // Ramp up 10% per frame
 *   .slowdown_rate = 0.05           // Ramp down 5% per frame
 * };
 * adaptive_sleep_init(&sleep_state, &config);
 *
 * while (running) {
 *   // Process one frame/packet/unit of work
 *   process_data();
 *
 *   // Sleep adaptively based on queue depth
 *   size_t queue_depth = get_queue_size();
 *   uint64_t sleep_ns = adaptive_sleep_calculate(&sleep_state, queue_depth, 10);
 *   platform_sleep_usec(sleep_ns / 1000);
 * }
 * ```
 *
 * @param state Pointer to adaptive sleep state (will be updated)
 * @param queue_depth Current number of items in queue/buffer
 * @param target_depth Desired queue depth (0 = empty queue target)
 * @return Sleep time in nanoseconds
 * @ingroup module_utilities
 */
uint64_t adaptive_sleep_calculate(adaptive_sleep_state_t *state, size_t queue_depth, size_t target_depth);

/**
 * @brief Calculate sleep time and immediately sleep for that duration
 *
 * Convenience wrapper that combines adaptive_sleep_calculate() with platform_sleep_usec().
 * Useful for simple loops that don't need to inspect the calculated sleep time.
 *
 * @param state Pointer to adaptive sleep state (will be updated)
 * @param queue_depth Current number of items in queue/buffer
 * @param target_depth Desired queue depth (0 = empty queue target)
 * @ingroup module_utilities
 */
void adaptive_sleep_do(adaptive_sleep_state_t *state, size_t queue_depth, size_t target_depth);

/** @} */
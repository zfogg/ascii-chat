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
 * // Use time_pretty() for human-readable formatted output
 * char duration_str[32];
 * time_pretty((uint64_t)elapsed_ns, -1, duration_str, sizeof(duration_str));
 * log_info("Frame took %s", duration_str);
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
// C11 stdatomic.h conflicts with MSVC's C++ <atomic> header on Windows.
// Include the appropriate header based on the compilation mode.
#if defined(__cplusplus) && defined(_WIN32)
#include <atomic>
using std::atomic_compare_exchange_weak_explicit;
using std::atomic_load_explicit;
using std::memory_order_relaxed;
// C11 _Atomic(T) syntax doesn't work in C++ - use std::atomic<T> instead
#define TIME_ATOMIC_UINT64 std::atomic<uint64_t>
#define TIME_ATOMIC_UINT64_INIT(val) {val}
#else
#include <stdatomic.h>
// C11 _Atomic type qualifier syntax
#define TIME_ATOMIC_UINT64 _Atomic uint64_t
#define TIME_ATOMIC_UINT64_INIT(val) val
#endif

// ============================================================================
// sokol_time.h Integration
// ============================================================================

// Include sokol_time.h declarations (WITHOUT implementation)
// SOKOL_TIME_IMPL is defined only in time.c to avoid duplicate symbols
#include <ascii-chat-deps/sokol/sokol_time.h>

// Include platform thread for thread ID in timer names
#include "../platform/thread.h"

// Include uthash wrapper for UBSan-safe hash functions
// Headers can include this even before common.h is fully processed
#include <ascii-chat/uthash.h>

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

// Floating-point versions (for time formatting and conversions)
#define NS_PER_US 1000.0
#define NS_PER_MS (1000.0 * NS_PER_US)
#define NS_PER_SEC (1000.0 * NS_PER_MS)
#define NS_PER_MIN (60.0 * NS_PER_SEC)
#define NS_PER_HOUR (60.0 * NS_PER_MIN)
#define NS_PER_DAY (24.0 * NS_PER_HOUR)
#define NS_PER_YEAR (365.25 * NS_PER_DAY) // Account for leap years

// Integer versions (for comparing uint64_t nanosecond values)
#define NS_PER_US_INT 1000ULL
#define NS_PER_MS_INT (1000ULL * NS_PER_US_INT)
#define NS_PER_SEC_INT (1000ULL * NS_PER_MS_INT)

// Microsecond versions (for API compatibility)
#define US_PER_MS_INT 1000ULL
#define US_PER_SEC_INT 1000000ULL

// Millisecond versions (for API compatibility)
#define MS_PER_SEC_INT 1000ULL

// Standard time unit conversions (seconds-based)
#define SEC_PER_MIN 60ULL
#define SEC_PER_HOUR (SEC_PER_MIN * SEC_PER_MIN)
#define SEC_PER_DAY (24ULL * SEC_PER_HOUR)

// Minute/hour/day to nanoseconds conversions
#define NS_PER_MIN_INT (SEC_PER_MIN * NS_PER_SEC_INT)
#define NS_PER_HOUR_INT (SEC_PER_HOUR * NS_PER_SEC_INT)
#define NS_PER_DAY_INT (SEC_PER_DAY * NS_PER_SEC_INT)

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
void timer_system_destroy(void);

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
      char _timer_name_with_tid[280];                                                                                  \
      snprintf(_timer_name_buf, sizeof(_timer_name_buf), name_fmt, ##__VA_ARGS__);                                     \
      snprintf(_timer_name_with_tid, sizeof(_timer_name_with_tid), "%s_tid%llu", _timer_name_buf,                      \
               (unsigned long long)asciichat_thread_current_id());                                                     \
      (void)timer_start(_timer_name_with_tid);                                                                         \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Start a timer without thread ID (for cross-thread timers)
 *
 * Use this variant when a timer may be stopped on a different thread than
 * it was started. The timer name will NOT include thread ID, so it must be
 * globally unique through other means (e.g., including a unique identifier
 * in the format string).
 *
 * @param name_fmt Printf-style format string for timer name
 * @param ... Format arguments to create unique key
 * @ingroup module_utilities
 */
#define START_TIMER_GLOBAL(name_fmt, ...)                                                                              \
  do {                                                                                                                 \
    if (timer_is_initialized()) {                                                                                      \
      char _timer_name_buf[256];                                                                                       \
      snprintf(_timer_name_buf, sizeof(_timer_name_buf), name_fmt, ##__VA_ARGS__);                                     \
      (void)timer_start(_timer_name_buf);                                                                              \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Stop a cross-thread timer and return elapsed time
 *
 * Use this variant when stopping a timer started with START_TIMER_GLOBAL.
 * The timer name must match exactly (without thread ID appended).
 *
 * @param name_fmt Printf-style format string for timer name (must match START_TIMER_GLOBAL)
 * @param ... Format arguments (must match START_TIMER_GLOBAL to create same key)
 * @return Elapsed time in nanoseconds, or -1.0 if timer not found
 * @ingroup module_utilities
 */
#define STOP_TIMER_GLOBAL(name_fmt, ...)                                                                               \
  ({                                                                                                                   \
    double _elapsed = -1.0;                                                                                            \
    if (timer_is_initialized()) {                                                                                      \
      char _timer_name_buf[256];                                                                                       \
      snprintf(_timer_name_buf, sizeof(_timer_name_buf), name_fmt, ##__VA_ARGS__);                                     \
      _elapsed = timer_stop(_timer_name_buf);                                                                          \
    }                                                                                                                  \
    _elapsed;                                                                                                          \
  })

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
      char _timer_name_with_tid[280];                                                                                  \
      snprintf(_timer_name_buf, sizeof(_timer_name_buf), name_fmt, ##__VA_ARGS__);                                     \
      snprintf(_timer_name_with_tid, sizeof(_timer_name_with_tid), "%s_tid%llu", _timer_name_buf,                      \
               (unsigned long long)asciichat_thread_current_id());                                                     \
      _elapsed = timer_stop(_timer_name_with_tid);                                                                     \
    }                                                                                                                  \
    _elapsed;                                                                                                          \
  })

// ============================================================================
// Time Formatting API
// ============================================================================


/**
 * @brief Format nanoseconds as pretty duration with spaces and configurable precision
 * @param nanoseconds Duration in nanoseconds
 * @param decimals Number of decimal places (0-9), or -1 for sensible defaults per range
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Formats a duration with space-separated number and unit, with configurable precision.
 * Automatically strips trailing zeros and decimal points.
 *
 * Examples (with decimals=-1):
 * - 500 ns     → "500 ns"
 * - 3500 ns    → "3.5 µs"
 * - 1253000 ns → "1.253 ms"
 * - 2500000000 ns → "2.5 s"
 * - 83456000000000 ns → "1:23.456"  (minutes range, MM:SS.fraction)
 * - 7530000000000 ns → "2:05:30"   (hours range, H:MM:SS)
 *
 * Unit selection logic:
 * - [0, 1k) ns       → "NNN ns"
 * - [1k, 1M) ns      → "N.N µs" (up to 3 decimals)
 * - [1M, 1G) ns      → "N.NNN ms" (up to 3 decimals)
 * - [1G, 60G) ns     → "N.NN s" (up to 2 decimals)
 * - [60G, 3.6T) ns   → "M:SS.fraction" (colon notation, up to 3 decimals)
 * - [3.6T+) ns       → "H:MM:SS" (colon notation)
 *
 * @note Buffer should be at least 32 bytes for all possible outputs
 * @note Thread-safe (no global state)
 * @note decimals=-1 uses appropriate defaults (3 for ns/µs/ms/colon, 2 for s)
 * @note decimals=0 produces no decimal point
 *
 * @ingroup module_utilities
 */
int time_pretty(uint64_t nanoseconds, int decimals, char *buffer, size_t buffer_size);

/**
 * @brief Format current monotonic time as pretty duration
 * @param decimals Number of decimal places (0-9), or -1 for sensible defaults per range
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Convenience wrapper that calls time_get_ns() and delegates to time_pretty().
 * Useful for measuring elapsed time since program start in a single call.
 *
 * @note Buffer should be at least 32 bytes for all possible outputs
 * @note Thread-safe (no global state)
 *
 * @ingroup module_utilities
 */
int time_pretty_now(int decimals, char *buffer, size_t buffer_size);

/**
 * @brief Format nanoseconds as human-readable relative duration (moment.js style)
 * @param nanoseconds Duration in nanoseconds
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Formats a duration using moment.js-compatible thresholds to produce natural language
 * relative time strings like "a few seconds ago", "3 minutes ago", "2 hours ago", etc.
 *
 * Threshold table (moment.js compatible):
 * - < 45 s        → "a few seconds ago"
 * - < 90 s        → "a minute ago"
 * - < 45 min      → "%d minutes ago"
 * - < 90 min      → "an hour ago"
 * - < 22 h        → "%d hours ago"
 * - < 36 h        → "a day ago"
 * - < 25 d        → "%d days ago"
 * - < 45 d        → "a month ago"
 * - < 11 months   → "%d months ago"
 * - < 18 months   → "a year ago"
 * - ≥ 18 months   → "%d years ago"
 *
 * @note Buffer should be at least 32 bytes for all possible outputs
 * @note Thread-safe (no global state)
 *
 * @ingroup module_utilities
 */
int time_human_readable(uint64_t nanoseconds, char *buffer, size_t buffer_size);

/**
 * @brief Format elapsed time as human-readable relative duration with past/future support
 * @param nanoseconds Signed elapsed nanoseconds (negative for future times)
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Formats a duration as a human-readable string with moment.js-compatible thresholds.
 * Supports both past times ("3 hours ago") and future times ("in 5 minutes").
 *
 * Examples:
 * - Positive: 3600000000000 ns → "an hour ago"
 * - Negative: -300000000000 ns → "in 5 minutes"
 *
 * @note Use positive values for elapsed time (past), negative for countdown (future)
 * @note Buffer should be at least 32 bytes for all possible outputs
 * @note Thread-safe (no global state)
 *
 * @ingroup module_utilities
 */
int time_human_readable_signed(int64_t nanoseconds, char *buffer, size_t buffer_size);

/**
 * @brief Format current monotonic time as human-readable relative duration
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Convenience wrapper that calls time_get_ns() and delegates to time_human_readable().
 * Useful for measuring time since program start in a single call.
 *
 * @note Buffer should be at least 32 bytes for all possible outputs
 * @note Thread-safe (no global state)
 *
 * @ingroup module_utilities
 */
int time_human_readable_now(char *buffer, size_t buffer_size);

/**
 * @brief Format uptime as HH:MM:SS string
 * @param hours Hours component (0-999)
 * @param minutes Minutes component (0-59)
 * @param seconds Seconds component (0-59)
 * @param buffer Output buffer for formatted string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Formats an uptime duration as a fixed "HH:MM:SS" string with zero-padding.
 * Useful for status displays where consistent formatting is preferred over
 * adaptive human-readable formats.
 *
 * Examples:
 * - format_uptime_hms(0, 0, 5, buf, size) -> "00:00:05"
 * - format_uptime_hms(1, 30, 45, buf, size) -> "01:30:45"
 * - format_uptime_hms(123, 45, 6, buf, size) -> "123:45:06"
 *
 * @note Buffer should be at least 12 bytes (HH:MM:SS + null terminator)
 * @note Thread-safe (no global state)
 * @note Hours component can exceed 99 (e.g., "123:45:06" for 123 hours)
 *
 * @ingroup module_utilities
 */
int format_uptime_hms(int hours, int minutes, int seconds, char *buffer, size_t buffer_size);

/**
 * @brief Stop a timer and log the result with a custom message
 *
 * Combines STOP_TIMER() with logging. The timer is stopped, elapsed time is retrieved,
 * and a log message is generated with the elapsed time appended in human-readable format.
 * Optionally filters based on a minimum elapsed time threshold.
 *
 * Usage:
 *   STOP_TIMER_AND_LOG(info, 0, "client_handshake", "Crypto handshake completed successfully");
 *   STOP_TIMER_AND_LOG(debug, 5000000, "process_frame_%d", "Frame %d processed", frame_id, frame_id);
 *   STOP_TIMER_AND_LOG(dev, NS_PER_MS_INT, "render", "Rendering complete");
 *
 * The macro will append " in X.XXms" (or appropriate unit) to your message automatically.
 * Supported log levels: dev, debug, info, warn, error, fatal
 *
 * @param log_level Log level name (dev, debug, info, warn, error, fatal - without log_ prefix)
 * @param threshold_ns Minimum elapsed time in nanoseconds to trigger log (0 = always log)
 * @param timer_name Timer name (must match START_TIMER call)
 * @param msg_fmt Printf-style format string for the message
 * @param ... Format arguments for both timer name and message
 * @ingroup module_utilities
 */
#define STOP_TIMER_AND_LOG(log_level, threshold_ns, timer_name, msg_fmt, ...)                                          \
  do {                                                                                                                 \
    double _elapsed_ns = STOP_TIMER(timer_name, ##__VA_ARGS__);                                                        \
    if (_elapsed_ns >= 0.0 && (threshold_ns == 0 || _elapsed_ns >= (double)(threshold_ns))) {                          \
      char _duration_str[32];                                                                                          \
      time_pretty((uint64_t)_elapsed_ns, -1, _duration_str, sizeof(_duration_str));                                    \
      log_##log_level(msg_fmt " in %s", ##__VA_ARGS__, _duration_str);                                                 \
    }                                                                                                                  \
  } while (0)

/**
 * @brief Stop a timer and log the result with rate limiting
 *
 * Combines STOP_TIMER() with rate-limited logging. The timer is stopped, elapsed time
 * is retrieved, and a log message is generated only if:
 * 1. The elapsed time exceeds the threshold (if non-zero), AND
 * 2. The specified time interval has passed since the last logged occurrence
 *
 * This prevents log spam from frequent operations while also filtering out fast operations.
 *
 * Usage:
 *   STOP_TIMER_AND_LOG_EVERY(dev, 1000000000, 0, "opus_encode", "Opus encode completed");
 *   STOP_TIMER_AND_LOG_EVERY(info, 5000000000, NS_PER_MS_INT, "process_frame_%d", "Frame %d processed", frame_id);
 *
 * The macro will append " in X.XXms" (or appropriate unit) to your message automatically.
 * Supported log levels: dev, debug, info, warn, error, fatal
 *
 * @param log_level Log level name (dev, debug, info, warn, error, fatal - without log_ prefix)
 * @param interval_ns Time interval in nanoseconds between log emissions (rate limiting)
 * @param threshold_ns Minimum elapsed time in nanoseconds to trigger log (0 = no threshold)
 * @param timer_name Timer name (must match START_TIMER call)
 * @param msg_fmt Printf-style format string for the message
 * @param ... Format arguments for both timer name and message
 * @ingroup module_utilities
 */
#define STOP_TIMER_AND_LOG_EVERY(log_level, interval_ns, threshold_ns, timer_name, msg_fmt, ...)                       \
  do {                                                                                                                 \
    double _elapsed_ns = STOP_TIMER(timer_name, ##__VA_ARGS__);                                                        \
    if (_elapsed_ns >= 0.0 && (threshold_ns == 0 || _elapsed_ns >= (double)(threshold_ns))) {                          \
      static TIME_ATOMIC_UINT64 _log_every_last_time = TIME_ATOMIC_UINT64_INIT(0);                                     \
      uint64_t _log_every_now = time_get_ns();                                                                         \
      uint64_t _log_every_last = atomic_load_explicit(&_log_every_last_time, memory_order_relaxed);                    \
      if (_log_every_now - _log_every_last >= (uint64_t)(interval_ns)) {                                               \
        if (atomic_compare_exchange_weak_explicit(&_log_every_last_time, &_log_every_last, _log_every_now,             \
                                                  memory_order_relaxed, memory_order_relaxed)) {                       \
          char _duration_str[32];                                                                                      \
          time_pretty((uint64_t)_elapsed_ns, -1, _duration_str, sizeof(_duration_str));                                \
          log_##log_level(msg_fmt " in %s", ##__VA_ARGS__, _duration_str);                                             \
        }                                                                                                              \
      }                                                                                                                \
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
 *   platform_sleep_us(sleep_ns / 1000);
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
 * Convenience wrapper that combines adaptive_sleep_calculate() with platform_sleep_us().
 * Useful for simple loops that don't need to inspect the calculated sleep time.
 *
 * @param state Pointer to adaptive sleep state (will be updated)
 * @param queue_depth Current number of items in queue/buffer
 * @param target_depth Desired queue depth (0 = empty queue target)
 * @ingroup module_utilities
 */
void adaptive_sleep_do(adaptive_sleep_state_t *state, size_t queue_depth, size_t target_depth);

/* ============================================================================
 * Time Format Validation and Formatting
 * ============================================================================ */

/**
 * @brief Validate strftime format string against known safe specifiers
 * @param format_str Format string to validate (e.g., "%H:%M:%S")
 * @return true if valid, false if contains invalid specifiers or syntax
 *
 * Validates format string by checking each % specifier against a whitelist
 * of known safe POSIX strftime specifiers. Returns false if:
 * - Contains unterminated % sequences
 * - Contains invalid/unsupported specifiers
 * - Contains malformed width/precision
 * - Contains unsupported locale-dependent specifiers
 *
 * **Supported specifiers:**
 * - Date: %Y (4-digit year), %m (month), %d (day), %j (day of year)
 * - Date (ISO): %F (full date), %G (ISO year), %g (ISO year short), %V (ISO week)
 * - Time: %H (24-hour), %M (minute), %S (second), %I (12-hour), %p (AM/PM)
 * - Time (combined): %T (full time HH:MM:SS), %s (seconds since epoch)
 * - Locale: %a (abbrev weekday), %A (full weekday), %b (abbrev month), %B (full month)
 * - Locale: %c (locale date/time), %x (locale date), %X (locale time)
 * - Timezone: %z (offset), %Z (name)
 * - Weekday: %w (0-6), %u (1-7)
 *
 * @ingroup module_utilities
 */
bool time_format_is_valid_strftime(const char *format_str);

/**
 * @brief Format current time using strftime format string
 * @param format_str strftime format string (should be validated with time_format_is_valid_strftime)
 * @param buf Output buffer
 * @param buf_size Output buffer size
 * @return Number of characters written (excluding null terminator), or 0 on error
 *
 * Formats current wall-clock time (CLOCK_REALTIME) using the provided strftime format.
 * Handles nanosecond precision separately: if format contains %S and buffer has room,
 * appends ".NNNNNN" for microseconds (rounded from nanoseconds).
 *
 * **Example outputs:**
 * - Format "%H:%M:%S" → "14:30:45.123456"
 * - Format "%Y-%m-%d" → "2026-02-16"
 * - Format "%F %T" → "2026-02-16 14:30:45.123456"
 *
 * **Notes:**
 * - Buffer size should be at least 64 bytes
 * - Automatic microsecond appending occurs only when %S is in format
 * - Output is locale-aware (UTF-8 safe from strftime)
 * - Returns 0 on error (strftime failure or buffer overflow)
 *
 * @ingroup module_utilities
 */
int time_format_now(const char *format_str, char *buf, size_t buf_size);

/**
 * @brief Safe wrapper for time formatting with validation and error codes
 * @param format_str Format string to use
 * @param buf Output buffer
 * @param buf_size Output buffer size
 * @return ASCIICHAT_OK on success, error code on failure (includes error context)
 *
 * Validates format string first, then formats time. Returns detailed error if:
 * - format_str is NULL
 * - buf is NULL
 * - buf_size < 64 (minimum safe size)
 * - format_str contains invalid specifiers
 * - strftime fails
 *
 * All errors include context message via SET_ERRNO for debugging.
 *
 * **Example:**
 * ```c
 * char timebuf[64];
 * asciichat_error_t err = time_format_safe("%H:%M:%S", timebuf, sizeof(timebuf));
 * if (err != ASCIICHAT_OK) {
 *     log_error("Failed to format time");
 *     return err;
 * }
 * log_info("Current time: %s", timebuf);
 * ```
 *
 * @ingroup module_utilities
 */
asciichat_error_t time_format_safe(const char *format_str, char *buf, size_t buf_size);

/** @} */

/* ============================================================================
 * Conversion Functions
 * ============================================================================ */

/**
 * @brief Convert struct timespec to nanoseconds
 * @param ts Pointer to timespec structure
 * @return Time in nanoseconds
 * @ingroup module_utilities
 *
 * Useful for converting CLOCK_MONOTONIC or CLOCK_REALTIME readings to nanoseconds.
 */
uint64_t time_timespec_to_ns(const struct timespec *ts);

/**
 * @brief Convert nanoseconds to struct timespec
 * @param ns Time in nanoseconds
 * @param ts Pointer to timespec structure (output)
 * @ingroup module_utilities
 *
 * Useful for nanosleep() or other system calls that require struct timespec.
 */
void time_ns_to_timespec(uint64_t ns, struct timespec *ts);

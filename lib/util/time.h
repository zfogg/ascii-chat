#pragma once

/**
 * @file util/time.h
 * @ingroup module_utilities
 * @brief ⏱️ High-precision timing utilities using sokol_time.h and uthash
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

// ============================================================================
// sokol_time.h Integration
// ============================================================================

// Include sokol_time.h declarations (WITHOUT implementation)
// SOKOL_IMPL is defined only in time.c to avoid duplicate symbols
#include "../../deps/sokol/sokol_time.h"
#include "util/uthash.h"

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
// Time Unit Constants
// ============================================================================

#define NS_PER_US 1000.0
#define NS_PER_MS 1000000.0
#define NS_PER_SEC 1000000000.0
#define NS_PER_MIN (NS_PER_SEC * 60.0)
#define NS_PER_HOUR (NS_PER_MIN * 60.0)
#define NS_PER_DAY (NS_PER_HOUR * 24.0)
#define NS_PER_YEAR (NS_PER_DAY * 365.25) // Account for leap years

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

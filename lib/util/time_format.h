#pragma once

/**
 * @file util/time_format.h
 * @ingroup module_utilities
 * @brief Human-readable time duration formatting
 *
 * This module provides functions to format time durations (in milliseconds)
 * into human-readable strings with appropriate units. The formatting is
 * optimized for readability:
 *
 * - Chooses the most appropriate unit (ns, µs, ms, s, m, h, d, y)
 * - Shows at most 2 significant units for clarity
 * - Omits unnecessary precision (e.g., "5ns" not "5.000ns")
 * - Formats large durations concisely (e.g., "1.2y" not "1y73d")
 *
 * Examples:
 *   5 nanoseconds  → "5ns"
 *   150 microseconds → "150µs"
 *   2.5 milliseconds → "2.5ms"
 *   1.5 seconds → "1.5s"
 *   90 seconds → "1m30s"
 *   5.5 hours → "5h30m"
 *   1.2 years → "1.2y"
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date November 2025
 */

#include <stdint.h>
#include <stdbool.h>

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
 * - For sub-hour durations: shows minutes and seconds
 * - For sub-day durations: shows hours and minutes
 * - For sub-year durations: shows days and hours
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

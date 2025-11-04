/**
 * @file util/time_format.c
 * @ingroup module_utilities
 * @brief Human-readable time duration formatting implementation
 */

#include "time_format.h"
#include "../common.h"
#include <stdio.h>
#include <math.h>

// ============================================================================
// Time Formatting Implementation
// ============================================================================

int format_duration_ns(double nanoseconds, char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) {
    return -1;
  }

  // Handle negative durations
  if (nanoseconds < 0) {
    nanoseconds = -nanoseconds;
  }

  int written = 0;

  // Nanoseconds (< 1µs)
  if (nanoseconds < NS_PER_US) {
    written = snprintf(buffer, buffer_size, "%.0fns", nanoseconds);
  }
  // Microseconds (< 1ms)
  else if (nanoseconds < NS_PER_MS) {
    double us = nanoseconds / NS_PER_US;
    if (us < 10.0) {
      written = snprintf(buffer, buffer_size, "%.1fµs", us);
    } else {
      written = snprintf(buffer, buffer_size, "%.0fµs", us);
    }
  }
  // Milliseconds (< 1s)
  else if (nanoseconds < NS_PER_SEC) {
    double ms = nanoseconds / NS_PER_MS;
    if (ms < 10.0) {
      written = snprintf(buffer, buffer_size, "%.1fms", ms);
    } else {
      written = snprintf(buffer, buffer_size, "%.0fms", ms);
    }
  }
  // Seconds (< 1m)
  else if (nanoseconds < NS_PER_MIN) {
    double s = nanoseconds / NS_PER_SEC;
    if (s < 10.0) {
      written = snprintf(buffer, buffer_size, "%.2fs", s);
    } else {
      written = snprintf(buffer, buffer_size, "%.1fs", s);
    }
  }
  // Minutes (< 1h) - show minutes and seconds
  else if (nanoseconds < NS_PER_HOUR) {
    int minutes = (int)(nanoseconds / NS_PER_MIN);
    int seconds = (int)((nanoseconds - (minutes * NS_PER_MIN)) / NS_PER_SEC);
    written = snprintf(buffer, buffer_size, "%dm%ds", minutes, seconds);
  }
  // Hours (< 1d) - show hours, minutes, and seconds
  else if (nanoseconds < NS_PER_DAY) {
    int hours = (int)(nanoseconds / NS_PER_HOUR);
    double remaining_ns = nanoseconds - ((double)hours * NS_PER_HOUR);
    int minutes = (int)(remaining_ns / NS_PER_MIN);
    double remaining_after_min = remaining_ns - ((double)minutes * NS_PER_MIN);
    int seconds = (int)(remaining_after_min / NS_PER_SEC);
    written = snprintf(buffer, buffer_size, "%dh%dm%ds", hours, minutes, seconds);
  }
  // Days (< 1y) - show days, hours, minutes, and seconds
  else if (nanoseconds < NS_PER_YEAR) {
    int days = (int)(nanoseconds / NS_PER_DAY);
    double remaining_ns = nanoseconds - ((double)days * NS_PER_DAY);
    int hours = (int)(remaining_ns / NS_PER_HOUR);
    remaining_ns = remaining_ns - ((double)hours * NS_PER_HOUR);
    int minutes = (int)(remaining_ns / NS_PER_MIN);
    double remaining_after_min = remaining_ns - ((double)minutes * NS_PER_MIN);
    int seconds = (int)(remaining_after_min / NS_PER_SEC);
    written = snprintf(buffer, buffer_size, "%dd%dh%dm%ds", days, hours, minutes, seconds);
  }
  // Years - show years with one decimal
  else {
    double years = nanoseconds / NS_PER_YEAR;
    written = snprintf(buffer, buffer_size, "%.1fy", years);
  }

  if (written < 0 || (size_t)written >= buffer_size) {
    return -1;
  }

  return written;
}

int format_duration_ms(double milliseconds, char *buffer, size_t buffer_size) {
  // Convert milliseconds to nanoseconds and use the nanosecond formatter
  double nanoseconds = milliseconds * NS_PER_MS;
  return format_duration_ns(nanoseconds, buffer, buffer_size);
}

int format_duration_s(double seconds, char *buffer, size_t buffer_size) {
  // Convert seconds to nanoseconds and use the nanosecond formatter
  double nanoseconds = seconds * NS_PER_SEC;
  return format_duration_ns(nanoseconds, buffer, buffer_size);
}

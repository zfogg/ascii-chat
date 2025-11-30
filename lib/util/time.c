/**
 * @file util/time.c
 * @ingroup module_utilities
 * @brief ⏱️ High-precision timing utilities implementation
 */

#define SOKOL_IMPL
#include "time.h"
#include "../common.h"
#include "../asciichat_errno.h"
#include "../platform/rwlock.h"
#include "uthash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
// Global State
// ============================================================================

/**
 * @brief Global timer manager
 */
static struct {
  timer_record_t *timers; ///< Hash table of active timers (uthash head pointer)
  rwlock_t rwlock;        ///< Read-write lock for thread-safe access (uthash requires external locking)
  bool initialized;       ///< Initialization state
} g_timer_manager = {
    .timers = NULL,
    .initialized = false,
};

// ============================================================================
// Timer System Implementation
// ============================================================================

bool timer_system_init(void) {
  if (g_timer_manager.initialized) {
    return true;
  }

  // Initialize sokol_time
  stm_setup();

  // Initialize rwlock (uthash requires external locking for writes, rwlock allows concurrent reads)
  if (rwlock_init(&g_timer_manager.rwlock) != 0) {
    SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to initialize timer rwlock");
    return false;
  }

  g_timer_manager.timers = NULL;
  g_timer_manager.initialized = true;

  log_debug("Timer system initialized");
  return true;
}

void timer_system_cleanup(void) {
  if (!g_timer_manager.initialized) {
    return;
  }

  rwlock_wrlock(&g_timer_manager.rwlock);

  // Free all timer records
  timer_record_t *current, *tmp;
  HASH_ITER(hh, g_timer_manager.timers, current, tmp) {
    HASH_DEL(g_timer_manager.timers, current);
    SAFE_FREE(current->name);
    SAFE_FREE(current);
  }

  g_timer_manager.timers = NULL;
  g_timer_manager.initialized = false;

  rwlock_wrunlock(&g_timer_manager.rwlock);
  rwlock_destroy(&g_timer_manager.rwlock);

  log_debug("Timer system cleaned up");
}

bool timer_start(const char *name) {
  if (!g_timer_manager.initialized) {
    SET_ERRNO(ERROR_INVALID_STATE, "Timer system not initialized");
    return false;
  }

  if (!name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Timer name is NULL");
    return false;
  }

  rwlock_wrlock(&g_timer_manager.rwlock);

  // Check if timer already exists - this is an error, timers must be unique
  timer_record_t *existing = NULL;
  HASH_FIND_STR(g_timer_manager.timers, name, existing);

  if (existing) {
    rwlock_wrunlock(&g_timer_manager.rwlock);
    SET_ERRNO(ERROR_INVALID_STATE, "Timer '%s' already exists - timers must have unique names", name);
    return false;
  }

  // Create new timer record
  timer_record_t *timer = SAFE_MALLOC(sizeof(timer_record_t), timer_record_t *);
  if (!timer) {
    rwlock_wrunlock(&g_timer_manager.rwlock);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate timer record");
    return false;
  }

  // Use platform-safe string duplication
  size_t name_len = strlen(name) + 1;
  timer->name = SAFE_MALLOC(name_len, char *);
  if (!timer->name) {
    SAFE_FREE(timer);
    rwlock_wrunlock(&g_timer_manager.rwlock);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate timer name");
    return false;
  }
  SAFE_STRNCPY(timer->name, name, name_len);

  timer->start_ticks = stm_now();

  // Add to hashtable
  HASH_ADD_KEYPTR(hh, g_timer_manager.timers, timer->name, strlen(timer->name), timer);

  rwlock_wrunlock(&g_timer_manager.rwlock);
  return true;
}

double timer_stop(const char *name) {
  if (!g_timer_manager.initialized) {
    SET_ERRNO(ERROR_INVALID_STATE, "Timer system not initialized");
    return -1.0;
  }

  if (!name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Timer name is NULL");
    return -1.0;
  }

  rwlock_wrlock(&g_timer_manager.rwlock);

  // Find timer
  timer_record_t *timer = NULL;
  HASH_FIND_STR(g_timer_manager.timers, name, timer);

  if (!timer) {
    rwlock_wrunlock(&g_timer_manager.rwlock);
    log_warn("Timer '%s' not found", name);
    return -1.0;
  }

  // Calculate elapsed time in nanoseconds for maximum precision
  uint64_t end_ticks = stm_now();
  uint64_t elapsed_ticks = stm_diff(end_ticks, timer->start_ticks);
  double elapsed_ns = stm_ns(elapsed_ticks);

  // Format duration for human-readable logging
  char duration_str[32];
  format_duration_ns(elapsed_ns, duration_str, sizeof(duration_str));

  // Log the result (debug level - caller can use return value for production logging)
  log_debug("Timer '%s': %s", name, duration_str);

  // Remove from hashtable
  HASH_DEL(g_timer_manager.timers, timer);
  SAFE_FREE(timer->name);
  SAFE_FREE(timer);

  rwlock_wrunlock(&g_timer_manager.rwlock);
  return elapsed_ns; // Return nanoseconds
}

bool timer_is_initialized(void) {
  return g_timer_manager.initialized;
}

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

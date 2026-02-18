/**
 * @file util/time.c
 * @ingroup module_utilities
 * @brief ⏱️ High-precision timing utilities implementation
 */

#include "ascii-chat/common/error_codes.h"
#include <ascii-chat/util/time.h>

#include <ascii-chat-deps/sokol/sokol_time.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/platform/abstraction.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

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

/**
 * @brief Track if sokol_time has been initialized (separate from full timer system init)
 */
static bool g_sokol_time_initialized = false;

// ============================================================================
// Core Monotonic Timing Implementation
// ============================================================================

uint64_t time_get_ns(void) {
  // sokol_time provides monotonic clock that never goes backwards
  // stm_ns() converts ticks to nanoseconds
  // Ensure sokol_time is initialized before use (defensive programming)
  if (!g_sokol_time_initialized) {
    stm_setup();
    g_sokol_time_initialized = true;
  }
  return (uint64_t)stm_ns(stm_now());
}

uint64_t time_get_realtime_ns(void) {
#ifdef _WIN32
  // Windows: Use GetSystemTimePreciseAsFileTime (Win8+) for high-precision wall clock
  // FILETIME is 100-nanosecond intervals since January 1, 1601
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  // Convert FILETIME to nanoseconds
  uint64_t filetime = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  // Convert 100-nanosecond intervals to nanoseconds
  return filetime * 100;
#else
  // Get wall-clock (real-time) timestamp
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    // Fallback on error (unlikely)
    return 0;
  }
  return time_timespec_to_ns(&ts);
#endif
}

void time_sleep_ns(uint64_t ns) {
  // Use platform abstraction for cross-platform sleep
  // Convert nanoseconds to microseconds (1 us = 1000 ns)
  unsigned int usec = (unsigned int)(ns / 1000);
  if (usec == 0 && ns > 0) {
    usec = 1; // Minimum 1 microsecond
  }
  platform_sleep_us(usec);
}

uint64_t time_elapsed_ns(uint64_t start_ns, uint64_t end_ns) {
  // Handle wraparound (defensive - uint64_t won't wrap in practice at nanosecond resolution)
  // but this is safe and handles any theoretical edge cases
  if (end_ns >= start_ns) {
    return end_ns - start_ns;
  }
  // Wraparound case (extremely unlikely)
  return (UINT64_MAX - start_ns) + end_ns + 1;
}

// ============================================================================
// Conversion Functions (moved from inline in header to avoid circular deps)
// ============================================================================

uint64_t time_timespec_to_ns(const struct timespec *ts) {
  if (!ts) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null ts");
    return 0;
  }
  return (uint64_t)ts->tv_sec * NS_PER_SEC_INT + (uint64_t)ts->tv_nsec;
}

void time_ns_to_timespec(uint64_t ns, struct timespec *ts) {
  if (!ts) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null ts");
    return;
  }
  ts->tv_sec = (time_t)(ns / NS_PER_SEC_INT);
  ts->tv_nsec = (long)(ns % NS_PER_SEC_INT);
}

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

  log_dev("Timer system initialized");
  return true;
}

void timer_system_destroy(void) {
  if (!g_timer_manager.initialized) {
    return;
  }

  rwlock_wrlock(&g_timer_manager.rwlock);

  // Free all timer records
  int timer_count = 0;
  timer_record_t *current, *tmp;
  HASH_ITER(hh, g_timer_manager.timers, current, tmp) {
    HASH_DEL(g_timer_manager.timers, current);
    SAFE_FREE(current->name);
    SAFE_FREE(current);
    timer_count++;
  }

  g_timer_manager.timers = NULL;
  g_timer_manager.initialized = false;

  rwlock_wrunlock(&g_timer_manager.rwlock);
  rwlock_destroy(&g_timer_manager.rwlock);

  log_debug("Timer system cleaned up (freed %d timers)", timer_count);
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

  // Log the result (dev level - only shown with --verbose)
  log_dev("Timer '%s': %s", name, duration_str);

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
    written = safe_snprintf(buffer, buffer_size, "%.0fns", nanoseconds);
  }
  // Microseconds (< 1ms)
  else if (nanoseconds < NS_PER_MS) {
    double us = nanoseconds / NS_PER_US;
    if (us < 10.0) {
      written = safe_snprintf(buffer, buffer_size, "%.1fµs", us);
    } else {
      written = safe_snprintf(buffer, buffer_size, "%.0fµs", us);
    }
  }
  // Milliseconds (< 1s)
  else if (nanoseconds < NS_PER_SEC) {
    double ms = nanoseconds / NS_PER_MS;
    if (ms < 10.0) {
      written = safe_snprintf(buffer, buffer_size, "%.1fms", ms);
    } else {
      written = safe_snprintf(buffer, buffer_size, "%.0fms", ms);
    }
  }
  // Seconds (< 1m)
  else if (nanoseconds < NS_PER_MIN) {
    double s = nanoseconds / NS_PER_SEC;
    if (s < 10.0) {
      written = safe_snprintf(buffer, buffer_size, "%.2fs", s);
    } else {
      written = safe_snprintf(buffer, buffer_size, "%.1fs", s);
    }
  }
  // Minutes (< 1h) - show minutes and seconds
  else if (nanoseconds < NS_PER_HOUR) {
    int minutes = (int)(nanoseconds / NS_PER_MIN);
    int seconds = (int)((nanoseconds - (minutes * NS_PER_MIN)) / NS_PER_SEC);
    written = safe_snprintf(buffer, buffer_size, "%dm%ds", minutes, seconds);
  }
  // Hours (< 1d) - show hours, minutes, and seconds
  else if (nanoseconds < NS_PER_DAY) {
    int hours = (int)(nanoseconds / NS_PER_HOUR);
    double remaining_ns = nanoseconds - ((double)hours * NS_PER_HOUR);
    int minutes = (int)(remaining_ns / NS_PER_MIN);
    double remaining_after_min = remaining_ns - ((double)minutes * NS_PER_MIN);
    int seconds = (int)(remaining_after_min / NS_PER_SEC);
    written = safe_snprintf(buffer, buffer_size, "%dh%dm%ds", hours, minutes, seconds);
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
    written = safe_snprintf(buffer, buffer_size, "%dd%dh%dm%ds", days, hours, minutes, seconds);
  }
  // Years - show years with one decimal
  else {
    double years = nanoseconds / NS_PER_YEAR;
    written = safe_snprintf(buffer, buffer_size, "%.1fy", years);
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

int format_uptime_hms(int hours, int minutes, int seconds, char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) {
    return -1;
  }

  // Validate components
  if (hours < 0 || minutes < 0 || minutes >= 60 || seconds < 0 || seconds >= 60) {
    return -1;
  }

  // Format as HH:MM:SS with zero-padding
  int written = safe_snprintf(buffer, buffer_size, "%02d:%02d:%02d", hours, minutes, seconds);

  if (written < 0 || (size_t)written >= buffer_size) {
    return -1;
  }

  return written;
}

// ============================================================================
// Adaptive Sleep Implementation
// ============================================================================

void adaptive_sleep_init(adaptive_sleep_state_t *state, const adaptive_sleep_config_t *config) {
  if (!state || !config) {
    return;
  }

  // Copy configuration
  state->config = *config;

  // Start at baseline speed (no speedup initially)
  state->current_speed_multiplier = config->min_speed_multiplier;

  // Initial sleep is the baseline
  state->last_sleep_ns = config->baseline_sleep_ns;
}

uint64_t adaptive_sleep_calculate(adaptive_sleep_state_t *state, size_t queue_depth, size_t target_depth) {
  if (!state) {
    return 0;
  }

  const adaptive_sleep_config_t *cfg = &state->config;

  // Determine if we need to speed up or slow down
  double desired_multiplier;

  if (queue_depth > target_depth) {
    // Queue is building up - speed up processing
    // The more items in queue, the faster we should process
    size_t backlog = queue_depth - target_depth;

    // Calculate desired speed based on backlog
    // Each item above target increases speed proportionally
    // This ensures we drain the queue faster the more backed up we are
    double backlog_factor = 1.0 + ((double)backlog / (double)(target_depth + 1));
    desired_multiplier = cfg->min_speed_multiplier * backlog_factor;

    // Clamp to max speed
    if (desired_multiplier > cfg->max_speed_multiplier) {
      desired_multiplier = cfg->max_speed_multiplier;
    }

    // Ramp up gradually based on speedup_rate
    double delta = desired_multiplier - state->current_speed_multiplier;
    state->current_speed_multiplier += delta * cfg->speedup_rate;

  } else {
    // Queue is at or below target - slow down to baseline
    desired_multiplier = cfg->min_speed_multiplier;

    // Ramp down gradually based on slowdown_rate
    double delta = desired_multiplier - state->current_speed_multiplier;
    state->current_speed_multiplier += delta * cfg->slowdown_rate;
  }

  // Ensure we stay within configured bounds
  if (state->current_speed_multiplier < cfg->min_speed_multiplier) {
    state->current_speed_multiplier = cfg->min_speed_multiplier;
  }
  if (state->current_speed_multiplier > cfg->max_speed_multiplier) {
    state->current_speed_multiplier = cfg->max_speed_multiplier;
  }

  // Calculate actual sleep time: baseline / speed_multiplier
  // Higher multiplier = shorter sleep = faster processing
  uint64_t sleep_ns = (uint64_t)(cfg->baseline_sleep_ns / state->current_speed_multiplier);

  // Store for debugging
  state->last_sleep_ns = sleep_ns;

  return sleep_ns;
}

void adaptive_sleep_do(adaptive_sleep_state_t *state, size_t queue_depth, size_t target_depth) {
  uint64_t sleep_ns = adaptive_sleep_calculate(state, queue_depth, target_depth);

  if (sleep_ns > 0) {
    platform_sleep_ns(sleep_ns);
  }
}

/* ============================================================================
 * Time Format Validation and Formatting Implementation
 * ============================================================================ */

/**
 * @brief Complete list of safe/supported POSIX strftime specifiers
 * These are the only specifiers we allow in format strings
 */
static const char SUPPORTED_SPECIFIERS[] = "YmmdHMSaAbBjwuIpZzcxXFTsGgV";

bool time_format_is_valid_strftime(const char *format_str) {
  if (!format_str) {
    return false;
  }

  for (const char *p = format_str; *p; p++) {
    if (*p == '%') {
      p++;
      if (!*p) {
        /* Unterminated % at end of string */
        log_error("Invalid time format: unterminated %% at end");
        return false;
      }

      if (*p == '%') {
        /* Escaped %% - this is valid, just skip it */
        continue;
      }

      /* Optional: flag characters (-, 0, +, space) */
      if (*p == '-' || *p == '0' || *p == '+' || *p == ' ') {
        p++;
        if (!*p) {
          log_error("Invalid time format: flag character without specifier");
          return false;
        }
      }

      /* Optional: width specifier (digits or *) */
      if (*p == '*' || isdigit((unsigned char)*p)) {
        while (*p && (isdigit((unsigned char)*p) || *p == '*')) {
          p++;
        }
        if (!*p) {
          log_error("Invalid time format: width specifier without specifier character");
          return false;
        }
      }

      /* Optional: precision specifier (.digits or .*) */
      if (*p == '.') {
        p++;
        if (!*p) {
          log_error("Invalid time format: precision specifier incomplete");
          return false;
        }
        while (*p && (isdigit((unsigned char)*p) || *p == '*')) {
          p++;
        }
        if (!*p) {
          log_error("Invalid time format: precision specifier without specifier character");
          return false;
        }
      }

      /* Optional: modifier characters (E for %Ex, O for %Ox) */
      if (*p == 'E' || *p == 'O') {
        p++;
        if (!*p) {
          log_error("Invalid time format: modifier character without specifier");
          return false;
        }
      }

      /* Validate that the final character is a supported specifier */
      if (!strchr(SUPPORTED_SPECIFIERS, *p)) {
        log_error("Invalid time format: unsupported specifier %%%c (at position %ld)", *p, (p - format_str));
        return false;
      }
    }
  }

  return true;
}

int time_format_now(const char *format_str, char *buf, size_t buf_size) {
  if (!format_str || !buf || buf_size < 2) {
    SET_ERRNO(ERROR_INVALID_PARAM, "time_format_now: invalid arguments - format_str=%p, buf=%p, buf_size=%zu",
              format_str, buf, buf_size);
    return 0;
  }

  /* Get current wall-clock time in nanoseconds */
  uint64_t ts_ns = time_get_realtime_ns();

  /* Extract seconds and nanoseconds */
  time_t seconds = (time_t)(ts_ns / NS_PER_SEC_INT);
  long nanoseconds = (long)(ts_ns % NS_PER_SEC_INT);

  /* Convert seconds to struct tm */
  struct tm tm_info;
  platform_localtime(&seconds, &tm_info);

  /* Format using strftime */
  size_t len = strftime(buf, buf_size, format_str, &tm_info);

  /* Check for strftime errors or buffer overflow */
  if (len == 0) {
    /* strftime returned 0 - either error or exact buffer fill */
    log_debug("strftime returned 0 for format: %s", format_str);
    return 0;
  }

  if (len >= buf_size - 1) {
    /* Buffer would overflow - strftime filled entire buffer or hit exact size */
    log_error("time_format_now: buffer too small (need %zu, have %zu)", len + 1, buf_size);
    return 0;
  }

  return (int)len;
}

asciichat_error_t time_format_safe(const char *format_str, char *buf, size_t buf_size) {
  /* Check NULL pointers */
  if (!format_str) {
    return SET_ERRNO(ERROR_INVALID_STATE, "time_format_safe: format_str is NULL");
  }

  if (!buf) {
    return SET_ERRNO(ERROR_INVALID_STATE, "time_format_safe: buf is NULL");
  }

  /* Check minimum buffer size */
  if (buf_size < 64) {
    return SET_ERRNO(ERROR_INVALID_STATE, "time_format_safe: buffer too small (minimum 64 bytes, got %zu)", buf_size);
  }

  /* Validate format string */
  if (!time_format_is_valid_strftime(format_str)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "time_format_safe: invalid time format: %s", format_str);
  }

  /* Format the time */
  int result = time_format_now(format_str, buf, buf_size);
  if (result <= 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "time_format_safe: strftime formatting failed");
  }

  return ASCIICHAT_OK;
}

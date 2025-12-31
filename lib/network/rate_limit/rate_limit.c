/**
 * @file network/rate_limit/rate_limit.c
 * @brief 🚦 Rate limiting implementation with backend abstraction
 */

#include "network/rate_limit/rate_limit.h"
#include "network/rate_limit/memory.h"
#include "network/rate_limit/sqlite.h"
#include "log/logging.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Event type strings for logging
static const char *event_type_strings[RATE_EVENT_MAX] = {
    [RATE_EVENT_SESSION_CREATE] = "session_create", [RATE_EVENT_SESSION_LOOKUP] = "session_lookup",
    [RATE_EVENT_SESSION_JOIN] = "session_join",     [RATE_EVENT_CONNECTION] = "connection",
    [RATE_EVENT_FRAME_SEND] = "frame_send",
};

// Default rate limits: conservative defaults to prevent abuse
const rate_limit_config_t DEFAULT_RATE_LIMITS[RATE_EVENT_MAX] = {
    [RATE_EVENT_SESSION_CREATE] = {.max_events = 10, .window_secs = 60}, // 10 creates per minute
    [RATE_EVENT_SESSION_LOOKUP] = {.max_events = 30, .window_secs = 60}, // 30 lookups per minute
    [RATE_EVENT_SESSION_JOIN] = {.max_events = 20, .window_secs = 60},   // 20 joins per minute
    [RATE_EVENT_CONNECTION] = {.max_events = 50, .window_secs = 60},     // 50 connections per minute
    [RATE_EVENT_FRAME_SEND] = {.max_events = 1000, .window_secs = 60},   // 1000 frames per minute
};

// ============================================================================
// Backend Interface
// ============================================================================

/**
 * @brief Rate limiter structure
 */
struct rate_limiter_s {
  const rate_limiter_backend_ops_t *ops; ///< Backend operations
  void *backend_data;                    ///< Backend-specific data
};

// ============================================================================
// Public API Implementation
// ============================================================================

rate_limiter_t *rate_limiter_create_memory(void) {
  rate_limiter_t *limiter = malloc(sizeof(rate_limiter_t));
  if (!limiter) {
    log_error("Failed to allocate rate limiter");
    return NULL;
  }

  limiter->backend_data = memory_backend_create();
  if (!limiter->backend_data) {
    free(limiter);
    return NULL;
  }

  limiter->ops = &memory_backend_ops;
  return limiter;
}

rate_limiter_t *rate_limiter_create_sqlite(const char *db_path) {
  rate_limiter_t *limiter = malloc(sizeof(rate_limiter_t));
  if (!limiter) {
    log_error("Failed to allocate rate limiter");
    return NULL;
  }

  limiter->backend_data = sqlite_backend_create(db_path);
  if (!limiter->backend_data) {
    free(limiter);
    return NULL;
  }

  limiter->ops = &sqlite_backend_ops;
  return limiter;
}

void rate_limiter_set_sqlite_db(rate_limiter_t *limiter, void *db) {
  if (!limiter || !limiter->backend_data) {
    return;
  }

  // Call the SQLite backend function to set the database handle
  sqlite_backend_set_db(limiter->backend_data, (sqlite3 *)db);
}

void rate_limiter_destroy(rate_limiter_t *limiter) {
  if (!limiter) {
    return;
  }

  if (limiter->ops && limiter->ops->destroy) {
    limiter->ops->destroy(limiter->backend_data);
  }

  free(limiter);
}

asciichat_error_t rate_limiter_check(rate_limiter_t *limiter, const char *ip_address, rate_event_type_t event_type,
                                     const rate_limit_config_t *config, bool *allowed) {
  if (!limiter || !limiter->ops || !limiter->ops->check) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rate limiter");
  }

  if (!ip_address || !allowed) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ip_address or allowed is NULL");
  }

  if (event_type >= RATE_EVENT_MAX) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid event_type: %d", event_type);
  }

  return limiter->ops->check(limiter->backend_data, ip_address, event_type, config, allowed);
}

asciichat_error_t rate_limiter_record(rate_limiter_t *limiter, const char *ip_address, rate_event_type_t event_type) {
  if (!limiter || !limiter->ops || !limiter->ops->record) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rate limiter");
  }

  if (!ip_address) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "ip_address is NULL");
  }

  if (event_type >= RATE_EVENT_MAX) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid event_type: %d", event_type);
  }

  return limiter->ops->record(limiter->backend_data, ip_address, event_type);
}

asciichat_error_t rate_limiter_cleanup(rate_limiter_t *limiter, uint32_t max_age_secs) {
  if (!limiter || !limiter->ops || !limiter->ops->cleanup) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rate limiter");
  }

  return limiter->ops->cleanup(limiter->backend_data, max_age_secs);
}

// ============================================================================
// Helper Functions (available to backends)
// ============================================================================

/**
 * @brief Get current time in milliseconds
 */
uint64_t rate_limiter_get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/**
 * @brief Get event type string for logging
 */
const char *rate_limiter_event_type_string(rate_event_type_t event_type) {
  if (event_type >= RATE_EVENT_MAX) {
    return "unknown";
  }
  return event_type_strings[event_type];
}

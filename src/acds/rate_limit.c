/**
 * @file acds/rate_limit.c
 * @brief 🚦 Rate limiting implementation
 *
 * Sliding window rate limiting with SQLite persistence.
 */

#include "acds/rate_limit.h"
#include "log/logging.h"
#include <string.h>
#include <time.h>

// Event type strings for database storage
static const char *event_type_strings[RATE_EVENT_MAX] = {
    [RATE_EVENT_SESSION_CREATE] = "session_create",
    [RATE_EVENT_SESSION_LOOKUP] = "session_lookup",
    [RATE_EVENT_SESSION_JOIN] = "session_join",
};

// Default rate limits: conservative defaults to prevent abuse
const rate_limit_config_t DEFAULT_RATE_LIMITS[RATE_EVENT_MAX] = {
    [RATE_EVENT_SESSION_CREATE] = {.max_events = 10, .window_secs = 60}, // 10 creates per minute
    [RATE_EVENT_SESSION_LOOKUP] = {.max_events = 30, .window_secs = 60}, // 30 lookups per minute
    [RATE_EVENT_SESSION_JOIN] = {.max_events = 20, .window_secs = 60},   // 20 joins per minute
};

/**
 * @brief Get current time in milliseconds
 */
static uint64_t get_current_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

asciichat_error_t rate_limit_check(sqlite3 *db, const char *ip_address, rate_event_type_t event_type,
                                   const rate_limit_config_t *config, bool *allowed) {
  if (!db || !ip_address || !allowed) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, ip_address, or allowed is NULL");
  }

  if (event_type >= RATE_EVENT_MAX) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid event_type: %d", event_type);
  }

  // Use provided config or default
  const rate_limit_config_t *limit = config ? config : &DEFAULT_RATE_LIMITS[event_type];

  // Get current time
  uint64_t now_ms = get_current_time_ms();
  uint64_t window_start_ms = now_ms - ((uint64_t)limit->window_secs * 1000);

  // Count events in the time window
  const char *sql = "SELECT COUNT(*) FROM rate_events "
                    "WHERE ip_address = ? AND event_type = ? AND timestamp >= ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare rate limit query: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_text(stmt, 1, ip_address, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, event_type_strings[event_type], -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)window_start_ms);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return SET_ERRNO(ERROR_CONFIG, "Failed to execute rate limit query: %s", sqlite3_errmsg(db));
  }

  uint32_t event_count = (uint32_t)sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  // Check if limit exceeded
  *allowed = (event_count < limit->max_events);

  if (!*allowed) {
    log_warn("Rate limit exceeded for %s (event: %s, count: %u/%u)", ip_address, event_type_strings[event_type],
             event_count, limit->max_events);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t rate_limit_record(sqlite3 *db, const char *ip_address, rate_event_type_t event_type) {
  if (!db || !ip_address) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or ip_address is NULL");
  }

  if (event_type >= RATE_EVENT_MAX) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid event_type: %d", event_type);
  }

  // Get current time
  uint64_t now_ms = get_current_time_ms();

  // Insert event
  const char *sql = "INSERT INTO rate_events (ip_address, event_type, timestamp) VALUES (?, ?, ?)";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare rate limit insert: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_text(stmt, 1, ip_address, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, event_type_strings[event_type], -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to record rate event: %s", sqlite3_errmsg(db));
  }

  log_debug("Rate event recorded: %s - %s", ip_address, event_type_strings[event_type]);
  return ASCIICHAT_OK;
}

asciichat_error_t rate_limit_cleanup(sqlite3 *db, uint32_t max_age_secs) {
  if (!db) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db is NULL");
  }

  // Default to 1 hour cleanup window
  if (max_age_secs == 0) {
    max_age_secs = 3600;
  }

  // Calculate cutoff time
  uint64_t now_ms = get_current_time_ms();
  uint64_t cutoff_ms = now_ms - ((uint64_t)max_age_secs * 1000);

  // Delete old events
  const char *sql = "DELETE FROM rate_events WHERE timestamp < ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare rate limit cleanup: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff_ms);

  rc = sqlite3_step(stmt);
  int changes = sqlite3_changes(db);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to cleanup rate events: %s", sqlite3_errmsg(db));
  }

  if (changes > 0) {
    log_debug("Cleaned up %d old rate events", changes);
  }

  return ASCIICHAT_OK;
}

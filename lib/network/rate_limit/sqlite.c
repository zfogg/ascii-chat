/**
 * @file network/rate_limit/sqlite.c
 * @brief 💾 SQLite rate limiting backend
 *
 * Persistent implementation for acds discovery server where persistence is needed.
 */

#include <ascii-chat/network/rate_limit/sqlite.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/time.h>
#include <sqlite3.h>
#include <string.h>

/**
 * @brief SQLite backend data
 */
typedef struct {
  sqlite3 *db; ///< SQLite database handle
} sqlite_backend_t;

static asciichat_error_t sqlite_check(void *backend_data, const char *ip_address, rate_event_type_t event_type,
                                      const rate_limit_config_t *config, bool *allowed) {
  sqlite_backend_t *backend = (sqlite_backend_t *)backend_data;

  // Use provided config or default
  const rate_limit_config_t *limit = config ? config : &DEFAULT_RATE_LIMITS[event_type];

  // Get current time in nanoseconds
  uint64_t now_ns = time_get_realtime_ns();
  uint64_t window_start_ns = now_ns - ((uint64_t)limit->window_secs * NS_PER_SEC_INT);

  // Count events in the time window
  const char *sql = "SELECT COUNT(*) FROM rate_events "
                    "WHERE ip_address = ? AND event_type = ? AND timestamp >= ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(backend->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare rate limit query: %s", sqlite3_errmsg(backend->db));
  }

  sqlite3_bind_text(stmt, 1, ip_address, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, rate_limiter_event_type_string(event_type), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)window_start_ns);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return SET_ERRNO(ERROR_CONFIG, "Failed to execute rate limit query: %s", sqlite3_errmsg(backend->db));
  }

  uint32_t event_count = (uint32_t)sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  // Check if limit exceeded
  *allowed = (event_count < limit->max_events);

  if (!*allowed) {
    log_warn("Rate limit exceeded for %s (event: %s, count: %u/%u)", ip_address,
             rate_limiter_event_type_string(event_type), event_count, limit->max_events);
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t sqlite_record(void *backend_data, const char *ip_address, rate_event_type_t event_type) {
  sqlite_backend_t *backend = (sqlite_backend_t *)backend_data;

  // Get current time in nanoseconds
  uint64_t now_ns = time_get_realtime_ns();

  // Insert event
  const char *sql = "INSERT INTO rate_events (ip_address, event_type, timestamp) VALUES (?, ?, ?)";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(backend->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare rate limit insert: %s", sqlite3_errmsg(backend->db));
  }

  sqlite3_bind_text(stmt, 1, ip_address, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, rate_limiter_event_type_string(event_type), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ns);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to record rate event: %s", sqlite3_errmsg(backend->db));
  }

  log_debug("Rate event recorded: %s - %s", ip_address, rate_limiter_event_type_string(event_type));
  return ASCIICHAT_OK;
}

static asciichat_error_t sqlite_cleanup(void *backend_data, uint32_t max_age_secs) {
  sqlite_backend_t *backend = (sqlite_backend_t *)backend_data;

  // Default to 1 hour cleanup window
  if (max_age_secs == 0) {
    max_age_secs = 3600;
  }

  // Calculate cutoff time in nanoseconds
  uint64_t now_ns = time_get_realtime_ns();
  uint64_t cutoff_ns = now_ns - ((uint64_t)max_age_secs * NS_PER_SEC_INT);

  // Delete old events
  const char *sql = "DELETE FROM rate_events WHERE timestamp < ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(backend->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare rate limit cleanup: %s", sqlite3_errmsg(backend->db));
  }

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff_ns);

  rc = sqlite3_step(stmt);
  int changes = sqlite3_changes(backend->db);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to cleanup rate events: %s", sqlite3_errmsg(backend->db));
  }

  if (changes > 0) {
    log_debug("Cleaned up %d old rate events", changes);
  }

  return ASCIICHAT_OK;
}

static void sqlite_destroy(void *backend_data) {
  sqlite_backend_t *backend = (sqlite_backend_t *)backend_data;
  if (!backend) {
    return;
  }

  // Note: We don't close the database here because it's owned by the caller
  // (ACDS server manages the database lifecycle)
  free(backend);
}

void *sqlite_backend_create(const char *db_path) {
  (void)db_path; // Database is provided externally, not created here

  sqlite_backend_t *backend = malloc(sizeof(sqlite_backend_t));
  if (!backend) {
    log_error("Failed to allocate SQLite backend");
    return NULL;
  }

  memset(backend, 0, sizeof(*backend));

  // Database handle will be set after creation by ACDS
  // This is a placeholder backend that will be initialized later
  log_debug("SQLite rate limiter backend allocated (database will be set externally)");
  return backend;
}

/**
 * @brief Set SQLite database handle for backend
 *
 * Called by ACDS after opening the database.
 */
void sqlite_backend_set_db(void *backend_data, sqlite3 *db) {
  sqlite_backend_t *backend = (sqlite_backend_t *)backend_data;
  if (backend) {
    backend->db = db;
  }
}

const rate_limiter_backend_ops_t sqlite_backend_ops = {
    .check = sqlite_check,
    .record = sqlite_record,
    .cleanup = sqlite_cleanup,
    .destroy = sqlite_destroy,
};

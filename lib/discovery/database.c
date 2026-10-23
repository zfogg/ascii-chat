#include <ascii-chat/otel.h>
#include <ascii-chat/stats/runtime.h>
/**
 * @file acds/database.c
 * @brief 💾 SQLite-based session management implementation
 *
 * SQLite is the single source of truth for all session data.
 * All session operations go directly to the database.
 * WAL mode provides good concurrent read performance.
 */

#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/strings.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/network/webrtc/turn_credentials.h>
#include <ascii-chat/util/time.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <sodium.h>

// ============================================================================
// SQL Query Constants (Defined Once - Compile-time string concatenation)
// ============================================================================

// Base SELECT for all session fields
#define SELECT_SESSION_BASE                                                                                            \
  "SELECT session_string, session_id, host_pubkey, password_hash, "                                                    \
  "max_participants, current_participants, capabilities, has_password, "                                               \
  "expose_ip_publicly, session_type, server_address, server_port, "                                                    \
  "created_at, expires_at, last_activity_at, initiator_id, host_established, "                                         \
  "host_participant_id, host_address, host_port, host_connection_type, "                                               \
  "in_migration, migration_start_ns FROM sessions"

// ============================================================================
// SQL schema for creating tables
// ============================================================================

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Generate random UUID (v4)
 */
#ifndef ASCIICHAT_WITH_OTEL
typedef struct {
  const char *name;
  const char *sql;
} gotel_query_t;
#endif
typedef enum {
#define DATABASE_QUERY(name, sql) QUERY_##name,
#include "queries.def"
#undef DATABASE_QUERY
} database_query_id_t;
static const gotel_query_t database_queries[] = {
#define DATABASE_QUERY(name, sql) [QUERY_##name] = {#name, sql},
#include "queries.def"
#undef DATABASE_QUERY
};

static void generate_uuid(uint8_t uuid_out[16]) {
  randombytes_buf(uuid_out, 16);
  uuid_out[6] = (uuid_out[6] & 0x0F) | 0x40; // Version 4
  uuid_out[8] = (uuid_out[8] & 0x3F) | 0x80; // RFC4122 variant
}

/**
 * @brief Get current time in milliseconds
 */
static uint64_t database_get_current_time_ms(void) {
  uint64_t current_time_ns = time_get_realtime_ns();
  return time_ns_to_ms(current_time_ns);
}

/**
 * @brief Verify password against hash
 */
static bool verify_password(const char *password, const char *hash) {
  return crypto_pwhash_str_verify(hash, password, strlen(password)) == 0;
}

/**
 * @brief Load session from SQLite row
 *
 * @param stmt Prepared statement positioned at a row
 * @return Allocated session_entry_t or NULL on error
 */
static session_entry_t *load_session_from_row(sqlite3_stmt *stmt) {
  session_entry_t *session = SAFE_MALLOC(sizeof(session_entry_t), session_entry_t *);
  if (!session) {
    return NULL;
  }
  memset(session, 0, sizeof(*session));

  // Column order from SELECT statements must match:
  // 0: session_string, 1: session_id, 2: host_pubkey, 3: password_hash,
  // 4: max_participants, 5: current_participants, 6: capabilities,
  // 7: has_password, 8: expose_ip_publicly, 9: session_type,
  // 10: server_address, 11: server_port, 12: created_at, 13: expires_at,
  // 14: last_activity_at, 15: initiator_id, 16: host_established, 17: host_participant_id,
  // 18: host_address, 19: host_port, 20: host_connection_type,
  // 21: in_migration, 22: migration_start_ns

  const char *str = (const char *)sqlite3_column_text(stmt, 0);
  if (str) {
    SAFE_STRNCPY(session->session_string, str, sizeof(session->session_string));
  }

  const void *blob = sqlite3_column_blob(stmt, 1);
  if (blob) {
    memcpy(session->session_id, blob, 16);
  }

  blob = sqlite3_column_blob(stmt, 2);
  if (blob) {
    memcpy(session->host_pubkey, blob, 32);
  }

  str = (const char *)sqlite3_column_text(stmt, 3);
  if (str) {
    SAFE_STRNCPY(session->password_hash, str, sizeof(session->password_hash));
  }

  session->max_participants = (uint8_t)sqlite3_column_int(stmt, 4);
  session->current_participants = (uint8_t)sqlite3_column_int(stmt, 5);
  session->capabilities = (uint8_t)sqlite3_column_int(stmt, 6);
  session->has_password = sqlite3_column_int(stmt, 7) != 0;
  session->expose_ip_publicly = sqlite3_column_int(stmt, 8) != 0;
  session->session_type = (uint8_t)sqlite3_column_int(stmt, 9);

  str = (const char *)sqlite3_column_text(stmt, 10);
  if (str) {
    SAFE_STRNCPY(session->server_address, str, sizeof(session->server_address));
  }

  session->server_port = (uint16_t)sqlite3_column_int(stmt, 11);
  session->created_at = (uint64_t)sqlite3_column_int64(stmt, 12);
  session->expires_at = (uint64_t)sqlite3_column_int64(stmt, 13);
  session->last_activity_at = (uint64_t)sqlite3_column_int64(stmt, 14);

  // Discovery mode host negotiation fields
  blob = sqlite3_column_blob(stmt, 15);
  if (blob) {
    memcpy(session->initiator_id, blob, 16);
  }

  session->host_established = sqlite3_column_int(stmt, 16) != 0;

  blob = sqlite3_column_blob(stmt, 17);
  if (blob) {
    memcpy(session->host_participant_id, blob, 16);
  }

  str = (const char *)sqlite3_column_text(stmt, 18);
  if (str) {
    SAFE_STRNCPY(session->host_address, str, sizeof(session->host_address));
  }

  session->host_port = (uint16_t)sqlite3_column_int(stmt, 19);
  session->host_connection_type = (uint8_t)sqlite3_column_int(stmt, 20);

  // Host migration state
  session->in_migration = sqlite3_column_int(stmt, 21) != 0;
  session->migration_start_ns = (uint64_t)sqlite3_column_int64(stmt, 22);

  return session;
}

/**
 * @brief Load participants for a session from database
 */
static void load_session_participants(sqlite3 *db, session_entry_t *session) {
  const char *sql = database_queries[QUERY_GetSessionParticipants].sql;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return;
  }

  sqlite3_bind_text(stmt, 1, session->session_string, -1, SQLITE_STATIC);

  size_t idx = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && idx < PARTICIPANT_LIMIT) {
    participant_t *p = SAFE_MALLOC(sizeof(participant_t), participant_t *);
    if (!p) {
      break;
    }
    memset(p, 0, sizeof(*p));

    const void *blob = sqlite3_column_blob(stmt, 0);
    if (blob) {
      memcpy(p->participant_id, blob, 16);
    }

    blob = sqlite3_column_blob(stmt, 1);
    if (blob) {
      memcpy(p->identity_pubkey, blob, 32);
    }

    p->joined_at = (uint64_t)sqlite3_column_int64(stmt, 2);

    session->participants[idx++] = p;
  }

  sqlite3_finalize(stmt);
}

// ============================================================================
// Database Lifecycle
// ============================================================================

static void database_telemetry_population(sqlite3 *db) {
  ASCII_OTEL_SCOPE(DATABASE_POPULATION);
  if (!ascii_otel_enabled())
    return;
  sqlite3_stmt *statement = NULL;
  if (sqlite3_prepare_v2(db, database_queries[QUERY_GetPopulation].sql, -1, &statement, NULL) != SQLITE_OK)
    return;
  sqlite3_bind_int64(statement, 1, (sqlite3_int64)database_get_current_time_ms());
  if (sqlite3_step(statement) == SQLITE_ROW) {
    stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_SESSIONS_ACTIVE, (uint64_t)sqlite3_column_int64(statement, 0));
    stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_PARTICIPANTS_ACTIVE, (uint64_t)sqlite3_column_int64(statement, 1));
  }
  sqlite3_finalize(statement);
}

asciichat_error_t database_init(const char *db_path, sqlite3 **db) {
  ASCII_OTEL_SCOPE(DATABASE_INIT);
  if (!db_path || !db) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db_path or db is NULL");
  }

  log_info("Opening database: %s", db_path);

  int rc = sqlite3_open(db_path, db);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    const char *err = sqlite3_errmsg(*db);
    sqlite3_close(*db);
    *db = NULL;
    return SET_ERRNO(ERROR_CONFIG, "Failed to open database: %s", err);
  }

#ifdef ASCIICHAT_WITH_OTEL
  ascii_otel_sqlite(*db, database_queries, sizeof(database_queries) / sizeof(database_queries[0]));
#endif
  // Enable Write-Ahead Logging for concurrent reads
  char *err_msg = NULL;
  rc = sqlite3_exec(*db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    log_warn("Failed to enable WAL mode: %s", err_msg ? err_msg : "unknown error");
    sqlite3_free(err_msg);
  }

  // Enable foreign key constraints
  rc = sqlite3_exec(*db, "PRAGMA foreign_keys=ON;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    log_error("Failed to enable foreign keys: %s", err_msg ? err_msg : "unknown error");
    sqlite3_free(err_msg);
    sqlite3_close(*db);
    *db = NULL;
    return SET_ERRNO(ERROR_CONFIG, "Failed to enable foreign keys");
  }

  // Create schema
  for (size_t query = QUERY_SchemaSessions; query <= QUERY_SchemaIdxRateEvents; ++query) {
    rc = sqlite3_exec(*db, database_queries[query].sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
      break;
  }
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    log_error("Failed to create schema: %s", err_msg ? err_msg : "unknown error");
    sqlite3_free(err_msg);
    sqlite3_close(*db);
    *db = NULL;
    return SET_ERRNO(ERROR_CONFIG, "Failed to create database schema");
  }

  database_telemetry_population(*db);
  log_info("Database initialized successfully (SQLite as single source of truth)");
  return ASCIICHAT_OK;
}

void database_close(sqlite3 *db) {
  ASCII_OTEL_SCOPE(DATABASE_CLOSE);
  if (!db) {
    return;
  }
  sqlite3_close(db);
  log_debug("Database closed");
}

// ============================================================================
// Session Operations
// ============================================================================

asciichat_error_t database_session_create(sqlite3 *db, const acip_session_create_t *req, const acds_config_t *config,
                                          acip_session_created_t *resp) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_CREATE);
  if (!db || !req || !config || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, req, config, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));

  // Generate or use reserved session string
  char session_string[ACIP_MAX_SESSION_STRING_LEN] = {0};
  if (req->reserved_string_len > 0) {
    const char *reserved_str = (const char *)(req + 1);
    size_t len = req->reserved_string_len < (ACIP_MAX_SESSION_STRING_LEN - 1) ? req->reserved_string_len
                                                                              : (ACIP_MAX_SESSION_STRING_LEN - 1);
    memcpy(session_string, reserved_str, len);
    session_string[len] = '\0';

    if (!is_session_string(session_string)) {
      asciichat_error_context_t ctx;
      if (HAS_ERRNO(&ctx)) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session string: %s (%s)", session_string, ctx.context_message);
      }
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session string: %s", session_string);
    }
  } else {
    asciichat_error_t result = acds_string_generate(session_string, sizeof(session_string));
    if (result != ASCIICHAT_OK) {
      return result;
    }
  }

  // Generate session ID
  uint8_t session_id[16];
  generate_uuid(session_id);

  // Set timestamps
  uint64_t now = database_get_current_time_ms();
  uint64_t expires_at = now + ACIP_SESSION_EXPIRATION_MS;

  // Calculate max_participants
  uint8_t max_participants = req->max_participants > 0 && req->max_participants <= PARTICIPANT_LIMIT
                                 ? req->max_participants
                                 : PARTICIPANT_LIMIT;

  // Insert into database
  const char *sql = database_queries[QUERY_CreateSession].sql;

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare session insert: %s", sqlite3_errmsg(db));
  }

  log_info("expose_ip_publicly=%d, server_address='%s' server_port=%u, session_type=%u, "
           "has_password=%u",
           req->expose_ip_publicly, req->server_address, req->server_port, req->session_type, req->has_password);
  sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, session_id, 16, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 3, req->identity_pubkey, 32, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, req->has_password ? (const char *)req->password_hash : NULL, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 5, max_participants);
  sqlite3_bind_int(stmt, 6, req->capabilities);
  sqlite3_bind_int(stmt, 7, req->has_password ? 1 : 0);
  sqlite3_bind_int(stmt, 8, req->expose_ip_publicly ? 1 : 0);
  sqlite3_bind_int(stmt, 9, req->session_type);
  sqlite3_bind_text(stmt, 10, req->server_address, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 11, req->server_port);
  sqlite3_bind_int64(stmt, 12, (sqlite3_int64)now);
  sqlite3_bind_int64(stmt, 13, (sqlite3_int64)expires_at);
  sqlite3_bind_int64(stmt, 14, (sqlite3_int64)now); // last_activity_at = created_at

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    if (rc == SQLITE_CONSTRAINT) {
      return SET_ERRNO(ERROR_INVALID_STATE, "Session string already exists: %s", session_string);
    }
    return SET_ERRNO(ERROR_CONFIG, "Failed to insert session: %s", sqlite3_errmsg(db));
  }

  // Fill response
  resp->session_string_len = (uint8_t)strlen(session_string);
  SAFE_STRNCPY(resp->session_string, session_string, sizeof(resp->session_string));
  memcpy(resp->session_id, session_id, 16);
  resp->expires_at = expires_at;
  resp->stun_count = config->stun_count;
  resp->turn_count = config->turn_count;

  log_info("Session created: %s (session_id=%02x%02x%02x%02x..., max_participants=%d, has_password=%d)", session_string,
           session_id[0], session_id[1], session_id[2], session_id[3], max_participants, req->has_password ? 1 : 0);

  database_telemetry_population(db);
  return ASCIICHAT_OK;
}

asciichat_error_t database_session_lookup(sqlite3 *db, const char *session_string, const acds_config_t *config,
                                          acip_session_info_t *resp) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_LOOKUP);
  if (!db || !session_string || !config || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, session_string, config, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));

  const char *sql = database_queries[QUERY_LookupSession].sql;

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare session lookup: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    resp->found = 0;
    log_debug("Session lookup failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  // Load session data
  resp->found = 1;

  const void *blob = sqlite3_column_blob(stmt, 1);
  if (blob) {
    memcpy(resp->session_id, blob, 16);
  }

  blob = sqlite3_column_blob(stmt, 2);
  if (blob) {
    memcpy(resp->host_pubkey, blob, 32);
  }

  resp->max_participants = (uint8_t)sqlite3_column_int(stmt, 4);
  resp->current_participants = (uint8_t)sqlite3_column_int(stmt, 5);
  resp->capabilities = (uint8_t)sqlite3_column_int(stmt, 6);
  resp->has_password = sqlite3_column_int(stmt, 7) != 0;
  resp->session_type = (uint8_t)sqlite3_column_int(stmt, 9);
  resp->created_at = (uint64_t)sqlite3_column_int64(stmt, 12);
  resp->expires_at = (uint64_t)sqlite3_column_int64(stmt, 13);

  // ACDS policy flags
  resp->require_server_verify = config->require_server_verify ? 1 : 0;
  resp->require_client_verify = config->require_client_verify ? 1 : 0;

  sqlite3_finalize(stmt);

  log_debug("Session lookup: %s (found, participants=%d/%d)", session_string, resp->current_participants,
            resp->max_participants);

  return ASCIICHAT_OK;
}

asciichat_error_t database_session_join(sqlite3 *db, const acip_session_join_t *req, const acds_config_t *config,
                                        acip_session_joined_t *resp) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_JOIN);
  if (!db || !req || !config || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, req, config, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));
  resp->success = 0;

  // Extract session string
  char session_string[ACIP_MAX_SESSION_STRING_LEN] = {0};
  size_t len = req->session_string_len < (ACIP_MAX_SESSION_STRING_LEN - 1) ? req->session_string_len
                                                                           : (ACIP_MAX_SESSION_STRING_LEN - 1);
  memcpy(session_string, req->session_string, len);
  session_string[len] = '\0';

  // Find session
  session_entry_t *session = database_session_find_by_string(db, session_string);
  if (!session) {
    ascii_otel_metric(ASCII_OTEL_METRIC_LOOKUP_MISS, 1);
    resp->error_code = ACIP_ERROR_SESSION_NOT_FOUND;
    SAFE_STRNCPY(resp->error_message, "Session not found", sizeof(resp->error_message));
    log_warn("Session join failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  // Check if session is full
  if (session->current_participants >= session->max_participants) {
    session_entry_destroy(session);
    resp->error_code = ACIP_ERROR_SESSION_FULL;
    SAFE_STRNCPY(resp->error_message, "Session is full", sizeof(resp->error_message));
    log_warn("Session join failed: %s (full)", session_string);
    return ASCIICHAT_OK;
  }

  // Verify password if required
  if (session->has_password && req->has_password) {
    if (!verify_password(req->password, session->password_hash)) {
      session_entry_destroy(session);
      resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
      SAFE_STRNCPY(resp->error_message, "Invalid password", sizeof(resp->error_message));
      log_warn("Session join failed: %s (invalid password)", session_string);
      return ASCIICHAT_OK;
    }
  } else if (session->has_password && !req->has_password) {
    session_entry_destroy(session);
    resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
    SAFE_STRNCPY(resp->error_message, "Password required", sizeof(resp->error_message));
    log_warn("Session join failed: %s (password required)", session_string);
    return ASCIICHAT_OK;
  }

  // Generate participant ID
  uint8_t participant_id[16];
  generate_uuid(participant_id);
  uint64_t now = database_get_current_time_ms();

  // Begin transaction
  char *err_msg = NULL;
  int rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    session_entry_destroy(session);
    log_error("Failed to begin transaction: %s", err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    return SET_ERRNO(ERROR_CONFIG, "Failed to begin transaction");
  }

  // Insert participant
  const char *insert_sql = database_queries[QUERY_InsertParticipant].sql;
  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    session_entry_destroy(session);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare participant insert: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, participant_id, 16, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, session->session_string, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 3, req->identity_pubkey, 32, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    session_entry_destroy(session);
    return SET_ERRNO(ERROR_CONFIG, "Failed to insert participant: %s", sqlite3_errmsg(db));
  }

  // Update participant count and last activity
  const char *update_sql = database_queries[QUERY_IncrementParticipants].sql;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
    sqlite3_bind_text(stmt, 2, session->session_string, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  // Set initiator_id if this is the first participant (for discovery mode tiebreaker)
  bool is_first_participant = (session->current_participants == 0);
  bool is_zero_initiator = true;
  for (int i = 0; i < 16; i++) {
    if (session->initiator_id[i] != 0) {
      is_zero_initiator = false;
      break;
    }
  }

  if (is_first_participant || is_zero_initiator) {
    const char *initiator_sql = database_queries[QUERY_SetSessionInitiator].sql;
    rc = sqlite3_prepare_v2(db, initiator_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
      sqlite3_bind_blob(stmt, 1, participant_id, 16, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, session->session_string, -1, SQLITE_STATIC);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      memcpy(session->initiator_id, participant_id, 16);
      log_debug("Set initiator_id to new participant %02x%02x...", participant_id[0], participant_id[1]);
    }
  }

  // Commit transaction
  rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    log_error("Failed to commit transaction: %s", err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    session_entry_destroy(session);
    return SET_ERRNO(ERROR_CONFIG, "Failed to commit transaction");
  }

  // Fill response
  resp->success = 1;
  resp->error_code = ACIP_ERROR_NONE;
  memcpy(resp->participant_id, participant_id, 16);
  memcpy(resp->session_id, session->session_id, 16);

  // Discovery mode host negotiation fields
  memcpy(resp->initiator_id, session->initiator_id, 16);
  resp->host_established = session->host_established ? 1 : 0;
  if (session->host_established) {
    memcpy(resp->host_id, session->host_participant_id, 16);
  }
  // peer_count = current_participants (excluding self) that need to negotiate
  resp->peer_count = session->current_participants; // Will be incremented after this returns
  // The transport type is not private server-address data. WebRTC clients need
  // it to choose the signaling path even while the server IP remains hidden.
  resp->session_type = session->session_type;

  log_debug("SESSION_JOIN response: session_id=%02x%02x%02x%02x..., participant_id=%02x%02x%02x%02x..., "
            "initiator=%02x%02x..., host_established=%d, peer_count=%d",
            resp->session_id[0], resp->session_id[1], resp->session_id[2], resp->session_id[3], resp->participant_id[0],
            resp->participant_id[1], resp->participant_id[2], resp->participant_id[3], resp->initiator_id[0],
            resp->initiator_id[1], resp->host_established, resp->peer_count);

  // IP disclosure logic
  log_debug("has_password=%d, expose_ip_publicly=%d, server_address='%s'", session->has_password,
            session->expose_ip_publicly, session->server_address);
  bool reveal_ip = session->has_password || session->expose_ip_publicly;
  log_debug("reveal_ip=%d", reveal_ip);

  if (reveal_ip) {
    SAFE_STRNCPY(resp->server_address, session->server_address, sizeof(resp->server_address));
    resp->server_port = session->server_port;
    // Generate TURN credentials for WebRTC sessions
    if (session->session_type == SESSION_TYPE_WEBRTC && config->turn_secret[0] != '\0') {
      turn_credentials_t turn_creds;
      asciichat_error_t turn_result =
          turn_generate_credentials(session_string, config->turn_secret, 86400, &turn_creds);
      if (turn_result == ASCIICHAT_OK) {
        SAFE_STRNCPY(resp->turn_username, turn_creds.username, sizeof(resp->turn_username));
        SAFE_STRNCPY(resp->turn_password, turn_creds.password, sizeof(resp->turn_password));
        log_debug("Generated TURN credentials for session %s", session_string);
      }
    }

    log_info("Participant joined session %s (participants=%d/%d, server=%s:%d, type=%s)", session_string,
             session->current_participants + 1, session->max_participants, resp->server_address, resp->server_port,
             session->session_type == SESSION_TYPE_WEBRTC ? "WebRTC" : "DirectTCP");
  } else {
    log_info("Participant joined session %s (participants=%d/%d, IP WITHHELD)", session_string,
             session->current_participants + 1, session->max_participants);
  }

  ascii_otel_call_session_used(session_string);

  session_entry_destroy(session);
  database_telemetry_population(db);
  return ASCIICHAT_OK;
}

asciichat_error_t database_session_leave(sqlite3 *db, const uint8_t session_id[16], const uint8_t participant_id[16]) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_LEAVE);
  if (!db || !session_id || !participant_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, session_id, or participant_id is NULL");
  }

  // Look up session_string from session_id
  char session_string[SESSION_STRING_BUFFER_SIZE] = {0};
  const char *lookup_sql = database_queries[QUERY_LookupSessionString].sql;
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, lookup_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare session lookup: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found");
  }

  const char *str = (const char *)sqlite3_column_text(stmt, 0);
  if (str) {
    SAFE_STRNCPY(session_string, str, sizeof(session_string));
  }
  sqlite3_finalize(stmt);

  if (session_string[0] == '\0') {
    return SET_ERRNO(ERROR_INVALID_STATE, "Session string is empty");
  }

  // Begin transaction
  char *err_msg = NULL;
  rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    log_error("Failed to begin transaction: %s", err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    return SET_ERRNO(ERROR_CONFIG, "Failed to begin transaction");
  }

  // Delete participant
  const char *del_sql = database_queries[QUERY_DeleteParticipant].sql;
  rc = sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare participant delete: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, participant_id, 16, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, session_string, -1, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to delete participant: %s", sqlite3_errmsg(db));
  }

  int changes = sqlite3_changes(db);
  if (changes == 0) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_INVALID_STATE, "Participant not in session");
  }

  uint64_t now = database_get_current_time_ms();

  // Decrement participant count and update last activity
  const char *update_sql = database_queries[QUERY_DecrementParticipants].sql;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
    sqlite3_bind_text(stmt, 2, session_string, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  // Check if session is now empty and delete if so
  const char *check_sql = database_queries[QUERY_GetParticipantCount].sql;
  rc = sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      int count = sqlite3_column_int(stmt, 0);

      if (count <= 0) {
        log_info("Session %s has no participants, deleting", session_string);
        sqlite3_finalize(stmt);

        // Delete empty session
        const char *del_session_sql = database_queries[QUERY_DeleteSession].sql;
        rc = sqlite3_prepare_v2(db, del_session_sql, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
          sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);
          sqlite3_step(stmt);
          sqlite3_finalize(stmt);
        }
        stmt = NULL;
      } else {
        log_info("Participant left session %s (participants=%d remaining)", session_string, count);
      }
    }
    if (stmt) {
      sqlite3_finalize(stmt);
    }
  }

  // Commit transaction
  rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    log_error("Failed to commit transaction: %s", err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to commit transaction");
  }

  database_telemetry_population(db);
  return ASCIICHAT_OK;
}

session_entry_t *database_session_find_by_id(sqlite3 *db, const uint8_t session_id[16]) {
  if (!db || !session_id) {
    return NULL;
  }

  const char *sql = database_queries[QUERY_GetSessionById].sql;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return NULL;
  }

  sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);

  session_entry_t *session = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    session = load_session_from_row(stmt);
    if (session) {
      load_session_participants(db, session);
    }
  }

  sqlite3_finalize(stmt);
  return session;
}

session_entry_t *database_session_find_by_string(sqlite3 *db, const char *session_string) {
  if (!db || !session_string) {
    return NULL;
  }

  const char *sql = database_queries[QUERY_GetSessionByString].sql;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);

  session_entry_t *session = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    session = load_session_from_row(stmt);
    if (session) {
      load_session_participants(db, session);
    }
  }

  sqlite3_finalize(stmt);
  return session;
}

void database_session_cleanup_expired(sqlite3 *db) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_CLEANUP_EXPIRED);
  if (!db) {
    return;
  }

  uint64_t now = database_get_current_time_ms();
  // 3 hours in milliseconds
  uint64_t inactivity_threshold = 3ULL * SEC_PER_HOUR * MS_PER_SEC_INT;
  uint64_t cutoff_time = now - inactivity_threshold;

  // Log sessions about to be deleted
  const char *log_sql = database_queries[QUERY_FindExpiredSessions].sql;
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, log_sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff_time);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *session_string = (const char *)sqlite3_column_text(stmt, 0);
      uint64_t last_activity = (uint64_t)sqlite3_column_int64(stmt, 1);
      uint64_t inactive_ms = now - last_activity;
      uint64_t inactive_hours = inactive_ms / (SEC_PER_HOUR * MS_PER_SEC_INT);
      log_info("Session %s inactive for %lu hours, deleting", session_string ? session_string : "<unknown>",
               inactive_hours);
    }
    sqlite3_finalize(stmt);
  }

  // Delete inactive sessions (CASCADE deletes participants)
  const char *del_sql = database_queries[QUERY_DeleteExpiredSessions].sql;
  if (sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff_time);
    sqlite3_step(stmt);
    int deleted = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (deleted > 0) {
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_SESSION_EXPIRATIONS, (uint64_t)deleted);
      log_info("Cleaned up %d inactive sessions (>3 hours)", deleted);
    }
  }
  database_telemetry_population(db);
}

asciichat_error_t database_session_update_host(sqlite3 *db, const uint8_t session_id[16],
                                               const uint8_t host_participant_id[16], const char *host_address,
                                               uint16_t host_port, uint8_t connection_type) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_UPDATE_HOST);
  if (!db || !session_id || !host_participant_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, session_id, or host_participant_id is NULL");
  }

  bool migrating = database_session_is_migration_ready(db, session_id, 0);
  uint64_t now = database_get_current_time_ms();

  const char *sql = database_queries[QUERY_UpdateHost].sql;

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare host update: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, host_participant_id, 16, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, host_address ? host_address : "", -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, host_port);
  sqlite3_bind_int(stmt, 4, connection_type);
  sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now);
  sqlite3_bind_blob(stmt, 6, session_id, 16, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to update host: %s", sqlite3_errmsg(db));
  }

  int changes = sqlite3_changes(db);
  if (changes == 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found for host update");
  }

  log_info("Session host updated: participant=%02x%02x..., address=%s:%u, type=%d", host_participant_id[0],
           host_participant_id[1], host_address ? host_address : "(none)", host_port, connection_type);

  if (migrating)
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HOST_MIGRATIONS, 1);
  return ASCIICHAT_OK;
}

asciichat_error_t database_session_clear_host(sqlite3 *db, const uint8_t session_id[16]) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_CLEAR_HOST);
  if (!db || !session_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or session_id is NULL");
  }

  const char *sql = database_queries[QUERY_ClearHost].sql;

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare host clear: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to clear host: %s", sqlite3_errmsg(db));
  }

  int changes = sqlite3_changes(db);
  if (changes == 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found for host clear");
  }

  log_info("Session host cleared (session=%02x%02x...) - ready for migration", session_id[0], session_id[1]);

  return ASCIICHAT_OK;
}

asciichat_error_t database_session_start_migration(sqlite3 *db, const uint8_t session_id[16]) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_START_MIGRATION);
  if (!db || !session_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or session_id is NULL");
  }

  uint64_t now_ns = time_get_realtime_ns();

  const char *sql = database_queries[QUERY_StartMigration].sql;

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare migration start: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ns);
  sqlite3_bind_blob(stmt, 2, session_id, 16, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to start migration: %s", sqlite3_errmsg(db));
  }

  int changes = sqlite3_changes(db);
  if (changes == 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found for migration start");
  }

  log_info("Session migration started (session=%02x%02x..., start_ns=%" PRIu64 ")", session_id[0], session_id[1],
           now_ns);

  return ASCIICHAT_OK;
}

bool database_session_is_migration_ready(sqlite3 *db, const uint8_t session_id[16], uint64_t migration_window_ms) {
  ASCII_OTEL_SCOPE(DATABASE_MIGRATION_READY);
  if (!db || !session_id) {
    return false;
  }

  uint64_t now_ns = time_get_realtime_ns();
  const char *sql = database_queries[QUERY_GetMigrationState].sql;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);

  bool ready = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    int in_migration = sqlite3_column_int(stmt, 0);
    uint64_t migration_start_ns = (uint64_t)sqlite3_column_int64(stmt, 1);

    if (in_migration) {
      uint64_t elapsed_ns = now_ns - migration_start_ns;
      uint64_t migration_window_ns = migration_window_ms * NS_PER_MS_INT;
      if (elapsed_ns >= migration_window_ns) {
        ready = true;
        log_debug("Migration window complete (session=%02x%02x..., elapsed=%lu ns, window=%lu ns)", session_id[0],
                  session_id[1], elapsed_ns, migration_window_ns);
      }
    }
  }

  sqlite3_finalize(stmt);
  return ready;
}

// ============================================================================
// Multi-Key Management Functions
// ============================================================================

asciichat_error_t database_session_add_key(sqlite3 *db, const char *session_string, const uint8_t identity_pubkey[32],
                                           uint32_t key_version) {
  ASCII_OTEL_SCOPE(DATABASE_ADD_KEY);
  if (!db || !session_string || !identity_pubkey) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for add_key");
  }

  const char *sql = database_queries[QUERY_AddSessionKey].sql;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare add_key statement: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, identity_pubkey, 32, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, key_version);
  sqlite3_bind_int64(stmt, 4, database_get_current_time_ms());

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to add session key: %s", sqlite3_errmsg(db));
  }

  log_debug("Added key to session %s (version=%u)", session_string, key_version);
  return ASCIICHAT_OK;
}

asciichat_error_t database_session_revoke_key(sqlite3 *db, const char *session_string,
                                              const uint8_t identity_pubkey[32]) {
  ASCII_OTEL_SCOPE(DATABASE_REVOKE_KEY);
  if (!db || !session_string || !identity_pubkey) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for revoke_key");
  }

  const char *sql = database_queries[QUERY_RevokeSessionKey].sql;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare revoke_key statement: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, identity_pubkey, 32, SQLITE_STATIC);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to revoke session key: %s", sqlite3_errmsg(db));
  }

  log_debug("Revoked key from session %s", session_string);
  return ASCIICHAT_OK;
}

bool database_session_verify_key(sqlite3 *db, const char *session_string, const uint8_t identity_pubkey[32]) {
  ASCII_OTEL_SCOPE(DATABASE_VERIFY_KEY);
  if (!db || !session_string || !identity_pubkey) {
    return false;
  }

  const char *sql = database_queries[QUERY_VerifySessionKey].sql;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, identity_pubkey, 32, SQLITE_STATIC);

  bool valid = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);

  return valid;
}

asciichat_error_t database_session_get_keys(sqlite3 *db, const char *session_string, uint8_t (*keys_out)[32],
                                            size_t max_keys, size_t *count_out) {
  ASCII_OTEL_SCOPE(DATABASE_GET_KEYS);
  if (!db || !session_string || !keys_out || !count_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for get_keys");
  }

  const char *sql = database_queries[QUERY_GetSessionKeys].sql;

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare get_keys statement: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);

  size_t count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && count < max_keys) {
    const void *blob = sqlite3_column_blob(stmt, 0);
    if (blob && sqlite3_column_bytes(stmt, 0) == 32) {
      memcpy(keys_out[count], blob, 32);
      count++;
    }
  }

  sqlite3_finalize(stmt);
  *count_out = count;

  log_debug("Retrieved %zu keys for session %s", count, session_string);
  return ASCIICHAT_OK;
}

// ============================================================================
// Statistics Queries
// ============================================================================

asciichat_error_t database_stats_session_count(sqlite3 *db, int64_t *count_out) {
  ASCII_OTEL_SCOPE(DATABASE_SESSION_COUNT);
  if (!db || !count_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or count_out is NULL");
  }

  const char *query = database_queries[QUERY_CountSessions].sql;
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare statement: %s", sqlite3_errmsg(db));
  }

  time_t now = time(NULL);
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);

  int64_t count = 0;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  if (rc == SQLITE_ROW) {
    count = sqlite3_column_int64(stmt, 0);
  } else if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return SET_ERRNO(ERROR_CONFIG, "Failed to execute query: %s", sqlite3_errmsg(db));
  }

  sqlite3_finalize(stmt);
  *count_out = count;

  log_debug("Active session count: %lld", (long long)count);
  return ASCIICHAT_OK;
}

asciichat_error_t database_stats_unique_clients(sqlite3 *db, int64_t *count_out) {
  ASCII_OTEL_SCOPE(DATABASE_UNIQUE_CLIENTS);
  if (!db || !count_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or count_out is NULL");
  }

  const char *query = database_queries[QUERY_CountUniqueClients].sql;
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
      ascii_otel_metric(ASCII_OTEL_METRIC_DB_BUSY, 1);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare statement: %s", sqlite3_errmsg(db));
  }

  int64_t count = 0;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE && !sqlite3_stmt_readonly(stmt))
    ascii_otel_metric(ASCII_OTEL_METRIC_DB_ROWS, sqlite3_changes(db));
  if (rc == SQLITE_ROW) {
    count = sqlite3_column_int64(stmt, 0);
  } else if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return SET_ERRNO(ERROR_CONFIG, "Failed to execute query: %s", sqlite3_errmsg(db));
  }

  sqlite3_finalize(stmt);
  *count_out = count;

  log_debug("Unique clients count: %lld", (long long)count);
  return ASCIICHAT_OK;
}

asciichat_error_t database_stats_snapshot(sqlite3 *db, database_session_stats_t *rows, size_t capacity,
                                          size_t *row_count, uint64_t *sessions, uint64_t *participants) {
  ASCII_OTEL_SCOPE(DATABASE_STATS_SNAPSHOT);
  if (!db || (!rows && capacity) || !row_count || !sessions || !participants)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid database stats snapshot");
  *row_count = 0;
  *sessions = *participants = 0;
  sqlite3_stmt *stmt = NULL;
  const char *sql = database_queries[QUERY_GetStatsSnapshot].sql;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return SET_ERRNO(ERROR_CONFIG, "Cannot query active session statistics");
  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)(time_get_realtime_ns() / NS_PER_MS_INT));
  int result;
  while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
    (*sessions)++;
    int count = sqlite3_column_int(stmt, 1);
    if (count > 0)
      *participants += (uint64_t)count;
    if (*row_count < capacity) {
      database_session_stats_t *row = &rows[(*row_count)++];
      snprintf(row->session_string, sizeof(row->session_string), "%s", (const char *)sqlite3_column_text(stmt, 0));
      row->participants = count > 0 ? (uint32_t)count : 0;
      row->host_ready = sqlite3_column_int(stmt, 2) != 0;
      row->migrating = sqlite3_column_int(stmt, 3) != 0;
    }
  }
  sqlite3_finalize(stmt);
  if (result != SQLITE_DONE)
    return SET_ERRNO(ERROR_CONFIG, "Active session statistics query failed");
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_SESSIONS_ACTIVE, *sessions);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_PARTICIPANTS_ACTIVE, *participants);
  return ASCIICHAT_OK;
}

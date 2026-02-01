/**
 * @file acds/database.c
 * @brief 💾 SQLite-based session management implementation
 *
 * SQLite is the single source of truth for all session data.
 * All session operations go directly to the database.
 * WAL mode provides good concurrent read performance.
 */

#include "discovery/database.h"
#include "discovery/strings.h"
#include "log/logging.h"
#include "network/webrtc/turn_credentials.h"
#include "util/time.h"
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <sodium.h>

// ============================================================================
// SQL Query Constants (Defined Once - Compile-time string concatenation)
// ============================================================================

// Base SELECT for all session fields
#define SELECT_SESSION_BASE                                                                                            \
  "SELECT session_id, session_string, host_pubkey, password_hash, "                                                    \
  "max_participants, current_participants, capabilities, has_password, "                                               \
  "expose_ip_publicly, session_type, server_address, server_port, "                                                    \
  "created_at, expires_at, initiator_id, host_established, "                                                           \
  "host_participant_id, host_address, host_port, host_connection_type, "                                               \
  "in_migration, migration_start_ms FROM sessions"

// ============================================================================
// SQL schema for creating tables
// ============================================================================

static const char *schema_sql =
    // Sessions table (extended schema with all fields)
    "CREATE TABLE IF NOT EXISTS sessions ("
    "  session_id BLOB PRIMARY KEY,"
    "  session_string TEXT UNIQUE NOT NULL,"
    "  host_pubkey BLOB NOT NULL,"
    "  password_hash TEXT,"
    "  max_participants INTEGER DEFAULT 4,"
    "  current_participants INTEGER DEFAULT 0,"
    "  capabilities INTEGER DEFAULT 3," // video + audio
    "  has_password INTEGER DEFAULT 0,"
    "  expose_ip_publicly INTEGER DEFAULT 0,"
    "  session_type INTEGER DEFAULT 0,"
    "  server_address TEXT,"
    "  server_port INTEGER DEFAULT 0,"
    "  created_at INTEGER NOT NULL,"
    "  expires_at INTEGER NOT NULL,"
    // Discovery mode host negotiation fields
    "  initiator_id BLOB,"                      // First participant (for tiebreaker)
    "  host_established INTEGER DEFAULT 0,"     // 0=negotiating, 1=host designated
    "  host_participant_id BLOB,"               // Current host's participant_id
    "  host_address TEXT,"                      // Host's reachable address
    "  host_port INTEGER DEFAULT 0,"            // Host's port
    "  host_connection_type INTEGER DEFAULT 0," // How to reach host
    // Host migration state
    "  in_migration INTEGER DEFAULT 0,"      // 0=not migrating, 1=collecting HOST_LOST packets
    "  migration_start_ms INTEGER DEFAULT 0" // When migration started (ms since epoch)
    ");"

    // Participants table
    "CREATE TABLE IF NOT EXISTS participants ("
    "  participant_id BLOB PRIMARY KEY,"
    "  session_id BLOB NOT NULL,"
    "  identity_pubkey BLOB NOT NULL,"
    "  joined_at INTEGER NOT NULL,"
    "  FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE"
    ");"

    // Rate limiting events
    "CREATE TABLE IF NOT EXISTS rate_events ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  ip_address TEXT NOT NULL,"
    "  event_type TEXT NOT NULL,"
    "  timestamp INTEGER NOT NULL"
    ");"

    // Indexes for efficient queries
    "CREATE INDEX IF NOT EXISTS idx_sessions_string ON sessions(session_string);"
    "CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);"
    "CREATE INDEX IF NOT EXISTS idx_participants_session ON participants(session_id);"
    "CREATE INDEX IF NOT EXISTS idx_rate_events ON rate_events(ip_address, event_type, timestamp);";

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Generate random UUID (v4)
 */
static void generate_uuid(uint8_t uuid_out[16]) {
  randombytes_buf(uuid_out, 16);
  uuid_out[6] = (uuid_out[6] & 0x0F) | 0x40; // Version 4
  uuid_out[8] = (uuid_out[8] & 0x3F) | 0x80; // RFC4122 variant
}

/**
 * @brief Get current time in milliseconds
 */
static uint64_t get_current_time_ms(void) {
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
  // 0: session_id, 1: session_string, 2: host_pubkey, 3: password_hash,
  // 4: max_participants, 5: current_participants, 6: capabilities,
  // 7: has_password, 8: expose_ip_publicly, 9: session_type,
  // 10: server_address, 11: server_port, 12: created_at, 13: expires_at,
  // 14: initiator_id, 15: host_established, 16: host_participant_id,
  // 17: host_address, 18: host_port, 19: host_connection_type,
  // 20: in_migration, 21: migration_start_ms

  const void *blob = sqlite3_column_blob(stmt, 0);
  if (blob) {
    memcpy(session->session_id, blob, 16);
  }

  const char *str = (const char *)sqlite3_column_text(stmt, 1);
  if (str) {
    SAFE_STRNCPY(session->session_string, str, sizeof(session->session_string));
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

  // Discovery mode host negotiation fields
  blob = sqlite3_column_blob(stmt, 14);
  if (blob) {
    memcpy(session->initiator_id, blob, 16);
  }

  session->host_established = sqlite3_column_int(stmt, 15) != 0;

  blob = sqlite3_column_blob(stmt, 16);
  if (blob) {
    memcpy(session->host_participant_id, blob, 16);
  }

  str = (const char *)sqlite3_column_text(stmt, 17);
  if (str) {
    SAFE_STRNCPY(session->host_address, str, sizeof(session->host_address));
  }

  session->host_port = (uint16_t)sqlite3_column_int(stmt, 18);
  session->host_connection_type = (uint8_t)sqlite3_column_int(stmt, 19);

  // Host migration state
  session->in_migration = sqlite3_column_int(stmt, 20) != 0;
  session->migration_start_ms = (uint64_t)sqlite3_column_int64(stmt, 21);

  return session;
}

/**
 * @brief Load participants for a session from database
 */
static void load_session_participants(sqlite3 *db, session_entry_t *session) {
  static const char *sql = "SELECT participant_id, identity_pubkey, joined_at "
                           "FROM participants WHERE session_id = ?";

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return;
  }

  sqlite3_bind_blob(stmt, 1, session->session_id, 16, SQLITE_STATIC);

  size_t idx = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && idx < MAX_PARTICIPANTS) {
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

asciichat_error_t database_init(const char *db_path, sqlite3 **db) {
  if (!db_path || !db) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db_path or db is NULL");
  }

  log_info("Opening database: %s", db_path);

  int rc = sqlite3_open(db_path, db);
  if (rc != SQLITE_OK) {
    const char *err = sqlite3_errmsg(*db);
    sqlite3_close(*db);
    *db = NULL;
    return SET_ERRNO(ERROR_CONFIG, "Failed to open database: %s", err);
  }

  // Enable Write-Ahead Logging for concurrent reads
  char *err_msg = NULL;
  rc = sqlite3_exec(*db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    log_warn("Failed to enable WAL mode: %s", err_msg ? err_msg : "unknown error");
    sqlite3_free(err_msg);
  }

  // Enable foreign key constraints
  rc = sqlite3_exec(*db, "PRAGMA foreign_keys=ON;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    log_error("Failed to enable foreign keys: %s", err_msg ? err_msg : "unknown error");
    sqlite3_free(err_msg);
    sqlite3_close(*db);
    *db = NULL;
    return SET_ERRNO(ERROR_CONFIG, "Failed to enable foreign keys");
  }

  // Create schema
  rc = sqlite3_exec(*db, schema_sql, NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    log_error("Failed to create schema: %s", err_msg ? err_msg : "unknown error");
    sqlite3_free(err_msg);
    sqlite3_close(*db);
    *db = NULL;
    return SET_ERRNO(ERROR_CONFIG, "Failed to create database schema");
  }

  log_info("Database initialized successfully (SQLite as single source of truth)");
  return ASCIICHAT_OK;
}

void database_close(sqlite3 *db) {
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

    if (!acds_string_validate(session_string)) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session string format: %s", session_string);
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
  uint64_t now = get_current_time_ms();
  uint64_t expires_at = now + ACIP_SESSION_EXPIRATION_MS;

  // Calculate max_participants
  uint8_t max_participants =
      req->max_participants > 0 && req->max_participants <= MAX_PARTICIPANTS ? req->max_participants : MAX_PARTICIPANTS;

  // Insert into database
  const char *sql = "INSERT INTO sessions "
                    "(session_id, session_string, host_pubkey, password_hash, max_participants, "
                    "current_participants, capabilities, has_password, expose_ip_publicly, "
                    "session_type, server_address, server_port, created_at, expires_at) "
                    "VALUES (?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?, ?, ?, ?)";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare session insert: %s", sqlite3_errmsg(db));
  }

  log_info("DATABASE_SESSION_CREATE: expose_ip_publicly=%d, server_address='%s' server_port=%u, session_type=%u, "
           "has_password=%u",
           req->expose_ip_publicly, req->server_address, req->server_port, req->session_type, req->has_password);
  sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, session_string, -1, SQLITE_STATIC);
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

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
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

  return ASCIICHAT_OK;
}

asciichat_error_t database_session_lookup(sqlite3 *db, const char *session_string, const acds_config_t *config,
                                          acip_session_info_t *resp) {
  if (!db || !session_string || !config || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, session_string, config, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));

  const char *sql = "SELECT session_id, session_string, host_pubkey, password_hash, "
                    "max_participants, current_participants, capabilities, has_password, "
                    "expose_ip_publicly, session_type, server_address, server_port, "
                    "created_at, expires_at FROM sessions WHERE session_string = ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare session lookup: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_text(stmt, 1, session_string, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    resp->found = 0;
    log_debug("Session lookup failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  // Load session data
  resp->found = 1;

  const void *blob = sqlite3_column_blob(stmt, 0);
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
    resp->error_code = ACIP_ERROR_SESSION_NOT_FOUND;
    SAFE_STRNCPY(resp->error_message, "Session not found", sizeof(resp->error_message));
    log_warn("Session join failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  // Check if session is full
  if (session->current_participants >= session->max_participants) {
    session_entry_free(session);
    resp->error_code = ACIP_ERROR_SESSION_FULL;
    SAFE_STRNCPY(resp->error_message, "Session is full", sizeof(resp->error_message));
    log_warn("Session join failed: %s (full)", session_string);
    return ASCIICHAT_OK;
  }

  // Verify password if required
  if (session->has_password && req->has_password) {
    if (!verify_password(req->password, session->password_hash)) {
      session_entry_free(session);
      resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
      SAFE_STRNCPY(resp->error_message, "Invalid password", sizeof(resp->error_message));
      log_warn("Session join failed: %s (invalid password)", session_string);
      return ASCIICHAT_OK;
    }
  } else if (session->has_password && !req->has_password) {
    session_entry_free(session);
    resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
    SAFE_STRNCPY(resp->error_message, "Password required", sizeof(resp->error_message));
    log_warn("Session join failed: %s (password required)", session_string);
    return ASCIICHAT_OK;
  }

  // Generate participant ID
  uint8_t participant_id[16];
  generate_uuid(participant_id);
  uint64_t now = get_current_time_ms();

  // Begin transaction
  char *err_msg = NULL;
  int rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    session_entry_free(session);
    log_error("Failed to begin transaction: %s", err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    return SET_ERRNO(ERROR_CONFIG, "Failed to begin transaction");
  }

  // Insert participant
  const char *insert_sql = "INSERT INTO participants (participant_id, session_id, identity_pubkey, joined_at) "
                           "VALUES (?, ?, ?, ?)";
  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    session_entry_free(session);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare participant insert: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, participant_id, 16, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, session->session_id, 16, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 3, req->identity_pubkey, 32, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    session_entry_free(session);
    return SET_ERRNO(ERROR_CONFIG, "Failed to insert participant: %s", sqlite3_errmsg(db));
  }

  // Update participant count
  const char *update_sql = "UPDATE sessions SET current_participants = current_participants + 1 "
                           "WHERE session_id = ?";
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_bind_blob(stmt, 1, session->session_id, 16, SQLITE_STATIC);
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
    const char *initiator_sql = "UPDATE sessions SET initiator_id = ? WHERE session_id = ?";
    rc = sqlite3_prepare_v2(db, initiator_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
      sqlite3_bind_blob(stmt, 1, participant_id, 16, SQLITE_STATIC);
      sqlite3_bind_blob(stmt, 2, session->session_id, 16, SQLITE_STATIC);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      memcpy(session->initiator_id, participant_id, 16);
      log_debug("Set initiator_id to new participant %02x%02x...", participant_id[0], participant_id[1]);
    }
  }

  // Commit transaction
  rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    log_error("Failed to commit transaction: %s", err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    session_entry_free(session);
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

  log_debug("SESSION_JOIN response: session_id=%02x%02x%02x%02x..., participant_id=%02x%02x%02x%02x..., "
            "initiator=%02x%02x..., host_established=%d, peer_count=%d",
            resp->session_id[0], resp->session_id[1], resp->session_id[2], resp->session_id[3], resp->participant_id[0],
            resp->participant_id[1], resp->participant_id[2], resp->participant_id[3], resp->initiator_id[0],
            resp->initiator_id[1], resp->host_established, resp->peer_count);

  // IP disclosure logic
  bool reveal_ip = false;
  log_info("DATABASE_SESSION_JOIN: has_password=%d, expose_ip_publicly=%d, server_address='%s'", session->has_password,
           session->expose_ip_publicly, session->server_address);
  if (session->has_password) {
    reveal_ip = true; // Password was verified
  } else if (session->expose_ip_publicly) {
    reveal_ip = true; // Explicit opt-in
  }
  log_info("DATABASE_SESSION_JOIN: reveal_ip=%d", reveal_ip);

  if (reveal_ip) {
    SAFE_STRNCPY(resp->server_address, session->server_address, sizeof(resp->server_address));
    resp->server_port = session->server_port;
    resp->session_type = session->session_type;

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

  session_entry_free(session);
  return ASCIICHAT_OK;
}

asciichat_error_t database_session_leave(sqlite3 *db, const uint8_t session_id[16], const uint8_t participant_id[16]) {
  if (!db || !session_id || !participant_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, session_id, or participant_id is NULL");
  }

  // Begin transaction
  char *err_msg = NULL;
  int rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    log_error("Failed to begin transaction: %s", err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    return SET_ERRNO(ERROR_CONFIG, "Failed to begin transaction");
  }

  // Delete participant
  const char *del_sql = "DELETE FROM participants WHERE participant_id = ? AND session_id = ?";
  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare participant delete: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, participant_id, 16, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, session_id, 16, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to delete participant: %s", sqlite3_errmsg(db));
  }

  int changes = sqlite3_changes(db);
  if (changes == 0) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_INVALID_STATE, "Participant not in session");
  }

  // Decrement participant count
  const char *update_sql = "UPDATE sessions SET current_participants = current_participants - 1 "
                           "WHERE session_id = ? AND current_participants > 0";
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  // Check if session is now empty and delete if so
  const char *check_sql = "SELECT current_participants, session_string FROM sessions WHERE session_id = ?";
  rc = sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      int count = sqlite3_column_int(stmt, 0);
      const char *session_string = (const char *)sqlite3_column_text(stmt, 1);

      if (count <= 0) {
        log_info("Session %s has no participants, deleting", session_string ? session_string : "<unknown>");
        sqlite3_finalize(stmt);

        // Delete empty session
        const char *del_session_sql = "DELETE FROM sessions WHERE session_id = ?";
        rc = sqlite3_prepare_v2(db, del_session_sql, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
          sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);
          sqlite3_step(stmt);
          sqlite3_finalize(stmt);
        }
        stmt = NULL;
      } else {
        log_info("Participant left session (participants=%d remaining)", count);
      }
    }
    if (stmt) {
      sqlite3_finalize(stmt);
    }
  }

  // Commit transaction
  rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    log_error("Failed to commit transaction: %s", err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to commit transaction");
  }

  return ASCIICHAT_OK;
}

session_entry_t *database_session_find_by_id(sqlite3 *db, const uint8_t session_id[16]) {
  if (!db || !session_id) {
    return NULL;
  }

  static const char sql[] = SELECT_SESSION_BASE " WHERE session_id = ?";

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

  static const char sql[] = SELECT_SESSION_BASE " WHERE session_string = ?";

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
  if (!db) {
    return;
  }

  uint64_t now = get_current_time_ms();

  // Log sessions about to be deleted
  const char *log_sql = "SELECT session_string FROM sessions WHERE expires_at < ?";
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, log_sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *session_string = (const char *)sqlite3_column_text(stmt, 0);
      log_info("Session %s expired, deleting", session_string ? session_string : "<unknown>");
    }
    sqlite3_finalize(stmt);
  }

  // Delete expired sessions (CASCADE deletes participants)
  const char *del_sql = "DELETE FROM sessions WHERE expires_at < ?";
  if (sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
    sqlite3_step(stmt);
    int deleted = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (deleted > 0) {
      log_info("Cleaned up %d expired sessions", deleted);
    }
  }
}

asciichat_error_t database_session_update_host(sqlite3 *db, const uint8_t session_id[16],
                                               const uint8_t host_participant_id[16], const char *host_address,
                                               uint16_t host_port, uint8_t connection_type) {
  if (!db || !session_id || !host_participant_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db, session_id, or host_participant_id is NULL");
  }

  const char *sql = "UPDATE sessions SET "
                    "host_established = 1, "
                    "host_participant_id = ?, "
                    "host_address = ?, "
                    "host_port = ?, "
                    "host_connection_type = ? "
                    "WHERE session_id = ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare host update: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, host_participant_id, 16, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, host_address ? host_address : "", -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, host_port);
  sqlite3_bind_int(stmt, 4, connection_type);
  sqlite3_bind_blob(stmt, 5, session_id, 16, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to update host: %s", sqlite3_errmsg(db));
  }

  int changes = sqlite3_changes(db);
  if (changes == 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found for host update");
  }

  log_info("Session host updated: participant=%02x%02x..., address=%s:%u, type=%d", host_participant_id[0],
           host_participant_id[1], host_address ? host_address : "(none)", host_port, connection_type);

  return ASCIICHAT_OK;
}

asciichat_error_t database_session_clear_host(sqlite3 *db, const uint8_t session_id[16]) {
  if (!db || !session_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or session_id is NULL");
  }

  const char *sql = "UPDATE sessions SET "
                    "host_established = 0, "
                    "host_participant_id = NULL, "
                    "host_address = NULL, "
                    "host_port = 0, "
                    "host_connection_type = 0 "
                    "WHERE session_id = ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare host clear: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
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
  if (!db || !session_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or session_id is NULL");
  }

  uint64_t now = get_current_time_ms();

  const char *sql = "UPDATE sessions SET "
                    "in_migration = 1, "
                    "migration_start_ms = ? "
                    "WHERE session_id = ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare migration start: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
  sqlite3_bind_blob(stmt, 2, session_id, 16, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to start migration: %s", sqlite3_errmsg(db));
  }

  int changes = sqlite3_changes(db);
  if (changes == 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found for migration start");
  }

  log_info("Session migration started (session=%02x%02x..., start_ms=%" PRIu64 ")", session_id[0], session_id[1], now);

  return ASCIICHAT_OK;
}

bool database_session_is_migration_ready(sqlite3 *db, const uint8_t session_id[16], uint64_t migration_window_ms) {
  if (!db || !session_id) {
    return false;
  }

  uint64_t now = get_current_time_ms();
  const char *sql = "SELECT in_migration, migration_start_ms FROM sessions WHERE session_id = ?";

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);

  bool ready = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    int in_migration = sqlite3_column_int(stmt, 0);
    uint64_t migration_start_ms = (uint64_t)sqlite3_column_int64(stmt, 1);

    if (in_migration) {
      uint64_t elapsed = now - migration_start_ms;
      if (elapsed >= migration_window_ms) {
        ready = true;
        log_debug("Migration window complete (session=%02x%02x..., elapsed=%" PRIu64 "ms, window=%" PRIu64 "ms)",
                  session_id[0], session_id[1], elapsed, migration_window_ms);
      }
    }
  }

  sqlite3_finalize(stmt);
  return ready;
}

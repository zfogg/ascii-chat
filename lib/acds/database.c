/**
 * @file acds/database.c
 * @brief 💾 SQLite persistence implementation
 *
 * Provides SQLite persistence for sessions, participants, and rate limiting.
 * Sessions are saved on creation and loaded on startup for crash recovery.
 *
 * RCU Integration:
 * - Session loading uses RCU lock-free hash table (cds_lfht)
 * - No global locks needed - RCU read-side provides synchronization
 */

#include "acds/database.h"
#include "log/logging.h"
#include "acds/session.h"
#include <string.h>
#include <time.h>
#include <urcu.h>
#include <urcu/rculfhash.h>

// SQL schema for creating tables
static const char *schema_sql =
    // Sessions table
    "CREATE TABLE IF NOT EXISTS sessions ("
    "  session_id BLOB PRIMARY KEY,"
    "  session_string TEXT UNIQUE NOT NULL,"
    "  host_pubkey BLOB NOT NULL,"
    "  password_hash TEXT,"
    "  max_participants INTEGER DEFAULT 4,"
    "  capabilities INTEGER DEFAULT 3," // video + audio
    "  created_at INTEGER NOT NULL,"
    "  expires_at INTEGER NOT NULL"
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

asciichat_error_t database_init(const char *db_path, sqlite3 **db) {
  if (!db_path || !db) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db_path or db is NULL");
  }

  log_info("Opening database: %s", db_path);

  // Open database
  int rc = sqlite3_open(db_path, db);
  if (rc != SQLITE_OK) {
    const char *err = sqlite3_errmsg(*db);
    sqlite3_close(*db);
    *db = NULL;
    return SET_ERRNO(ERROR_CONFIG, "Failed to open database: %s", err);
  }

  // Enable Write-Ahead Logging for better concurrency
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

  log_info("Database initialized successfully");
  return ASCIICHAT_OK;
}

asciichat_error_t database_load_sessions(sqlite3 *db, session_registry_t *registry) {
  if (!db || !registry) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or registry is NULL");
  }

  // Get current time
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t now = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;

  // Prepare statement to load non-expired sessions
  const char *sql = "SELECT session_id, session_string, host_pubkey, password_hash, "
                    "max_participants, capabilities, created_at, expires_at "
                    "FROM sessions WHERE expires_at > ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare session load query: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);

  size_t loaded_count = 0;

  // Iterate through sessions
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    // Allocate session entry
    session_entry_t *session = SAFE_MALLOC(sizeof(session_entry_t), session_entry_t *);
    if (!session) {
      sqlite3_finalize(stmt);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate session entry");
    }

    memset(session, 0, sizeof(*session));

    // Load session data
    memcpy(session->session_id, sqlite3_column_blob(stmt, 0), 16);
    const char *session_string = (const char *)sqlite3_column_text(stmt, 1);
    SAFE_STRNCPY(session->session_string, session_string, sizeof(session->session_string));
    memcpy(session->host_pubkey, sqlite3_column_blob(stmt, 2), 32);

    const char *password_hash = (const char *)sqlite3_column_text(stmt, 3);
    if (password_hash) {
      SAFE_STRNCPY(session->password_hash, password_hash, sizeof(session->password_hash));
      session->has_password = true;
    } else {
      session->has_password = false;
    }

    session->max_participants = (uint8_t)sqlite3_column_int(stmt, 4);
    session->capabilities = (uint8_t)sqlite3_column_int(stmt, 5);
    session->created_at = (uint64_t)sqlite3_column_int64(stmt, 6);
    session->expires_at = (uint64_t)sqlite3_column_int64(stmt, 7);
    session->current_participants = 0; // Will be updated when loading participants

    // Load participants for this session
    sqlite3_stmt *part_stmt = NULL;
    const char *part_sql = "SELECT participant_id, identity_pubkey, joined_at "
                           "FROM participants WHERE session_id = ?";

    rc = sqlite3_prepare_v2(db, part_sql, -1, &part_stmt, NULL);
    if (rc == SQLITE_OK) {
      sqlite3_bind_blob(part_stmt, 1, session->session_id, 16, SQLITE_STATIC);

      size_t part_idx = 0;
      while ((rc = sqlite3_step(part_stmt)) == SQLITE_ROW && part_idx < MAX_PARTICIPANTS) {
        participant_t *participant = SAFE_MALLOC(sizeof(participant_t), participant_t *);
        if (participant) {
          memcpy(participant->participant_id, sqlite3_column_blob(part_stmt, 0), 16);
          memcpy(participant->identity_pubkey, sqlite3_column_blob(part_stmt, 1), 32);
          participant->joined_at = (uint64_t)sqlite3_column_int64(part_stmt, 2);

          session->participants[part_idx] = participant;
          session->current_participants++;
          part_idx++;
        }
      }

      sqlite3_finalize(part_stmt);
    }

    /* Add to RCU hash table (no lock needed)
       RCU provides automatic synchronization for concurrent readers */
    unsigned long hash = 5381; // DJB2 hash
    const char *str = session->session_string;
    int c;
    while ((c = (unsigned char)*str++)) {
      hash = ((hash << 5) + hash) + c;
    }

    cds_lfht_node_init(&session->hash_node);
    struct cds_lfht_node *ret_node = cds_lfht_add_unique(registry->sessions, hash,
                                                         /* match callback */
                                                         NULL, // Using NULL match for now (will use default equality)
                                                         session->session_string, &session->hash_node);

    if (ret_node != &session->hash_node) {
      /* Session already exists - free this duplicate */
      for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
        SAFE_FREE(session->participants[i]);
      }
      SAFE_FREE(session);
      log_warn("Duplicate session in database: %s (skipping)", session_string);
      continue;
    }

    loaded_count++;
  }

  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    log_warn("Session loading ended with status %d: %s", rc, sqlite3_errmsg(db));
  }

  log_info("Loaded %zu sessions from database", loaded_count);
  return ASCIICHAT_OK;
}

asciichat_error_t database_save_session(sqlite3 *db, const session_entry_t *session) {
  if (!db || !session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or session is NULL");
  }

  // Begin transaction
  char *err_msg = NULL;
  int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    log_error("Failed to begin transaction: %s", err_msg ? err_msg : "unknown error");
    sqlite3_free(err_msg);
    return SET_ERRNO(ERROR_CONFIG, "Failed to begin transaction");
  }

  // Insert or replace session
  const char *sql = "INSERT OR REPLACE INTO sessions "
                    "(session_id, session_string, host_pubkey, password_hash, "
                    "max_participants, capabilities, created_at, expires_at) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";

  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare session save query: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, session->session_id, 16, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, session->session_string, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 3, session->host_pubkey, 32, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, session->has_password ? session->password_hash : NULL, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 5, session->max_participants);
  sqlite3_bind_int(stmt, 6, session->capabilities);
  sqlite3_bind_int64(stmt, 7, (sqlite3_int64)session->created_at);
  sqlite3_bind_int64(stmt, 8, (sqlite3_int64)session->expires_at);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to save session: %s", sqlite3_errmsg(db));
  }

  // Delete old participants (will re-insert current ones)
  const char *del_sql = "DELETE FROM participants WHERE session_id = ?";
  rc = sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_bind_blob(stmt, 1, session->session_id, 16, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  // Insert participants
  const char *part_sql = "INSERT INTO participants (participant_id, session_id, identity_pubkey, joined_at) "
                         "VALUES (?, ?, ?, ?)";

  for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
    if (session->participants[i]) {
      rc = sqlite3_prepare_v2(db, part_sql, -1, &stmt, NULL);
      if (rc != SQLITE_OK) {
        continue;
      }

      sqlite3_bind_blob(stmt, 1, session->participants[i]->participant_id, 16, SQLITE_STATIC);
      sqlite3_bind_blob(stmt, 2, session->session_id, 16, SQLITE_STATIC);
      sqlite3_bind_blob(stmt, 3, session->participants[i]->identity_pubkey, 32, SQLITE_STATIC);
      sqlite3_bind_int64(stmt, 4, (sqlite3_int64)session->participants[i]->joined_at);

      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }
  }

  // Commit transaction
  rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    log_error("Failed to commit transaction: %s", err_msg ? err_msg : "unknown error");
    sqlite3_free(err_msg);
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return SET_ERRNO(ERROR_CONFIG, "Failed to commit transaction");
  }

  log_debug("Session %s saved to database", session->session_string);
  return ASCIICHAT_OK;
}

asciichat_error_t database_delete_session(sqlite3 *db, const uint8_t session_id[16]) {
  if (!db || !session_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "db or session_id is NULL");
  }

  const char *sql = "DELETE FROM sessions WHERE session_id = ?";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to prepare session delete query: %s", sqlite3_errmsg(db));
  }

  sqlite3_bind_blob(stmt, 1, session_id, 16, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to delete session: %s", sqlite3_errmsg(db));
  }

  log_debug("Session deleted from database");
  return ASCIICHAT_OK;
}

void database_close(sqlite3 *db) {
  if (!db) {
    return;
  }

  sqlite3_close(db);
  log_debug("Database closed");
}

#pragma once

/**
 * @file acds/database.h
 * @brief 💾 SQLite-based session management for discovery service
 *
 * SQLite is the single source of truth for all session data.
 * All session operations (create, lookup, join, leave) go directly
 * to the database. WAL mode provides concurrent read access.
 */

#include <stdint.h>
#include <stdbool.h>
#include <sqlite3.h>
#include "common.h"
#include "acds/session.h"
#include "acds/main.h"
#include "network/acip/acds.h"

// ============================================================================
// Database Lifecycle
// ============================================================================

/**
 * @brief Initialize database and create schema
 *
 * @param db_path Path to SQLite database file
 * @param db Output database handle
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_init(const char *db_path, sqlite3 **db);

/**
 * @brief Close database
 *
 * @param db Database handle to close
 */
void database_close(sqlite3 *db);

// ============================================================================
// Session Operations (SQLite as single source of truth)
// ============================================================================

/**
 * @brief Create new session
 *
 * @param db Database handle
 * @param req Session creation request
 * @param config ACDS server configuration
 * @param resp Session creation response (output)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_create(sqlite3 *db, const acip_session_create_t *req, const acds_config_t *config,
                                          acip_session_created_t *resp);

/**
 * @brief Lookup session by string
 *
 * @param db Database handle
 * @param session_string Session string to look up
 * @param config ACDS server configuration (for policy flags)
 * @param resp Session info response (output)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_lookup(sqlite3 *db, const char *session_string, const acds_config_t *config,
                                          acip_session_info_t *resp);

/**
 * @brief Join existing session
 *
 * @param db Database handle
 * @param req Join request
 * @param config ACDS server configuration (for TURN credentials)
 * @param resp Join response (output)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_join(sqlite3 *db, const acip_session_join_t *req, const acds_config_t *config,
                                        acip_session_joined_t *resp);

/**
 * @brief Leave session
 *
 * @param db Database handle
 * @param session_id Session UUID
 * @param participant_id Participant UUID
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_leave(sqlite3 *db, const uint8_t session_id[16], const uint8_t participant_id[16]);

/**
 * @brief Find session by session_id
 *
 * Allocates and returns a session_entry_t. Caller must free with session_entry_free().
 *
 * @param db Database handle
 * @param session_id Session UUID to find
 * @return Allocated session entry (caller owns), or NULL if not found
 */
session_entry_t *database_session_find_by_id(sqlite3 *db, const uint8_t session_id[16]);

/**
 * @brief Find session by session_string
 *
 * Allocates and returns a session_entry_t. Caller must free with session_entry_free().
 *
 * @param db Database handle
 * @param session_string Session string to find
 * @return Allocated session entry (caller owns), or NULL if not found
 */
session_entry_t *database_session_find_by_string(sqlite3 *db, const char *session_string);

/**
 * @brief Clean up expired sessions
 *
 * Removes sessions that have exceeded their 24-hour lifetime.
 * Called periodically by background cleanup thread.
 *
 * @param db Database handle
 */
void database_session_cleanup_expired(sqlite3 *db);

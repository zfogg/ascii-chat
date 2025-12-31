#pragma once

/**
 * @file acds/database.h
 * @brief 💾 SQLite persistence for discovery service
 *
 * Handles session persistence, rate limiting, and recovery from crashes.
 */

#include <stdint.h>
#include <stdbool.h>
#include <sqlite3.h>
#include "core/common.h"
#include "acds/session.h"

/**
 * @brief Initialize database and create schema
 *
 * @param db_path Path to SQLite database file
 * @param db Output database handle
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_init(const char *db_path, sqlite3 **db);

/**
 * @brief Load active sessions from database into registry
 *
 * @param db Database handle
 * @param registry Session registry to populate
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_load_sessions(sqlite3 *db, session_registry_t *registry);

/**
 * @brief Save session to database
 *
 * @param db Database handle
 * @param session Session entry to save
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_save_session(sqlite3 *db, const session_entry_t *session);

/**
 * @brief Delete session from database
 *
 * @param db Database handle
 * @param session_id Session UUID
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_delete_session(sqlite3 *db, const uint8_t session_id[16]);

/**
 * @brief Close database
 *
 * @param db Database handle to close
 */
void database_close(sqlite3 *db);

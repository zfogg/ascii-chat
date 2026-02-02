/**
 * @file network/rate_limit/sqlite.h
 * @brief 💾 SQLite rate limiting backend interface
 */

#pragma once

#include "../../network/rate_limit/memory.h" // For backend_ops_t typedef
#include <sqlite3.h>

/**
 * @brief SQLite backend operations vtable
 */
extern const rate_limiter_backend_ops_t sqlite_backend_ops;

/**
 * @brief Create SQLite backend instance
 * @param db_path Path to SQLite database (NULL = in-memory)
 * @return Backend instance or NULL on failure
 */
void *sqlite_backend_create(const char *db_path);

/**
 * @brief Set SQLite database handle for backend
 *
 * Called by ACDS after opening the database, since the database
 * lifecycle is managed externally.
 *
 * @param backend_data Backend instance from sqlite_backend_create()
 * @param db SQLite database handle
 */
void sqlite_backend_set_db(void *backend_data, sqlite3 *db);

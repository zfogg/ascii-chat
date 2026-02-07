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
// NOTE: Use explicit path to avoid Windows include resolution picking up options/common.h
#include "../common.h"
#include "../discovery/session.h"
#include "discovery-service/main.h"
#include "../network/acip/acds.h"

/**
 * @brief Discovery database handle (sqlite3 wrapper)
 */
typedef sqlite3 discovery_database_t;

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
 * Allocates and returns a session_entry_t. Caller must free with session_entry_destroy().
 *
 * @param db Database handle
 * @param session_id Session UUID to find
 * @return Allocated session entry (caller owns), or NULL if not found
 */
session_entry_t *database_session_find_by_id(sqlite3 *db, const uint8_t session_id[16]);

/**
 * @brief Find session by session_string
 *
 * Allocates and returns a session_entry_t. Caller must free with session_entry_destroy().
 *
 * @param db Database handle
 * @param session_string Session string to find
 * @return Allocated session entry (caller owns), or NULL if not found
 */
session_entry_t *database_session_find_by_string(sqlite3 *db, const char *session_string);

/**
 * @brief Clean up inactive sessions
 *
 * Removes sessions that have been inactive for more than 3 hours.
 * Inactivity is measured by last_activity_at (updated on join/leave/host updates).
 * Called periodically by background cleanup thread.
 *
 * @param db Database handle
 */
void database_session_cleanup_expired(sqlite3 *db);

/**
 * @brief Update session host (discovery mode)
 *
 * Called when a participant announces they are hosting. Updates the
 * session's host fields so new joiners know where to connect.
 *
 * @param db Database handle
 * @param session_id Session UUID
 * @param host_participant_id Host's participant UUID
 * @param host_address Host's reachable address
 * @param host_port Host's port
 * @param connection_type How to reach host (ACIP_CONNECTION_TYPE_*)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_update_host(sqlite3 *db, const uint8_t session_id[16],
                                               const uint8_t host_participant_id[16], const char *host_address,
                                               uint16_t host_port, uint8_t connection_type);

/**
 * @brief Clear session host (discovery mode - host migration)
 *
 * Called when the current host disconnects or fails. Clears host fields
 * so remaining participants can negotiate a new host.
 *
 * @param db Database handle
 * @param session_id Session UUID
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_clear_host(sqlite3 *db, const uint8_t session_id[16]);

/**
 * @brief Start host migration (discovery mode)
 *
 * Called when the current host disconnects. Marks session as in_migration
 * and starts a collection window for HOST_LOST candidates.
 *
 * @param db Database handle
 * @param session_id Session UUID
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_start_migration(sqlite3 *db, const uint8_t session_id[16]);

/**
 * @brief Check if migration window has completed
 *
 * Called periodically to check if migration collection window timeout has expired.
 * Returns the session if migration is ready for host election.
 *
 * @param db Database handle
 * @param session_id Session UUID
 * @param migration_window_ms Collection window duration in milliseconds
 * @return true if collection window has completed, false otherwise
 */
bool database_session_is_migration_ready(sqlite3 *db, const uint8_t session_id[16], uint64_t migration_window_ms);

// ============================================================================
// Multi-Key Management (for session key rotation and multiple identities)
// ============================================================================

/**
 * @brief Add a key to a session
 *
 * @param db Database handle
 * @param session_string Session string
 * @param identity_pubkey Ed25519 public key (32 bytes)
 * @param key_version Key version for tracking rotation
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_add_key(sqlite3 *db, const char *session_string, const uint8_t identity_pubkey[32],
                                           uint32_t key_version);

/**
 * @brief Revoke a key from a session
 *
 * @param db Database handle
 * @param session_string Session string
 * @param identity_pubkey Ed25519 public key to revoke (32 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_revoke_key(sqlite3 *db, const char *session_string,
                                              const uint8_t identity_pubkey[32]);

/**
 * @brief Check if a key is valid for a session (exists and not revoked)
 *
 * @param db Database handle
 * @param session_string Session string
 * @param identity_pubkey Ed25519 public key to check (32 bytes)
 * @return true if key is valid, false otherwise
 */
bool database_session_verify_key(sqlite3 *db, const char *session_string, const uint8_t identity_pubkey[32]);

/**
 * @brief Get all active keys for a session
 *
 * @param db Database handle
 * @param session_string Session string
 * @param keys_out Output array for keys (caller must provide buffer)
 * @param max_keys Maximum number of keys to retrieve
 * @param count_out Output: number of keys retrieved
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t database_session_get_keys(sqlite3 *db, const char *session_string, uint8_t (*keys_out)[32],
                                            size_t max_keys, size_t *count_out);

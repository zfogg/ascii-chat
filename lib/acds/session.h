#pragma once

/**
 * @file acds/session.h
 * @brief 🎯 Session registry for discovery service (sharded rwlock implementation)
 *
 * In-memory hash table of active sessions with high concurrency via sharding.
 * Uses sharded rwlocks for:
 *   - Reduced lock contention by distributing sessions across 16 shards
 *   - uthash for per-shard hash tables (header-only, no external dependency)
 *   - Simple, portable implementation using platform abstraction layer
 *
 * Sessions expire after 24 hours and are cleaned up by background thread.
 * Fine-grained per-entry locking protects participant lists.
 */

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "platform/abstraction.h"
#include "network/acip/acds.h"
#include "acds/main.h"
#include "uthash.h"

/**
 * @brief Maximum participants per session
 */
#define MAX_PARTICIPANTS 8

/**
 * @brief Number of shards for the session registry
 *
 * Using 16 shards provides good lock contention reduction while keeping
 * memory overhead reasonable. Sessions are distributed across shards
 * using FNV-1a hash of the session_string.
 */
#define SESSION_REGISTRY_NUM_SHARDS 16

/**
 * @brief Participant in a session
 */
typedef struct {
  uint8_t participant_id[16];  ///< UUID
  uint8_t identity_pubkey[32]; ///< Ed25519 public key
  uint64_t joined_at;          ///< Unix timestamp (ms)
} participant_t;

/**
 * @brief Session entry (uthash node)
 *
 * Layout optimized for uthash:
 *   - session_string is the lookup key
 *   - Participant list protected by fine-grained mutex
 *   - UT_hash_handle at end for cache locality
 */
typedef struct session_entry {
  char session_string[48]; ///< e.g., "swift-river-mountain" (lookup key)
  uint8_t session_id[16];  ///< UUID

  uint8_t host_pubkey[32];      ///< Host's Ed25519 key
  uint8_t capabilities;         ///< bit 0: video, bit 1: audio
  uint8_t max_participants;     ///< 1-8
  uint8_t current_participants; ///< Active participant count

  char password_hash[128]; ///< Argon2id hash (if has_password)
  bool has_password;       ///< Password protection flag
  bool expose_ip_publicly; ///< Allow IP disclosure without verification (explicit opt-in via --acds-expose-ip)
  uint8_t session_type;    ///< acds_session_type_t: 0=DIRECT_TCP, 1=WEBRTC

  uint64_t created_at; ///< Unix timestamp (ms)
  uint64_t expires_at; ///< Unix timestamp (ms) - created_at + 24h

  // Server connection information (where clients should connect)
  char server_address[64]; ///< IPv4/IPv6 address or hostname
  uint16_t server_port;    ///< Port number for client connection

  participant_t *participants[MAX_PARTICIPANTS]; ///< Participant array
  mutex_t participant_mutex;                     ///< Fine-grained lock for participant list

  /* uthash handle - must be named 'hh' for uthash macros */
  UT_hash_handle hh;
} session_entry_t;

/**
 * @brief Single shard of the session registry
 *
 * Each shard has its own rwlock and hash table, allowing concurrent
 * access to different shards without contention.
 */
typedef struct {
  rwlock_t lock;             ///< Per-shard read-write lock
  session_entry_t *sessions; ///< uthash hash table head (NULL when empty)
} session_shard_t;

/**
 * @brief Session registry (sharded rwlock)
 *
 * Uses sharded rwlock + uthash for high concurrency:
 *   - 16 shards reduce lock contention under high concurrency
 *   - uthash provides O(1) lookups within each shard
 *   - Fine-grained per-entry locking for participant modifications
 *   - Uses platform abstraction layer for portable rwlocks
 */
typedef struct {
  session_shard_t shards[SESSION_REGISTRY_NUM_SHARDS];
} session_registry_t;

/**
 * @brief Initialize session registry
 *
 * @param registry Registry structure to initialize
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_registry_init(session_registry_t *registry);

/**
 * @brief Create new session
 *
 * @param registry Session registry
 * @param req Session creation request
 * @param resp Session creation response (output)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_create(session_registry_t *registry, const acip_session_create_t *req,
                                 const acds_config_t *config, acip_session_created_t *resp);

/**
 * @brief Lookup session by string
 *
 * @param registry Session registry
 * @param session_string Session string to look up
 * @param config ACDS server configuration (for policy flags)
 * @param resp Session info response (output)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_lookup(session_registry_t *registry, const char *session_string, const acds_config_t *config,
                                 acip_session_info_t *resp);

/**
 * @brief Join existing session
 *
 * @param registry Session registry
 * @param req Join request
 * @param config ACDS server configuration (for TURN credentials)
 * @param resp Join response (output)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_join(session_registry_t *registry, const acip_session_join_t *req,
                               const acds_config_t *config, acip_session_joined_t *resp);

/**
 * @brief Leave session
 *
 * @param registry Session registry
 * @param session_id Session UUID
 * @param participant_id Participant UUID
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_leave(session_registry_t *registry, const uint8_t session_id[16],
                                const uint8_t participant_id[16]);

/**
 * @brief Clean up expired sessions
 *
 * Removes sessions that have exceeded their 24-hour lifetime.
 * Called periodically by background cleanup thread.
 *
 * @param registry Session registry
 */
void session_cleanup_expired(session_registry_t *registry);

/**
 * @brief Destroy session registry
 *
 * @param registry Registry to destroy
 */
void session_registry_destroy(session_registry_t *registry);

/**
 * @brief Find session by session_id
 *
 * Thread-safe lookup that acquires/releases the appropriate shard lock.
 * Note: The returned pointer is only valid for read access immediately after
 * the call. For modifications, use the specialized session_* functions.
 *
 * @param registry Session registry
 * @param session_id Session UUID to find
 * @return Pointer to session entry, or NULL if not found
 */
session_entry_t *session_find_by_id(session_registry_t *registry, const uint8_t session_id[16]);

/**
 * @brief Find session by session_string
 *
 * Thread-safe lookup that acquires/releases the appropriate shard lock.
 * Note: The returned pointer is only valid for read access immediately after
 * the call. For modifications, use the specialized session_* functions.
 *
 * @param registry Session registry
 * @param session_string Session string to find
 * @return Pointer to session entry, or NULL if not found
 */
session_entry_t *session_find_by_string(session_registry_t *registry, const char *session_string);

/**
 * @brief Iterate over all sessions (for database operations)
 *
 * Calls the callback for each session while holding the appropriate shard lock.
 * The callback should NOT store the session pointer - it's only valid during the callback.
 *
 * @param registry Session registry
 * @param callback Function to call for each session
 * @param user_data User data passed to callback
 */
void session_foreach(session_registry_t *registry, void (*callback)(session_entry_t *session, void *user_data),
                     void *user_data);

/**
 * @brief Add a session entry directly to the registry (for database loading)
 *
 * This bypasses the normal creation flow and adds a pre-populated entry.
 * Used by database_load_sessions() to restore sessions from disk.
 *
 * @param registry Session registry
 * @param session Pre-allocated and populated session entry (ownership transferred)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_add_entry(session_registry_t *registry, session_entry_t *session);

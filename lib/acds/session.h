#pragma once

/**
 * @file acds/session.h
 * @brief 🎯 Session registry for discovery service (lock-free RCU implementation)
 *
 * In-memory hash table of active sessions with lock-free concurrent access.
 * Uses liburcu for:
 *   - Lock-free hash table (cds_lfht) replacing uthash + rwlock
 *   - RCU synchronization for safe concurrent reads/writes
 *   - Deferred memory freeing via call_rcu()
 *
 * Sessions expire after 24 hours and are cleaned up by background thread.
 * Fine-grained per-entry locking protects participant lists.
 */

/* CRITICAL: RCU macros MUST be defined before urcu headers are included.
 * These are defined globally via cmake (cmake/targets/SourceFiles.cmake)
 * and passed via -D compiler flags, so we don't redefine them here. */

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "platform/abstraction.h"
#include "network/acip/acds.h"
#include "acds/main.h"

/* RCU library includes - include <urcu.h> (not urcu/urcu-mb.h) to enable URCU_API_MAP
 * which provides inline API functions like cds_lfht_new(), rcu_read_lock(), call_rcu(), etc. */
#include <urcu.h>
#include <urcu/rculfhash.h>

/**
 * @brief Maximum participants per session
 */
#define MAX_PARTICIPANTS 8

/**
 * @brief Participant in a session
 */
typedef struct {
  uint8_t participant_id[16];  ///< UUID
  uint8_t identity_pubkey[32]; ///< Ed25519 public key
  uint64_t joined_at;          ///< Unix timestamp (ms)
} participant_t;

/**
 * @brief Session entry (RCU hash table node)
 *
 * Layout optimized for RCU:
 *   - Hash table node at end to improve cache locality
 *   - Participant list protected by fine-grained mutex
 *   - RCU head for deferred freeing
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

  /* RCU integration */
  struct cds_lfht_node hash_node; ///< RCU lock-free hash table node (keyed by session_string)
  struct rcu_head rcu_head;       ///< For deferred freeing via call_rcu()
} session_entry_t;

/**
 * @brief Session registry (lock-free RCU)
 *
 * Replaced uthash + rwlock with RCU lock-free hash table:
 *   - No global lock on lookups (5-10x performance improvement)
 *   - Fine-grained per-entry locking for participant modifications
 *   - Automatic memory management via RCU grace periods
 */
typedef struct {
  struct cds_lfht *sessions; ///< RCU lock-free hash table
  /* NO rwlock_t - RCU provides read-side synchronization! */
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

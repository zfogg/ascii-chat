#pragma once

/**
 * @file acds/session.h
 * @brief 🎯 Session registry for discovery service
 *
 * In-memory hash table of active sessions with thread-safe access.
 * Sessions expire after 24 hours and are cleaned up by background thread.
 */

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "platform/abstraction.h"
#include "network/acip/acds.h"
#include "acds/main.h"

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
 * @brief Session entry (hash table node)
 */
typedef struct session_entry {
  char session_string[48]; ///< e.g., "swift-river-mountain"
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

  UT_hash_handle hh; ///< uthash handle (keyed by session_string)
} session_entry_t;

/**
 * @brief Session registry (thread-safe)
 */
typedef struct {
  session_entry_t *sessions; ///< uthash table
  rwlock_t lock;             ///< Reader-writer lock
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

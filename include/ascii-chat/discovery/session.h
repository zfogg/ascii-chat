#pragma once

/**
 * @file acds/session.h
 * @brief 🎯 Session data structures for discovery service
 *
 * Session and participant data structures for ACDS.
 * Sessions are stored in SQLite as the single source of truth.
 * This file provides only data structure definitions.
 */

#include <stdint.h>
#include <stdbool.h>
// NOTE: Use explicit path to avoid Windows include resolution picking up options/common.h
#include "../common.h"
#include "../network/acip/acds.h"
#include "../discovery/strings.h"

/**
 * @brief Maximum participants per session
 */
#if !defined(MAX_PARTICIPANTS)
#define MAX_PARTICIPANTS 8
#endif

/**
 * @brief Participant in a session
 */
typedef struct {
  uint8_t participant_id[16];  ///< UUID
  uint8_t identity_pubkey[32]; ///< Ed25519 public key
  uint64_t joined_at;          ///< Unix timestamp (ms)
} participant_t;

/**
 * @brief Host migration state for collecting HOST_LOST packets
 *
 * When host disconnects, ACDS starts a collection window to gather NAT quality
 * info from remaining participants for re-election.
 */
typedef struct {
  uint8_t participant_id[16]; ///< Participant proposing new host
  uint8_t nat_quality_tier;   ///< NAT tier for this participant
  uint16_t upload_kbps;       ///< Upload bandwidth
  uint32_t rtt_to_acds_ns;    ///< Latency to ACDS in nanoseconds
  uint8_t connection_type;    ///< How they can be reached
} host_lost_candidate_t;

/**
 * @brief Session entry data structure
 *
 * Contains all session metadata. Stored in SQLite database.
 */
typedef struct session_entry {
  char session_string[SESSION_STRING_BUFFER_SIZE]; ///< e.g., "affectionate-acquaintance-acquaintance"
  uint8_t session_id[16];                          ///< UUID

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

  // Discovery mode host negotiation fields
  uint8_t initiator_id[16];        ///< First participant who created/joined the session
  bool host_established;           ///< Whether a host has been designated (false = still negotiating)
  uint8_t host_participant_id[16]; ///< Current host's participant_id (valid if host_established)
  char host_address[64];           ///< Host's reachable address (valid if host_established)
  uint16_t host_port;              ///< Host's port (valid if host_established)
  uint8_t host_connection_type;    ///< acip_connection_type_t: how to reach host

  // Host migration state (when host disconnects)
  bool in_migration;           ///< Currently collecting HOST_LOST packets
  uint64_t migration_start_ns; ///< When migration started in nanoseconds (for collection window timeout)
  host_lost_candidate_t *migration_candidates[MAX_PARTICIPANTS]; ///< Candidates received during migration

  participant_t *participants[MAX_PARTICIPANTS]; ///< Participant array (in-memory only)
} session_entry_t;

/**
 * @brief Free a session entry and all its resources
 * @param entry Session entry to free
 */
void session_entry_free(session_entry_t *entry);

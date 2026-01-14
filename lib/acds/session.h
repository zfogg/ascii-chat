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
#include "common.h"
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
 * @brief Session entry data structure
 *
 * Contains all session metadata. Stored in SQLite database.
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

  participant_t *participants[MAX_PARTICIPANTS]; ///< Participant array (in-memory only)
} session_entry_t;

/**
 * @brief Free a session entry and all its resources
 * @param entry Session entry to free
 */
void session_entry_free(session_entry_t *entry);

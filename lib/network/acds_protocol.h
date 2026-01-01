#pragma once

/**
 * @file acds/protocol.h
 * @brief ASCII-Chat Discovery Service (ACDS) Protocol Message Formats
 *
 * This file defines the binary message formats for the ACIP discovery protocol.
 * All messages use packed structs sent over TCP using the existing ACIP packet
 * infrastructure (packet_header_t + payload).
 *
 * Protocol Design:
 * - Raw TCP transport (port 27225 default)
 * - Binary ACIP packets (NOT JSON)
 * - Reuses existing crypto handshake (CRYPTO_KEY_EXCHANGE_*, CRYPTO_AUTH_*)
 * - Ed25519 identity signatures for session authentication
 * - Ephemeral sessions (24-hour expiration)
 */

#include <stdint.h>
#include <stdbool.h>
#include "network/packet.h"

// ============================================================================
// Session Management Messages
// ============================================================================

/**
 * @brief SESSION_CREATE (0x20) - Create new session
 * Client -> Discovery Server
 *
 * Payload structure (fixed + variable):
 * - Fixed part: acip_session_create_t (295 bytes)
 * - Variable part: reserved_string (if reserved_string_len > 0)
 */
typedef struct __attribute__((packed)) {
  uint8_t identity_pubkey[32]; ///< Ed25519 public key of session host
  uint8_t signature[64];       ///< Signs: type || timestamp || capabilities
  uint64_t timestamp;          ///< Unix ms (replay protection)

  uint8_t capabilities;     ///< Bit 0: video, Bit 1: audio
  uint8_t max_participants; ///< 1-8 participants allowed

  uint8_t has_password;       ///< 0 = no password, 1 = password protected
  uint8_t password_hash[128]; ///< Argon2id hash (only if has_password == 1)

  uint8_t reserved_string_len; ///< 0 = auto-generate, >0 = use provided string
  // char  reserved_string[];        ///< Variable length, follows if len > 0

  // Server connection information (where clients should connect)
  char server_address[64]; ///< IPv4/IPv6 address or hostname (null-terminated)
  uint16_t server_port;    ///< Port number for client connection
} acip_session_create_t;

/**
 * @brief SESSION_CREATED (0x21) - Session created response
 * Discovery Server -> Client
 *
 * Payload structure (fixed + variable):
 * - Fixed part: acip_session_created_t (66 bytes)
 * - Variable part: stun_server_t[stun_count] + turn_server_t[turn_count]
 */
typedef struct __attribute__((packed)) {
  uint8_t session_string_len; ///< Length of session string (e.g., 20 for "swift-river-mountain")
  char session_string[48];    ///< Null-padded session string
  uint8_t session_id[16];     ///< UUID as bytes (not string)
  uint64_t expires_at;        ///< Unix ms (created_at + 24 hours)

  uint8_t stun_count; ///< Number of STUN servers
  uint8_t turn_count; ///< Number of TURN servers
  // Followed by: stun_server_t[stun_count], turn_server_t[turn_count]
} acip_session_created_t;

/**
 * @brief STUN server configuration
 */
typedef struct __attribute__((packed)) {
  uint8_t host_len; ///< Length of host string
  char host[64];    ///< e.g., "stun:discovery.ascii.chat:3478"
} stun_server_t;

/**
 * @brief TURN server configuration with credentials
 */
typedef struct __attribute__((packed)) {
  uint8_t url_len; ///< Length of URL
  char url[64];    ///< e.g., "turn:discovery.ascii.chat:3478"
  uint8_t username_len;
  char username[32];
  uint8_t credential_len;
  char credential[64]; ///< Time-limited credential
} turn_server_t;

/**
 * @brief SESSION_LOOKUP (0x22) - Lookup session by string
 * Client -> Discovery Server
 */
typedef struct __attribute__((packed)) {
  uint8_t session_string_len;
  char session_string[48];
} acip_session_lookup_t;

/**
 * @brief SESSION_INFO (0x23) - Session info response
 * Discovery Server -> Client
 *
 * NOTE: Does NOT include server connection information (IP/port).
 * Server address is only revealed after authentication via SESSION_JOIN.
 */
typedef struct __attribute__((packed)) {
  uint8_t found;           ///< 0 = not found, 1 = found
  uint8_t session_id[16];  ///< Valid only if found == 1
  uint8_t host_pubkey[32]; ///< Host's Ed25519 public key
  uint8_t capabilities;    ///< Session capabilities
  uint8_t max_participants;
  uint8_t current_participants;
  uint8_t has_password; ///< 1 = password required to join
  uint64_t created_at;  ///< Unix ms
  uint64_t expires_at;  ///< Unix ms

  // ACDS Policy Flags (enforced by discovery server)
  uint8_t require_server_verify; ///< ACDS policy: server must verify client identity
  uint8_t require_client_verify; ///< ACDS policy: client must verify server identity
} acip_session_info_t;

/**
 * @brief SESSION_JOIN (0x24) - Join existing session
 * Client -> Discovery Server
 *
 * Payload structure (fixed):
 * - acip_session_join_t (241 bytes)
 */
typedef struct __attribute__((packed)) {
  uint8_t session_string_len;
  char session_string[48];
  uint8_t identity_pubkey[32]; ///< Joiner's Ed25519 public key
  uint8_t signature[64];       ///< Signs: type || timestamp || session_string
  uint64_t timestamp;          ///< Unix ms

  uint8_t has_password;
  char password[128]; ///< Cleartext password (TLS protects transport)
} acip_session_join_t;

/**
 * @brief SESSION_JOINED (0x25) - Session join response
 * Discovery Server -> Client
 *
 * Server connection information is ONLY revealed after successful authentication
 * (password verification or identity verification). This prevents IP address
 * leakage to unauthenticated clients who only know the session string.
 */
typedef struct __attribute__((packed)) {
  uint8_t success;         ///< 0 = failed, 1 = joined
  uint8_t error_code;      ///< Error code if success == 0
  char error_message[128]; ///< Human-readable error

  uint8_t participant_id[16]; ///< UUID for this participant (valid if success == 1)
  uint8_t session_id[16];     ///< Session UUID

  // Server connection information (ONLY if success == 1)
  char server_address[64]; ///< IPv4/IPv6 address or hostname (null-terminated)
  uint16_t server_port;    ///< Port number for client connection
} acip_session_joined_t;

/**
 * @brief SESSION_LEAVE (0x26) - Leave session
 * Client -> Discovery Server
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t participant_id[16];
} acip_session_leave_t;

/**
 * @brief SESSION_END (0x27) - End session (host only)
 * Host -> Discovery Server
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t signature[64]; ///< Host proves ownership
} acip_session_end_t;

/**
 * @brief SESSION_RECONNECT (0x28) - Reconnect to session
 * Client -> Discovery Server
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t participant_id[16];
  uint8_t signature[64]; ///< Prove identity
} acip_session_reconnect_t;

// ============================================================================
// WebRTC Signaling Messages
// ============================================================================

/**
 * @brief WEBRTC_SDP (0x30) - SDP offer/answer relay
 * Bidirectional (relayed through discovery server)
 *
 * Payload structure (fixed + variable):
 * - Fixed: acip_webrtc_sdp_t (50 bytes)
 * - Variable: sdp_data (SDP string)
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];   ///< Session UUID
  uint8_t sender_id[16];    ///< Participant UUID
  uint8_t recipient_id[16]; ///< All zeros = broadcast to all
  uint8_t sdp_type;         ///< 0 = offer, 1 = answer
  uint16_t sdp_len;         ///< Length of SDP data
  // char  sdp_data[];               ///< Variable length SDP string
} acip_webrtc_sdp_t;

/**
 * @brief WEBRTC_ICE (0x31) - ICE candidate relay
 * Bidirectional (relayed through discovery server)
 *
 * Payload structure (fixed + variable):
 * - Fixed: acip_webrtc_ice_t (50 bytes)
 * - Variable: candidate (ICE candidate string)
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t sender_id[16];
  uint8_t recipient_id[16];
  uint16_t candidate_len;
  // char  candidate[];              ///< Variable length ICE candidate
} acip_webrtc_ice_t;

// ============================================================================
// String Reservation Messages (Future)
// ============================================================================

/**
 * @brief STRING_RESERVE (0x40) - Reserve a session string
 * Client -> Discovery Server
 */
typedef struct __attribute__((packed)) {
  uint8_t identity_pubkey[32];
  uint8_t signature[64];
  uint64_t timestamp;
  uint8_t string_len;
  char string[48];
  uint32_t duration_days; ///< How long to reserve (1-365)
} acip_string_reserve_t;

/**
 * @brief STRING_RESERVED (0x41) - String reservation response
 * Discovery Server -> Client
 */
typedef struct __attribute__((packed)) {
  uint8_t success;
  uint8_t error_code;
  char error_message[128];
  uint64_t expires_at; ///< Unix ms
} acip_string_reserved_t;

/**
 * @brief STRING_RENEW (0x42) - Renew string reservation
 * Client -> Discovery Server
 */
typedef struct __attribute__((packed)) {
  uint8_t identity_pubkey[32];
  uint8_t signature[64];
  uint64_t timestamp;
  uint8_t string_len;
  char string[48];
  uint32_t duration_days;
} acip_string_renew_t;

/**
 * @brief STRING_RELEASE (0x43) - Release string reservation
 * Client -> Discovery Server
 */
typedef struct __attribute__((packed)) {
  uint8_t identity_pubkey[32];
  uint8_t signature[64];
  uint64_t timestamp;
  uint8_t string_len;
  char string[48];
} acip_string_release_t;

// ============================================================================
// Meta Messages
// ============================================================================

/**
 * @brief ERROR (0xFF) - Generic error response
 * Discovery Server -> Client
 */
typedef struct __attribute__((packed)) {
  uint8_t error_code;      ///< Error code (TBD: define error code enum)
  char error_message[256]; ///< Human-readable error
} acip_error_t;

// ============================================================================
// Error Codes
// ============================================================================

typedef enum {
  ACIP_ERROR_NONE = 0,
  ACIP_ERROR_SESSION_NOT_FOUND = 1,
  ACIP_ERROR_SESSION_FULL = 2,
  ACIP_ERROR_INVALID_PASSWORD = 3,
  ACIP_ERROR_INVALID_SIGNATURE = 4,
  ACIP_ERROR_RATE_LIMITED = 5,
  ACIP_ERROR_STRING_TAKEN = 6,
  ACIP_ERROR_STRING_INVALID = 7,
  ACIP_ERROR_INTERNAL = 255
} acip_error_code_t;

// ============================================================================
// Helper Macros
// ============================================================================

/** @brief Maximum session string length (e.g., "swift-river-mountain" = 20 chars) */
#define ACIP_MAX_SESSION_STRING_LEN 48

/** @brief Session expiration time (24 hours in milliseconds) */
#define ACIP_SESSION_EXPIRATION_MS (24ULL * 60 * 60 * 1000)

/** @brief Discovery server default port */
#define ACIP_DISCOVERY_DEFAULT_PORT 27225

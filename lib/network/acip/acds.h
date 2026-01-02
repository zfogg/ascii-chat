/**
 * @file network/acip/acds.h
 * @brief ASCII-Chat Discovery Service (ACDS) Protocol Message Formats
 * @ingroup acip
 * @addtogroup acds
 * @{
 *
 * This module defines the binary message formats for the ACIP discovery protocol.
 * All messages use packed structs sent over TCP using the existing ACIP packet
 * infrastructure (packet_header_t + payload).
 *
 * PROTOCOL DESIGN:
 * ================
 * - Raw TCP transport (port 27225 default)
 * - Binary ACIP packets (NOT JSON)
 * - Reuses existing crypto handshake (CRYPTO_KEY_EXCHANGE_*, CRYPTO_AUTH_*)
 * - Ed25519 identity signatures for session authentication
 * - Ephemeral sessions (24-hour expiration)
 *
 * MESSAGE STRUCTURE:
 * ==================
 * All ACDS messages follow the standard ACIP packet structure:
 * - Header: packet_header_t (magic, type, length, CRC32, client_id)
 * - Payload: Message-specific packed struct (defined here)
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - network/acip/protocol.h: Defines ACIP packet types (100-199)
 * - network/packet.h: Provides packet infrastructure (header, CRC, etc.)
 * - src/acds/: ACDS server implementation
 *
 * @note All structures are packed with __attribute__((packed)) for wire format.
 * @note Payload sizes include both fixed and variable-length portions.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0 (ACIP Protocol Refactoring)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "network/packet.h"
#include "network/acip/protocol.h"
#include "network/webrtc/stun.h"
#include "network/webrtc/turn.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pack network protocol structures tightly for wire format
#ifdef _WIN32
#pragma pack(push, 1)
#endif

/**
 * @brief Session connection type
 *
 * Determines how clients connect to the session host:
 * - DIRECT_TCP: Clients connect directly to server IP:port (default, requires public IP)
 * - WEBRTC: Clients use WebRTC P2P mesh with STUN/TURN (works behind NAT)
 *
 * @ingroup acds
 */
typedef enum {
  SESSION_TYPE_DIRECT_TCP = 0, ///< Direct TCP connection to server IP:port (default)
  SESSION_TYPE_WEBRTC = 1      ///< WebRTC P2P mesh with STUN/TURN relay
} acds_session_type_t;

/**
 * @name ACDS Session Management Messages
 * @{
 * @ingroup acds
 */

/**
 * @brief SESSION_CREATE (PACKET_TYPE_ACIP_SESSION_CREATE) - Create new session
 *
 * Direction: Client -> Discovery Server
 *
 * Payload structure (fixed + variable):
 * - Fixed part: acip_session_create_t (295 bytes)
 * - Variable part: reserved_string (if reserved_string_len > 0)
 *
 * The client requests creation of a new session with specific capabilities,
 * optionally providing a pre-reserved session string. The server responds
 * with SESSION_CREATED containing the session identifier.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t identity_pubkey[32]; ///< Ed25519 public key of session host
  uint8_t signature[64];       ///< Signs: type || timestamp || capabilities
  uint64_t timestamp;          ///< Unix ms (replay protection)

  uint8_t capabilities;     ///< Bit 0: video, Bit 1: audio
  uint8_t max_participants; ///< 1-8 participants allowed
  uint8_t session_type;     ///< acds_session_type_t: 0=DIRECT_TCP (default), 1=WEBRTC

  uint8_t has_password;       ///< 0 = no password, 1 = password protected
  uint8_t password_hash[128]; ///< Argon2id hash (only if has_password == 1)
  uint8_t expose_ip_publicly; ///< 0 = require verification, 1 = allow public IP disclosure (explicit --acds-expose-ip
                              ///< opt-in)

  uint8_t reserved_string_len; ///< 0 = auto-generate, >0 = use provided string
  // char  reserved_string[];        ///< Variable length, follows if len > 0

  // Server connection information (where clients should connect)
  // For DIRECT_TCP: server_address and server_port specify where to connect
  // For WEBRTC: these fields are ignored, signaling happens through ACDS
  char server_address[64]; ///< IPv4/IPv6 address or hostname (null-terminated)
  uint16_t server_port;    ///< Port number for client connection
} acip_session_create_t;

/**
 * @brief SESSION_CREATED (PACKET_TYPE_ACIP_SESSION_CREATED) - Session created response
 *
 * Direction: Discovery Server -> Client
 *
 * Payload structure (fixed + variable):
 * - Fixed part: acip_session_created_t (66 bytes)
 * - Variable part: stun_server_t[stun_count] + turn_server_t[turn_count]
 *
 * The server responds to SESSION_CREATE with the generated session identifier,
 * session string (either auto-generated or the provided reserved string), and
 * optional STUN/TURN server information for WebRTC connectivity.
 *
 * @ingroup acds
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
 * @note STUN server configuration (stun_server_t) is defined in networking/webrtc/stun.h
 * @note TURN server configuration (turn_server_t) is defined in networking/webrtc/turn.h
 */

/**
 * @brief SESSION_LOOKUP (PACKET_TYPE_ACIP_SESSION_LOOKUP) - Lookup session by string
 *
 * Direction: Client -> Discovery Server
 *
 * The client queries for session information using the session string.
 * Server responds with SESSION_INFO containing basic session metadata
 * (but NOT the server connection information, which is only revealed
 * after successful authentication via SESSION_JOIN).
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_string_len;
  char session_string[48];
} acip_session_lookup_t;

/**
 * @brief SESSION_INFO (PACKET_TYPE_ACIP_SESSION_INFO) - Session info response
 *
 * Direction: Discovery Server -> Client
 *
 * **SECURITY NOTE**: Does NOT include server connection information (IP/port).
 * Server address is only revealed after authentication via SESSION_JOIN.
 * This prevents IP address leakage to unauthenticated clients.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t found;           ///< 0 = not found, 1 = found
  uint8_t session_id[16];  ///< Valid only if found == 1
  uint8_t host_pubkey[32]; ///< Host's Ed25519 public key
  uint8_t capabilities;    ///< Session capabilities
  uint8_t max_participants;
  uint8_t current_participants;
  uint8_t session_type; ///< acds_session_type_t: 0=DIRECT_TCP, 1=WEBRTC
  uint8_t has_password; ///< 1 = password required to join
  uint64_t created_at;  ///< Unix ms
  uint64_t expires_at;  ///< Unix ms

  // ACDS Policy Flags (enforced by discovery server)
  uint8_t require_server_verify; ///< ACDS policy: server must verify client identity
  uint8_t require_client_verify; ///< ACDS policy: client must verify server identity
} acip_session_info_t;

/**
 * @brief SESSION_JOIN (PACKET_TYPE_ACIP_SESSION_JOIN) - Join existing session
 *
 * Direction: Client -> Discovery Server
 *
 * Payload structure: acip_session_join_t (241 bytes fixed)
 *
 * The client requests to join an existing session, providing identity proof
 * via Ed25519 signature and optionally a password. Server responds with
 * SESSION_JOINED containing server connection information upon successful
 * authentication.
 *
 * @ingroup acds
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
 * @brief SESSION_JOINED (PACKET_TYPE_ACIP_SESSION_JOINED) - Session join response
 *
 * Direction: Discovery Server -> Client
 *
 * **CRITICAL SECURITY**: Server connection information (IP/port) is ONLY
 * revealed after successful authentication (password verification or identity
 * verification). This prevents IP address leakage to unauthenticated clients
 * who only know the session string.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t success;         ///< 0 = failed, 1 = joined
  uint8_t error_code;      ///< Error code if success == 0
  char error_message[128]; ///< Human-readable error

  uint8_t participant_id[16]; ///< UUID for this participant (valid if success == 1)
  uint8_t session_id[16];     ///< Session UUID

  // Server connection information (ONLY if success == 1)
  uint8_t session_type;    ///< acds_session_type_t: 0=DIRECT_TCP, 1=WEBRTC
  char server_address[64]; ///< IPv4/IPv6 address or hostname (null-terminated)
  uint16_t server_port;    ///< Port number for client connection

  // TURN credentials for WebRTC NAT traversal (ONLY if session_type == SESSION_TYPE_WEBRTC)
  // Generated by ACDS server using HMAC-SHA1 with shared secret
  // Format follows RFC 5766 time-limited TURN authentication
  char turn_username[128]; ///< Format: "{timestamp}:{session_id}"
  char turn_password[128]; ///< Base64-encoded HMAC-SHA1(secret, username)
} acip_session_joined_t;

/**
 * @brief SESSION_LEAVE (PACKET_TYPE_ACIP_SESSION_LEAVE) - Leave session
 *
 * Direction: Client -> Discovery Server
 *
 * The client gracefully leaves a session, allowing the server to update
 * participant count and potentially notify other participants.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t participant_id[16];
} acip_session_leave_t;

/**
 * @brief SESSION_END (PACKET_TYPE_ACIP_SESSION_END) - End session (host only)
 *
 * Direction: Host -> Discovery Server
 *
 * The session host terminates the session, preventing new joins and
 * notifying all participants. Requires signature proof of host identity.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t signature[64]; ///< Host proves ownership
} acip_session_end_t;

/**
 * @brief SESSION_RECONNECT (PACKET_TYPE_ACIP_SESSION_RECONNECT) - Reconnect to session
 *
 * Direction: Client -> Discovery Server
 *
 * The client reconnects to a session after disconnection, using stored
 * participant ID and identity proof to resume participation.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t participant_id[16];
  uint8_t signature[64]; ///< Prove identity
} acip_session_reconnect_t;

/** @} */

/**
 * @name ACDS WebRTC Signaling Messages
 * @{
 * @ingroup acds
 */

/**
 * @brief WEBRTC_SDP (PACKET_TYPE_ACIP_WEBRTC_SDP) - SDP offer/answer relay
 *
 * Direction: Bidirectional (relayed through discovery server)
 *
 * Payload structure (fixed + variable):
 * - Fixed: acip_webrtc_sdp_t (50 bytes)
 * - Variable: sdp_data (SDP string)
 *
 * WebRTC session description protocol messages are relayed through the
 * discovery server to facilitate peer-to-peer connection establishment.
 *
 * @ingroup acds
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
 * @brief WEBRTC_ICE (PACKET_TYPE_ACIP_WEBRTC_ICE) - ICE candidate relay
 *
 * Direction: Bidirectional (relayed through discovery server)
 *
 * Payload structure (fixed + variable):
 * - Fixed: acip_webrtc_ice_t (50 bytes)
 * - Variable: candidate (ICE candidate string)
 *
 * WebRTC ICE candidates are relayed through the discovery server to
 * facilitate NAT traversal during peer-to-peer connection establishment.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t sender_id[16];
  uint8_t recipient_id[16];
  uint16_t candidate_len;
  // char  candidate[];              ///< Variable length ICE candidate
} acip_webrtc_ice_t;

/** @} */

/**
 * @name ACDS String Reservation Messages (Future)
 * @{
 * @ingroup acds
 */

/**
 * @brief STRING_RESERVE (PACKET_TYPE_ACIP_STRING_RESERVE) - Reserve a session string
 *
 * Direction: Client -> Discovery Server
 *
 * **FUTURE FEATURE**: Reserve a memorable session string for future use,
 * preventing others from using it for a specified duration.
 *
 * @ingroup acds
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
 * @brief STRING_RESERVED (PACKET_TYPE_ACIP_STRING_RESERVED) - String reservation response
 *
 * Direction: Discovery Server -> Client
 *
 * **FUTURE FEATURE**: Confirms successful string reservation or reports error.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t success;
  uint8_t error_code;
  char error_message[128];
  uint64_t expires_at; ///< Unix ms
} acip_string_reserved_t;

/**
 * @brief STRING_RENEW (PACKET_TYPE_ACIP_STRING_RENEW) - Renew string reservation
 *
 * Direction: Client -> Discovery Server
 *
 * **FUTURE FEATURE**: Extends an existing string reservation before expiration.
 *
 * @ingroup acds
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
 * @brief STRING_RELEASE (PACKET_TYPE_ACIP_STRING_RELEASE) - Release string reservation
 *
 * Direction: Client -> Discovery Server
 *
 * **FUTURE FEATURE**: Voluntarily releases a reserved string before expiration.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t identity_pubkey[32];
  uint8_t signature[64];
  uint64_t timestamp;
  uint8_t string_len;
  char string[48];
} acip_string_release_t;

/** @} */

/**
 * @name ACDS Error Handling
 * @{
 * @ingroup acds
 */

/**
 * @brief ERROR (PACKET_TYPE_ACIP_ERROR) - Generic error response
 *
 * Direction: Discovery Server -> Client
 *
 * Generic error response used when no specific response packet type exists.
 * Contains error code and human-readable message.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t error_code;      ///< Error code (see acip_error_code_t)
  char error_message[256]; ///< Human-readable error
} acip_error_t;

/**
 * @brief ACIP error codes
 *
 * Standard error codes returned in ACIP error responses.
 *
 * @ingroup acds
 */
typedef enum {
  ACIP_ERROR_NONE = 0,              ///< No error (success)
  ACIP_ERROR_SESSION_NOT_FOUND = 1, ///< Session does not exist
  ACIP_ERROR_SESSION_FULL = 2,      ///< Session has reached max participants
  ACIP_ERROR_INVALID_PASSWORD = 3,  ///< Password verification failed
  ACIP_ERROR_INVALID_SIGNATURE = 4, ///< Identity signature invalid
  ACIP_ERROR_RATE_LIMITED = 5,      ///< Too many requests from this IP
  ACIP_ERROR_STRING_TAKEN = 6,      ///< Requested string already reserved
  ACIP_ERROR_STRING_INVALID = 7,    ///< String format invalid
  ACIP_ERROR_INTERNAL = 255         ///< Internal server error
} acip_error_code_t;

/** @} */

/**
 * @name ACDS Protocol Constants
 * @{
 * @ingroup acds
 */

/** @brief Maximum session string length (e.g., "swift-river-mountain" = 20 chars) */
#define ACIP_MAX_SESSION_STRING_LEN 48

/** @brief Session expiration time (24 hours in milliseconds) */
#define ACIP_SESSION_EXPIRATION_MS (24ULL * 60 * 60 * 1000)

/** @brief Discovery server default port */
#define ACIP_DISCOVERY_DEFAULT_PORT 27225

/** @} */

#ifdef _WIN32
#pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif

/** @} */ /* acds */

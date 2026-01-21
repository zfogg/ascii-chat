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
#include "options/options.h"
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
 * - Fixed part: acip_session_created_t
 * - Variable part: stun_server_t[stun_count] + turn_server_t[turn_count]
 *
 * The server responds to SESSION_CREATE with the generated session identifier,
 * session string (either auto-generated or the provided reserved string), and
 * optional STUN/TURN server information for WebRTC connectivity.
 *
 * The creator is assigned a participant_id and is considered the session initiator
 * (controls session settings in discovery mode).
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_string_len; ///< Length of session string (e.g., 20 for "swift-river-mountain")
  char session_string[48];    ///< Null-padded session string
  uint8_t session_id[16];     ///< UUID as bytes (not string)
  uint8_t participant_id[16]; ///< Creator's participant ID (they are a participant too)
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
 * **HOST NEGOTIATION**: When host_established == 0, the joiner must negotiate
 * with existing peers to determine who becomes the host. When host_established == 1,
 * the joiner can connect directly to the established host.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t success;         ///< 0 = failed, 1 = joined
  uint8_t error_code;      ///< Error code if success == 0
  char error_message[128]; ///< Human-readable error

  uint8_t participant_id[16]; ///< UUID for this participant (valid if success == 1)
  uint8_t session_id[16];     ///< Session UUID
  uint8_t initiator_id[16];   ///< Who created the session (controls settings)

  // Host status for discovery mode negotiation
  uint8_t host_established; ///< 0 = no host yet (negotiate), 1 = host exists (connect directly)
  uint8_t host_id[16];      ///< Host's participant ID (valid if host_established == 1)

  // Peer information for host negotiation (only relevant if host_established == 0)
  uint8_t peer_count; ///< Number of other participants to negotiate with
  // Followed by: uint8_t peer_ids[peer_count][16] (variable length)

  // Server connection information (ONLY if success == 1 AND host_established == 1)
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
 * @brief PARTICIPANT_JOINED (PACKET_TYPE_ACIP_PARTICIPANT_JOINED) - New participant notification
 *
 * Direction: Discovery Server -> Existing Participants
 *
 * Sent to all existing participants when a new participant joins the session.
 * In discovery mode, this triggers NAT quality exchange and host negotiation
 * between the new joiner and existing participants.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];             ///< Session UUID
  uint8_t new_participant_id[16];     ///< UUID of the new participant
  uint8_t new_participant_pubkey[32]; ///< Ed25519 public key of new participant
  uint8_t current_participant_count;  ///< Total participants including new one
} acip_participant_joined_t;

/**
 * @brief PARTICIPANT_LEFT (PACKET_TYPE_ACIP_PARTICIPANT_LEFT) - Participant left notification
 *
 * Direction: Discovery Server -> Remaining Participants
 *
 * Sent to remaining participants when someone leaves the session (gracefully or timeout).
 * If the leaving participant was the host, this triggers host migration.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];          ///< Session UUID
  uint8_t left_participant_id[16]; ///< UUID of participant who left
  uint8_t was_host;                ///< 1 if the leaving participant was the host
  uint8_t remaining_count;         ///< Participants remaining in session
} acip_participant_left_t;

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
 * @name ACDS Ring Consensus Protocol (New P2P Design)
 * @{
 * @ingroup acds
 *
 * NEW ARCHITECTURE: Proactive future host election every 5 minutes.
 * Participants form a virtual ring and rotate who collects NAT data.
 * Every 5 minutes, a new "quorum leader" emerges with complete knowledge
 * and elects the future host. This pre-elected host is announced to all
 * participants so they know who will take over if current host dies.
 *
 * Benefits:
 * - No election delay when host dies (future host already known)
 * - Fresh NAT data every 5 minutes (not stale)
 * - Automatic rotation ensures fair load (each participant gets a turn)
 * - Ring topology enables P2P coordination without central ACDS
 */

/**
 * @brief PARTICIPANT_LIST (PACKET_TYPE_ACIP_PARTICIPANT_LIST) - Ordered ring list
 *
 * Direction: ACDS -> All Participants
 *
 * Broadcast by ACDS after session join or when participant joins/leaves.
 * Lists all participants in deterministic ring order (by join time or participant ID).
 * Participants use this to determine:
 * - My position in the ring
 * - Who is next in ring (for NAT collection)
 * - Who is quorum leader this round (last in rotation)
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t num_participants; ///< Number of participants in session
  // Followed by: participant_entry_t[num_participants] (variable length)
  // participant_entry_t {
  //   uint8_t participant_id[16];
  //   char address[64];
  //   uint16_t port;
  //   uint8_t connection_type;
  // }
} acip_participant_list_t;

/**
 * @brief Participant entry in ring (variable-length data following PARTICIPANT_LIST)
 *
 * Follows acip_participant_list_t in packet payload. Array of num_participants entries.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t participant_id[16];
  char address[64];        ///< Participant's address (for direct connection)
  uint16_t port;           ///< Participant's listening port
  uint8_t connection_type; ///< acip_connection_type_t
} acip_participant_entry_t;

/**
 * @brief RING_COLLECT (PACKET_TYPE_ACIP_RING_COLLECT) - NAT quality request
 *
 * Direction: Previous Participant -> Next Participant (via direct connection)
 *
 * Sent during ring rotation to request NAT quality from next participant.
 * Forms the "spoke" of the ring, where one participant collects from all others.
 *
 * During each 5-minute round:
 * - Participant[0] connects to Participant[1], gets NAT data
 * - Participant[1] connects to Participant[2], gets NAT data
 * - ... (continues around ring)
 * - Participant[N-1] has all NAT data, runs election, announces future host
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t from_participant_id[16]; ///< Who is requesting
  uint8_t to_participant_id[16];   ///< Who is requested
  uint64_t round_number;           ///< Which 5-minute round (for detection of stale requests)
} acip_ring_collect_t;

/** @} */

/**
 * @name ACDS Discovery Mode Messages (Host Negotiation & Migration)
 * @{
 * @ingroup acds
 */

/**
 * @brief NAT type classification for host selection
 * @ingroup acds
 */
typedef enum {
  ACIP_NAT_TYPE_OPEN = 0,            ///< No NAT (public IP)
  ACIP_NAT_TYPE_FULL_CONE = 1,       ///< Full cone NAT (easiest to traverse)
  ACIP_NAT_TYPE_RESTRICTED = 2,      ///< Address-restricted cone NAT
  ACIP_NAT_TYPE_PORT_RESTRICTED = 3, ///< Port-restricted cone NAT
  ACIP_NAT_TYPE_SYMMETRIC = 4        ///< Symmetric NAT (hardest, requires TURN)
} acip_nat_type_t;

/**
 * @brief Connection type for host announcement
 * @ingroup acds
 */
typedef enum {
  ACIP_CONNECTION_TYPE_DIRECT_PUBLIC = 0, ///< Direct public IP connection
  ACIP_CONNECTION_TYPE_UPNP = 1,          ///< UPnP/NAT-PMP port mapping
  ACIP_CONNECTION_TYPE_STUN = 2,          ///< STUN hole-punching
  ACIP_CONNECTION_TYPE_TURN = 3           ///< TURN relay (fallback)
} acip_connection_type_t;

/**
 * @brief NETWORK_QUALITY (PACKET_TYPE_ACIP_NETWORK_QUALITY) - Unified quality metrics
 *
 * Direction: Participant -> Others (via ring collection, WebRTC signaling, or direct P2P)
 *
 * **NEW DESIGN**: Unified packet for all network quality metrics, replacing separate
 * NAT_QUALITY, HOST_LOST NAT fields, etc. Used in three contexts:
 *
 * 1. **Initial Negotiation** (during host selection before session established)
 *    - Exchanged between first two participants to determine initial host
 *
 * 2. **Ring Collection** (proactive, every 5 minutes)
 *    - Quorum leader collects from all participants for future host election
 *    - Fresh NAT data ensures optimal host selection
 *    - Participants know future host before current host dies
 *
 * 3. **Migration Recovery** (if current host dies unexpectedly)
 *    - Participants exchange fresh NETWORK_QUALITY if pre-elected future host unavailable
 *    - Enables fallback re-election
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t participant_id[16];

  // NAT detection results
  uint8_t has_public_ip;       ///< STUN reflexive == local IP
  uint8_t upnp_available;      ///< UPnP/NAT-PMP port mapping works
  uint8_t upnp_mapped_port[2]; ///< Port we mapped (network byte order)
  uint8_t stun_nat_type;       ///< acip_nat_type_t classification
  uint8_t lan_reachable;       ///< Same subnet as peer (mDNS/ARP)
  uint32_t stun_latency_ms;    ///< RTT to STUN server

  // Bandwidth measurements (critical for host selection)
  uint32_t upload_kbps;    ///< Upload bandwidth in Kbps (from ACDS test)
  uint32_t download_kbps;  ///< Download bandwidth in Kbps (informational)
  uint16_t rtt_to_acds_ms; ///< Latency to ACDS server
  uint8_t jitter_ms;       ///< Packet timing variance (0-255ms)
  uint8_t packet_loss_pct; ///< Packet loss percentage (0-100)

  // Connection info
  char public_address[64]; ///< Our public IP (if has_public_ip or upnp)
  uint16_t public_port;    ///< Our public port

  // ICE candidate summary
  uint8_t ice_candidate_types; ///< Bitmask: 1=host, 2=srflx, 4=relay
} acip_nat_quality_t;

/**
 * @brief HOST_ANNOUNCEMENT (PACKET_TYPE_ACIP_HOST_ANNOUNCEMENT) - Host declaration
 *
 * Direction: Participant -> ACDS
 *
 * Sent by the participant who won host negotiation to announce they are
 * starting the server. ACDS stores this and includes it in future SESSION_JOINED
 * responses.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t host_id[16];     ///< My participant ID
  char host_address[64];   ///< Where clients should connect
  uint16_t host_port;      ///< Port
  uint8_t connection_type; ///< acip_connection_type_t
} acip_host_announcement_t;

/**
 * @brief HOST_DESIGNATED (PACKET_TYPE_ACIP_HOST_DESIGNATED) - Host assignment
 *
 * Direction: ACDS -> All Participants
 *
 * Sent by ACDS after receiving HOST_ANNOUNCEMENT to notify all participants
 * who the host is and where to connect.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t host_id[16];
  char host_address[64];
  uint16_t host_port;
  uint8_t connection_type; ///< acip_connection_type_t
} acip_host_designated_t;

/**
 * @brief HOST_LOST (PACKET_TYPE_ACIP_HOST_LOST) - Host disconnect notification
 *
 * Direction: Participant -> ACDS
 *
 * Lightweight notification that a participant detected the host disconnected.
 * NAT quality data is NOT included - migration participants use pre-elected
 * future host instead of re-electing. If future host unavailable, participants
 * can request fresh NETWORK_QUALITY exchange for re-election if needed.
 *
 * **NEW P2P DESIGN**: This is now just a notification for ACDS bookkeeping.
 * Actual migration happens peer-to-peer without ACDS involvement - participants
 * connect to pre-elected future host immediately upon detection.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t participant_id[16];  ///< Who is reporting
  uint8_t last_host_id[16];    ///< The host that disconnected
  uint32_t disconnect_reason;  ///< 0=unknown, 1=timeout, 2=tcp_reset, 3=graceful
  uint64_t disconnect_time_ms; ///< When disconnect was detected (Unix ms)
} acip_host_lost_t;

/**
 * @brief FUTURE_HOST_ELECTED (PACKET_TYPE_ACIP_FUTURE_HOST_ELECTED) - Future host announcement
 *
 * Direction: Quorum Leader -> ACDS -> All Participants
 *
 * **NEW P2P DESIGN**: Sent proactively by quorum leader after completing
 * ring consensus (every 5 minutes or when new participant joins). Announces
 * to all participants who will become host if current host dies.
 *
 * **Key Insight**: Future host is PRE-ELECTED and stored by everyone.
 * When current host dies, participants don't need to elect - they immediately:
 * - Future host: starts hosting (already know they will)
 * - Others: connect to future host (address already stored)
 * - Total failover time: <500ms (no election!)
 *
 * This packet is broadcast by ACDS after receiving it from quorum leader,
 * ensuring all participants know the migration plan before host dies.
 *
 * @ingroup acds
 */
typedef struct __attribute__((packed)) {
  uint8_t session_id[16];
  uint8_t future_host_id[16];   ///< Who will host if current host dies
  char future_host_address[64]; ///< Where to connect when needed
  uint16_t future_host_port;    ///< Port number
  uint8_t connection_type;      ///< acip_connection_type_t (DIRECT, UPNP, STUN, TURN)
  uint64_t elected_at_round;    ///< Which 5-minute round this was elected in
} acip_future_host_elected_t;

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

/** @brief Discovery server default port (use OPT_ACDS_PORT_INT_DEFAULT from options.h) */
#define ACIP_DISCOVERY_DEFAULT_PORT OPT_ACDS_PORT_INT_DEFAULT

/** @brief Default port for discovery mode hosts (use OPT_PORT_INT_DEFAULT from options.h) */
#define ACIP_HOST_DEFAULT_PORT OPT_PORT_INT_DEFAULT

/** @} */

#ifdef _WIN32
#pragma pack(pop)
#endif

#ifdef __cplusplus
}
#endif

/** @} */ /* acds */

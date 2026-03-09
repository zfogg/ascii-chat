/**
 * @file network/acip/protocol.h
 * @brief ascii-chat IP Protocol (ACIP) packet type definitions
 * @ingroup network
 * @addtogroup acip
 * @{
 *
 * This module defines the ACIP protocol packet types used for session management,
 * WebRTC signaling, and discovery service communication. ACIP is a binary protocol
 * over TCP used by the ascii-chat Discovery Service (ACDS) and clients.
 *
 * # ACIP Binary Protocol Format
 *
 * ## Protocol Overview
 * - **Transport**: TCP (default port 27225 for ACDS)
 * - **Packet Format**: Binary, little-endian
 * - **Authentication**: Ed25519 digital signatures
 * - **Encryption**: Optional TLS or application-level encryption
 * - **Reliability**: CRC32 checksum per packet
 * - **Session Identifiers**: UUID (16 bytes) for sessions and participants
 *
 * ## Packet Structure
 * All ACIP packets follow the same packet_header_t structure:
 * ```
 * Offset  Size  Field          Description
 * ------  ----  -----          -----------
 * 0       8     magic          Magic number (0xA5C11C4A1 = "ASCIICHAT" in hex)
 * 8       2     type           Packet type (PACKET_TYPE_ACIP_* enum, 6000-6199)
 * 10      4     length         Payload length in bytes (0-2MB)
 * 14      4     crc32          CRC32 checksum of payload
 * 18      4     client_id      Client identifier (assigned by server)
 * 22      N     payload        Payload data (N = length)
 * ------
 * Total: 22 + payload_length bytes
 * ```
 *
 * ## Version Information
 * - **ACIP Version**: 1.0
 * - **Release Date**: January 2026
 * - **Status**: Production
 * - **Backward Compatibility**: ACIP 1.0 packets are version-locked to 1.0
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Session management packet type definitions (6000-6008)
 * 2. WebRTC signaling packet type definitions (6009-6010)
 * 3. String reservation packet type definitions (6020-6023)
 * 4. Ring consensus and host negotiation (6050-6068)
 * 5. Bandwidth testing (6070-6075)
 * 6. Discovery service control and error handling (6190, 6199)
 *
 * PACKET RANGE ALLOCATION:
 * ========================
 * ACIP packets use range 6000-6199 for discovery protocol operations.
 *
 * - 6000-6008: Session management (CREATE, JOIN, LEAVE, etc.)
 * - 6009-6010: WebRTC signaling (SDP offers/answers, ICE candidates)
 * - 6020-6023: String reservation (reserve, renew, release)
 * - 6050-6068: Ring consensus and host negotiation
 * - 6070-6075: Bandwidth testing and broadcast acknowledgment
 * - 6190, 6199: Discovery ping and error response
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - network/packet.h: Defines packet_header_t and uses ACIP packet types in packet_type_t enum
 * - network/acip/acds.h: ACDS-specific message structures and payload definitions
 * - network/acip/transport.h: Transport abstraction layer for different connection types
 * - lib/network/acip/: Protocol implementation files (send.c, handlers.c, etc.)
 *
 * @note ACIP packets are defined in the same packet_type_t enum as ascii-chat
 *       packets, but use a separate numeric range (6000-6199).
 *
 * @note All ACIP packets use the same packet header structure as ascii-chat
 *       (magic, type, length, CRC32, client_id).
 *
 * @note Payload CRC32 is computed over payload bytes only (not the header).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0 (ACIP Protocol Refactoring)
 */

#pragma once

#include <stdint.h>
#include "../../network/packet/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name ACIP Packet Type Reference
 * @{
 * @ingroup acip
 *
 * ACIP packet types are defined in the main packet_type_t enum (network/packet.h).
 *
 * **Range**: 6000-6199 (reserved for ACIP discovery protocol)
 *
 * ## Session Management Packets (6000-6008)
 * Handles session creation, discovery, joining, and termination.
 *
 * - `6000`: SESSION_CREATE - Client requests new session creation
 * - `6001`: SESSION_CREATED - Server confirms session created with ID
 * - `6002`: SESSION_LOOKUP - Client queries for session by string identifier
 * - `6003`: SESSION_INFO - Server returns session metadata (no connection details)
 * - `6004`: SESSION_JOIN - Client requests to join existing session
 * - `6005`: SESSION_JOINED - Server confirms join with connection details
 * - `6006`: SESSION_LEAVE - Client notifies leaving session
 * - `6007`: SESSION_END - Server notifies session ended
 * - `6008`: SESSION_RECONNECT - Client reconnects after temporary disconnect
 *
 * ## WebRTC Signaling Packets (6009-6010)
 * Relays WebRTC session description and ICE candidate exchange.
 *
 * - `6009`: WEBRTC_SDP - SDP offer/answer for P2P negotiation
 * - `6010`: WEBRTC_ICE - ICE candidate for NAT traversal
 *
 * ## String Reservation Packets (6020-6023)
 * Handles session string reservation for custom identifiers.
 *
 * - `6020`: STRING_RESERVE - Request to reserve custom session string
 * - `6021`: STRING_RESERVED - Confirmation of reserved string
 * - `6022`: STRING_RENEW - Request to extend string reservation
 * - `6023`: STRING_RELEASE - Request to release reserved string
 *
 * ## Ring Consensus Packets (6050-6051)
 * Proactive future host election every 5 minutes.
 *
 * - `6050`: PARTICIPANT_LIST - Participant list with ring order (ACDS -> Participants)
 * - `6051`: RING_COLLECT - Ring collect request (Participant -> Next Participant)
 *
 * ## Host Negotiation & Migration Packets (6060-6068)
 * Dynamic host selection and failover.
 *
 * - `6060`: NETWORK_QUALITY - Network quality metrics exchange
 * - `6061`: HOST_ANNOUNCEMENT - Host announcement (Participant -> ACDS)
 * - `6062`: HOST_DESIGNATED - Host designated (ACDS -> All Participants)
 * - `6063`: SETTINGS_SYNC - Settings sync (Initiator -> Host -> All Participants)
 * - `6064`: SETTINGS_ACK - Settings acknowledgment (Participant -> Initiator)
 * - `6065`: HOST_LOST - Host lost notification (Participant -> ACDS)
 * - `6066`: FUTURE_HOST_ELECTED - Future host elected announcement
 * - `6067`: PARTICIPANT_JOINED - Participant joined notification (ACDS -> Participants)
 * - `6068`: PARTICIPANT_LEFT - Participant left notification (ACDS -> Participants)
 *
 * ## Bandwidth Testing Packets (6070-6071)
 * Network bandwidth measurement during NAT quality detection.
 *
 * - `6070`: BANDWIDTH_TEST - Bandwidth test request (Client -> ACDS)
 * - `6071`: BANDWIDTH_RESULT - Bandwidth test result (ACDS -> Client)
 *
 * ## Broadcast Acknowledgment (6075)
 * - `6075`: BROADCAST_ACK - Broadcast acknowledgment packet
 *
 * ## Control & Discovery Packets (6190-6199)
 * Protocol control and discovery service operations.
 *
 * - `6190`: DISCOVERY_PING - Keepalive ping from client
 * - `6199`: ERROR - Generic error response (payload: acip_error_t)
 *
 * @see packet_type_t in network/packet.h for actual definitions
 * @see network/acip/acds.h for ACDS message structures
 */

// ACIP packet types are defined in packet_type_t enum (network/packet.h):
//
// Session Management (6000-6008):
//   PACKET_TYPE_ACIP_SESSION_CREATE      = 6000  // Client -> Server: Create session
//   PACKET_TYPE_ACIP_SESSION_CREATED     = 6001  // Server -> Client: Session created
//   PACKET_TYPE_ACIP_SESSION_LOOKUP      = 6002  // Client -> Server: Lookup session
//   PACKET_TYPE_ACIP_SESSION_INFO        = 6003  // Server -> Client: Session info
//   PACKET_TYPE_ACIP_SESSION_JOIN        = 6004  // Client -> Server: Join session
//   PACKET_TYPE_ACIP_SESSION_JOINED      = 6005  // Server -> Client: Join confirmed
//   PACKET_TYPE_ACIP_SESSION_LEAVE       = 6006  // Client -> Server: Leave session
//   PACKET_TYPE_ACIP_SESSION_END         = 6007  // Server -> Client: Session ended
//   PACKET_TYPE_ACIP_SESSION_RECONNECT   = 6008  // Client -> Server: Reconnect
//
// WebRTC Signaling (6009-6010):
//   PACKET_TYPE_ACIP_WEBRTC_SDP          = 6009  // Bidirectional: SDP offer/answer
//   PACKET_TYPE_ACIP_WEBRTC_ICE          = 6010  // Bidirectional: ICE candidate
//
// String Reservation (6020-6023):
//   PACKET_TYPE_ACIP_STRING_RESERVE      = 6020  // Client -> Server: Reserve string
//   PACKET_TYPE_ACIP_STRING_RESERVED     = 6021  // Server -> Client: String reserved
//   PACKET_TYPE_ACIP_STRING_RENEW        = 6022  // Client -> Server: Renew reservation
//   PACKET_TYPE_ACIP_STRING_RELEASE      = 6023  // Client -> Server: Release string
//
// Ring Consensus (6050-6051):
//   PACKET_TYPE_ACIP_PARTICIPANT_LIST    = 6050  // ACDS -> Participants: Participant list
//   PACKET_TYPE_ACIP_RING_COLLECT        = 6051  // Participant -> Next: Ring collect
//
// Host Negotiation (6060-6068):
//   PACKET_TYPE_ACIP_NETWORK_QUALITY     = 6060  // Bidirectional: Quality metrics
//   PACKET_TYPE_ACIP_HOST_ANNOUNCEMENT   = 6061  // Participant -> ACDS: Host announcement
//   PACKET_TYPE_ACIP_HOST_DESIGNATED     = 6062  // ACDS -> All: Host designated
//   PACKET_TYPE_ACIP_SETTINGS_SYNC       = 6063  // Initiator -> Host -> All: Settings
//   PACKET_TYPE_ACIP_SETTINGS_ACK        = 6064  // Participant -> Initiator: Ack
//   PACKET_TYPE_ACIP_HOST_LOST           = 6065  // Participant -> ACDS: Host lost
//   PACKET_TYPE_ACIP_FUTURE_HOST_ELECTED = 6066  // Quorum Leader -> All: Future host elected
//   PACKET_TYPE_ACIP_PARTICIPANT_JOINED  = 6067  // ACDS -> Participants: New participant
//   PACKET_TYPE_ACIP_PARTICIPANT_LEFT    = 6068  // ACDS -> Participants: Participant left
//
// Bandwidth Testing (6070-6071):
//   PACKET_TYPE_ACIP_BANDWIDTH_TEST      = 6070  // Client -> ACDS: Bandwidth test
//   PACKET_TYPE_ACIP_BANDWIDTH_RESULT    = 6071  // ACDS -> Client: Bandwidth result
//
// Broadcast Acknowledgment (6075):
//   PACKET_TYPE_ACIP_BROADCAST_ACK       = 6075  // Broadcast acknowledgment
//
// Control & Discovery (6190, 6199):
//   PACKET_TYPE_ACIP_DISCOVERY_PING      = 6190  // Client -> ACDS: Keepalive ping
//   PACKET_TYPE_ACIP_ERROR               = 6199  // Server -> Client: Error message

/** @} */

/**
 * @name ACIP Protocol Utilities
 * @{
 * @ingroup acip
 */

/**
 * @brief Check if packet type is an ACIP packet
 * @param type Packet type to check
 * @return true if packet is ACIP protocol (range 6000-6199), false otherwise
 *
 * Use this to distinguish ACIP packets from ascii-chat packets.
 *
 * @ingroup acip
 */
static inline bool packet_is_acip_type(uint16_t type) {
  return (type >= 100 && type <= 199);
}

/**
 * @brief Check if ACIP packet type is a session management packet
 * @param type Packet type to check
 * @return true if packet is session management (6000-6008), false otherwise
 *
 * @ingroup acip
 */
static inline bool packet_is_acip_session_type(uint16_t type) {
  return (type >= PACKET_TYPE_ACIP_SESSION_CREATE && type <= PACKET_TYPE_ACIP_SESSION_RECONNECT);
}

/**
 * @brief Check if ACIP packet type is a WebRTC signaling packet
 * @param type Packet type to check
 * @return true if packet is WebRTC signaling (6009-6010), false otherwise
 *
 * @ingroup acip
 */
static inline bool packet_is_acip_webrtc_type(uint16_t type) {
  return (type >= PACKET_TYPE_ACIP_WEBRTC_SDP && type <= PACKET_TYPE_ACIP_WEBRTC_ICE);
}

/**
 * @brief Check if ACIP packet type is a string reservation packet
 * @param type Packet type to check
 * @return true if packet is string reservation (6020-6023), false otherwise
 *
 * @ingroup acip
 */
static inline bool packet_is_acip_string_type(uint16_t type) {
  return (type >= PACKET_TYPE_ACIP_STRING_RESERVE && type <= PACKET_TYPE_ACIP_STRING_RELEASE);
}

/** @} */

/** @} */ /* acip */

#ifdef __cplusplus
}
#endif

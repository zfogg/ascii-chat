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
 * 0       4     magic          Magic number (0x4C494341 = "ACIL" in little-endian)
 * 4       2     type           Packet type (PACKET_TYPE_ACIP_* enum, 100-199)
 * 6       2     reserved       Reserved (0)
 * 8       4     length         Payload length in bytes (0-2MB)
 * 12      4     crc32          CRC32 checksum of payload
 * 16      4     client_id      Client identifier (assigned by server)
 * 20      N     payload        Payload data (N = length)
 * ------
 * Total: 20 + payload_length bytes
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
 * 1. Session management packet type definitions (100-109)
 * 2. WebRTC signaling packet type definitions (110-119)
 * 3. String reservation packet type definitions (120-129)
 * 4. Discovery service control packet type definitions (150-198)
 * 5. Error handling packet type definitions
 *
 * PACKET RANGE ALLOCATION:
 * ========================
 * ACIP packets use range 100-199 to avoid conflicts with ascii-chat protocol (1-99).
 *
 * - 100-109: Session management (CREATE, JOIN, LEAVE, etc.)
 * - 110-119: WebRTC signaling (SDP offers/answers, ICE candidates)
 * - 120-129: String reservation (reserve, renew, release)
 * - 150-198: Control and utilities (PING, discovery)
 * - 199: Generic error response
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - network/packet.h: Defines packet_header_t and uses ACIP packet types in packet_type_t enum
 * - network/acip/acds.h: ACDS-specific message structures and payload definitions
 * - network/acip/transport.h: Transport abstraction layer for different connection types
 * - lib/network/acip/: Protocol implementation files (send.c, handlers.c, etc.)
 *
 * @note ACIP packets are defined in the same packet_type_t enum as ascii-chat
 *       packets, but use a separate numeric range (100-199).
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
 * **Range**: 100-199 (reserved for ACIP, avoiding ascii-chat packets 1-99)
 *
 * ## Session Management Packets (100-109)
 * Handles session creation, discovery, joining, and termination.
 *
 * - `100`: SESSION_CREATE - Client requests new session creation
 * - `101`: SESSION_CREATED - Server confirms session created with ID
 * - `102`: SESSION_LOOKUP - Client queries for session by string identifier
 * - `103`: SESSION_INFO - Server returns session metadata (no connection details)
 * - `104`: SESSION_JOIN - Client requests to join existing session
 * - `105`: SESSION_JOINED - Server confirms join with connection details
 * - `106`: SESSION_LEAVE - Client notifies leaving session
 * - `107`: SESSION_END - Server notifies session ended
 * - `108`: SESSION_RECONNECT - Client reconnects after temporary disconnect
 *
 * ## WebRTC Signaling Packets (110-119)
 * Relays WebRTC session description and ICE candidate exchange.
 *
 * - `110`: WEBRTC_SDP - SDP offer/answer for P2P negotiation
 * - `111`: WEBRTC_ICE - ICE candidate for NAT traversal
 *
 * ## String Reservation Packets (120-129)
 * Handles session string reservation for custom identifiers (future use).
 *
 * - `120`: STRING_RESERVE - Request to reserve custom session string
 * - `121`: STRING_RESERVED - Confirmation of reserved string
 * - `122`: STRING_RENEW - Request to extend string reservation
 * - `123`: STRING_RELEASE - Request to release reserved string
 *
 * ## Participant Notifications (130-149)
 * Broadcasts to participants about session changes.
 *
 * - `130`: PARTICIPANT_JOINED - New participant joined session
 * - `131`: PARTICIPANT_LEFT - Participant left session
 * - `132`: PARTICIPANT_CAPABILITIES - Participant updated capabilities
 *
 * ## Control & Discovery Packets (150-198)
 * Protocol control, keepalive, and discovery service operations.
 *
 * - `150`: DISCOVERY_PING - Keepalive ping from client
 * - `151-198`: Reserved for future control packets
 *
 * ## Error Packets (199)
 * - `199`: ERROR - Generic error response (payload: acip_error_t)
 *
 * @see packet_type_t in network/packet.h for actual definitions
 * @see network/acip/acds.h for ACDS message structures
 */

// ACIP packet types are defined in packet_type_t enum (network/packet.h):
//
// Session Management (100-109):
//   PACKET_TYPE_ACIP_SESSION_CREATE      = 100   // Client -> Server: Create session
//   PACKET_TYPE_ACIP_SESSION_CREATED     = 101   // Server -> Client: Session created
//   PACKET_TYPE_ACIP_SESSION_LOOKUP      = 102   // Client -> Server: Lookup session
//   PACKET_TYPE_ACIP_SESSION_INFO        = 103   // Server -> Client: Session info
//   PACKET_TYPE_ACIP_SESSION_JOIN        = 104   // Client -> Server: Join session
//   PACKET_TYPE_ACIP_SESSION_JOINED      = 105   // Server -> Client: Join confirmed
//   PACKET_TYPE_ACIP_SESSION_LEAVE       = 106   // Client -> Server: Leave session
//   PACKET_TYPE_ACIP_SESSION_END         = 107   // Server -> Client: Session ended
//   PACKET_TYPE_ACIP_SESSION_RECONNECT   = 108   // Client -> Server: Reconnect
//
// WebRTC Signaling (110-119):
//   PACKET_TYPE_ACIP_WEBRTC_SDP          = 110   // Bidirectional: SDP offer/answer
//   PACKET_TYPE_ACIP_WEBRTC_ICE          = 111   // Bidirectional: ICE candidate
//
// Participant Notifications (130-139):
//   PACKET_TYPE_ACIP_PARTICIPANT_JOINED  = 130   // Server -> All: New participant
//   PACKET_TYPE_ACIP_PARTICIPANT_LEFT    = 131   // Server -> All: Participant left
//
// String Reservation (120-129):
//   PACKET_TYPE_ACIP_STRING_RESERVE      = 120   // Client -> Server: Reserve string
//   PACKET_TYPE_ACIP_STRING_RESERVED     = 121   // Server -> Client: String reserved
//   PACKET_TYPE_ACIP_STRING_RENEW        = 122   // Client -> Server: Renew reservation
//   PACKET_TYPE_ACIP_STRING_RELEASE      = 123   // Client -> Server: Release string
//
// Control & Discovery (150-198):
//   PACKET_TYPE_ACIP_DISCOVERY_PING      = 150   // Client -> Server: Keepalive
//
// Error Response:
//   PACKET_TYPE_ACIP_ERROR               = 199   // Server -> Client: Error message

/** @} */

/**
 * @name ACIP Protocol Utilities
 * @{
 * @ingroup acip
 */

/**
 * @brief Check if packet type is an ACIP packet
 * @param type Packet type to check
 * @return true if packet is ACIP protocol (range 100-199), false otherwise
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
 * @return true if packet is session management (100-109), false otherwise
 *
 * @ingroup acip
 */
static inline bool packet_is_acip_session_type(uint16_t type) {
  return (type >= PACKET_TYPE_ACIP_SESSION_CREATE && type <= PACKET_TYPE_ACIP_SESSION_RECONNECT);
}

/**
 * @brief Check if ACIP packet type is a WebRTC signaling packet
 * @param type Packet type to check
 * @return true if packet is WebRTC signaling (110-119), false otherwise
 *
 * @ingroup acip
 */
static inline bool packet_is_acip_webrtc_type(uint16_t type) {
  return (type >= PACKET_TYPE_ACIP_WEBRTC_SDP && type <= PACKET_TYPE_ACIP_WEBRTC_ICE);
}

/**
 * @brief Check if ACIP packet type is a string reservation packet
 * @param type Packet type to check
 * @return true if packet is string reservation (120-129), false otherwise
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

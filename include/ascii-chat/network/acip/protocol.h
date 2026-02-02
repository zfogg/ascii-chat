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
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Session management packet type definitions
 * 2. WebRTC signaling packet type definitions
 * 3. String reservation packet type definitions
 * 4. Discovery service control packet type definitions
 *
 * PACKET RANGE ALLOCATION:
 * ========================
 * ACIP packets use range 100-199 to avoid conflicts with ascii-chat protocol (1-99).
 *
 * - 100-109: Session management
 * - 110-119: WebRTC signaling
 * - 120-129: String reservation
 * - 150-198: Control and utilities
 * - 199: Generic error response
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - network/packet.h: Uses ACIP packet types in packet_type_t enum
 * - network/acip/acds.h: ACDS-specific message structures
 *
 * @note ACIP packets are defined in the same packet_type_t enum as ascii-chat
 *       packets, but use a separate numeric range (100-199).
 *
 * @note All ACIP packets use the same packet header structure as ascii-chat
 *       (magic, type, length, CRC32, client_id).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0 (ACIP Protocol Refactoring)
 */

#pragma once

#include <stdint.h>
#include <ascii-chat/packet.h>

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
 * **Packet Type Allocation**:
 * - 100-109: Session management (CREATE, LOOKUP, JOIN, LEAVE, etc.)
 * - 110-119: WebRTC signaling (SDP, ICE)
 * - 120-129: String reservation (RESERVE, RENEW, RELEASE)
 * - 150-198: Control and utilities (PING, etc.)
 * - 199: Generic error response
 *
 * @see packet_type_t in network/packet.h for actual definitions
 * @see network/acip/acds.h for ACDS message structures
 */

// ACIP packet types are defined in packet_type_t enum (network/packet.h):
//   PACKET_TYPE_ACIP_SESSION_CREATE      = 100
//   PACKET_TYPE_ACIP_SESSION_CREATED     = 101
//   PACKET_TYPE_ACIP_SESSION_LOOKUP      = 102
//   PACKET_TYPE_ACIP_SESSION_INFO        = 103
//   PACKET_TYPE_ACIP_SESSION_JOIN        = 104
//   PACKET_TYPE_ACIP_SESSION_JOINED      = 105
//   PACKET_TYPE_ACIP_SESSION_LEAVE       = 106
//   PACKET_TYPE_ACIP_SESSION_END         = 107
//   PACKET_TYPE_ACIP_SESSION_RECONNECT   = 108
//   PACKET_TYPE_ACIP_WEBRTC_SDP          = 110
//   PACKET_TYPE_ACIP_WEBRTC_ICE          = 111
//   PACKET_TYPE_ACIP_STRING_RESERVE      = 120
//   PACKET_TYPE_ACIP_STRING_RESERVED     = 121
//   PACKET_TYPE_ACIP_STRING_RENEW        = 122
//   PACKET_TYPE_ACIP_STRING_RELEASE      = 123
//   PACKET_TYPE_ACIP_DISCOVERY_PING      = 150
//   PACKET_TYPE_ACIP_ERROR               = 199

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

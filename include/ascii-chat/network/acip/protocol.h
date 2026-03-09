/**
 * @file network/acip/protocol.h
 * @brief ascii-chat Binary Network Protocol (complete system including ACIP discovery service)
 * @ingroup network
 * @addtogroup acip
 * @{
 *
 * This module documents the complete ascii-chat binary network protocol, including:
 * - Universal packet header structure used by ALL packet types
 * - Core protocol negotiation and cryptographic handshake
 * - Session communication (messages, media, audio, control)
 * - ACIP discovery service protocol (6000-6199 range)
 *
 * # ASCII-CHAT BINARY PROTOCOL ARCHITECTURE
 *
 * ## Complete Protocol Overview
 * - **Transport**: TCP (client-server) or custom transports (WebRTC, WebSocket)
 * - **Packet Format**: Binary, little-endian with fixed headers
 * - **Magic Number**: 0xA5C11C4A1 (validates all packet types)
 * - **Authentication**: Ed25519 digital signatures (handshake) or Diffie-Hellman
 * - **Encryption**: XSalsa20-Poly1305 AEAD (after handshake)
 * - **Integrity**: CRC32 checksum per packet payload
 * - **Client Identification**: 32-bit client ID (0 = server, >0 = client)
 *
 * ## Universal Packet Structure
 * ALL packets in ascii-chat (regardless of type) use the same 22-byte header:
 * ```
 * Offset  Size  Field          Description
 * ------  ----  -----          -----------
 * 0       8     magic          Magic number 0xA5C11C4A1 (spells "ASCIICHAT" in hex)
 * 8       2     type           Packet type enum (packet_type_t) - see full range below
 * 10      4     length         Payload length in bytes (0 = header-only, max ~2MB)
 * 14      4     crc32          CRC32 checksum of payload (0 if length == 0)
 * 18      4     client_id      Client identifier (0=server, 1-9=clients)
 * 22      N     payload        Payload data (N = length)
 * ------
 * Total: 22 + payload_length bytes
 * ```
 *
 * ## Complete Packet Type System
 * The ascii-chat binary protocol uses multiple packet type ranges for different purposes:
 *
 * **Type 1: Protocol Version Negotiation**
 * - PACKET_TYPE_PROTOCOL_VERSION = 1 (version and capabilities exchange)
 *
 * **Types 1000-1203: Cryptographic Handshake & Rekeying**
 * - 1000-1099: Client hello and crypto capability exchange
 * - 1100-1109: Handshake exchange (key exchange, authentication)
 * - 1200-1203: Session encryption and rekeying
 * - Note: All handshake packets are ALWAYS sent unencrypted (plaintext)
 *
 * **Types 2000-2004: Session Messages**
 * - 2000: Terminal size updates (width/height changes)
 * - 2001: Audio message data
 * - 2002: Text message data
 * - 2003: Error packets with error codes and messages
 * - 2004: Remote logging (bidirectional log forwarding)
 *
 * **Types 3000-3002: Media Frames**
 * - 3000: ASCII art frames (pre-rendered terminal representation)
 * - 3001: Raw RGB image frames
 * - 3002: H.265/HEVC encoded video frames
 *
 * **Types 4000-4001: Audio Packets**
 * - 4000: Batched raw audio samples
 * - 4001: Batched Opus-encoded audio (pre-compressed)
 *
 * **Types 5000-5008: Control & State**
 * - 5000: Client capabilities (color, features, etc.)
 * - 5001: Keepalive ping packet
 * - 5002: Keepalive pong response
 * - 5003: Client join notification
 * - 5004: Client leave notification
 * - 5005: Stream start request
 * - 5006: Stream stop request
 * - 5007: Clear console command
 * - 5008: Server state broadcast to clients
 *
 * **Types 6000-6199: ACIP Discovery Service Protocol**
 * - 6000-6008: Session management (create, join, leave, lookup)
 * - 6009-6010: WebRTC signaling (SDP, ICE candidates)
 * - 6020-6023: String reservation (reserve, renew, release)
 * - 6050-6051: Ring consensus protocol (future host election)
 * - 6060-6068: Host negotiation & migration (dynamic host failover)
 * - 6070-6071: Bandwidth testing
 * - 6075: Broadcast acknowledgment
 * - 6100-6104: Ring consensus statistics and election
 * - 6190-6199: Discovery keepalive ping and error responses
 *
 * ## Encryption & Handshake Flow
 *
 * **Unencrypted Phase** (connection establishment):
 * 1. Client sends CRYPTO_CLIENT_HELLO (type 1000)
 * 2. Server sends CRYPTO_CAPABILITIES (type 1100)
 * 3. Exchange keys via CRYPTO_KEY_EXCHANGE_INIT/RESP (1102, 1103)
 * 4. Authentication via CRYPTO_AUTH_CHALLENGE/RESPONSE (1104, 1105)
 * 5. Server sends CRYPTO_HANDSHAKE_COMPLETE (type 1108)
 *
 * **Encrypted Phase** (after handshake):
 * - All non-handshake packets are encrypted with XSalsa20-Poly1305
 * - Handshake packets are NEVER encrypted (protocol compliance)
 * - Rekeying packets (types 1201-1203) use old keys, then switch to new keys
 *
 * ## ACIP (ASCII-Chat IP) Discovery Service
 * ACIP is the discovery protocol layer that runs on top of the core binary protocol.
 * It enables P2P session discovery without a central server (decentralized mode).
 *
 * **ACIP Use Cases:**
 * - Creating sessions with unique string identifiers (e.g., "blue-mountain-tiger")
 * - Looking up and joining sessions by string
 * - WebRTC offer/answer exchange for peer negotiation
 * - Dynamic host election when original host disconnects
 * - Participant notification when others join/leave
 *
 * **ACIP Transport:**
 * - Default TCP port: 27225 (configurable for ACDS - ASCII-Chat Discovery Service)
 * - Uses same packet_header_t structure as all other packets
 * - Supports Ed25519 identity signatures for authentication
 * - Optional password protection per session
 *
 * ## Integration with Other Modules
 * - **network/packet.h**: Complete packet_type_t enum with all ranges
 * - **network/packet/parsing.h**: Packet parsing and serialization
 * - **network/acip/acds.h**: ACDS message payload structures
 * - **network/acip/transport.h**: Transport abstraction (TCP, WebSocket, WebRTC)
 * - **network/acip/acds_client.h**: ACDS client connection API
 * - **crypto/crypto.h**: Encryption/decryption operations
 * - **lib/network/acip** (send.c, handlers.c, etc.): Protocol implementation
 *
 * ## Version Information
 * - **Protocol Version**: 1.0
 * - **Release Date**: January 2026
 * - **Status**: Production
 * - **Packet Format Stability**: Version-locked to 1.0 (no breaking changes)
 *
 * @note The packet_header_t structure is identical for ALL 200+ packet types.
 *       The only difference is the 'type' field and payload structure.
 *
 * @note CRC32 is computed over payload bytes ONLY (not including the 22-byte header).
 *
 * @note This file documents protocol architecture. Detailed packet definitions
 *       are in network/packet.h (packet_type_t enum). Detailed ACIP message
 *       structures are in network/acip/acds.h.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0 (Complete Binary Protocol Documentation)
 */

#pragma once

#include <stdint.h>
#include "../../network/packet/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name ACIP Discovery Service Packet Types (6000-6199)
 * @{
 * @ingroup acip
 *
 * ACIP (ASCII-Chat IP) packet types form part of the complete packet_type_t enum
 * defined in network/packet.h. ACIP packets are used for discovery service operations
 * and P2P session coordination.
 *
 * All ACIP packets use the same universal packet_header_t (22 bytes) and are
 * transmitted via TCP to the ACDS (ASCII-Chat Discovery Service) server.
 *
 * **Range**: 6000-6199 (reserved for ACIP discovery protocol)
 *
 * ## Session Management Packets (6000-6008)
 * Handles session creation, discovery, joining, and termination.
 *
 * - `6000`: SESSION_CREATE - Host creates new session with identity and capabilities
 * - `6001`: SESSION_CREATED - ACDS confirms session created with session ID
 * - `6002`: SESSION_LOOKUP - Client queries for session by string identifier
 * - `6003`: SESSION_INFO - ACDS returns session metadata (public info only)
 * - `6004`: SESSION_JOIN - Client requests to join existing session
 * - `6005`: SESSION_JOINED - ACDS confirms join with server connection details
 * - `6006`: SESSION_LEAVE - Client notifies leaving session
 * - `6007`: SESSION_END - ACDS notifies session ended (last participant left)
 * - `6008`: SESSION_RECONNECT - Client reconnects after temporary disconnect
 *
 * ## WebRTC Signaling Packets (6009-6010)
 * Relays WebRTC session description and ICE candidate exchange for P2P negotiation.
 *
 * - `6009`: WEBRTC_SDP - SDP offer/answer for P2P connection negotiation
 * - `6010`: WEBRTC_ICE - ICE candidate for NAT traversal
 *
 * ## String Reservation Packets (6020-6023)
 * Handles session string reservation for custom session identifiers.
 *
 * - `6020`: STRING_RESERVE - Request to reserve custom session string
 * - `6021`: STRING_RESERVED - ACDS confirms string reservation
 * - `6022`: STRING_RENEW - Request to extend string reservation lifetime
 * - `6023`: STRING_RELEASE - Request to release reserved string
 *
 * ## Ring Consensus Packets (6050-6051)
 * Proactive future host election every 5 minutes using ring-based consensus.
 *
 * - `6050`: PARTICIPANT_LIST - Participant list with ring order (ACDS -> Participants)
 * - `6051`: RING_COLLECT - Ring collect request (Participant -> Next Participant)
 *
 * ## Host Negotiation & Migration (6060-6068)
 * Dynamic host selection and failover when original host disconnects.
 *
 * - `6060`: NETWORK_QUALITY - Network quality metrics exchange (all participants)
 * - `6061`: HOST_ANNOUNCEMENT - Host announcement (Participant -> ACDS)
 * - `6062`: HOST_DESIGNATED - Host designated (ACDS -> All Participants)
 * - `6063`: SETTINGS_SYNC - Settings sync (Initiator -> Host -> All Participants)
 * - `6064`: SETTINGS_ACK - Settings acknowledgment (Participant -> Initiator)
 * - `6065`: HOST_LOST - Host lost notification (Participant -> ACDS)
 * - `6066`: FUTURE_HOST_ELECTED - Future host elected announcement
 * - `6067`: PARTICIPANT_JOINED - Participant joined notification (ACDS -> Participants)
 * - `6068`: PARTICIPANT_LEFT - Participant left notification (ACDS -> Participants)
 *
 * ## Bandwidth Testing (6070-6071)
 * Network bandwidth measurement during NAT quality detection.
 *
 * - `6070`: BANDWIDTH_TEST - Bandwidth test request (Client -> ACDS)
 * - `6071`: BANDWIDTH_RESULT - Bandwidth test result (ACDS -> Client)
 *
 * ## Broadcast Acknowledgment (6075)
 * - `6075`: BROADCAST_ACK - Broadcast acknowledgment for reliable delivery
 *
 * ## Ring Consensus Statistics (6100-6104)
 * Statistics collection for ring-based consensus election.
 *
 * - `6100`: RING_MEMBERS - Ring member list announcement (ACDS -> All)
 * - `6101`: STATS_COLLECTION_START - Start statistics collection phase
 * - `6102`: STATS_UPDATE - Statistics update during collection (Participant -> Next)
 * - `6103`: RING_ELECTION_RESULT - Final election result (Leader -> All)
 * - `6104`: STATS_ACK - Acknowledgment of statistics update (Participant -> Sender)
 *
 * ## Discovery Service Control (6190-6199)
 * Protocol control and discovery service operations.
 *
 * - `6190`: DISCOVERY_PING - Keepalive ping from client (verifies connection)
 * - `6199`: ERROR - Generic error response (contains error code and message)
 *
 * @see packet_type_t in network/packet.h for complete packet enum definitions
 * @see network/packet.h for PACKET_HEADER structure and magic constant
 * @see network/acip/acds.h for ACDS message payload structures
 * @see network/acip/acds_client.h for ACDS client connection API
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
 * @brief Check if packet type is an ACIP discovery packet
 * @param type Packet type to check
 * @return true if packet is ACIP protocol (range 6000-6199), false otherwise
 *
 * Use this to distinguish ACIP discovery packets from other ascii-chat packet types.
 *
 * @ingroup acip
 */
static inline bool packet_is_acip_type(uint16_t type) {
  return (type >= 6000 && type <= 6199);
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

/**
 * @file network/acip/acds_server.h
 * @brief ACIP server-side protocol library and ACDS server utilities
 * @ingroup acip
 * @addtogroup acds_server
 * @{
 *
 * Server-side ACIP (ascii-chat IP Protocol) utilities for:
 * - Packet validation and parsing
 * - Protocol compliance checking
 * - Error response generation
 * - Common server-side packet handling
 *
 * # ACDS Server Protocol
 *
 * ## Server Role in ACIP
 * The ACDS (ascii-chat Discovery Service) server orchestrates:
 * 1. **Session Management**: Creating, joining, leaving sessions
 * 2. **Credential Verification**: Password/identity authentication
 * 3. **Connection Brokerage**: Revealing host IP only to authenticated clients
 * 4. **WebRTC Coordination**: Relaying SDP and ICE candidates
 * 5. **Participant Tracking**: Maintaining session roster and state
 *
 * ## Packet Processing Flow
 * 1. **Receive**: acip_transport_recv() reads packet from socket
 * 2. **Validate**: Verify packet header (magic, CRC32, length)
 * 3. **Dispatch**: Route to appropriate handler (session, webrtc, etc.)
 * 4. **Process**: Handler executes business logic
 * 5. **Respond**: Send response packet back to client
 *
 * ## Security Features
 * - **Identity Verification**: Ed25519 digital signatures (replay-protected with timestamps)
 * - **Password Auth**: Argon2id hashing (resistant to brute-force)
 * - **IP Privacy**: Host IP only revealed after authentication
 * - **CRC32 Validation**: Detect payload corruption
 * - **Timestamp Validation**: Prevent replay attacks (5-minute window)
 *
 * ## Session Database
 * - **Storage**: SQLite (persistent across restarts)
 * - **Schema**: session_id (UUID) → session_info (metadata, expiration)
 * - **Expiration**: Sessions expire after 24 hours
 * - **Cleanup**: Automatic garbage collection of expired sessions
 *
 * ## Packet Type Reference (Server Perspective)
 * - **Inbound**: SESSION_CREATE, SESSION_LOOKUP, SESSION_JOIN, SESSION_LEAVE, WEBRTC_SDP, WEBRTC_ICE
 * - **Outbound**: SESSION_CREATED, SESSION_INFO, SESSION_JOINED, SESSION_END, PARTICIPANT_JOINED, PARTICIPANT_LEFT
 * - **Broadcast**: PARTICIPANT_* notifications to all session members
 *
 * **ACIP Protocol Overview:**
 * - Binary TCP protocol (not HTTP/JSON)
 * - Packet-based with CRC32 validation
 * - Ed25519 identity signatures for authentication
 * - Session management and WebRTC signaling
 *
 * **Primary Use Case:**
 * Library functions for building ACIP servers (like ACDS - ascii-chat
 * Discovery Service). Provides reusable validation and packet handling
 * logic that any ACIP server implementation can use.
 *
 * **Integration:**
 * - Used by src/acds/ ACDS server implementation
 * - Can be used by custom ACIP server implementations
 * - Complements network/acip/acds_client.h (client-side protocol)
 *
 * @note This module provides library-level utilities. The ACDS server
 *       application code is in src/acds/.
 *
 * @see network/acip/acds_client.h for client-side ACIP library
 * @see network/acip/acds.h for ACDS message structures
 * @see network/acip/protocol.h for ACIP packet types
 * @see network/acip/transport.h for transport abstraction layer
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0 (ACDS Server API)
 * @}
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../asciichat_errno.h"
#include "../../network/acip/protocol.h"
#include "../../network/acip/acds.h"
#include "../../network/packet/packet.h"
#include "../../platform/socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name ACIP Packet Validation
 * @{
 * @ingroup acip_server
 */

/**
 * @brief Validate ACIP packet type is acceptable for server
 * @param type Packet type to validate
 * @return true if valid server-side ACIP packet, false otherwise
 *
 * Checks if packet type is a valid ACIP packet that a server
 * should accept from clients (range 100-199).
 *
 * @ingroup acip_server
 */
static inline bool acip_server_is_valid_packet_type(packet_type_t type) {
  return packet_is_acip_type(type);
}

/**
 * @brief Validate ACIP session create request
 * @param req Session create request packet
 * @return ASCIICHAT_OK if valid, error code if invalid
 *
 * Validates that a SESSION_CREATE request has all required fields
 * populated correctly (identity, signature, capabilities, etc.).
 *
 * @ingroup acip_server
 */
asciichat_error_t acip_server_validate_session_create(const acip_session_create_t *req);

/**
 * @brief Validate ACIP session join request
 * @param req Session join request packet
 * @return ASCIICHAT_OK if valid, error code if invalid
 *
 * Validates that a SESSION_JOIN request has all required fields
 * populated correctly (session string, identity, signature, etc.).
 *
 * @ingroup acip_server
 */
asciichat_error_t acip_server_validate_session_join(const acip_session_join_t *req);

/** @} */

/**
 * @name ACIP Error Response Helpers
 * @{
 * @ingroup acip_server
 */

/**
 * @brief Send ACIP error response to client
 * @param sockfd Client socket
 * @param error_code ACIP error code
 * @param error_message Human-readable error message (max 255 chars)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Sends PACKET_TYPE_ACIP_ERROR response to client with specified
 * error code and message. Message will be truncated if longer than
 * 255 characters.
 *
 * @ingroup acip_server
 */
asciichat_error_t acip_server_send_error(socket_t sockfd, acip_error_code_t error_code, const char *error_message);

/** @} */

/** @} */ /* acip_server */

#ifdef __cplusplus
}
#endif

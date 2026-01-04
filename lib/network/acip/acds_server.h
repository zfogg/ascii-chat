/**
 * @file network/acip/server.h
 * @brief ACIP server-side protocol library
 * @ingroup acip
 * @addtogroup acip_server
 * @{
 *
 * Server-side ACIP (ASCII-Chat IP Protocol) utilities for:
 * - Packet validation and parsing
 * - Protocol compliance checking
 * - Error response generation
 * - Common server-side packet handling
 *
 * **ACIP Protocol Overview:**
 * - Binary TCP protocol (not HTTP/JSON)
 * - Packet-based with CRC32 validation
 * - Ed25519 identity signatures
 * - Session management and WebRTC signaling
 *
 * **Primary Use Case:**
 * Library functions for building ACIP servers (like ACDS - ASCII-Chat
 * Discovery Service). Provides reusable validation and packet handling
 * logic that any ACIP server implementation can use.
 *
 * **Integration:**
 * - Used by src/acds/ ACDS server implementation
 * - Can be used by custom ACIP server implementations
 * - Complements network/acip/client.h (client-side protocol)
 *
 * @note This module provides library-level utilities. The ACDS server
 *       application code is in src/acds/.
 *
 * @see network/acip/client.h for client-side ACIP library
 * @see network/acip/acds.h for ACDS message structures
 * @see network/acip/protocol.h for ACIP packet types
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0 (ACIP Protocol Refactoring)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "asciichat_errno.h"
#include "network/acip/protocol.h"
#include "network/acip/acds.h"
#include "network/packet.h"
#include "platform/socket.h"

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

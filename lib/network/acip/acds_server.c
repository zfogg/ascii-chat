/**
 * @file network/acip/server.c
 * @brief ACIP server-side protocol implementation
 * @ingroup acip
 *
 * Provides server-side ACIP protocol utilities for:
 * - Packet validation
 * - Error response generation
 * - Protocol compliance checking
 *
 * ACIP (ascii-chat IP Protocol) is the wire protocol for session
 * discovery and WebRTC signaling. This module provides reusable
 * server-side utilities for any ACIP server implementation.
 */

#include "network/acip/acds_server.h"
#include "network/packet.h"
#include "log/logging.h"
#include "common.h"

#include <string.h>

// ============================================================================
// Packet Validation
// ============================================================================

asciichat_error_t acip_server_validate_session_create(const acip_session_create_t *req) {
  if (!req) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Session create request is NULL");
  }

  // Validate capabilities (only bits 0-1 should be set)
  if (req->capabilities & ~0x03) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid capabilities bits (only 0-1 allowed)");
  }

  // Validate max_participants (1-8)
  if (req->max_participants < 1 || req->max_participants > 8) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid max_participants (must be 1-8)");
  }

  // Validate server address is not empty
  if (req->server_address[0] == '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Server address is empty");
  }

  // Validate server port is non-zero
  if (req->server_port == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Server port is zero");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t acip_server_validate_session_join(const acip_session_join_t *req) {
  if (!req) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Session join request is NULL");
  }

  // Validate session string length
  if (req->session_string_len == 0 || req->session_string_len > ACIP_MAX_SESSION_STRING_LEN) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session string length");
  }

  // Validate session string is not empty
  if (req->session_string[0] == '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Session string is empty");
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// Error Response Helpers
// ============================================================================

asciichat_error_t acip_server_send_error(socket_t sockfd, acip_error_code_t error_code, const char *error_message) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket");
  }

  if (!error_message) {
    error_message = "Unknown error";
  }

  acip_error_t error_packet;
  memset(&error_packet, 0, sizeof(error_packet));
  error_packet.error_code = (uint8_t)error_code;

  // Safely copy error message (truncate if needed)
  size_t msg_len = strlen(error_message);
  if (msg_len >= sizeof(error_packet.error_message)) {
    msg_len = sizeof(error_packet.error_message) - 1;
  }
  memcpy(error_packet.error_message, error_message, msg_len);
  error_packet.error_message[msg_len] = '\0';

  // Send ACIP_ERROR packet
  int result = send_packet(sockfd, PACKET_TYPE_ACIP_ERROR, &error_packet, sizeof(error_packet));
  if (result < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send ACIP error packet");
  }

  return ASCIICHAT_OK;
}

/**
 * @file network/errors.c
 * @brief Network error handling utilities implementation
 */

#include <ascii-chat/network/errors.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/log/log.h>
#include <string.h>

bool network_error_is_local_rejection(asciichat_error_t code) {
  return code == ERROR_INVALID_PARAM || code == ERROR_INVALID_FRAME || code == ERROR_NETWORK_SIZE ||
         code == ERROR_BUFFER_OVERFLOW || code == ERROR_BUFFER_FULL || code == ERROR_MEMORY;
}
network_error_action_t network_error_action(asciichat_error_t code) {
  switch (code) {
  case ERROR_AUDIO:
    return NETWORK_ERROR_CONTINUE;
  case ERROR_SESSION_FULL:
  case ERROR_RATE_LIMITED:
  case ERROR_NETWORK:
  case ERROR_NETWORK_TIMEOUT:
  case ERROR_NETWORK_CONNECT:
    return NETWORK_ERROR_RETRY;
  default:
    return NETWORK_ERROR_STOP;
  }
}
asciichat_error_t network_error_public_code(asciichat_error_t code) {
  switch (code) {
  case ERROR_AUDIO:
  case ERROR_SESSION_FULL:
  case ERROR_SESSION_NOT_FOUND:
  case ERROR_RATE_LIMITED:
  case ERROR_INVALID_PASSWORD:
  case ERROR_INVALID_SIGNATURE:
  case ERROR_CRYPTO_AUTH:
  case ERROR_CRYPTO_VERIFICATION:
  case ERROR_NETWORK_PROTOCOL:
  case ERROR_ACDS_STRING_TAKEN:
  case ERROR_ACDS_STRING_INVALID:
    return code;
  case ERROR_CRYPTO:
    return ERROR_CRYPTO_AUTH;
  default:
    return ERROR_INTERNAL;
  }
}
asciichat_error_t network_error_from_acip(uint8_t code) {
  switch (code) {
  case ACIP_ERROR_SESSION_NOT_FOUND:
    return ERROR_SESSION_NOT_FOUND;
  case ACIP_ERROR_SESSION_FULL:
    return ERROR_SESSION_FULL;
  case ACIP_ERROR_INVALID_PASSWORD:
    return ERROR_INVALID_PASSWORD;
  case ACIP_ERROR_INVALID_SIGNATURE:
    return ERROR_INVALID_SIGNATURE;
  case ACIP_ERROR_RATE_LIMITED:
    return ERROR_RATE_LIMITED;
  case ACIP_ERROR_STRING_TAKEN:
    return ERROR_ACDS_STRING_TAKEN;
  case ACIP_ERROR_STRING_INVALID:
    return ERROR_ACDS_STRING_INVALID;
  default:
    return ERROR_INTERNAL;
  }
}
asciichat_error_t network_error_decode(packet_type_t type, const void *payload, size_t size) {
  asciichat_error_t code;
  if (!payload)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Missing remote error payload");
  if (type == PACKET_TYPE_ACIP_ERROR) {
    if (size != sizeof(acip_error_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid discovery error payload length");
    code = network_error_from_acip(((const acip_error_t *)payload)->error_code);
  } else if (type == PACKET_TYPE_ERROR_MESSAGE) {
    char message[MAX_ERROR_MESSAGE_LENGTH + 1];
    asciichat_error_t result = packet_parse_error_message(payload, size, &code, message, sizeof(message), NULL);
    if (result != ASCIICHAT_OK)
      return result;
    code = network_error_public_code(code);
  } else {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected rejection packet type");
  }
  return SET_ERRNO(code, "Remote request rejected: %s", asciichat_error_string(code));
}

asciichat_error_t send_error_packet(socket_t sockfd, asciichat_error_t error_code) {
  return send_error_packet_message(sockfd, error_code, asciichat_error_string(error_code));
}

asciichat_error_t send_error_packet_message(socket_t sockfd, asciichat_error_t error_code, const char *message) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket");
  }

  // Keep the legacy parameter for source compatibility; only allowlisted text goes on the wire.
  (void)message;

  // Construct ACIP error packet
  acip_error_t error = {0};
  switch (error_code) {
  case ERROR_SESSION_NOT_FOUND:
    error.error_code = ACIP_ERROR_SESSION_NOT_FOUND;
    break;
  case ERROR_SESSION_FULL:
    error.error_code = ACIP_ERROR_SESSION_FULL;
    break;
  case ERROR_INVALID_PASSWORD:
    error.error_code = ACIP_ERROR_INVALID_PASSWORD;
    break;
  case ERROR_INVALID_SIGNATURE:
    error.error_code = ACIP_ERROR_INVALID_SIGNATURE;
    break;
  case ERROR_RATE_LIMITED:
    error.error_code = ACIP_ERROR_RATE_LIMITED;
    break;
  case ERROR_ACDS_STRING_TAKEN:
    error.error_code = ACIP_ERROR_STRING_TAKEN;
    break;
  case ERROR_ACDS_STRING_INVALID:
    error.error_code = ACIP_ERROR_STRING_INVALID;
    break;
  default:
    error.error_code = ACIP_ERROR_INTERNAL;
    break;
  }
  SAFE_STRNCPY(error.error_message, asciichat_error_string(network_error_public_code(error_code)),
               sizeof(error.error_message));

  // Send error packet
  int result = send_packet(sockfd, PACKET_TYPE_ACIP_ERROR, &error, sizeof(error));
  if (result < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send error packet");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t check_and_record_rate_limit(rate_limiter_t *rate_limiter, const char *client_ip,
                                              rate_event_type_t event_type, bool *allowed) {
  if (!allowed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing rate-limit decision output");
  *allowed = false;
  asciichat_error_t result = rate_limiter_check(rate_limiter, client_ip, event_type, NULL, allowed);
  if (result != ASCIICHAT_OK) {
    *allowed = false;
    return result;
  }
  if (!*allowed)
    return ASCIICHAT_OK;
  result = rate_limiter_record(rate_limiter, client_ip, event_type);
  if (result != ASCIICHAT_OK)
    *allowed = false;
  return result;
}

asciichat_error_t check_and_record_packet_rate_limit(rate_limiter_t *rate_limiter, const char *client_ip,
                                                     packet_type_t packet_type, bool *allowed) {
  if (!allowed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing rate-limit decision output");
  *allowed = false;
  // Map packet type to rate event type
  rate_event_type_t event_type;

  switch (packet_type) {
  case PACKET_TYPE_IMAGE_FRAME:
    event_type = RATE_EVENT_IMAGE_FRAME;
    break;

  case PACKET_TYPE_AUDIO_BATCH:
  case PACKET_TYPE_AUDIO_OPUS_BATCH:
    event_type = RATE_EVENT_AUDIO;
    break;

  case PACKET_TYPE_PING:
  case PACKET_TYPE_PONG:
    event_type = RATE_EVENT_PING;
    break;

  case PACKET_TYPE_CLIENT_JOIN:
    event_type = RATE_EVENT_CLIENT_JOIN;
    break;

  case PACKET_TYPE_CLIENT_CAPABILITIES:
  case PACKET_TYPE_STREAM_START:
  case PACKET_TYPE_STREAM_STOP:
  case PACKET_TYPE_CLIENT_LEAVE:
    event_type = RATE_EVENT_CONTROL;
    break;

  default:
    // No rate limiting for other packet types
    *allowed = true;
    return ASCIICHAT_OK;
  }

  // Use the existing check_and_record_rate_limit function
  return check_and_record_rate_limit(rate_limiter, client_ip, event_type, allowed);
}

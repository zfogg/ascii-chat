/**
 * @file network/errors.c
 * @brief Network error handling utilities implementation
 */

#include "network/errors.h"
#include "network/network.h"
#include "log/logging.h"
#include <string.h>

asciichat_error_t send_error_packet(socket_t sockfd, asciichat_error_t error_code) {
  return send_error_packet_message(sockfd, error_code, asciichat_error_string(error_code));
}

asciichat_error_t send_error_packet_message(socket_t sockfd, asciichat_error_t error_code, const char *message) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket");
  }

  if (!message) {
    message = asciichat_error_string(error_code);
  }

  // Construct ACIP error packet
  acip_error_t error = {0};
  error.error_code = (uint8_t)error_code;
  SAFE_STRNCPY(error.error_message, message, sizeof(error.error_message));

  // Send error packet
  int result = send_packet(sockfd, PACKET_TYPE_ACIP_ERROR, &error, sizeof(error));
  if (result < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send error packet");
  }

  return ASCIICHAT_OK;
}

bool check_and_record_rate_limit(rate_limiter_t *rate_limiter, const char *client_ip, rate_event_type_t event_type,
                                 socket_t client_socket, const char *operation_name) {
  bool allowed = false;
  asciichat_error_t rate_check = rate_limiter_check(rate_limiter, client_ip, event_type, NULL, &allowed);

  if (rate_check != ASCIICHAT_OK || !allowed) {
    send_error_packet_message(client_socket, ERROR_RATE_LIMITED, "Rate limit exceeded. Please try again later.");
    log_warn("Rate limit exceeded for %s from %s", operation_name, client_ip);
    return false;
  }

  // Record the rate limit event
  rate_limiter_record(rate_limiter, client_ip, event_type);
  return true;
}

bool check_and_record_packet_rate_limit(rate_limiter_t *rate_limiter, const char *client_ip, socket_t client_socket,
                                        packet_type_t packet_type) {
  // Map packet type to rate event type
  rate_event_type_t event_type;
  const char *packet_name;

  switch (packet_type) {
  case PACKET_TYPE_IMAGE_FRAME:
    event_type = RATE_EVENT_IMAGE_FRAME;
    packet_name = "IMAGE_FRAME";
    break;

  case PACKET_TYPE_AUDIO_BATCH:
  case PACKET_TYPE_AUDIO_OPUS_BATCH:
    event_type = RATE_EVENT_AUDIO;
    packet_name = "AUDIO";
    break;

  case PACKET_TYPE_PING:
  case PACKET_TYPE_PONG:
    event_type = RATE_EVENT_PING;
    packet_name = "PING";
    break;

  case PACKET_TYPE_CLIENT_JOIN:
    event_type = RATE_EVENT_CLIENT_JOIN;
    packet_name = "CLIENT_JOIN";
    break;

  case PACKET_TYPE_CLIENT_CAPABILITIES:
  case PACKET_TYPE_STREAM_START:
  case PACKET_TYPE_STREAM_STOP:
  case PACKET_TYPE_CLIENT_LEAVE:
    event_type = RATE_EVENT_CONTROL;
    packet_name = "CONTROL";
    break;

  default:
    // No rate limiting for other packet types
    return true;
  }

  // Use the existing check_and_record_rate_limit function
  return check_and_record_rate_limit(rate_limiter, client_ip, event_type, client_socket, packet_name);
}

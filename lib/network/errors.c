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

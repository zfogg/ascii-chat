#pragma once

/**
 * @file network/errors.h
 * @brief Network error handling utilities
 *
 * Provides helper functions for sending error responses and handling
 * common error patterns in network protocols.
 */

#include "common.h"
#include "platform/socket.h"
#include "network/acip/acds.h"
#include "network/rate_limit/rate_limit.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send an ACIP error packet using asciichat_error_t
 *
 * Converts an asciichat_error_t code to an ACIP error packet and sends it.
 * Uses asciichat_error_string() for the error message.
 *
 * @param sockfd Socket to send error on
 * @param error_code Error code from asciichat_error_t enum
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t send_error_packet(socket_t sockfd, asciichat_error_t error_code);

/**
 * @brief Send an ACIP error packet with custom message
 *
 * Converts an asciichat_error_t code to an ACIP error packet with a
 * custom error message.
 *
 * @param sockfd Socket to send error on
 * @param error_code Error code from asciichat_error_t enum
 * @param message Custom error message (max 255 chars)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t send_error_packet_message(socket_t sockfd, asciichat_error_t error_code, const char *message);

/**
 * @brief Check rate limit and send error if exceeded
 *
 * Helper function that checks rate limit, sends error response if exceeded,
 * and records the event if allowed. Encapsulates the common pattern:
 * 1. Check rate limit
 * 2. Send ERROR_RATE_LIMITED if exceeded
 * 3. Record event if allowed
 *
 * @param rate_limiter Rate limiter instance
 * @param client_ip Client IP address for logging
 * @param event_type Type of event being rate limited
 * @param client_socket Socket to send error packet on
 * @param operation_name Name of operation for logging (e.g., "SESSION_CREATE")
 * @return true if allowed (and event recorded), false if rate limited
 */
bool check_and_record_rate_limit(rate_limiter_t *rate_limiter, const char *client_ip, rate_event_type_t event_type,
                                 socket_t client_socket, const char *operation_name);

/**
 * @brief Map packet type to rate event type and check rate limit
 *
 * Maps packet_type_t to the corresponding rate_event_type_t and performs
 * rate limiting check. Sends ERROR_RATE_LIMITED response if exceeded.
 *
 * Packet type to rate event mapping:
 * - IMAGE_FRAME -> RATE_EVENT_IMAGE_FRAME
 * - AUDIO, AUDIO_BATCH, AUDIO_OPUS, AUDIO_OPUS_BATCH -> RATE_EVENT_AUDIO
 * - PING, PONG -> RATE_EVENT_PING
 * - CLIENT_JOIN -> RATE_EVENT_CLIENT_JOIN
 * - CLIENT_CAPABILITIES, STREAM_START, STREAM_STOP, CLIENT_LEAVE -> RATE_EVENT_CONTROL
 * - All other packets -> No rate limiting (always allowed)
 *
 * @param rate_limiter Rate limiter instance
 * @param client_ip Client IP address
 * @param client_socket Client socket for sending error response
 * @param packet_type Packet type being processed
 * @return true if allowed (and event recorded), false if rate limited
 */
bool check_and_record_packet_rate_limit(rate_limiter_t *rate_limiter, const char *client_ip, socket_t client_socket,
                                        packet_type_t packet_type);

#ifdef __cplusplus
}
#endif

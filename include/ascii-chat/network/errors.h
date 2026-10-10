#pragma once

/**
 * @file network/errors.h
 * @brief Network error handling utilities
 *
 * Provides helper functions for sending error responses and handling
 * common error patterns in network protocols.
 */

#include "../common.h"
#include "../platform/socket.h"
#include "../network/acip/acds.h"
#include "../network/rate_limit/rate_limit.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True only for local packet construction failures that have not reached transport I/O. */
bool network_error_is_local_rejection(asciichat_error_t code);
/** Allowlisted public application code; internal failures become ERROR_INTERNAL. */
asciichat_error_t network_error_public_code(asciichat_error_t code);
asciichat_error_t network_error_from_acip(uint8_t code);
/** Decode either discovery or application error packets with bounded payload validation. */
asciichat_error_t network_error_decode(packet_type_t type, const void *payload, size_t size);
typedef enum { NETWORK_ERROR_RETRY, NETWORK_ERROR_STOP, NETWORK_ERROR_CONTINUE } network_error_action_t;
network_error_action_t network_error_action(asciichat_error_t code);

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
 * @brief Send an ACIP error packet with a public message
 *
 * Maps application codes to discovery wire codes and uses their fixed public text.
 * The legacy message argument is ignored to avoid exposing internal diagnostics.
 *
 * @param sockfd Socket to send error on
 * @param error_code Error code from asciichat_error_t enum
 * @param message Legacy argument, ignored
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t send_error_packet_message(socket_t sockfd, asciichat_error_t error_code, const char *message);

/**
 * @brief Check rate limit and record an allowed event
 *
 * Helper function that checks rate limit
 * and records the event if allowed. Encapsulates the common pattern:
 * 1. Check rate limit
 * 2. Return allowed=false if exceeded
 * 3. Record event if allowed
 *
 * @param rate_limiter Rate limiter instance
 * @param client_ip Client IP address for logging
 * @param event_type Type of event being rate limited
 * @param allowed Output quota decision; false on failure
 * @return ASCIICHAT_OK for a completed decision, typed failure otherwise
 */
asciichat_error_t check_and_record_rate_limit(rate_limiter_t *rate_limiter, const char *client_ip,
                                              rate_event_type_t event_type, bool *allowed);

/**
 * @brief Map packet type to rate event type and check rate limit
 *
 * Maps packet_type_t to the corresponding rate_event_type_t and performs
 * rate limiting check. The caller owns any error response and recovery scope.
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
 * @param packet_type Packet type being processed
 * @param allowed Output quota decision; false on failure
 * @return ASCIICHAT_OK for a completed decision, typed failure otherwise
 */
asciichat_error_t check_and_record_packet_rate_limit(rate_limiter_t *rate_limiter, const char *client_ip,
                                                     packet_type_t packet_type, bool *allowed);

#ifdef __cplusplus
}
#endif

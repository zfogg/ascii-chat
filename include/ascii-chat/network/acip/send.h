/**
 * @file network/acip/send.h
 * @brief ACIP shared/bidirectional packet sending functions
 *
 * Provides bidirectional send functions (used by both client and server):
 * - Audio sending (PCM, Opus, batched)
 * - Ping/Pong keepalive
 * - Error messages and remote logging
 * - Low-level packet_send_via_transport() utility
 *
 * For client-specific sends (image frames, join/leave, capabilities):
 *   → See lib/network/acip/client.h
 *
 * For server-specific sends (ASCII frames, state updates, console control):
 *   → See lib/network/acip/server.h
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/messages.h>
#include <ascii-chat/network/packet.h> // For packet_type_t
#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// Generic Packet Sending
// =============================================================================

/**
 * @brief Send arbitrary packet via transport (generic packet sender)
 *
 * Low-level function that builds packet header and sends via transport.
 * Most code should use the type-specific functions (acip_send_ping, etc.),
 * but this is useful for generic wrappers that handle multiple packet types.
 *
 * @param transport Transport instance
 * @param type Packet type
 * @param payload Packet payload (can be NULL if payload_len==0)
 * @param payload_len Payload length
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t packet_send_via_transport(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len);

// Video/ASCII frame functions moved to:
// - acip_send_ascii_frame → lib/network/acip/server.h
// - acip_send_image_frame → lib/network/acip/client.h

// =============================================================================
// Audio Sending
// =============================================================================

/**
 * @brief Send audio batch packet
 *
 * @param transport Transport instance
 * @param samples Audio samples (float array)
 * @param num_samples Number of samples
 * @param batch_count Number of batches aggregated
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_audio_batch(acip_transport_t *transport, const float *samples, uint32_t num_samples,
                                        uint32_t batch_count);

/**
 * @brief Send Opus-encoded audio packet
 *
 * @param transport Transport instance
 * @param opus_data Opus-encoded audio data
 * @param opus_len Length of encoded data
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_audio_opus(acip_transport_t *transport, const void *opus_data, size_t opus_len);

/**
 * @brief Send batched Opus-encoded audio frames
 *
 * @param transport Transport instance
 * @param opus_data Opus-encoded data (multiple frames concatenated)
 * @param opus_len Total length of encoded data
 * @param frame_sizes Array of individual frame sizes
 * @param frame_count Number of frames in batch
 * @param sample_rate Sample rate in Hz
 * @param frame_duration Duration per frame in milliseconds
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_audio_opus_batch(acip_transport_t *transport, const void *opus_data, size_t opus_len,
                                             const uint16_t *frame_sizes, uint32_t frame_count, uint32_t sample_rate,
                                             uint32_t frame_duration);

// =============================================================================
// Control/Signaling
// =============================================================================

/**
 * @brief Send ping packet
 *
 * @param transport Transport instance
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_ping(acip_transport_t *transport);

/**
 * @brief Send pong packet
 *
 * @param transport Transport instance
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_pong(acip_transport_t *transport);

// Client control functions moved to lib/network/acip/client.h:
// - acip_send_client_join, acip_send_client_leave
// - acip_send_stream_start, acip_send_stream_stop
// - acip_send_capabilities, acip_send_protocol_version
//
// Server control functions moved to lib/network/acip/server.h:
// - acip_send_clear_console, acip_send_server_state

// =============================================================================
// Messages/Errors
// =============================================================================

/**
 * @brief Send error message packet
 *
 * @param transport Transport instance
 * @param error_code Error code from asciichat_error_t
 * @param message Error message (will be truncated to MAX_ERROR_MESSAGE_LENGTH)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_error(acip_transport_t *transport, uint32_t error_code, const char *message);

/**
 * @brief Send remote log packet
 *
 * @param transport Transport instance
 * @param log_level Log level (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)
 * @param direction Direction hint (0=client->server, 1=server->client)
 * @param message Log message
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_remote_log(acip_transport_t *transport, uint8_t log_level, uint8_t direction,
                                       const char *message);

// =============================================================================
// ACDS (Discovery Server) Response Sending
// =============================================================================

/**
 * @brief Send SESSION_CREATED response packet
 *
 * @param transport Transport instance
 * @param response Session created response structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_session_created(acip_transport_t *transport, const acip_session_created_t *response);

/**
 * @brief Send SESSION_INFO response packet
 *
 * @param transport Transport instance
 * @param info Session info structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_session_info(acip_transport_t *transport, const acip_session_info_t *info);

/**
 * @brief Send SESSION_JOINED response packet
 *
 * @param transport Transport instance
 * @param response Session joined response structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_session_joined(acip_transport_t *transport, const acip_session_joined_t *response);

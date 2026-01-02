/**
 * @file network/acip/send.h
 * @brief ACIP protocol packet sending functions (transport-agnostic)
 *
 * Provides functions to send all ACIP packet types over any transport.
 * Functions use acip_transport_t interface, making them work with
 * TCP, WebSocket, or any future transport.
 *
 * DESIGN PATTERN:
 * ===============
 * All send functions follow the same pattern:
 * 1. Take acip_transport_t* instead of socket_t
 * 2. Build packet structure
 * 3. Call acip_transport_send()
 * 4. Return asciichat_error_t
 *
 * Example usage:
 * ```c
 * acip_transport_t *tcp = acip_tcp_transport_create(sockfd, crypto_ctx);
 *
 * // Send ASCII frame over TCP
 * acip_send_ascii_frame(tcp, frame_data, width, height);
 *
 * // Same code works with WebSocket
 * acip_transport_t *ws = acip_websocket_transport_create(ws_handle, crypto_ctx);
 * acip_send_ascii_frame(ws, frame_data, width, height);
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "network/acip/transport.h"
#include "network/acip/messages.h"
#include "network/packet.h" // For packet_type_t
#include "asciichat_errno.h"
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

// =============================================================================
// Video/ASCII Frame Sending
// =============================================================================

/**
 * @brief Send ASCII frame packet
 *
 * @param transport Transport instance
 * @param frame_data ASCII frame data (null-terminated string)
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_ascii_frame(acip_transport_t *transport, const char *frame_data, uint32_t width,
                                        uint32_t height);

/**
 * @brief Send image frame packet
 *
 * @param transport Transport instance
 * @param pixel_data RGB pixel data
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param pixel_format Pixel format (0=RGB24, 1=RGBA32, etc.)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_image_frame(acip_transport_t *transport, const void *pixel_data, uint32_t width,
                                        uint32_t height, uint32_t pixel_format);

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

/**
 * @brief Send client join packet
 *
 * @param transport Transport instance
 * @param capabilities Client capability flags
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_client_join(acip_transport_t *transport, uint8_t capabilities);

/**
 * @brief Send client leave packet
 *
 * @param transport Transport instance
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_client_leave(acip_transport_t *transport);

/**
 * @brief Send stream start packet
 *
 * @param transport Transport instance
 * @param stream_types Stream type flags (STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_stream_start(acip_transport_t *transport, uint8_t stream_types);

/**
 * @brief Send stream stop packet
 *
 * @param transport Transport instance
 * @param stream_types Stream type flags
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_stream_stop(acip_transport_t *transport, uint8_t stream_types);

/**
 * @brief Send clear console packet
 *
 * @param transport Transport instance
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_clear_console(acip_transport_t *transport);

/**
 * @brief Send server state packet
 *
 * @param transport Transport instance
 * @param state Server state structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_server_state(acip_transport_t *transport, const server_state_packet_t *state);

/**
 * @brief Send client capabilities packet
 *
 * @param transport Transport instance
 * @param cap_data Capability data
 * @param cap_len Data length
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_capabilities(acip_transport_t *transport, const void *cap_data, size_t cap_len);

/**
 * @brief Send protocol version packet
 *
 * @param transport Transport instance
 * @param version Protocol version structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_protocol_version(acip_transport_t *transport, const protocol_version_packet_t *version);

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

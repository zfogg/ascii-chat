/**
 * @file network/acip/client.h
 * @brief ACIP client-side protocol API
 * @ingroup acip
 *
 * Client-side API for ascii-chat protocol.
 * Provides functions for:
 * - Receiving and dispatching packets from server
 * - Sending image frames to server
 * - Sending client control messages (join, leave, stream control)
 * - Sending capabilities and protocol version
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#ifndef NETWORK_ACIP_CLIENT_H
#define NETWORK_ACIP_CLIENT_H

#include "../../network/acip/transport.h"
#include "../../network/acip/handlers.h"
#include "../../network/packet.h"
#include "../../asciichat_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Client Receive API
// =============================================================================

/**
 * @brief Receive packet from server and dispatch to callbacks
 *
 * High-level receive function for client side. Receives a single packet
 * from the transport, handles decryption, and dispatches to appropriate
 * callback handler.
 *
 * @param transport Transport instance
 * @param callbacks Callback structure for handling received packets
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_client_receive_and_dispatch(acip_transport_t *transport,
                                                   const acip_client_callbacks_t *callbacks);

// =============================================================================
// Client Send API (client → server)
// =============================================================================

/**
 * @brief Send image frame to server (client → server)
 *
 * Sends webcam video frame as RGB24 pixel data.
 *
 * @param transport Transport instance
 * @param pixel_data RGB24 pixel data (width * height * 3 bytes)
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param pixel_format Pixel format (e.g., RGB24)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_image_frame(acip_transport_t *transport, const void *pixel_data, uint32_t width,
                                        uint32_t height, uint32_t pixel_format);

/**
 * @brief Announce client join to server (client → server)
 *
 * Notifies server that client is ready to participate in session.
 *
 * @param transport Transport instance
 * @param capabilities Client capability bitmask (video, audio, etc.)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_client_join(acip_transport_t *transport, uint8_t capabilities);

/**
 * @brief Notify server of client leaving (client → server)
 *
 * Clean disconnect notification to server.
 *
 * @param transport Transport instance
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_client_leave(acip_transport_t *transport);

/**
 * @brief Request to start media streaming (client → server)
 *
 * Notifies server that client wants to start sending video/audio.
 *
 * @param transport Transport instance
 * @param stream_types Stream type bitmask (video, audio, etc.)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_stream_start(acip_transport_t *transport, uint8_t stream_types);

/**
 * @brief Request to stop media streaming (client → server)
 *
 * Notifies server that client wants to stop sending video/audio.
 *
 * @param transport Transport instance
 * @param stream_types Stream type bitmask (video, audio, etc.)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_stream_stop(acip_transport_t *transport, uint8_t stream_types);

/**
 * @brief Send terminal capabilities to server (client → server)
 *
 * Reports client's terminal capabilities (color support, dimensions, etc.)
 *
 * @param transport Transport instance
 * @param cap_data Capability data buffer
 * @param cap_len Capability data length
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_capabilities(acip_transport_t *transport, const void *cap_data, size_t cap_len);

/**
 * @brief Send protocol version to server (client → server)
 *
 * Negotiates protocol version with server.
 *
 * @param transport Transport instance
 * @param version Protocol version structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_protocol_version(acip_transport_t *transport, const protocol_version_packet_t *version);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_ACIP_CLIENT_H

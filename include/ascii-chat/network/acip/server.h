/**
 * @file network/acip/server.h
 * @brief ACIP server-side protocol API
 * @ingroup acip
 *
 * Server-side API for ascii-chat protocol.
 * Provides functions for:
 * - Receiving and dispatching packets from clients
 * - Sending ASCII frames to clients
 * - Sending server state updates
 * - Sending console control commands
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#ifndef NETWORK_ACIP_SERVER_H
#define NETWORK_ACIP_SERVER_H

#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/asciichat_errno.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Server Receive API
// =============================================================================

/**
 * @brief Receive packet from client and dispatch to callbacks
 *
 * High-level receive function for server side. Receives a single packet
 * from the transport, handles decryption, and dispatches to appropriate
 * callback handler.
 *
 * @param transport Transport instance
 * @param client_ctx Client context pointer (passed to callbacks)
 * @param callbacks Callback structure for handling received packets
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_server_receive_and_dispatch(acip_transport_t *transport, void *client_ctx,
                                                   const acip_server_callbacks_t *callbacks);

// =============================================================================
// Server Send API (server → client)
// =============================================================================

/**
 * @brief Send ASCII frame to client (server → client)
 *
 * Sends rendered ASCII art frame to client for display.
 *
 * @param transport Transport instance
 * @param frame_data ASCII frame data (null-terminated string)
 * @param width Frame width in characters
 * @param height Frame height in characters
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_ascii_frame(acip_transport_t *transport, const char *frame_data, size_t frame_size,
                                        uint32_t width, uint32_t height);

/**
 * @brief Send clear console command to client (server → client)
 *
 * Instructs client to clear the terminal/console.
 *
 * @param transport Transport instance
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_clear_console(acip_transport_t *transport);

/**
 * @brief Send server state update to client (server → client)
 *
 * Notifies client of server state changes (client count, grid layout, etc.)
 *
 * @param transport Transport instance
 * @param state Server state structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_send_server_state(acip_transport_t *transport, const server_state_packet_t *state);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_ACIP_SERVER_H

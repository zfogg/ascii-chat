/**
 * @file network/acip/acds_handlers.h
 * @brief ACIP Discovery Server (ACDS) packet handlers
 *
 * Provides O(1) array-based packet dispatching for ascii-chat Discovery Server.
 * Handles session management, WebRTC signaling, and discovery protocol packets.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "network/acip/transport.h"
#include "network/acip/messages.h"
#include "network/acip/acds.h"
#include "asciichat_errno.h"
#include <stddef.h>

// =============================================================================
// ACDS Handler Callbacks
// =============================================================================

/**
 * @brief ACDS packet handler callbacks
 *
 * Discovery server implements these callbacks to handle incoming packets.
 * NULL callbacks are skipped (no-op).
 *
 * All callbacks receive:
 * - client_socket: Socket file descriptor for the client connection
 * - client_ip: IP address string of the client
 * - app_ctx: Application context (e.g., acds_server_t*)
 */
typedef struct {
  /** @brief Called when client requests session creation */
  void (*on_session_create)(const acip_session_create_t *req, int client_socket, const char *client_ip, void *app_ctx);

  /** @brief Called when client looks up session info */
  void (*on_session_lookup)(const acip_session_lookup_t *req, int client_socket, const char *client_ip, void *app_ctx);

  /** @brief Called when client joins a session */
  void (*on_session_join)(const acip_session_join_t *req, int client_socket, const char *client_ip, void *app_ctx);

  /** @brief Called when client leaves a session */
  void (*on_session_leave)(const acip_session_leave_t *req, int client_socket, const char *client_ip, void *app_ctx);

  /** @brief Called when client sends WebRTC SDP offer/answer */
  void (*on_webrtc_sdp)(const acip_webrtc_sdp_t *sdp, size_t payload_len, int client_socket, const char *client_ip,
                        void *app_ctx);

  /** @brief Called when client sends WebRTC ICE candidate */
  void (*on_webrtc_ice)(const acip_webrtc_ice_t *ice, size_t payload_len, int client_socket, const char *client_ip,
                        void *app_ctx);

  /** @brief Called when client sends discovery ping */
  void (*on_discovery_ping)(const void *payload, size_t payload_len, int client_socket, const char *client_ip,
                            void *app_ctx);

  /** @brief Called when client announces they are hosting (discovery mode) */
  void (*on_host_announcement)(const acip_host_announcement_t *announcement, int client_socket, const char *client_ip,
                               void *app_ctx);

  /** @brief Called when participant reports host has disconnected (discovery mode) */
  void (*on_host_lost)(const acip_host_lost_t *host_lost, int client_socket, const char *client_ip, void *app_ctx);

  /** @brief Application context (passed to all callbacks) */
  void *app_ctx;
} acip_acds_callbacks_t;

/**
 * @brief Handle incoming ACDS packet with O(1) dispatch
 *
 * Dispatches packet to appropriate callback based on type.
 * Uses array-based lookup for constant-time handler selection.
 *
 * @param transport Transport instance (unused, for API consistency)
 * @param type Packet type
 * @param payload Packet payload
 * @param payload_len Payload length
 * @param client_socket Client socket file descriptor
 * @param client_ip Client IP address string
 * @param callbacks Application callbacks
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_handle_acds_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                          size_t payload_len, int client_socket, const char *client_ip,
                                          const acip_acds_callbacks_t *callbacks);

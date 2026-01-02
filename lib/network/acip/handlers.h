/**
 * @file network/acip/handlers.h
 * @brief ACIP protocol packet handlers (transport-agnostic)
 *
 * Provides handler functions for all ACIP packet types (1-199).
 * Handlers are transport-agnostic - they work with any acip_transport_t.
 *
 * DESIGN PATTERN:
 * ===============
 * Handlers use callback pattern to decouple protocol logic from application logic.
 * Applications register callbacks for events they care about.
 *
 * Example usage:
 * ```c
 * // Define application callbacks
 * void my_on_ascii_frame(const ascii_frame_packet_t *frame, const void *data, size_t len, void *ctx) {
 *   // Render frame to terminal
 * }
 *
 * // Register callbacks
 * acip_client_callbacks_t callbacks = {
 *   .on_ascii_frame = my_on_ascii_frame,
 *   .app_ctx = my_app_state
 * };
 *
 * // Process incoming packet
 * acip_handle_client_packet(transport, type, payload, payload_len, &callbacks);
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "network/acip/transport.h"
#include "network/acip/messages.h"
#include "asciichat_errno.h"
#include <stddef.h>

// =============================================================================
// Client-Side Handler Callbacks
// =============================================================================

/**
 * @brief Client-side packet handler callbacks
 *
 * Applications implement these callbacks to handle incoming packets.
 * NULL callbacks are skipped (no-op).
 */
typedef struct {
  /** @brief Called when ASCII frame received from server */
  void (*on_ascii_frame)(const ascii_frame_packet_t *header, const void *frame_data, size_t data_len, void *ctx);

  /** @brief Called when audio batch received from server */
  void (*on_audio_batch)(const audio_batch_packet_t *header, const float *samples, size_t num_samples, void *ctx);

  /** @brief Called when Opus audio received from server */
  void (*on_audio_opus)(const void *opus_data, size_t opus_len, void *ctx);

  /** @brief Called when Opus batch received from server */
  void (*on_audio_opus_batch)(const void *batch_data, size_t batch_len, void *ctx);

  /** @brief Called when server state update received */
  void (*on_server_state)(const server_state_packet_t *state, void *ctx);

  /** @brief Called when error message received */
  void (*on_error)(const error_packet_t *header, const char *message, void *ctx);

  /** @brief Called when remote log received */
  void (*on_remote_log)(const remote_log_packet_t *header, const char *message, void *ctx);

  /** @brief Called when ping received (should send pong) */
  void (*on_ping)(void *ctx);

  /** @brief Called when pong received */
  void (*on_pong)(void *ctx);

  /** @brief Application context (passed to all callbacks) */
  void *app_ctx;
} acip_client_callbacks_t;

/**
 * @brief Handle incoming packet on client side
 *
 * Dispatches packet to appropriate callback based on type.
 * Transport-agnostic - works with TCP, WebSocket, etc.
 *
 * @param transport Transport instance
 * @param type Packet type
 * @param payload Packet payload
 * @param payload_len Payload length
 * @param callbacks Application callbacks
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_handle_client_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len, const acip_client_callbacks_t *callbacks);

// =============================================================================
// Server-Side Handler Callbacks
// =============================================================================

/**
 * @brief Server-side packet handler callbacks
 *
 * Applications implement these callbacks to handle incoming packets.
 * NULL callbacks are skipped (no-op).
 */
typedef struct {
  /** @brief Called when client sends image frame */
  void (*on_image_frame)(const image_frame_packet_t *header, const void *pixel_data, size_t data_len, void *client_ctx,
                         void *app_ctx);

  /** @brief Called when client sends audio batch */
  void (*on_audio_batch)(const audio_batch_packet_t *header, const float *samples, size_t num_samples, void *client_ctx,
                         void *app_ctx);

  /** @brief Called when client sends Opus audio */
  void (*on_audio_opus)(const void *opus_data, size_t opus_len, void *client_ctx, void *app_ctx);

  /** @brief Called when client sends Opus batch */
  void (*on_audio_opus_batch)(const void *batch_data, size_t batch_len, void *client_ctx, void *app_ctx);

  /** @brief Called when client joins */
  void (*on_client_join)(const void *join_data, size_t data_len, void *client_ctx, void *app_ctx);

  /** @brief Called when client leaves */
  void (*on_client_leave)(void *client_ctx, void *app_ctx);

  /** @brief Called when client starts streaming */
  void (*on_stream_start)(uint8_t stream_types, void *client_ctx, void *app_ctx);

  /** @brief Called when client stops streaming */
  void (*on_stream_stop)(uint8_t stream_types, void *client_ctx, void *app_ctx);

  /** @brief Called when client sends capabilities */
  void (*on_capabilities)(const void *cap_data, size_t data_len, void *client_ctx, void *app_ctx);

  /** @brief Called when ping received (should send pong) */
  void (*on_ping)(void *client_ctx, void *app_ctx);

  /** @brief Called when remote log received from client */
  void (*on_remote_log)(const remote_log_packet_t *header, const char *message, void *client_ctx, void *app_ctx);

  /** @brief Application context (passed to all callbacks) */
  void *app_ctx;
} acip_server_callbacks_t;

/**
 * @brief Handle incoming packet on server side
 *
 * Dispatches packet to appropriate callback based on type.
 * Transport-agnostic - works with TCP, WebSocket, etc.
 *
 * @param transport Transport instance
 * @param type Packet type
 * @param payload Packet payload
 * @param payload_len Payload length
 * @param client_ctx Per-client context (e.g., client_info_t*)
 * @param callbacks Application callbacks
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t acip_handle_server_packet(acip_transport_t *transport, packet_type_t type, const void *payload,
                                            size_t payload_len, void *client_ctx,
                                            const acip_server_callbacks_t *callbacks);

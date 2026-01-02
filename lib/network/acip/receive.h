/**
 * @file network/acip/receive.h
 * @brief ACIP transport receive API with automatic handler dispatch
 *
 * Provides high-level receive functions that combine packet reception
 * with automatic dispatch to registered ACIP handlers. This creates a
 * symmetric API with the send functions (acip_send_*).
 *
 * DESIGN PATTERN:
 * ===============
 * Instead of manually calling receive_packet_secure() and then dispatching,
 * applications use these wrapper functions for a cleaner callback-based API:
 *
 * ```c
 * // Old pattern (manual):
 * packet_envelope_t envelope;
 * receive_packet_secure(sock, crypto_ctx, true, &envelope);
 * acip_handle_server_packet(transport, envelope.type, envelope.data, ...);
 * buffer_pool_free(envelope.allocated_buffer, ...);
 *
 * // New pattern (callback-based):
 * acip_transport_receive_and_dispatch_server(transport, client_ctx, &callbacks);
 * ```
 *
 * BENEFITS:
 * =========
 * - Symmetric API: receiving matches sending pattern
 * - Transport-agnostic: works with TCP, WebSocket, etc.
 * - Automatic cleanup: envelope buffers freed automatically
 * - Less boilerplate: one call instead of 3-4
 * - Error handling: consistent asciichat_error_t returns
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "network/acip/transport.h"
#include "network/acip/handlers.h"
#include "asciichat_errno.h"

/**
 * @brief Receive and dispatch one packet on server side
 *
 * Receives one packet via transport, automatically dispatches to the
 * appropriate ACIP handler callback, and cleans up allocated buffers.
 *
 * This function blocks until a packet arrives or an error occurs.
 *
 * @param transport Transport instance (must have valid socket)
 * @param client_ctx Per-client context (e.g., client_info_t*) passed to callbacks
 * @param callbacks Server-side handler callbacks
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Blocks until packet received or error
 * @note Automatically frees envelope buffer
 * @note Returns ERROR_NETWORK on connection close/EOF
 * @note Returns ERROR_CRYPTO on security violation
 */
asciichat_error_t acip_transport_receive_and_dispatch_server(acip_transport_t *transport, void *client_ctx,
                                                             const acip_server_callbacks_t *callbacks);

/**
 * @brief Receive and dispatch one packet on client side
 *
 * Receives one packet via transport, automatically dispatches to the
 * appropriate ACIP handler callback, and cleans up allocated buffers.
 *
 * This function blocks until a packet arrives or an error occurs.
 *
 * @param transport Transport instance (must have valid socket)
 * @param callbacks Client-side handler callbacks
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Blocks until packet received or error
 * @note Automatically frees envelope buffer
 * @note Returns ERROR_NETWORK on connection close/EOF
 * @note Returns ERROR_CRYPTO on security violation
 */
asciichat_error_t acip_transport_receive_and_dispatch_client(acip_transport_t *transport,
                                                             const acip_client_callbacks_t *callbacks);

/**
 * @file client/protocol.h
 * @ingroup client_protocol
 * @brief ascii-chat Client Protocol Handler Interface
 *
 * Defines the protocol handling interface for client-side packet
 * processing and data reception thread management.
 *
 * The protocol handler manages the data reception thread, which continuously
 * receives packets from the server, decrypts them, and dispatches them to
 * appropriate handlers based on packet type. It also detects connection loss
 * and signals the main thread to initiate reconnection.
 *
 * ## Command Types Handled
 *
 * The client processes these command/packet types from the server:
 * - **PACKET_TYPE_ASCII_FRAME**: Terminal display frame (rendered to stdout)
 * - **PACKET_TYPE_AUDIO_BATCH**: Opus-encoded audio samples (sent to audio playback)
 * - **PACKET_TYPE_PONG**: Keepalive response (recorded for timeout detection)
 * - **PACKET_TYPE_CLEAR_CONSOLE**: Terminal clear command (resets display)
 * - **PACKET_TYPE_SERVER_STATE**: Server state updates (logged for debugging)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 *
 * @see topic_client_protocol "Protocol Architecture"
 */

#pragma once

#include <stdbool.h>
#include <ascii-chat/network/acip/handlers.h>

/**
 * @brief Start protocol connection handling
 *
 * Spawns the data reception thread in the global client worker thread pool.
 * The thread begins receiving and processing packets from the server.
 * Must be called after the server connection is established and the
 * cryptographic handshake is complete.
 *
 * The thread will:
 * 1. Continuously receive packets from the server socket
 * 2. Decrypt packets using the negotiated session key
 * 3. Dispatch packets to appropriate handlers based on type
 * 4. Detect connection loss and signal the main thread
 *
 * @return 0 on success, negative on error (thread pool full or other resources exhausted)
 *
 * @note Must be paired with protocol_stop_connection() to properly shut down
 *       the thread before reconnection or exit.
 *
 * @ingroup client_protocol
 *
 * @see protocol_stop_connection "Stop data reception thread"
 * @see protocol_connection_lost "Check if connection was lost"
 */
int protocol_start_connection();

/**
 * @brief Stop protocol connection handling
 *
 * Signals the data reception thread to exit and waits for it to join.
 * Performs proper cleanup to prevent resource leaks and race conditions
 * during reconnection or shutdown.
 *
 * The function:
 * 1. Closes the server socket (interrupts blocking recv() in data thread)
 * 2. Sets the global shutdown flag (signals thread to exit)
 * 3. Waits for the data reception thread to join (polls for thread exit)
 *
 * This must be called before reconnecting to prevent socket descriptor leaks
 * and to ensure the thread has fully cleaned up before reuse.
 *
 * @note This function blocks until the data reception thread exits.
 *       Typical wait time: <100ms (socket closure immediately interrupts recv()).
 *
 * @ingroup client_protocol
 *
 * @see protocol_start_connection "Start data reception thread"
 */
void protocol_stop_connection();

/**
 * @brief Check if connection has been lost
 *
 * Atomically checks whether the data reception thread has detected connection loss.
 * Connection loss is detected by:
 * - Socket read error (recv() returns error)
 * - Socket closed by server (recv() returns 0)
 * - Decryption failure (corrupted stream)
 * - Invalid packet magic number (out of sync)
 *
 * Once loss is detected, this flag remains true until the next successful connection.
 *
 * @return true if connection lost (detected by protocol thread), false otherwise
 *
 * @note This is checked by the main thread to trigger reconnection logic.
 *       The flag is set atomically by the protocol thread and checked atomically by main.
 *
 * @ingroup client_protocol
 *
 * @see server_connection_lost "Signal connection loss (called by protocol thread)"
 */
bool protocol_connection_lost();

/**
 * @brief Get ACIP client callbacks for packet dispatch
 *
 * Returns the packet dispatch callbacks structure used to route received packets
 * to appropriate handlers. This is primarily used for ACDS signaling packet
 * dispatch and WebRTC peer negotiation.
 *
 * The callbacks structure includes handlers for:
 * - ASCII frame rendering
 * - Audio batch processing
 * - Server state updates
 * - Connection control commands (clear, reset, etc.)
 *
 * @return Pointer to client callbacks structure (never NULL)
 *
 * Used by WebRTC sessions to receive and dispatch ACDS signaling packets.
 *
 * @ingroup client_protocol
 */
const acip_client_callbacks_t *protocol_get_acip_callbacks();

/**
 * @file client/protocol.h
 * @ingroup client_protocol
 * @brief ascii-chat Client Protocol Handler Interface
 *
 * Defines the protocol handling interface for client-side packet
 * processing and data reception thread management.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdbool.h>
#include "network/acip/handlers.h"

/**
 * @brief Start protocol connection handling
 * @return 0 on success, negative on error
 *
 * @ingroup client_protocol
 */
int protocol_start_connection();

/**
 * @brief Stop protocol connection handling
 *
 * @ingroup client_protocol
 */
void protocol_stop_connection();

/**
 * @brief Check if connection has been lost
 * @return true if connection lost, false otherwise
 *
 * @ingroup client_protocol
 */
bool protocol_connection_lost();

/**
 * @brief Get ACIP client callbacks for packet dispatch
 * @return Pointer to client callbacks structure
 *
 * Used by WebRTC sessions to receive and dispatch ACDS signaling packets.
 *
 * @ingroup client_protocol
 */
const acip_client_callbacks_t *protocol_get_acip_callbacks();

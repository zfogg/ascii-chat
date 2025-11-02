/**
 * @file protocol.h
 * @brief ASCII-Chat Client Protocol Handler Interface
 *
 * Defines the protocol handling interface for client-side packet
 * processing and data reception thread management.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Start protocol connection handling
 * @return 0 on success, negative on error
 */
int protocol_start_connection();

/**
 * @brief Stop protocol connection handling
 */
void protocol_stop_connection();

/**
 * @brief Check if connection has been lost
 * @return true if connection lost, false otherwise
 */
bool protocol_connection_lost();

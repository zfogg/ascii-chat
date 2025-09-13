/**
 * @file keepalive.h
 * @brief ASCII-Chat Client Connection Keepalive Management Interface
 *
 * Defines the interface for connection keepalive thread management
 * and ping/pong coordination in the ASCII-Chat client.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Start keepalive/ping thread
 * @return 0 on success, negative on error
 */
int keepalive_start_thread();

/**
 * @brief Stop keepalive/ping thread
 */
void keepalive_stop_thread();

/**
 * @brief Check if keepalive thread has exited
 * @return true if thread exited, false otherwise
 */
bool keepalive_thread_exited();
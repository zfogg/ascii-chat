/**
 * @file main.h
 * @brief ASCII-Chat Client Main Module Interface
 *
 * Defines the main entry point interface and global shutdown coordination.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Check if shutdown has been requested
 * @return true if shutdown requested, false otherwise
 */
bool should_exit();

/**
 * @brief Signal that shutdown should be requested
 */
void signal_exit();
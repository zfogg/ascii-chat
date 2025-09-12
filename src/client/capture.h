/**
 * @file capture.h
 * @brief ASCII-Chat Client Media Capture Management Interface
 *
 * Defines the interface for webcam video capture and transmission
 * management in the ASCII-Chat client.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Initialize capture subsystem
 * @return 0 on success, negative on error
 */
int capture_init();

/**
 * @brief Start capture thread
 * @return 0 on success, negative on error
 */
int capture_start_thread();

/**
 * @brief Stop capture thread
 */
void capture_stop_thread();

/**
 * @brief Check if capture thread has exited
 * @return true if thread exited, false otherwise
 */
bool capture_thread_exited();

/**
 * @brief Cleanup capture subsystem
 */
void capture_cleanup();
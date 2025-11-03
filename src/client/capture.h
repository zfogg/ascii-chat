/**
 * @file client/capture.h
 * @ingroup client_capture
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
 *
 * @ingroup client_capture
 */
int capture_init();

/**
 * @brief Start capture thread
 * @return 0 on success, negative on error
 *
 * @ingroup client_capture
 */
int capture_start_thread();

/**
 * @brief Stop capture thread
 *
 * @ingroup client_capture
 */
void capture_stop_thread();

/**
 * @brief Check if capture thread has exited
 * @return true if thread exited, false otherwise
 *
 * @ingroup client_capture
 */
bool capture_thread_exited();

/**
 * @brief Cleanup capture subsystem
 *
 * @ingroup client_capture
 */
void capture_cleanup();

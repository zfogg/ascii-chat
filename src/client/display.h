/**
 * @file client/display.h
 * @ingroup client_display
 * @brief ASCII-Chat Client Display Management Interface
 *
 * Defines the display subsystem interface for terminal rendering,
 * TTY management, and frame output operations.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <stdbool.h>
#include "platform/terminal.h"

/**
 * @brief Initialize display subsystem
 * @return 0 on success, negative on error
 *
 * @ingroup client_display
 */
int display_init();

/**
 * @brief Check if display has TTY capability
 * @return true if TTY available, false otherwise
 *
 * @ingroup client_display
 */
bool display_has_tty();

/**
 * @brief Perform full display reset
 *
 * @ingroup client_display
 */
void display_full_reset();

/**
 * @brief Reset display state for new connection
 *
 * Call this when starting a new connection to reset first frame tracking.
 *
 * @ingroup client_display
 */
void display_reset_for_new_connection();

/**
 * @brief Disable terminal logging for first frame
 *
 * Call this before clearing the display for the first frame to prevent
 * log output from interfering with ASCII display.
 *
 * @ingroup client_display
 */
void display_disable_logging_for_first_frame();

/**
 * @brief Render ASCII frame to display
 * @param frame_data ASCII frame data to render
 * @param is_snapshot_frame Whether this is the final snapshot frame
 *
 * @ingroup client_display
 */
void display_render_frame(const char *frame_data, bool is_snapshot_frame);

/**
 * @brief Cleanup display subsystem
 *
 * @ingroup client_display
 */
void display_cleanup();

extern tty_info_t g_tty_info;

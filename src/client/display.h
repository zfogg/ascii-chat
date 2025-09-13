/**
 * @file display.h
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

/**
 * @brief Initialize display subsystem
 * @return 0 on success, negative on error
 */
int display_init();

/**
 * @brief Check if display has TTY capability
 * @return true if TTY available, false otherwise
 */
bool display_has_tty();

/**
 * @brief Perform full display reset
 */
void display_full_reset();

/**
 * @brief Render ASCII frame to display
 * @param frame_data ASCII frame data to render
 * @param is_snapshot_frame Whether this is the final snapshot frame
 */
void display_render_frame(const char *frame_data, bool is_snapshot_frame);

/**
 * @brief Cleanup display subsystem
 */
void display_cleanup();

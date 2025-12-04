/**
 * @file client/mirror.h
 * @ingroup client_mirror
 * @brief ascii-chat Client Mirror Mode Interface
 *
 * Defines the interface for local webcam mirror mode, allowing users
 * to view their webcam as ASCII art directly in the terminal without
 * connecting to a server.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

/**
 * @brief Run mirror mode main loop
 *
 * Initializes webcam and terminal, then continuously captures frames,
 * converts them to ASCII art, and displays them locally. Runs until
 * the user presses Ctrl+C or an error occurs.
 *
 * @return 0 on success, non-zero error code on failure
 *
 * @ingroup client_mirror
 */
int mirror_main(void);

/**
 * @file client/main.h
 * @ingroup client_main
 * @brief ascii-chat Client Mode Entry Point Header
 *
 * This header exposes the client mode entry point for the unified binary architecture.
 * The unified binary dispatches to client_main() when invoked as `ascii-chat client`.
 *
 * ## Unified Binary Architecture
 *
 * The ascii-chat application uses a single binary with multiple operating modes:
 * - **ascii-chat server** - Run as server (multi-client connection manager)
 * - **ascii-chat client** - Run as client (connects to server, streams video/audio)
 *
 * This design provides several benefits:
 * - Simplified distribution (single binary to install)
 * - Reduced disk space (shared library code)
 * - Easier testing (one binary to build and deploy)
 * - Consistent versioning across modes
 *
 * ## Mode Entry Point Contract
 *
 * Each mode entry point (server_main, client_main) must:
 * - Accept no arguments: `int mode_main(void)`
 * - Options are already parsed by main dispatcher (available via global opt_* variables)
 * - Return 0 on success, non-zero error code on failure
 * - Perform mode-specific initialization and main loop
 * - Perform complete cleanup before returning
 *
 * ## Implementation Notes
 *
 * The client_main() function is the original main() from src/client/main.c,
 * adapted to the new dispatcher pattern. Common initialization (options parsing,
 * logging setup, lock debugging, --show-capabilities) now happens in src/main.c before dispatch.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 * @version 2.0
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Client mode entry point for unified binary
 *
 * This function implements the complete client lifecycle including:
 * - Client-specific initialization (display, capture, audio)
 * - Server connection with reconnection logic
 * - Media streaming and frame display
 * - Graceful shutdown and cleanup
 *
 * Options are already parsed by the main dispatcher before this function
 * is called, so they are available via global opt_* variables.
 *
 * @return 0 on success, non-zero error code on failure
 *
 * @example
 * // Invoked by dispatcher after options are parsed:
 * // $ ascii-chat client --address localhost --audio
 * //   (Options parsed in main.c, then client_main() called)
 */
int client_main(void);

/**
 * @brief Check if client should exit
 *
 * Thread-safe check of the global exit flag. Used by all client threads
 * to coordinate graceful shutdown.
 *
 * @return true if exit signal received, false otherwise
 */
bool should_exit(void);

/**
 * @brief Signal client to exit
 *
 * Sets the global exit flag to trigger graceful shutdown of all client
 * threads. Thread-safe and can be called from signal handlers.
 */
void signal_exit(void);

/**
 * @brief Signal client to exit with an error code
 *
 * Sets both the exit flag and an error code that can be retrieved
 * by the main thread to determine the exit status.
 *
 * @param error The error code to set (from asciichat_error_t)
 */
void signal_exit_with_error(int error);

/**
 * @brief Get the exit error code
 *
 * Returns the error code set by signal_exit_with_error(), or ASCIICHAT_OK
 * if no error was set.
 *
 * @return The error code set during exit, or ASCIICHAT_OK
 */
int get_exit_error(void);

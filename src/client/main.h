/**
 * @file client/main.h
 * @ingroup client_main
 * @brief ASCII-Chat Client Mode Entry Point Header
 *
 * This header exposes the client mode entry point for the unified binary architecture.
 * The unified binary dispatches to client_main() when invoked as `ascii-chat client`.
 *
 * ## Unified Binary Architecture
 *
 * The ASCII-Chat application uses a single binary with multiple operating modes:
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
 * - Accept standard main() signature: `int mode_main(int argc, char *argv[])`
 * - Parse mode-specific options from argv (mode name already consumed)
 * - Return 0 on success, non-zero error code on failure
 * - Handle --help and --version flags internally via options_init()
 * - Perform complete cleanup before returning
 *
 * ## Implementation Notes
 *
 * The client_main() function is the original main() from src/client/main.c,
 * simply renamed to integrate with the mode dispatcher pattern. All client
 * functionality remains unchanged - only the entry point name differs.
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
 * - Command line option parsing
 * - Platform and display initialization
 * - Webcam and audio capture setup
 * - Server connection with reconnection logic
 * - Media streaming and frame display
 * - Graceful shutdown and cleanup
 *
 * The function signature matches standard main() to allow seamless
 * integration with the mode dispatcher.
 *
 * @param argc Argument count (excluding mode name)
 * @param argv Argument vector (mode name already removed by dispatcher)
 * @return 0 on success, non-zero error code on failure
 *
 * @example
 * // Invoked by dispatcher as:
 * // $ ascii-chat client --address localhost --audio
 * //   ^^^^^^^^^^^          ^^^^^^^^^^^^^^^^^^^^^^^^^
 * //   (dispatcher)         (passed to client_main)
 */
int client_main(int argc, char *argv[]);

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

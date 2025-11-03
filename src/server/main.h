/**
 * @file server/main.h
 * @ingroup server_main
 * @brief ASCII-Chat Server Mode Entry Point Header
 *
 * This header exposes the server mode entry point for the unified binary architecture.
 * The unified binary dispatches to server_main() when invoked as `ascii-chat server`.
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
 * The server_main() function is the original main() from src/server/main.c,
 * simply renamed to integrate with the mode dispatcher pattern. All server
 * functionality remains unchanged - only the entry point name differs.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 * @version 2.0
 */

#pragma once

/**
 * @brief Server mode entry point for unified binary
 *
 * This function implements the complete server lifecycle including:
 * - Command line option parsing
 * - Platform and crypto initialization
 * - Network socket setup and binding
 * - Main connection accept loop
 * - Client lifecycle management
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
 * // $ ascii-chat server --port 8080
 * //   ^^^^^^^^^^^          ^^^^^^^^^^^^^
 * //   (dispatcher)         (passed to server_main)
 */
int server_main(int argc, char *argv[]);

/**
 * @file server/main.h
 * @ingroup server_main
 * @brief ascii-chat Server Mode Entry Point Header
 *
 * This header exposes the server mode entry point for the unified binary architecture.
 * The unified binary dispatches to server_main() when invoked as `ascii-chat server`.
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
 * The server_main() function is the original main() from src/server/main.c,
 * adapted to the new dispatcher pattern. Common initialization (options parsing,
 * logging setup, lock debugging) now happens in src/main.c before dispatch.
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
 * - Server-specific initialization (crypto, shutdown callback)
 * - Network socket setup and binding
 * - Main connection accept loop
 * - Client lifecycle management
 * - Graceful shutdown and cleanup
 *
 * Options are already parsed by the main dispatcher before this function
 * is called, so they are available via global opt_* variables.
 *
 * @return 0 on success, non-zero error code on failure
 *
 * @example
 * // Invoked by dispatcher after options are parsed:
 * // $ ascii-chat server --port 8080
 * //   (Options parsed in main.c, then server_main() called)
 */
int server_main(void);

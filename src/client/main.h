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
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/network/client.h>
#include <ascii-chat/crypto/handshake/common.h>

/**
 * @brief Global crypto handshake context for this client connection
 *
 * Stores the cryptographic state for secure communication with the server.
 * Initialized once per client execution and cleaned up on shutdown.
 * Thread-safe for use across multiple client threads.
 */
extern crypto_handshake_context_t g_crypto_ctx;

/**
 * @brief Global client worker thread pool
 *
 * Manages all client worker threads including:
 * - Data reception thread (protocol.c)
 * - Webcam capture thread (capture.c)
 * - Ping/keepalive thread (keepalive.c)
 * - Audio capture thread (audio.c)
 * - Audio sender thread (audio.c)
 *
 * This pool is initialized in client_main() and accessible from all
 * client modules for spawning and managing worker threads.
 */
extern thread_pool_t *g_client_worker_pool;

/**
 * @brief Global client application context
 *
 * Central state management structure for the client, containing both
 * network and application-layer state.
 *
 * This instance provides:
 * - Connection state management via active_transport (TCP or WebSocket)
 * - Audio queue management for async audio streaming
 * - Thread handles for worker threads (data reception, capture, ping, audio)
 * - Display state and terminal capability tracking
 * - Crypto handshake context for encrypted connections
 * - Protocol state (client ID, server initialization status)
 *
 * Initialized by app_client_create() in client_main().
 * Destroyed by app_client_destroy() at cleanup.
 *
 * All client modules (protocol.c, audio.c, capture.c, display.c)
 * should use this instance instead of accessing global connection state.
 */
extern app_client_t *g_client;

/**
 * @brief Global WebRTC peer manager for P2P connections
 *
 * Manages WebRTC peer connections for ACDS session participants.
 * Handles SDP/ICE exchange and DataChannel lifecycle for P2P ACIP transport.
 *
 * NULL if WebRTC not initialized (TCP-only mode).
 * Initialized when joining/creating ACDS sessions with WebRTC transport.
 */
extern struct webrtc_peer_manager *g_peer_manager;

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
 * @par Example
 * @code{.sh}
 * # Invoked by dispatcher after options are parsed:
 * ascii-chat client --address localhost --audio
 * # Options parsed in main.c, then client_main() called
 * @endcode
 */
int client_main(void);

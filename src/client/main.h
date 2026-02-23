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
 * - Client-specific subsystem initialization (display, capture, audio)
 * - Server connection establishment with exponential backoff reconnection
 * - Main event loop coordinating connection state and thread lifecycle
 * - Media streaming (webcam frames and optional audio)
 * - Frame display to terminal
 * - Graceful shutdown and resource cleanup
 *
 * ## Lifecycle Overview
 *
 * The client follows this high-level state flow:
 * 1. **Initialize**: Set up display, capture, audio subsystems
 * 2. **Connect Loop**:
 *    - Establish TCP connection to server
 *    - Perform cryptographic handshake
 *    - Spawn worker threads (data reception, capture, keepalive, audio)
 *    - Monitor connection health
 *    - Detect connection loss via thread exit or socket errors
 * 3. **Cleanup**: Stop threads, close socket, reset terminal
 * 4. **Reconnect**: Exponential backoff, retry (unless fatal auth error)
 * 5. **Exit**: On user signal (SIGINT) or fatal error
 *
 * ## Connection Failure Handling
 *
 * **Transient Errors** (retryable):
 * - Connection refused, timeout, DNS failure → exponential backoff retry
 * - Network temporarily unavailable → wait and retry
 *
 * **Fatal Errors** (no retry):
 * - Authentication failed (`CONNECTION_ERROR_AUTH_FAILED`) → exit immediately
 * - Host key verification failed (`CONNECTION_ERROR_HOST_KEY_FAILED`) → exit immediately
 *
 * ## Thread Management
 *
 * The client spawns several worker threads coordinated by the main thread:
 * - **Data Reception Thread**: Receives and processes server packets
 * - **Webcam Capture Thread**: Captures frames, transmits to server
 * - **Ping/Keepalive Thread**: Sends periodic pings, detects timeouts
 * - **Audio Threads** (optional): Captures/plays audio with Opus encoding
 *
 * All threads check the global `g_should_exit` flag and exit cleanly on shutdown.
 * Main thread waits for all threads to exit before reconnection or final cleanup.
 *
 * ## Options
 *
 * Options are already parsed by the main dispatcher (src/main.c) before this
 * function is called. They are available via the global options API (GET_OPTION, etc.).
 *
 * Key options affecting behavior:
 * - `opt_address`: Server address or session string
 * - `opt_port`: Server port (default 27224)
 * - `opt_audio_enabled`: Enable audio capture/playback
 * - `opt_snapshot`: Capture single frame and exit
 * - `opt_client_key`, `opt_server_key`: SSH keys for authentication
 *
 * @return Exit code:
 *         - 0: Success (clean exit or snapshot completed)
 *         - 1: Initialization/configuration error
 *         - 2: Connection fatal error (auth failed)
 *         - 130: Signal interrupt (SIGINT/Ctrl+C)
 *
 * @par Example
 * @code{.sh}
 * # Invoked by dispatcher after options are parsed:
 * ascii-chat client --address localhost --audio
 *
 * # Dispatcher (src/main.c) handles:
 * # - Option parsing
 * # - Log initialization
 * # - Signal handler setup
 *
 * # Then calls client_main() which handles:
 * # - Subsystem init (display, capture, audio)
 * # - Connection loop with reconnection logic
 * # - Thread coordination
 * # - Graceful shutdown
 * @endcode
 *
 * @see topic_client "Client Architecture Overview"
 * @see topic_client_main "Client Lifecycle Details"
 * @see topic_client_connection "Connection Management"
 * @see topic_client_protocol "Packet Processing"
 */
int client_main(void);

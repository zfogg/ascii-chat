/**
 * @file main.c
 * @brief ascii-chat Client Main Entry Point
 *
 * This module serves as the main entry point for the ascii-chat client application.
 * It orchestrates the entire client lifecycle including initialization, connection
 * management, and the primary event loop that manages reconnection logic.
 *
 * ## Architecture Overview
 *
 * The client follows a modular threading architecture:
 * - **Main thread**: Connection management and event coordination
 * - **Data reception thread**: Handles incoming packets from server
 * - **Ping thread**: Maintains connection keepalive
 * - **Webcam capture thread**: Captures and transmits video frames
 * - **Audio capture thread**: Captures and transmits audio data (optional)
 *
 * ## Connection Management
 *
 * The client implements robust reconnection logic with exponential backoff:
 * 1. Initial connection attempt
 * 2. On connection loss, attempt reconnection with increasing delays
 * 3. Maximum delay cap to prevent excessive wait times
 * 4. Clean thread lifecycle management across reconnections
 *
 * ## Thread Lifecycle
 *
 * Each connection cycle follows this pattern:
 * 1. **Connection Establishment**: Socket creation and server handshake
 * 2. **Thread Spawning**: Start all worker threads for the connection
 * 3. **Active Monitoring**: Monitor connection health and thread status
 * 4. **Connection Loss Detection**: Detect broken connections via thread exit
 * 5. **Cleanup Phase**: Join all threads and reset connection state
 * 6. **Reconnection Cycle**: Repeat from step 1 unless shutdown requested
 *
 * ## Integration Points
 *
 * - **server.c**: Connection establishment and socket management
 * - **protocol.c**: Initial capability negotiation and join packets
 * - **display.c**: Terminal initialization and control
 * - **capture.c**: Media capture subsystem initialization
 * - **audio.c**: Audio subsystem initialization (if enabled)
 * - **keepalive.c**: Ping thread management
 *
 * ## Error Handling
 *
 * The main loop implements graceful error recovery:
 * - Fatal initialization errors cause immediate exit
 * - Network errors trigger reconnection attempts
 * - Signal handling for graceful shutdown (SIGINT, SIGWINCH)
 * - Resource cleanup on all exit paths
 *
 * ## Platform Compatibility
 *
 * Uses platform abstraction layer for:
 * - Socket operations and network initialization
 * - Thread management and synchronization
 * - Signal handling differences between Unix and Windows
 * - Terminal I/O and control sequences
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 * @version 2.0
 */

#include "main.h"
#include "../main.h" // Global exit API
#include "server.h"
#include "protocol.h"
#include "crypto.h"
#include "display.h"
#include "capture.h"
#include "audio.h"
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/log/interactive_grep.h>
#include "session/session_log_buffer.h"
#include "session/client_like.h"
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/network/mdns/discovery_tui.h>
#include <ascii-chat/network/mdns/discovery.h>

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/log/json.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/util/time.h>   // For time macros
#include <ascii-chat/util/url.h>    // For URL parsing and WebSocket detection
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/tcp/client.h>
#include <ascii-chat/network/client.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/client.h>
#include "connection_state.h"
#include <ascii-chat/util/path.h>

#ifndef NDEBUG
#include <ascii-chat/debug/sync.h>
#ifdef DEBUG_MEMORY
#include <ascii-chat/debug/memory.h>
#endif
#endif

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <fcntl.h>

/* ============================================================================
 * Global State Variables
 * ============================================================================ */

/**
 * Global client worker thread pool
 *
 * Manages all client worker threads including:
 * - Data reception thread (protocol.c)
 * - Webcam capture thread (capture.c)
 * - Ping/keepalive thread (keepalive.c)
 * - Audio capture thread (audio.c)
 * - Audio sender thread (audio.c)
 */
thread_pool_t *g_client_worker_pool = NULL;

/**
 * Global application client context
 *
 * Central connection and application state for the client.
 * Contains transport-agnostic state: audio, threads, display, crypto.
 * Network-specific state (socket, connection flags) is in the active transport client.
 *
 * Initialized by app_client_create() in client_main().
 * Destroyed by app_client_destroy() at cleanup.
 */
app_client_t *g_client = NULL;

/**
 * @brief Global WebRTC peer manager (legacy compatibility)
 *
 * Client mode no longer uses WebRTC, but protocol.c still references this.
 * Always NULL in client mode.
 */
struct webrtc_peer_manager *g_peer_manager = NULL;

/**
 * @brief Client connection session state for session_client_like integration
 *
 * Holds per-session state needed across reconnections:
 * - Connection fallback context for multi-stage attempts
 * - Discovered server address (from LAN discovery or session string)
 * - Reconnection attempt counter
 * - Flag tracking if any successful connection has occurred
 *
 * Used by client_run() callback to manage the connection/reconnection loop.
 */
typedef struct {
  connection_attempt_context_t connection_ctx; // Fallback connection context (embedded)
  const char *discovered_address;              // From LAN/session discovery
  int discovered_port;                         // From LAN/session discovery
  int reconnect_attempt;                       // Current reconnection attempt number
  bool has_ever_connected;                     // Track if connection ever succeeded
} client_session_state_t;

static client_session_state_t g_client_session = {0};

/* Signal handling is now centralized in src/main.c via setup_signal_handlers()
 * Client mode uses set_interrupt_callback(server_connection_shutdown) to register
 * its network shutdown as the interrupt handler. SIGWINCH is still client-specific.
 */

/**
 * Platform-compatible SIGWINCH handler for terminal resize events
 *
 * Automatically updates terminal dimensions and notifies server when
 * both width and height are set to auto-detect mode. On Windows, this
 * is a no-op since SIGWINCH is not available.
 *
 * @param sigwinch The signal number (unused)
 */
#ifndef _WIN32
static void client_handle_sigwinch(int sigwinch) {
  (void)(sigwinch);

  // Terminal was resized, update dimensions and recalculate aspect ratio
  // ONLY if both width and height are auto (not manually set)
  if (GET_OPTION(auto_width) && GET_OPTION(auto_height)) {
    // Get terminal size and update via proper RCU setters
    unsigned short int term_width, term_height;
    if (get_terminal_size(&term_width, &term_height) == ASCIICHAT_OK) {
      options_set_int("width", (int)term_width);
      options_set_int("height", (int)term_height);
    }

    // Send new size to server if connected
    if (server_connection_is_active()) {
      if (threaded_send_terminal_size_with_auto_detect(GET_OPTION(width), GET_OPTION(height)) < 0) {
        log_warn("Failed to send terminal capabilities to server: %s", network_error_string());
      } else {
        display_full_reset();
        log_set_terminal_output(false);
      }
    }
  }
}
#else
// Windows-compatible signal handler (no-op implementation)
static void client_handle_sigwinch(int sigwinch) {
  (void)(sigwinch);
  log_console(LOG_DEBUG, "SIGWINCH received (Windows no-op implementation)");
}
#endif

/**
 * Perform complete client shutdown and resource cleanup
 *
 * This function is registered with atexit() to ensure proper cleanup
 * regardless of how the program terminates. Order of cleanup is important
 * to prevent race conditions and resource leaks.
 *
 * Safe to call multiple times (idempotent).
 */
static void shutdown_client() {
  // Guard against double cleanup (can be called explicitly + via atexit)
  static bool shutdown_done = false;
  if (shutdown_done) {
    return;
  }
  shutdown_done = true;

  log_debug("[SHUTDOWN] 1. Starting shutdown");

  // Set global shutdown flag to stop all threads
  signal_exit();
  log_debug("[SHUTDOWN] 2. signal_exit() called");

  // Stop splash animation thread before any resource cleanup
  splash_intro_done();
  log_debug("[SHUTDOWN] 3. splash_intro_done() returned");

  // IMPORTANT: Stop all protocol threads BEFORE cleaning up resources
  // protocol_stop_connection() shuts down the socket to interrupt blocking recv(),
  // then waits for the data reception thread and capture thread to exit.
  // This prevents race conditions where threads access freed resources.
  log_debug("[SHUTDOWN] 4. About to call protocol_stop_connection()");
  protocol_stop_connection();
  log_debug("[SHUTDOWN] 5. protocol_stop_connection() returned");

  // Destroy client worker thread pool (all threads already stopped by protocol_stop_connection)
  log_debug("[SHUTDOWN] 6. About to destroy thread pool");
  if (g_client_worker_pool) {
    thread_pool_destroy(g_client_worker_pool);
    g_client_worker_pool = NULL;
  }
  log_debug("[SHUTDOWN] 7. Thread pool destroyed");

  // Destroy application client context
  log_debug("[SHUTDOWN] 8. About to destroy app_client");
  if (g_client) {
    app_client_destroy(&g_client);
    log_debug("Application client context destroyed successfully");
  }
  log_debug("[SHUTDOWN] 9. app_client destroyed");

  // Now safe to cleanup server connection (socket already closed by protocol_stop_connection)
  // Legacy cleanup - will be removed after full migration to tcp_client
  log_debug("[SHUTDOWN] 10. About to cleanup server connection");
  server_connection_cleanup();
  log_debug("[SHUTDOWN] 11. Server connection cleaned up");

  // Cleanup capture subsystems (capture thread already stopped by protocol_stop_connection)
  log_debug("[SHUTDOWN] 12. About to cleanup capture");
  capture_cleanup();
  log_debug("[SHUTDOWN] 13. Capture cleaned up");

  // Print audio analysis report if enabled
  if (GET_OPTION(audio_analysis_enabled)) {
    audio_analysis_print_report();
    audio_analysis_destroy();
  }

  audio_cleanup();

#ifndef NDEBUG
  // Stop lock debug thread BEFORE display_cleanup() because the debug thread uses
  // _kbhit()/_getch() on Windows which interact with the console. If we close the
  // CON handle first, the debug thread can hang on console I/O, blocking process exit.
  debug_sync_destroy();
#endif

  // Cleanup display and terminal state
  display_cleanup();

  // Cleanup core systems
  buffer_pool_cleanup_global();

  // Disable keepawake mode (re-allow OS to sleep)
  platform_disable_keepawake();

  // Clean up symbol cache (before log_destroy)
  // This must be called BEFORE log_destroy() as symbol_cache_destroy() uses log_debug()
  // Safe to call even if atexit() runs - it's idempotent (checks g_symbol_cache_initialized)
  // Also called via platform_destroy() atexit handler, but explicit call ensures proper ordering
  symbol_cache_destroy();

  // Clean up binary path cache explicitly
  // Note: This is also called by platform_destroy() via atexit(), but it's idempotent
  platform_cleanup_binary_path_cache();

  // Clean up errno context (allocated strings, backtrace symbols)
  asciichat_errno_destroy();

  // Clean up RCU-based options state
  options_state_destroy();

  log_debug("Client shutdown complete");
  log_destroy();

#ifndef NDEBUG
  // Join the debug threads as the very last thing (after log_destroy since threads may log)
  debug_sync_cleanup_thread();
  debug_memory_thread_cleanup();
#endif
}

#ifndef NDEBUG
#endif

/**
 * Initialize all client subsystems
 *
 * Performs initialization in dependency order, with error checking
 * and cleanup on failure. This function must be called before
 * entering the main connection loop.
 *
 * @return 0 on success, non-zero error code on failure
 */
static int initialize_client_systems(void) {
  // All shared subsystem initialization (timer, logging, platform, buffer pool)
  // is now done by asciichat_shared_init() in src/main.c BEFORE options_init()

  // Initialize client worker thread pool (needed for network protocol threads)
  // This is required for all modes that connect to the server, including snapshot mode
  if (!g_client_worker_pool) {
    g_client_worker_pool = thread_pool_create("client_workers");
    if (!g_client_worker_pool) {
      log_fatal("Failed to create client worker thread pool");
      return ERROR_THREAD;
    }
  }

  // Ensure logging output is available for connection attempts (unless it was disabled with --quiet)
  if (!GET_OPTION(quiet)) {
    log_set_terminal_output(true);
  }
  log_truncate_if_large();

  // Display subsystem is now initialized by session_client_like_run()
  // No need to call display_init() here

  // Initialize application client context
  if (!g_client) {
    g_client = app_client_create();
    if (!g_client) {
      log_fatal("Failed to create application client context");
      return ERROR_NETWORK;
    }
    log_debug("Application client context created successfully");
  }

  // Initialize server connection management (legacy - will be migrated to tcp_client)
  if (server_connection_init() != 0) {
    log_fatal("Failed to initialize server connection");
    return ERROR_NETWORK;
  }

  // Initialize capture subsystems
  int capture_result = capture_init();
  if (capture_result != 0) {
    log_fatal("Failed to initialize capture subsystem");
    return capture_result;
  }

  // Initialize audio if enabled (skip in snapshot mode - no server connection needed)
  if (GET_OPTION(audio_enabled) && !GET_OPTION(snapshot_mode)) {
    if (audio_client_init() != 0) {
      log_warn("Failed to initialize audio system");
      // Continue without audio instead of crashing (e.g., ARM systems with audio device incompatibility)
    }

    // Initialize audio analysis if requested
    if (GET_OPTION(audio_analysis_enabled)) {
      if (audio_analysis_init() != 0) {
        log_warn("Failed to initialize audio analysis");
      }
    }
  }

  return 0;
}

/**
 * Client mode entry point for unified binary
 *
 * Orchestrates the complete client lifecycle:
 *  - System initialization and resource allocation
 *  - Signal handler registration
 *  - Main connection/reconnection loop
 *  - Graceful shutdown and cleanup
 *
 * @return 0 on success, error code on failure
 */
/* ============================================================================
 * Client Connection/Reconnection Loop (run_fn for session_client_like)
 * ============================================================================
 *
 * This callback is executed after shared initialization (media/audio/display)
 * is complete. It manages the entire client connection lifecycle:
 * 1. Connect to server (with fallback stages: TCP, WebRTC+STUN, WebRTC+TURN)
 * 2. Exchange media/audio with server
 * 3. On disconnection, attempt reconnection based on policy
 * 4. Exit on user request or max reconnection limit
 */

/**
 * Reconnection policy callback for client mode
 *
 * Determines whether to attempt reconnection after a connection failure.
 * Client mode generally wants to keep retrying unless snapshot mode or shutdown is requested.
 *
 * @param last_error      Error code from the failed connection attempt
 * @param attempt_number  Current reconnection attempt number (1-based)
 * @param user_data       Unused (NULL)
 * @return true to attempt reconnection, false to exit
 */
static bool client_should_reconnect(asciichat_error_t last_error, int attempt_number, void *user_data) {
  (void)last_error;
  (void)attempt_number;
  (void)user_data;

  // In snapshot mode, don't reconnect - exit after first failure
  if (GET_OPTION(snapshot_mode)) {
    log_error("Connection lost in snapshot mode - not retrying");
    return false;
  }

  // Otherwise, allow reconnection (framework handles max_reconnect_attempts limit)
  return true;
}

/**
 * Single connection attempt callback for session_client_like framework
 *
 * Handles one complete connection cycle: attempt, protocol startup, monitoring, cleanup.
 * The framework wraps this in a retry loop based on max_reconnect_attempts and
 * should_reconnect_callback configuration.
 *
 * On connection success: runs protocol until disconnection, then returns error
 * On connection failure: returns error immediately
 * On user request (Ctrl+C): returns with current status
 *
 * @param capture   Initialized capture context (provided by session_client_like)
 * @param display   Initialized display context (provided by session_client_like)
 * @param user_data Unused (NULL)
 * @return ASCIICHAT_OK if successful, error code if connection failed
 */
static asciichat_error_t client_run(session_capture_ctx_t *capture, session_display_ctx_t *display, void *user_data) {
  (void)capture;   // Provided by session_client_like, used by protocol threads
  (void)user_data; // Unused

  // Make the framework-created display context available to protocol threads
  display_set_context(display);

  // Get the render loop's should_exit callback for monitoring
  bool (*render_should_exit)(void *) = session_client_like_get_render_should_exit();
  if (!render_should_exit) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Render should_exit callback not initialized");
  }

  // Attempt connection with fallback stages (TCP, WebRTC+STUN, WebRTC+TURN)
  // Get pre-created TCP client from framework if available
  tcp_client_t *framework_tcp_client = session_client_like_get_tcp_client();
  asciichat_error_t connection_result = connection_attempt_tcp(
      &g_client_session.connection_ctx,
      g_client_session.discovered_address != NULL ? g_client_session.discovered_address : "",
      (uint16_t)(g_client_session.discovered_port > 0 ? g_client_session.discovered_port : 27224),
      framework_tcp_client);

  // Check if shutdown was requested during connection attempt
  if (should_exit()) {
    log_info("Shutdown requested during connection attempt");
    return ERROR_NETWORK;
  }

  if (connection_result != ASCIICHAT_OK) {
    // Connection failed - need to stop audio threads that were initialized early
    // (even though protocol_start_connection was never called)
    audio_stop_thread();
    // Framework will handle retry based on config
    log_error("Connection attempt failed");
    return connection_result;
  }

  // Connection successful - integrate transport into server layer
  if (g_client_session.connection_ctx.active_transport) {
    server_connection_set_transport(g_client_session.connection_ctx.active_transport);
    g_client_session.connection_ctx.active_transport = NULL;
  } else {
    log_error("Connection succeeded but no active transport");
    return ERROR_NETWORK;
  }

  // Log connection status
  if (!g_client_session.has_ever_connected) {
    log_info("Connected");
    g_client_session.has_ever_connected = true;
  } else {
    log_info("Reconnected");
  }

  // Start protocol worker threads for this connection
  if (protocol_start_connection() != 0) {
    log_error("Failed to start connection protocols");
    protocol_stop_connection();
    server_connection_close();
    return ERROR_NETWORK;
  }

  // Monitor connection until it breaks or shutdown is requested
  while (!should_exit() && server_connection_is_active()) {
    if (protocol_connection_lost()) {
      log_debug("Connection lost detected");
      break;
    }
    platform_sleep_us(100 * US_PER_MS_INT);
  }

  if (should_exit()) {
    log_debug("Shutdown requested, cleaning up connection");
  } else {
    log_debug("Connection lost, preparing for reconnection attempt");
  }

  // Clean up this connection for potential reconnection
  protocol_stop_connection();
  server_connection_close();

  // Recreate thread pool for clean reconnection
  if (g_client_worker_pool) {
    thread_pool_destroy(g_client_worker_pool);
    g_client_worker_pool = NULL;
  }
  g_client_worker_pool = thread_pool_create("client_reconnect");
  if (!g_client_worker_pool) {
    log_error("Failed to recreate worker thread pool");
    return ERROR_THREAD;
  }

  // Reset connection context for next attempt
  connection_context_cleanup(&g_client_session.connection_ctx);
  memset(&g_client_session.connection_ctx, 0, sizeof(g_client_session.connection_ctx));
  if (connection_context_init(&g_client_session.connection_ctx) != ASCIICHAT_OK) {
    log_error("Failed to re-initialize connection context");
    return ERROR_NETWORK;
  }

  // Clear screen and show splash on reconnection to provide visual feedback to user
  if (!GET_OPTION(quiet)) {
    terminal_clear_screen();
    splash_intro_start(NULL);
  }

  // Return error to signal reconnection needed (framework handles the retry)
  return ERROR_NETWORK;
}

int client_main(void) {
  log_debug("client_main() starting");

  // Initialize client-specific systems (NOT shared with session_client_like)
  // This includes: thread pool, display layer, app client context, server connection
  int init_result = initialize_client_systems();
  if (init_result != 0) {
#ifndef NDEBUG
    // Debug builds: automatically fall back to test pattern if webcam is in use
    if (init_result == ERROR_WEBCAM_IN_USE && !GET_OPTION(test_pattern)) {
      log_warn("Webcam is in use - automatically falling back to test pattern mode (debug build only)");

      // Enable test pattern mode via RCU update
      asciichat_error_t update_result = options_set_bool("test_pattern", true);
      if (update_result != ASCIICHAT_OK) {
        log_error("Failed to update options for test pattern fallback");
        FATAL(init_result, "%s", asciichat_error_string(init_result));
      }

      // Retry initialization with test pattern enabled
      init_result = initialize_client_systems();
      if (init_result != 0) {
        log_error("Failed to initialize even with test pattern fallback");
        webcam_print_init_error_help(init_result);
        FATAL(init_result, "%s", asciichat_error_string(init_result));
      }
      log_debug("Successfully initialized with test pattern fallback");

      // Clear the error state since we successfully recovered
      CLEAR_ERRNO();
    } else
#endif
    {
      // Release builds or other errors: print help and exit
      if (init_result == ERROR_WEBCAM || init_result == ERROR_WEBCAM_IN_USE || init_result == ERROR_WEBCAM_PERMISSION) {
        webcam_print_init_error_help(init_result);
        FATAL(init_result, "%s", asciichat_error_string(init_result));
      }
      // For other errors, just exit with the error code
      return init_result;
    }
  }

  // Register cleanup function for graceful shutdown
  (void)atexit(shutdown_client);

  // Register client interrupt callback (socket shutdown on SIGTERM/Ctrl+C)
  // Global signal handlers (SIGTERM, SIGPIPE, Ctrl+C) are set up in setup_signal_handlers() in src/main.c
  set_interrupt_callback(server_connection_shutdown);

#ifndef _WIN32
  // Register SIGWINCH for terminal resize handling (client-specific, not in framework)
  platform_signal(SIGWINCH, client_handle_sigwinch);
#endif

  /* ========================================================================
   * Client-Specific: LAN/Session Discovery
   * ========================================================================
   * This phase discovers the server to connect to via:
   * - LAN discovery (mDNS)
   * - Session string lookup (ACDS)
   * - Direct address/port
   */

  // LAN Discovery: If --scan flag is set, discover servers on local network
  const options_t *opts = options_get();
  if (opts && opts->lan_discovery &&
      (opts->address[0] == '\0' || is_localhost_ipv4(opts->address) || strcmp(opts->address, "localhost") == 0)) {
    log_debug("LAN discovery: --scan flag set, querying for available servers");

    discovery_tui_config_t lan_config;
    memset(&lan_config, 0, sizeof(lan_config));
    lan_config.timeout_ms = 2 * MS_PER_SEC_INT; // Wait up to 2 seconds for responses
    lan_config.max_servers = 20;                // Support up to 20 servers on LAN
    lan_config.quiet = true;                    // Quiet during discovery, TUI will show status

    int discovered_count = 0;
    discovery_tui_server_t *discovered_servers = discovery_tui_query(&lan_config, &discovered_count);

    // Use TUI for server selection
    int selected_index = discovery_tui_select(discovered_servers, discovered_count);

    if (selected_index < 0) {
      // User cancelled or no servers found
      if (discovered_count == 0) {
        // No servers found - log message and prevent any further output
        // Lock the terminal so other threads can't write and our error
        // will be the last message
        log_lock_terminal();

        // Log single message with embedded newlines to prevent multiple log entries
        log_error("No ascii-chat servers found on the local network.\nUse 'ascii-chat client <address>' to connect "
                  "manually.");

        // Exit without cleanup
        platform_force_exit(1);
      }
      // User cancelled (had servers to choose from but pressed cancel)
      log_debug("LAN discovery: User cancelled server selection");
      if (discovered_servers) {
        discovery_tui_free_results(discovered_servers);
      }
      return 1; // User cancelled
    }

    // Update options with discovered server's address and port
    discovery_tui_server_t *selected = &discovered_servers[selected_index];
    const char *selected_address = discovery_tui_get_best_address(selected);

    // We need to modify options, but they're immutable via RCU
    // Create a new options copy with updated address/port
    options_t *opts_new = SAFE_MALLOC(sizeof(options_t), options_t *);
    if (opts_new) {
      memcpy(opts_new, opts, sizeof(options_t));
      SAFE_STRNCPY(opts_new->address, selected_address, sizeof(opts_new->address));

      opts_new->port = (int)selected->port;

      log_debug("LAN discovery: Selected server '%s' at %s:%d", selected->name, opts_new->address, opts_new->port);

      // Note: In a real scenario, we'd update the global options via RCU
      // For now, we'll use the updated values directly for connection
      // This is a known limitation - proper RCU update requires more infrastructure
    }

    discovery_tui_free_results(discovered_servers);
  }

  // =========================================================================
  // Client-Specific: Server Address Resolution
  // =========================================================================
  // Client mode supports:
  // 1. Direct address/port (--address HOST --port PORT) - handled by options
  // 2. LAN discovery (--scan) - handled above
  // 3. WebSocket URL (direct ws:// or wss:// connection string)
  //
  // Note: Session string discovery via ACDS is handled by discovery mode only.
  //       Client mode does NOT use ACDS or session strings.

  const char *discovered_address = NULL;
  int discovered_port = 0;

  // Check if user provided a WebSocket URL as the server address
  const options_t *opts_websocket = options_get();
  const char *provided_address = opts_websocket && opts_websocket->address[0] != '\0' ? opts_websocket->address : NULL;

  if (provided_address && url_is_websocket(provided_address)) {
    // Direct WebSocket connection
    log_debug("Client: Direct WebSocket URL: %s", provided_address);
    discovered_address = provided_address;
    discovered_port = 0; // Port is embedded in the URL
  }

  if (discovered_port > 0) {
    log_debug("Client: discovered_address=%s, discovered_port=%d", discovered_address ? discovered_address : "NULL",
              discovered_port);
  } else {
    log_debug("Client: discovered_address=%s", discovered_address ? discovered_address : "NULL");
  }

  // Store discovered address/port in session state for client_run() callback
  static char address_storage[BUFFER_SIZE_SMALL];
  if (discovered_address) {
    SAFE_STRNCPY(address_storage, discovered_address, sizeof(address_storage));
    g_client_session.discovered_address = address_storage;
    g_client_session.discovered_port = discovered_port;
  } else {
    const options_t *opts_fallback = options_get();
    if (opts_fallback && opts_fallback->address[0] != '\0') {
      SAFE_STRNCPY(address_storage, opts_fallback->address, sizeof(address_storage));
      g_client_session.discovered_address = address_storage;
      g_client_session.discovered_port = opts_fallback->port;
    } else {
      g_client_session.discovered_address = "localhost";
      g_client_session.discovered_port = 27224;
    }
  }

  // Initialize connection context for first attempt
  asciichat_error_t ctx_init_result = connection_context_init(&g_client_session.connection_ctx);
  if (ctx_init_result != ASCIICHAT_OK) {
    log_error("Failed to initialize connection context");
    return 1;
  }

  /* ========================================================================
   * Configure and Run Shared Client-Like Session Framework
   * ========================================================================
   * session_client_like_run() handles all shared initialization:
   * - Terminal output management (force stderr if piped)
   * - Keepawake system (platform sleep prevention)
   * - Splash screen lifecycle
   * - Media source selection (webcam, file, URL, test pattern)
   * - FPS probing for media files
   * - Audio initialization and lifecycle
   * - Display context creation
   * - Proper cleanup ordering (critical for PortAudio)
   *
   * Client mode provides:
   * - client_run() callback: connection loop, protocol startup, monitoring
   * - client_should_reconnect() callback: reconnection policy
   * - Reconnection configuration: attempts, delay, callbacks
   */

  // Get reconnect attempts setting (-1 = unlimited, 0 = no retry, >0 = retry N times)
  int reconnect_attempts = GET_OPTION(reconnect_attempts);

  // Create TCP client for network mode (will be used by session_client_like_run)
  tcp_client_t *client_tcp = tcp_client_create();
  if (!client_tcp) {
    log_error("Failed to create TCP client for connection attempts");
    return 1; // Return error to exit client
  }

  // Configure session_client_like with client-specific settings
  session_client_like_config_t config = {
      .run_fn = client_run,
      .run_user_data = NULL,
      .tcp_client = client_tcp,
      .websocket_client = NULL,
      .discovery = NULL,
      .custom_should_exit = NULL,
      .exit_user_data = NULL,
      .keyboard_handler = NULL, // Client mode: server drives display
      .max_reconnect_attempts = reconnect_attempts,
      .should_reconnect_callback = client_should_reconnect,
      .reconnect_user_data = NULL,
      .reconnect_delay_ms = 1000,         // 1 second delay between reconnection attempts
      .print_newline_on_tty_exit = false, // Server/client manages cursor
  };

  log_debug("[CLIENT_MAIN] About to call session_client_like_run() with %d attempts", reconnect_attempts);
  asciichat_error_t session_result = session_client_like_run(&config);
  log_debug("[CLIENT_MAIN] session_client_like_run() returned %d", session_result);

  // Note: TCP client lifecycle is managed by session_client_like_run() and connection attempts
  // Do not destroy it here as it may be reused or already cleaned up

  // Cleanup connection context
  connection_context_cleanup(&g_client_session.connection_ctx);

  // Cleanup session log buffer (used by splash screen in session_client_like)
  // NOTE: session_log_buffer_destroy() is already called in session_client_like_run()
  // during its cleanup phase, so we don't call it here to avoid double-destroy of mutex

  log_debug("ascii-chat client shutting down");

  // IMPORTANT: Stop worker threads and join them BEFORE memory report
  // atexit(shutdown_client) won't run if interrupted by SIGTERM, so call explicitly
  shutdown_client();

  // Cleanup remaining shared subsystems (buffer pool, platform, etc.)
  // Note: atexit(asciichat_shared_destroy) is registered in main.c,
  // but won't run if interrupted by signals (SIGTERM from timeout/killall)
  asciichat_shared_destroy();

  return (session_result == ASCIICHAT_OK) ? 0 : 1;
}

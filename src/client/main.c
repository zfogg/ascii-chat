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
#include "server.h"
#include "protocol.h"
#include "crypto.h"
#include "display.h"
#include "capture.h"
#include "audio.h"
#include <ascii-chat/session/splash.h>
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
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/tcp/client.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include "webrtc.h"
#include "connection_state.h"
#include <ascii-chat/util/path.h>

#ifndef NDEBUG
#include <ascii-chat/debug/lock.h>
#ifdef DEBUG_MEMORY
#include <ascii-chat/debug/memory.h>
#endif
#endif

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <fcntl.h>

/* ============================================================================
 * Global State Variables
 * ============================================================================ */

/** Global flag indicating shutdown has been requested */
atomic_bool g_should_exit = false;

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
 * Global TCP client instance
 *
 * Central connection and state management structure for the client.
 * Replaces scattered global variables from server.c, protocol.c, audio.c, etc.
 * Follows the same pattern as tcp_server_t used by the server.
 *
 * Initialized by tcp_client_create() in client_main().
 * Destroyed by tcp_client_destroy() at cleanup.
 */
tcp_client_t *g_client = NULL;

/**
 * @brief Global WebRTC peer manager for P2P connections
 *
 * NULL if WebRTC not initialized. Initialized when joining/creating ACDS sessions.
 */
struct webrtc_peer_manager *g_peer_manager = NULL;

/**
 * Check if shutdown has been requested
 *
 * @return true if shutdown requested, false otherwise
 */
bool should_exit() {
  return atomic_load(&g_should_exit);
}

/**
 * Signal that shutdown should be requested
 */
void signal_exit() {
  atomic_store(&g_should_exit, true);
}

/**
 * Console control handler for Ctrl+C and related events
 *
 * Implements double-tap behavior: first Ctrl+C requests graceful shutdown,
 * second Ctrl+C within the same session forces immediate exit.
 *
 * @note On Windows, this runs in a separate thread (via SetConsoleCtrlHandler)
 *       and can safely use standard library functions.
 * @note On Unix, this runs in signal context - use only async-signal-safe functions.
 *
 * @param event The control event that occurred
 * @return true if the event was handled
 */
static bool console_ctrl_handler(console_ctrl_event_t event) {
  // Handle Ctrl+C, Ctrl+Break, and SIGTERM (CONSOLE_CLOSE)
  if (event != CONSOLE_CTRL_C && event != CONSOLE_CTRL_BREAK && event != CONSOLE_CLOSE) {
    return false;
  }

  // If this is the second Ctrl-C, force exit
  static _Atomic int ctrl_c_count = 0;
  int count = atomic_fetch_add(&ctrl_c_count, 1) + 1;

  if (count > 1) {
    platform_force_exit(1);
  }

  // Signal all subsystems to shutdown (async-signal-safe operations only)
  atomic_store(&g_should_exit, true);
  server_connection_shutdown(); // Only uses atomics and socket_shutdown

  // Let the main thread handle cleanup via atexit handlers.
  // The webcam_flush() call in capture_stop_thread() will interrupt
  // any blocking ReadSample() calls, allowing clean shutdown.

  return true; // Event handled
}

/**
 * Unix signal handler for graceful shutdown on SIGTERM
 *
 * @param sig The signal number (unused)
 */
#ifndef _WIN32
static void client_handle_sigterm(int sig) {
  (void)sig; // Unused
  log_info_nofile("SIGTERM received - shutting down client...");
  // Signal all subsystems to shutdown (async-signal-safe operations only)
  atomic_store(&g_should_exit, true);
  server_connection_shutdown(); // Only uses atomics and socket_shutdown
}
#else
// Windows-compatible signal handler (no-op implementation)
static void client_handle_sigterm(int sig) {
  (void)sig;
  // SIGTERM handled via console_ctrl_handler on Windows
}
#endif

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
  log_debug_nofile("SIGWINCH received (Windows no-op implementation)");
}
#endif

/**
 * Perform complete client shutdown and resource cleanup
 *
 * This function is registered with atexit() to ensure proper cleanup
 * regardless of how the program terminates. Order of cleanup is important
 * to prevent race conditions and resource leaks.
 */
static void shutdown_client() {
  // Set global shutdown flag to stop all threads
  atomic_store(&g_should_exit, true);

  // IMPORTANT: Stop all protocol threads BEFORE cleaning up resources
  // protocol_stop_connection() shuts down the socket to interrupt blocking recv(),
  // then waits for the data reception thread and capture thread to exit.
  // This prevents race conditions where threads access freed resources.
  protocol_stop_connection();

  // Destroy client worker thread pool (all threads already stopped by protocol_stop_connection)
  if (g_client_worker_pool) {
    thread_pool_destroy(g_client_worker_pool);
    g_client_worker_pool = NULL;
  }

  // Destroy TCP client instance (replaces scattered server.c cleanup)
  if (g_client) {
    tcp_client_destroy(&g_client);
    log_debug("TCP client instance destroyed successfully");
  }

  // Now safe to cleanup server connection (socket already closed by protocol_stop_connection)
  // Legacy cleanup - will be removed after full migration to tcp_client
  server_connection_cleanup();

  // Cleanup capture subsystems (capture thread already stopped by protocol_stop_connection)
  capture_cleanup();

  // Print audio analysis report if enabled
  if (GET_OPTION(audio_analysis_enabled)) {
    audio_analysis_print_report();
    audio_analysis_cleanup();
  }

  audio_cleanup();

#ifndef NDEBUG
  // Stop lock debug thread BEFORE display_cleanup() because the debug thread uses
  // _kbhit()/_getch() on Windows which interact with the console. If we close the
  // CON handle first, the debug thread can hang on console I/O, blocking process exit.
  lock_debug_cleanup();
#endif

  // Cleanup display and terminal state
  display_cleanup();

  // Cleanup core systems
  buffer_pool_cleanup_global();

  // Disable keepawake mode (re-allow OS to sleep)
  platform_disable_keepawake();

  // Clean up symbol cache (before log_destroy)
  // This must be called BEFORE log_destroy() as symbol_cache_cleanup() uses log_debug()
  // Safe to call even if atexit() runs - it's idempotent (checks g_symbol_cache_initialized)
  // Also called via platform_cleanup() atexit handler, but explicit call ensures proper ordering
  symbol_cache_cleanup();

  // Clean up binary path cache explicitly
  // Note: This is also called by platform_cleanup() via atexit(), but it's idempotent
  platform_cleanup_binary_path_cache();

  // Clean up errno context (allocated strings, backtrace symbols)
  asciichat_errno_cleanup();

  // Clean up RCU-based options state
  options_state_shutdown();

  log_debug("Client shutdown complete");
  log_destroy();

#ifndef NDEBUG
  // Join the debug thread as the very last thing (after log_destroy since thread may log)
  lock_debug_cleanup_thread();
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
static int initialize_client_systems(bool shared_init_completed) {
  if (!shared_init_completed) {
    // Initialize platform-specific functionality (Winsock, etc)
    if (platform_init() != 0) {
      (void)fprintf(stderr, "FATAL: Failed to initialize platform\n");
      return 1;
    }
    // NOTE: Platform cleanup is now registered by asciichat_shared_shutdown() in src/main.c
    // which is called via atexit() after asciichat_shared_init()

    // Initialize palette based on command line options
    const options_t *opts = options_get();
    const char *custom_chars = opts && opts->palette_custom_set ? opts->palette_custom : NULL;
    palette_type_t palette_type = GET_OPTION(palette_type);
    if (apply_palette_config(palette_type, custom_chars) != 0) {
      log_error("Failed to apply palette configuration");
      return 1;
    }

    // Initialize logging with appropriate settings
    char *validated_log_file = NULL;
    log_level_t log_level = GET_OPTION(log_level);
    const char *log_file = opts && opts->log_file[0] != '\0' ? opts->log_file : "client.log";

    if (strlen(log_file) > 0) {
      asciichat_error_t log_path_result = path_validate_user_path(log_file, PATH_ROLE_LOG_FILE, &validated_log_file);
      if (log_path_result != ASCIICHAT_OK || !validated_log_file || strlen(validated_log_file) == 0) {
        // Invalid log file path, fall back to default and warn
        (void)fprintf(stderr, "WARNING: Invalid log file path specified, using default 'client.log'\n");
        // Use regular file logging (not mmap) by default so developers can tail -f the log file
        log_init("client.log", log_level, true, false /* use_mmap */);
      } else {
        // Use regular file logging (not mmap) by default so developers can tail -f the log file
        log_init(validated_log_file, log_level, true, false /* use_mmap */);
      }
      SAFE_FREE(validated_log_file);
    } else {
      // Use regular file logging (not mmap) by default so developers can tail -f the log file
      log_init("client.log", log_level, true, false /* use_mmap */);
    }

    // Initialize memory debugging if enabled
#ifdef DEBUG_MEMORY
    bool quiet_mode = (GET_OPTION(quiet)) || (GET_OPTION(snapshot_mode));
    debug_memory_set_quiet_mode(quiet_mode);
    // NOTE: Memory report is now registered by asciichat_shared_shutdown() in src/main.c
    // which is called via atexit() after asciichat_shared_init()
#endif

    // Initialize global shared buffer pool
    buffer_pool_init_global();
    // NOTE: Buffer pool cleanup is now registered by asciichat_shared_shutdown() in src/main.c
    // which is called via atexit() after asciichat_shared_init()
  }

  // Initialize WebRTC library (required for P2P DataChannel connections)
  // Must be called regardless of shared_init_completed status
  asciichat_error_t webrtc_result = webrtc_init();
  if (webrtc_result != ASCIICHAT_OK) {
    log_fatal("Failed to initialize WebRTC library: %s", asciichat_error_string(webrtc_result));
    return ERROR_NETWORK;
  }
  (void)atexit(webrtc_cleanup);
  log_debug("WebRTC library initialized successfully");

  // Initialize client worker thread pool (always needed, even if shared init done)
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

  // Initialize display subsystem
  if (display_init() != 0) {
    log_fatal("Failed to initialize display subsystem");
    return ERROR_DISPLAY;
  }

  // Initialize TCP client instance (replaces scattered server.c globals)
  if (!g_client) {
    g_client = tcp_client_create();
    if (!g_client) {
      log_fatal("Failed to create TCP client instance");
      return ERROR_NETWORK;
    }
    log_debug("TCP client instance created successfully");
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

  // Initialize audio if enabled
  if (GET_OPTION(audio_enabled)) {
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
int client_main(void) {

  // Initialize all client subsystems (shared init already completed)
  int init_result = initialize_client_systems(true);
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
      init_result = initialize_client_systems(true);
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

  // Handle keepawake: check for mutual exclusivity and apply mode default
  // Client default: keepawake ENABLED (use --no-keepawake to disable)
  if (GET_OPTION(enable_keepawake) && GET_OPTION(disable_keepawake)) {
    FATAL(ERROR_INVALID_PARAM, "--keepawake and --no-keepawake are mutually exclusive");
  }
  if (!GET_OPTION(disable_keepawake)) {
    (void)platform_enable_keepawake();
  }

  // Install console control handler for graceful Ctrl+C handling
  // Uses SetConsoleCtrlHandler on Windows, sigaction on Unix - more reliable than CRT signal()
  platform_set_console_ctrl_handler(console_ctrl_handler);

  // Register signal handlers for graceful shutdown, terminal resize, and error handling
  platform_signal_handler_t signal_handlers[] = {
      {SIGWINCH, client_handle_sigwinch}, // Terminal resize (Unix only)
      {SIGTERM, client_handle_sigterm},   // SIGTERM for timeout(1) support
      {SIGPIPE, SIG_IGN},                 // Ignore broken pipe (we handle write errors ourselves)
  };
  platform_register_signal_handlers(signal_handlers, 3);

  // Keep terminal logging enabled so user can see connection attempts
  // It will be disabled after first successful connection
  // Note: No initial terminal reset - display will only be cleared when first frame arrives

  // Start the intro splash screen (non-blocking) - it will display while we initialize
  // The splash will continue until splash_intro_done() is called when first frame is ready
  splash_intro_start(NULL);

  // Track if we've ever successfully connected during this session
  static bool has_ever_connected = false;

  // Startup message only logged to file (terminal output is disabled by default)

  /* ========================================================================
   * Main Connection Loop
   * ========================================================================
   */

  int reconnect_attempt = 0;

  // LAN Discovery: If --scan flag is set, discover servers on local network
  const options_t *opts = options_get();
  if (opts && opts->lan_discovery &&
      (opts->address[0] == '\0' || strcmp(opts->address, "127.0.0.1") == 0 ||
       strcmp(opts->address, "localhost") == 0)) {
    log_debug("LAN discovery: --scan flag set, querying for available servers");

    discovery_tui_config_t lan_config;
    memset(&lan_config, 0, sizeof(lan_config));
    lan_config.timeout_ms = 2000; // Wait up to 2 seconds for responses
    lan_config.max_servers = 20;  // Support up to 20 servers on LAN
    lan_config.quiet = true;      // Quiet during discovery, TUI will show status

    int discovered_count = 0;
    discovery_tui_server_t *discovered_servers = discovery_tui_query(&lan_config, &discovered_count);

    // Use TUI for server selection
    int selected_index = discovery_tui_select(discovered_servers, discovered_count);

    if (selected_index < 0) {
      // User cancelled or no servers found
      if (discovered_count == 0) {
        // No servers found - print message and prevent any further output
        // Lock the terminal so other threads can't write
        log_lock_terminal();

        fprintf(stderr, "\n");
        fprintf(stderr, "No ascii-chat servers found on the local network.\n");
        fprintf(stderr, "Use 'ascii-chat client <address>' to connect manually.\n");
        fflush(stderr);

        // Log to file for debugging
        log_file_msg("\n");
        log_file_msg("No ascii-chat servers found on the local network.\n");
        log_file_msg("Use 'ascii-chat client <address>' to connect manually.\n");

        // Redirect stderr and stdout to /dev/null so cleanup handlers can't write to console
        // This is safe because we've already printed our final message
        platform_stdio_redirect_to_null_permanent();

        // Exit - cleanup handlers will try to write to /dev/null instead of console
        exit(1);
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
  // Connection Fallback Context Setup
  // =========================================================================
  //
  // Initialize connection fallback context for 3-stage connection attempts:
  // 1. Direct TCP (3s timeout)
  // 2. WebRTC + STUN (8s timeout)
  // 3. WebRTC + TURN (15s timeout)
  //
  connection_attempt_context_t connection_ctx = {0};
  asciichat_error_t ctx_init_result =
      connection_context_init(&connection_ctx,
                              GET_OPTION(prefer_webrtc),      // --prefer-webrtc flag
                              GET_OPTION(no_webrtc),          // --no-webrtc flag
                              GET_OPTION(webrtc_skip_stun),   // --webrtc-skip-stun flag
                              GET_OPTION(webrtc_disable_turn) // --webrtc-disable-turn flag
      );

  if (ctx_init_result != ASCIICHAT_OK) {
    log_error("Failed to initialize connection context");
    return 1;
  }

  // =========================================================================
  // PHASE 1: Parallel mDNS + ACDS Session Discovery
  // =========================================================================
  //
  // When a session string is detected (e.g., "swift-river-mountain"),
  // we perform parallel discovery on both mDNS (local LAN) and ACDS (internet):
  //
  // 1. **mDNS Discovery**: Query _ascii-chat._tcp.local for servers with
  //    matching session_string in TXT records (2s timeout)
  // 2. **ACDS Discovery**: Lookup session on ACDS server (5s timeout)
  // 3. **Race to Success**: Return first successful result, cancel the other
  // 4. **Verification**: Check host_pubkey against --server-key or --acds-insecure
  //
  // Usage modes:
  // - `ascii-chat swift-river-mountain` → mDNS-only (local LAN, no verification)
  // - `ascii-chat --server-key PUBKEY swift-river-mountain` → verified
  // - `ascii-chat --acds-insecure swift-river-mountain` → parallel without verification
  //
  const char *discovered_address = NULL;
  int discovered_port = 0;

  const options_t *opts_discovery = options_get();
  const char *session_string =
      opts_discovery && opts_discovery->session_string[0] != '\0' ? opts_discovery->session_string : "";

  if (session_string[0] != '\0') {
    log_debug("Session string detected: '%s' - performing parallel discovery (mDNS + ACDS)", session_string);

    // Configure discovery coordinator
    discovery_config_t discovery_cfg;
    discovery_config_init_defaults(&discovery_cfg);

    // Parse expected server key if provided
    uint8_t expected_pubkey[32];
    memset(expected_pubkey, 0, 32);
    if (opts_discovery && opts_discovery->server_key[0] != '\0') {
      asciichat_error_t parse_err = hex_to_pubkey(opts_discovery->server_key, expected_pubkey);
      if (parse_err == ASCIICHAT_OK) {
        discovery_cfg.expected_pubkey = expected_pubkey;
        log_debug("Server key verification enabled");
      } else {
        log_warn("Failed to parse server key - skipping verification");
      }
    }

    // Enable insecure mode if requested
    if (opts_discovery && opts_discovery->discovery_insecure) {
      discovery_cfg.insecure_mode = true;
      log_warn("ACDS insecure mode enabled - no server key verification");
    }

    // Configure ACDS connection details
    if (opts_discovery && opts_discovery->discovery_server[0] != '\0') {
      SAFE_STRNCPY(discovery_cfg.acds_server, opts_discovery->discovery_server, sizeof(discovery_cfg.acds_server));
    }
    if (opts_discovery && opts_discovery->discovery_port > 0) {
      discovery_cfg.acds_port = (uint16_t)opts_discovery->discovery_port;
    }

    // Set password for session join (use pointer since it persists through discovery)
    if (opts_discovery && opts_discovery->password[0] != '\0') {
      discovery_cfg.password = opts_discovery->password; // Safe: opts_discovery from options_get() persists
      log_debug("Password configured for session join");
    }

    // Perform parallel discovery
    discovery_result_t discovery_result;
    memset(&discovery_result, 0, sizeof(discovery_result));
    asciichat_error_t discovery_err = discover_session_parallel(session_string, &discovery_cfg, &discovery_result);

    if (discovery_err != ASCIICHAT_OK || !discovery_result.success) {
      fprintf(stderr, "Error: Failed to discover session '%s'\n", session_string);
      fprintf(stderr, "  - Not found via mDNS (local network)\n");
      fprintf(stderr, "  - Not found via ACDS (discovery server)\n");
      fprintf(stderr, "\nDid you mean to:\n");
      fprintf(stderr, "  ascii-chat server           # Start a new server\n");
      fprintf(stderr, "  ascii-chat client HOST      # Connect to specific host\n");
      return 1;
    }

    // Log discovery result
    const char *source_name = discovery_result.source == DISCOVERY_SOURCE_MDNS ? "mDNS (LAN)" : "ACDS (internet)";
    log_debug("Session discovered via %s: %s:%d", source_name, discovery_result.server_address,
              discovery_result.server_port);

    // Set discovered address/port for connection
    // Copy to static buffers since discovery_result goes out of scope after this block
    static char address_buffer[BUFFER_SIZE_SMALL];
    SAFE_STRNCPY(address_buffer, discovery_result.server_address, sizeof(address_buffer));
    discovered_address = address_buffer;
    discovered_port = discovery_result.server_port;

    // Populate session context for WebRTC fallback (if discovery came from ACDS)
    if (discovery_result.source == DISCOVERY_SOURCE_ACDS) {
      SAFE_STRNCPY(connection_ctx.session_ctx.session_string, session_string,
                   sizeof(connection_ctx.session_ctx.session_string));
      memcpy(connection_ctx.session_ctx.session_id, discovery_result.session_id, 16);
      memcpy(connection_ctx.session_ctx.participant_id, discovery_result.participant_id, 16);
      SAFE_STRNCPY(connection_ctx.session_ctx.server_address, discovery_result.server_address,
                   sizeof(connection_ctx.session_ctx.server_address));
      connection_ctx.session_ctx.server_port = discovery_result.server_port;
      log_debug("Populated session context for WebRTC fallback (session='%s')", session_string);
    }

    log_info("Connecting to %s:%d", discovered_address, discovery_result.server_port);
  }

  const options_t *opts_conn = options_get();
  while (!should_exit()) {
    // Handle connection establishment or reconnection
    const char *address = discovered_address
                              ? discovered_address
                              : (opts_conn && opts_conn->address[0] != '\0' ? opts_conn->address : "localhost");
    int port = discovered_port > 0 ? discovered_port : (opts_conn ? opts_conn->port : OPT_PORT_INT_DEFAULT);

    // Update connection context with current attempt number
    connection_ctx.reconnect_attempt = reconnect_attempt;

    // Get ACDS server configuration from CLI options (defaults: 127.0.0.1:27225)
    const char *acds_server = GET_OPTION(discovery_server);
    if (!acds_server || acds_server[0] == '\0') {
      acds_server = "127.0.0.1"; // Fallback if option not set
    }
    int acds_port = GET_OPTION(discovery_port);
    if (acds_port <= 0 || acds_port > 65535) {
      acds_port = OPT_ACDS_PORT_INT_DEFAULT;
    }

    // Attempt connection with 3-stage fallback (TCP → STUN → TURN)
    asciichat_error_t connection_result =
        connection_attempt_with_fallback(&connection_ctx, address, (uint16_t)port, acds_server, (uint16_t)acds_port);

    // Check if shutdown was requested during connection attempt
    if (should_exit()) {
      log_info("Shutdown requested during connection attempt");
      return 0;
    }

    // Check if connection attempt succeeded
    // Handle the error result appropriately
    int connection_success = (connection_result == ASCIICHAT_OK) ? 0 : -1;

    if (connection_success != 0) {
      // TODO Part 5: Handle specific error codes from connection_result
      // For now, treat all non-OK results as connection failure
      // Authentication errors would be returned as ERROR_CRYPTO_HANDSHAKE

      // In snapshot mode, exit immediately on connection failure - no retries
      // Snapshot mode is for quick single-frame captures, not persistent connections
      if (GET_OPTION(snapshot_mode)) {
        log_error("Connection failed in snapshot mode - exiting without retry");
        return 1;
      }

      // Connection failed - check if we should retry based on --reconnect setting
      reconnect_attempt++;

      // Get reconnect attempts setting (-1 = unlimited, 0 = no retry, >0 = retry N times)
      int reconnect_attempts = GET_OPTION(reconnect_attempts);

      // Check reconnection policy
      if (reconnect_attempts == 0) {
        // --reconnect off: Exit immediately on first failure
        log_error("Connection failed (reconnection disabled via --reconnect off)");
        return 1;
      } else if (reconnect_attempts > 0 && reconnect_attempt > reconnect_attempts) {
        // --reconnect N: Exceeded max retry attempts
        log_error("Connection failed after %d attempts (limit set by --reconnect %d)", reconnect_attempt - 1,
                  reconnect_attempts);
        return 1;
      }
      // else: reconnect_attempts == -1 (auto) means retry forever

      if (has_ever_connected) {
        display_full_reset();
        if (!GET_OPTION(quiet)) {
          log_set_terminal_output(true);
        }
      } else {
        // Add newline to separate from ASCII art display for first-time connection failures
        printf("\n");
      }

      if (has_ever_connected) {
        if (reconnect_attempts == -1) {
          log_info("Reconnecting (attempt %d)...", reconnect_attempt);
        } else {
          log_info("Reconnecting (attempt %d/%d)...", reconnect_attempt, reconnect_attempts);
        }
      } else {
        if (reconnect_attempts == -1) {
          log_info("Connecting (attempt %d)...", reconnect_attempt);
        } else {
          log_info("Connecting (attempt %d/%d)...", reconnect_attempt, reconnect_attempts);
        }
      }

      // Continue retrying based on reconnection policy
      continue;
    }

    // Connection successful - reset counters and flags
    reconnect_attempt = 0;

    // Integrate the active transport from connection fallback into server connection layer
    // (Transport is TCP for Stage 1, WebRTC DataChannel for Stages 2/3)
    if (connection_ctx.active_transport) {
      server_connection_set_transport(connection_ctx.active_transport);
      log_debug("Active transport integrated into server connection layer");

      // CRITICAL: Transfer ownership - NULL out context's pointers to prevent double-free
      // server_connection_set_transport() now owns the transport, so context must not destroy it
      connection_ctx.tcp_transport = NULL;
      connection_ctx.webrtc_transport = NULL;
      connection_ctx.active_transport = NULL;
    } else {
      log_error("Connection succeeded but no active transport - this should never happen");
      continue; // Retry connection
    }

    // Show appropriate connection message based on whether this is first connection or reconnection
    if (!has_ever_connected) {
      log_info("Connected");
      has_ever_connected = true;
    } else {
      log_info("Reconnected");
    }

    // Start all worker threads for this connection
    if (protocol_start_connection() != 0) {
      log_error("Failed to start connection protocols");
      server_connection_close();

      // In snapshot mode, exit immediately on protocol failure - no retries
      if (GET_OPTION(snapshot_mode)) {
        log_error("Protocol startup failed in snapshot mode - exiting without retry");
        return 1;
      }

      continue;
    }

    // Terminal logging is now disabled - ASCII display can begin cleanly
    // Don't clear terminal here - let the first frame handler clear it
    // This prevents clearing the terminal before we're ready to display content

    /* ====================================================================
     * Connection Monitoring Loop
     * ====================================================================
     */

    // Monitor connection health until it breaks or shutdown is requested
    while (!should_exit() && server_connection_is_active()) {
      // Check if any critical threads have exited (indicates connection lost)
      if (protocol_connection_lost()) {
        log_debug("Connection lost detected by protocol threads");
        break;
      }

      platform_sleep_usec(100 * 1000); // 0.1 second monitoring interval
    }

    if (should_exit()) {
      log_debug("Shutdown requested, exiting main loop");
      break;
    }

    // Connection broken - clean up this connection and prepare for reconnect
    log_debug("Connection lost, cleaning up for reconnection");

    // Re-enable terminal logging when connection is lost for debugging reconnection
    // (but only if we've ever successfully connected before and --quiet wasn't set)
    if (has_ever_connected && !GET_OPTION(quiet)) {
      printf("\n");
      log_set_terminal_output(true);
    }

    protocol_stop_connection();
    server_connection_close();

    // In snapshot mode, exit immediately on connection loss - no reconnection
    // Snapshot mode is for quick single-frame captures, not persistent connections
    if (GET_OPTION(snapshot_mode)) {
      log_error("Connection lost in snapshot mode - exiting without reconnection");
      return 1;
    }

    // Add a brief delay before attempting reconnection to prevent excessive reconnection loops
    if (has_ever_connected) {
      log_debug("Waiting 1 second before attempting reconnection...");
      platform_sleep_usec(1000000); // 1 second delay
    }

    log_debug("Cleanup complete, will attempt reconnection");
  }

  // Cleanup connection context (closes any active transports)
  connection_context_cleanup(&connection_ctx);

  log_debug("ascii-chat client shutting down");
  return 0;
}

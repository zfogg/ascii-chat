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
#include "audio/analysis.h"
#include "video/webcam/webcam.h"

#include "platform/abstraction.h"
#include "platform/init.h"
#include "platform/terminal.h"
#include "platform/symbols.h"
#include "platform/system.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access
#include "buffer_pool.h"
#include "video/palette.h"
#include "network/network.h"
#include "network/tcp/client.h"
#include "network/acip/client.h"
#include "network/acip/acds.h"
#include "network/acip/receive.h"
#include "network/webrtc/peer_manager.h"
#include "webrtc.h"
#include "util/path.h"

#ifndef NDEBUG
#include "debug/lock.h"
#ifdef DEBUG_MEMORY
#include "debug/memory.h"
#endif
#endif

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>

#ifdef _WIN32
#include <windows.h>
#endif

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
 * @brief Global WebRTC peer transport (ACIP transport wrapping DataChannel)
 *
 * NULL until DataChannel opens. Set by webrtc_transport_ready_callback().
 * This replaces g_client_transport when using WebRTC instead of TCP.
 */
static acip_transport_t *g_webrtc_peer_transport = NULL;

/**
 * @brief ACDS receive thread handle for WebRTC sessions
 *
 * Used to receive incoming SDP/ICE signaling messages from remote peers
 * via the ACDS server during WebRTC session establishment.
 */
static asciithread_t g_acds_receive_thread;
static bool g_acds_receive_thread_active = false;

/**
 * @brief Callback when WebRTC DataChannel is ready
 *
 * Called by peer manager when DataChannel opens and gets wrapped in ACIP transport.
 * This transport can then be used for ACIP communication instead of TCP.
 *
 * @param transport ACIP transport wrapping the DataChannel
 * @param participant_id Remote participant UUID
 * @param user_data User context (unused)
 */
static void webrtc_transport_ready_callback(acip_transport_t *transport, const uint8_t participant_id[16],
                                            void *user_data) {
  (void)user_data;

  log_info("WebRTC DataChannel ready for participant %02x%02x%02x%02x...", participant_id[0], participant_id[1],
           participant_id[2], participant_id[3]);

  // Store transport for ACIP communication
  // TODO: For multi-party sessions, need to manage multiple transports
  // For now, just store the first one
  if (!g_webrtc_peer_transport) {
    g_webrtc_peer_transport = transport;
    log_info("WebRTC peer transport established - ready for ACIP communication");
  } else {
    log_warn("Multiple WebRTC transports not yet supported - ignoring additional peer");
    // TODO: Store in a hash table keyed by participant_id
    acip_transport_destroy(transport);
  }
}

/**
 * @brief ACDS receive thread function for WebRTC signaling
 *
 * Continuously receives and dispatches ACDS signaling packets (SDP/ICE)
 * from remote peers during WebRTC session establishment.
 *
 * This thread runs concurrently with the main event loop, enabling
 * bidirectional signaling for WebRTC connections.
 *
 * @param arg Pointer to acip_transport_t for ACDS connection
 * @return NULL on thread exit
 */
static void *acds_receive_thread_func(void *arg) {
  acip_transport_t *acds_transport = (acip_transport_t *)arg;
  if (!acds_transport) {
    log_error("ACDS receive thread started with NULL transport");
    return NULL;
  }

  log_info("ACDS receive thread started - listening for WebRTC signaling");

  // Get client callbacks for packet dispatch
  const acip_client_callbacks_t *callbacks = protocol_get_acip_callbacks();
  if (!callbacks) {
    log_error("Failed to get ACIP client callbacks");
    return NULL;
  }

  // Receive loop
  while (g_acds_receive_thread_active && !should_exit()) {
    // Receive and dispatch one packet (blocking call)
    asciichat_error_t result = acip_transport_receive_and_dispatch_client(acds_transport, callbacks);

    if (result != ASCIICHAT_OK) {
      if (result == ERROR_NETWORK) {
        log_info("ACDS connection closed");
      } else if (result == ERROR_CRYPTO) {
        log_error("ACDS crypto error - connection compromised");
      } else {
        log_error("ACDS receive error: %s", asciichat_error_string(result));
      }
      break;
    }
  }

  log_info("ACDS receive thread exiting");
  g_acds_receive_thread_active = false;
  return NULL;
}

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
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#else
    _exit(1);
#endif
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
 * Platform-compatible SIGWINCH handler for terminal resize events
 *
 * Automatically updates terminal dimensions and notifies server when
 * both width and height are set to auto-detect mode. On Windows, this
 * is a no-op since SIGWINCH is not available.
 *
 * @param sigwinch The signal number (unused)
 */
#ifndef _WIN32
static void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);

  // Terminal was resized, update dimensions and recalculate aspect ratio
  // ONLY if both width and height are auto (not manually set)
  if (GET_OPTION(auto_width) && GET_OPTION(auto_height)) {
    update_dimensions_to_terminal_size((options_t *)options_get());

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
static void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  log_debug("SIGWINCH received (Windows no-op implementation)");
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

  log_info("Client shutdown complete");
  log_destroy();

#ifndef NDEBUG
  // Join the debug thread as the very last thing (after log_destroy since thread may log)
  lock_debug_cleanup_thread();
#endif
}

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
    (void)atexit(platform_cleanup);

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
        log_init("client.log", log_level, true, true /* use_mmap */);
      } else {
        log_init(validated_log_file, log_level, true, true /* use_mmap */);
      }
      SAFE_FREE(validated_log_file);
    } else {
      log_init("client.log", log_level, true, true /* use_mmap */);
    }

    // Initialize memory debugging if enabled
#ifdef DEBUG_MEMORY
    bool quiet_mode = (GET_OPTION(quiet)) || (GET_OPTION(snapshot_mode));
    debug_memory_set_quiet_mode(quiet_mode);
    if (!(GET_OPTION(snapshot_mode))) {
      (void)atexit(debug_memory_report);
    }
#endif

    // Initialize global shared buffer pool
    buffer_pool_init_global();
    (void)atexit(buffer_pool_cleanup_global);
  }

  // Initialize client worker thread pool (always needed, even if shared init done)
  if (!g_client_worker_pool) {
    g_client_worker_pool = thread_pool_create("client_workers");
    if (!g_client_worker_pool) {
      log_fatal("Failed to create client worker thread pool");
      return ERROR_THREAD;
    }
  }

  // Ensure logging output is available for connection attempts
  log_set_terminal_output(true);
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
      log_fatal("Failed to initialize audio system");
      return ERROR_AUDIO;
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
  // Dispatcher already printed capabilities, but honor flag defensively
  if (GET_OPTION(show_capabilities)) {
    terminal_capabilities_t caps = detect_terminal_capabilities();
    caps = apply_color_mode_override(caps);
    print_terminal_capabilities(&caps);
    return 0;
  }

  // Initialize all client subsystems (shared init already completed)
  int init_result = initialize_client_systems(true);
  if (init_result != 0) {
    // Check if this is a webcam-related error and print help
    if (init_result == ERROR_WEBCAM || init_result == ERROR_WEBCAM_IN_USE || init_result == ERROR_WEBCAM_PERMISSION) {
      webcam_print_init_error_help(init_result);
      FATAL(init_result, "%s", asciichat_error_string(init_result));
    }
    // For other errors, just exit with the error code
    return init_result;
  }

  // Register cleanup function for graceful shutdown
  (void)atexit(shutdown_client);

  // Install console control handler for graceful Ctrl+C handling
  // Uses SetConsoleCtrlHandler on Windows, sigaction on Unix - more reliable than CRT signal()
  platform_set_console_ctrl_handler(console_ctrl_handler);

  // Install SIGWINCH handler for terminal resize (Unix only, no-op on Windows)
  platform_signal(SIGWINCH, sigwinch_handler);

#ifndef _WIN32
  // Ignore SIGPIPE - we'll handle write errors ourselves (not available on Windows)
  platform_signal(SIGPIPE, SIG_IGN);
#endif

  // Keep terminal logging enabled so user can see connection attempts
  // It will be disabled after first successful connection
  // Note: No initial terminal reset - display will only be cleared when first frame arrives

  // Track if we've ever successfully connected during this session
  static bool has_ever_connected = false;

  // Startup message only logged to file (terminal output is disabled by default)

  /* ========================================================================
   * Main Connection Loop
   * ========================================================================
   */

  int reconnect_attempt = 0;
  bool first_connection = true;

  // ACDS Discovery: If session_string is set, lookup server via ACDS
  const char *discovered_address = NULL;
  const char *discovered_port = NULL;

  const options_t *opts_acds = options_get();
  const char *session_string = opts_acds && opts_acds->session_string[0] != '\0' ? opts_acds->session_string : "";
  const char *password = opts_acds && opts_acds->password[0] != '\0' ? opts_acds->password : "";

  if (session_string[0] != '\0') {
    log_info("Session string detected: %s - performing ACDS discovery", session_string);

    // Configure ACDS client
    acds_client_config_t acds_config;
    acds_client_config_init_defaults(&acds_config);

    // Use configured ACDS server (from --acds-server and --acds-port options)
    const char *acds_server = opts_acds->acds_server[0] != '\0' ? opts_acds->acds_server : "127.0.0.1";
    int acds_port = opts_acds->acds_port > 0 ? opts_acds->acds_port : 27225;

    SAFE_STRNCPY(acds_config.server_address, acds_server, sizeof(acds_config.server_address));
    acds_config.server_port = (uint16_t)acds_port;
    acds_config.timeout_ms = 5000;

    // Connect to ACDS server
    acds_client_t acds_client;
    asciichat_error_t acds_connect_result = acds_client_connect(&acds_client, &acds_config);
    if (acds_connect_result != ASCIICHAT_OK) {
      log_error("Failed to connect to ACDS server at %s:%d", acds_config.server_address, acds_config.server_port);
      return 1;
    }

    // Lookup session
    acds_session_lookup_result_t lookup_result;
    asciichat_error_t lookup_err = acds_session_lookup(&acds_client, session_string, &lookup_result);

    if (lookup_err != ASCIICHAT_OK || !lookup_result.found) {
      acds_client_disconnect(&acds_client);
      fprintf(stderr, "Error: '%s' is not a valid mode or session string\n", session_string);
      fprintf(stderr, "  - Not a recognized mode (server, client, mirror)\n");
      fprintf(stderr, "  - Not an active session on the discovery server\n");
      fprintf(stderr, "\nDid you mean to:\n");
      fprintf(stderr, "  ascii-chat server           # Start a new server\n");
      fprintf(stderr, "  ascii-chat client           # Connect to localhost\n");
      fprintf(stderr, "  ascii-chat client HOST      # Connect to specific host\n");
      return 1;
    }

    log_info("Session found: %s (%d/%d participants, password=%s)", session_string, lookup_result.current_participants,
             lookup_result.max_participants, lookup_result.has_password ? "required" : "not required");

    // Join session to get server connection information
    acds_session_join_params_t join_params;
    memset(&join_params, 0, sizeof(join_params));
    join_params.session_string = session_string;
    join_params.has_password = lookup_result.has_password && password[0] != '\0';
    if (join_params.has_password) {
      SAFE_STRNCPY(join_params.password, password, sizeof(join_params.password));
    }
    // TODO: Provide Ed25519 identity for signature when required by ACDS policies
    memset(join_params.identity_pubkey, 0, 32);
    memset(join_params.identity_seckey, 0, 64);

    acds_session_join_result_t join_result;
    asciichat_error_t join_err = acds_session_join(&acds_client, &join_params, &join_result);

    if (join_err != ASCIICHAT_OK || !join_result.success) {
      acds_client_disconnect(&acds_client);
      fprintf(stderr, "Error: Failed to join session '%s'\n", session_string);
      if (join_result.error_message[0] != '\0') {
        fprintf(stderr, "  %s\n", join_result.error_message);
      }
      return 1;
    }

    // Handle WebRTC vs Direct TCP sessions
    if (join_result.session_type == SESSION_TYPE_WEBRTC) {
      // WebRTC session: Keep ACDS connection for signaling
      log_info("Joined WebRTC session successfully (participant ID: %02x%02x..., session ID: %02x%02x...)",
               join_result.participant_id[0], join_result.participant_id[1], join_result.session_id[0],
               join_result.session_id[1]);

      // Get crypto context for transport encryption
      const crypto_context_t *crypto_ctx = crypto_client_is_ready() ? crypto_client_get_context() : NULL;

      // Configure STUN servers for ICE (using public Google STUN servers as defaults)
      // TODO: Get STUN/TURN servers from ACDS SESSION_JOINED response or use configured servers
      static stun_server_t default_stun_servers[] = {
          {.host_len = 24, .host = "stun:stun.l.google.com:19302"},
          {.host_len = 25, .host = "stun:stun1.l.google.com:19302"},
      };

      // Configure peer manager with joiner role (joiners initiate connections)
      webrtc_peer_manager_config_t peer_config = {
          .role = WEBRTC_ROLE_JOINER,
          .stun_servers = default_stun_servers,
          .stun_count = sizeof(default_stun_servers) / sizeof(default_stun_servers[0]),
          .turn_servers = NULL, // TODO: Get from ACDS if available
          .turn_count = 0,
          .on_transport_ready = webrtc_transport_ready_callback,
          .user_data = NULL,
          .crypto_ctx = (crypto_context_t *)crypto_ctx,
      };

      // Get signaling callbacks (send SDP/ICE via ACDS)
      webrtc_signaling_callbacks_t signaling_callbacks = webrtc_get_signaling_callbacks();

      // Create peer manager
      asciichat_error_t peer_result = webrtc_peer_manager_create(&peer_config, &signaling_callbacks, &g_peer_manager);
      if (peer_result != ASCIICHAT_OK) {
        log_error("Failed to create WebRTC peer manager: %s", asciichat_error_string(peer_result));
        acds_client_disconnect(&acds_client);
        return 1;
      }

      log_info("WebRTC peer manager created successfully");

      // Create ACIP transport from ACDS socket for signaling
      acip_transport_t *acds_transport = acip_tcp_transport_create(acds_client.socket, (crypto_context_t *)crypto_ctx);
      if (!acds_transport) {
        log_error("Failed to create ACDS transport for WebRTC signaling");
        webrtc_peer_manager_destroy(g_peer_manager);
        g_peer_manager = NULL;
        acds_client_disconnect(&acds_client);
        return 1;
      }

      // Set ACDS transport for signaling callbacks
      webrtc_set_acds_transport(acds_transport);

      // Set session context for signaling
      webrtc_set_session_context(join_result.session_id, join_result.participant_id);

      log_info("WebRTC peer manager initialized - waiting for connections");

      // Initiate P2P connection to remote participants
      // TODO: Get list of current participants from ACDS
      // For now, we'll use broadcast (all zeros) to connect to all participants
      // The session creator will respond with an answer, establishing the connection
      uint8_t broadcast_id[16] = {0}; // All zeros = broadcast to all participants

      log_info("Initiating WebRTC connection to session participants");
      asciichat_error_t connect_result =
          webrtc_peer_manager_connect(g_peer_manager, join_result.session_id, broadcast_id);

      if (connect_result != ASCIICHAT_OK) {
        log_error("Failed to initiate WebRTC connection: %s", asciichat_error_string(connect_result));
        webrtc_peer_manager_destroy(g_peer_manager);
        g_peer_manager = NULL;
        acip_transport_destroy(acds_transport);
        acds_client_disconnect(&acds_client);
        return 1;
      }

      log_info("WebRTC connection initiated - SDP offer sent via ACDS");

      // Start ACDS receive thread for incoming SDP/ICE signaling
      g_acds_receive_thread_active = true;
      int thread_result = ascii_thread_create(&g_acds_receive_thread, acds_receive_thread_func, acds_transport);
      if (thread_result != 0) {
        log_error("Failed to create ACDS receive thread: %d", thread_result);
        g_acds_receive_thread_active = false;
        webrtc_peer_manager_destroy(g_peer_manager);
        g_peer_manager = NULL;
        acip_transport_destroy(acds_transport);
        acds_client_disconnect(&acds_client);
        return 1;
      }

      log_info("ACDS receive thread started - ready to process incoming WebRTC signaling");

      // Simple event loop: wait for WebRTC connection to establish
      // TODO: Implement proper event loop with DataChannel event handling
      // For now, just sleep and wait for Ctrl+C
      log_info("WebRTC session active - waiting for DataChannel to open");
      log_info("Press Ctrl+C to exit");

      // Wait for shutdown signal
      while (!should_exit()) {
        platform_sleep_ms(100);

        // Check if DataChannel is ready
        if (g_webrtc_peer_transport) {
          log_info("WebRTC DataChannel established - ACIP communication ready");
          // TODO: Start normal client operation (capture, display, etc.)
          // For now, just keep the connection alive
        }
      }

      // Cleanup
      log_info("Shutting down WebRTC session");

      // Stop ACDS receive thread
      if (g_acds_receive_thread_active) {
        g_acds_receive_thread_active = false;
        log_info("Waiting for ACDS receive thread to exit");
        ascii_thread_join(&g_acds_receive_thread, NULL);
        log_info("ACDS receive thread stopped");
      }

      if (g_webrtc_peer_transport) {
        acip_transport_destroy(g_webrtc_peer_transport);
        g_webrtc_peer_transport = NULL;
      }
      webrtc_peer_manager_destroy(g_peer_manager);
      g_peer_manager = NULL;
      acip_transport_destroy(acds_transport);
      acds_client_disconnect(&acds_client);

      log_info("WebRTC session ended");
      return 0;
    } else {
      // Direct TCP session: Disconnect from ACDS and connect to server
      acds_client_disconnect(&acds_client);

      log_info("Joined Direct TCP session successfully, connecting to server: %s:%d", join_result.server_address,
               join_result.server_port);

      // Set discovered address/port for connection
      discovered_address = join_result.server_address;

      // Convert port to string for discovered_port
      static char port_buffer[8];
      snprintf(port_buffer, sizeof(port_buffer), "%d", join_result.server_port);
      discovered_port = port_buffer;
    }
  }

  const options_t *opts_conn = options_get();
  while (!should_exit()) {
    // Handle connection establishment or reconnection
    const char *address = discovered_address
                              ? discovered_address
                              : (opts_conn && opts_conn->address[0] != '\0' ? opts_conn->address : "localhost");
    const char *port_str =
        discovered_port ? discovered_port : (opts_conn && opts_conn->port[0] != '\0' ? opts_conn->port : "27224");
    int port = atoi(port_str);
    int connection_result =
        server_connection_establish(address, port, reconnect_attempt, first_connection, has_ever_connected);

    if (connection_result != 0) {
      // Check for authentication failure (code -2) - exit immediately without retry
      if (connection_result == -2) {
        // Detailed error message already printed by crypto handshake code
        return 1;
      }

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
        log_set_terminal_output(true);
      } else {
        // Add newline to separate from ASCII art display for first-time connection failures
        printf("\n");
      }

      if (has_ever_connected) {
        if (reconnect_attempts == -1) {
          log_info("Reconnection attempt #%d... (unlimited retries)", reconnect_attempt);
        } else {
          log_info("Reconnection attempt #%d/%d...", reconnect_attempt, reconnect_attempts);
        }
      } else {
        if (reconnect_attempts == -1) {
          log_info("Connection attempt #%d... (unlimited retries)", reconnect_attempt);
        } else {
          log_info("Connection attempt #%d/%d...", reconnect_attempt, reconnect_attempts);
        }
      }

      // Continue retrying based on reconnection policy
      continue;
    }

    // Connection successful - reset counters and flags
    reconnect_attempt = 0;
    first_connection = false;

    // Show appropriate connection message based on whether this is first connection or reconnection
    if (!has_ever_connected) {
      log_info("Connected successfully, starting worker threads");
      has_ever_connected = true;
    } else {
      log_info("Reconnected successfully, starting worker threads");
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
        log_info("Connection lost detected by protocol threads");
        break;
      }

      platform_sleep_usec(100 * 1000); // 0.1 second monitoring interval
    }

    if (should_exit()) {
      log_info("Shutdown requested, exiting main loop");
      break;
    }

    // Connection broken - clean up this connection and prepare for reconnect
    log_info("Connection lost, cleaning up for reconnection");

    // Re-enable terminal logging when connection is lost for debugging reconnection
    // (but only if we've ever successfully connected before)
    if (has_ever_connected) {
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
      log_info("Waiting 1 second before attempting reconnection...");
      platform_sleep_usec(1000000); // 1 second delay
    }

    log_info("Cleanup complete, will attempt reconnection");
  }

  log_info("ascii-chat client shutting down");
  return 0;
}

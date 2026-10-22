#include <ascii-chat/network/acip/settings.h>
#include "connection_internal.h"
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
 * - Signal handling for graceful shutdown and platform terminal resize detection
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

#include <ascii-chat/ui/notice.h>
#include <ascii-chat/ui/controller.h>
#include "session/protocol.h"

#include "main.h"
#include "../main.h" // Global exit API
#include "server.h"
#include "protocol_adapter.h"
#include "crypto.h"
#include "display.h"
#include "capture.h"
#include "audio.h"
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/session/session_log_buffer.h>
#include "session/client_like.h"
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/ui/mdns.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/network/mdns/discovery.h>

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/json.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/util/time.h>   // For time macros
#include <ascii-chat/util/url.h>    // For URL parsing and WebSocket detection
#include <ascii-chat/buffer.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/client.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/app_callbacks.h>

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
#include <ascii-chat/atomic.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <fcntl.h>

/* ============================================================================
 * Global State Variables
 * ============================================================================ */

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
  const char *discovered_address; // From LAN/session discovery
  int discovered_port;            // From LAN/session discovery
  int reconnect_attempt;          // Current reconnection attempt number
  bool has_ever_connected;        // Track if connection ever succeeded
} client_session_state_t;

static client_session_state_t g_client_session = {0};
static acip_websocket_runtime_t *g_websocket_runtime;

/* Signal handling is now centralized in src/main.c via setup_signal_handlers()
 * Signal handlers publish application exit; the owner interrupts network I/O
 * in normal thread context. Resize detection is polled through lib/platform.
 */

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

  // Stop splash animation thread before any resource cleanup
  splash_intro_done();

  audio_cleanup();
  capture_cleanup();
  acip_websocket_runtime_destroy(&g_websocket_runtime);

  // Print audio analysis report if enabled
  // Print audio analysis report if enabled
  if (GET_OPTION(audio_analysis_enabled)) {
    audio_analysis_print_report();
    audio_analysis_destroy();
  }

  // Cleanup display and terminal state
  // WORKAROUND: Skip cleanup during shutdown to avoid hanging
  // display_cleanup();

  // Cleanup core systems
  // WORKAROUND: Skip cleanup during shutdown to avoid hanging
  // buffer_system_cleanup();

  // Disable keepawake mode (re-allow OS to sleep)
  platform_disable_keepawake();

  log_debug("Client shutdown complete");
  log_destroy();
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
static int initialize_client_systems(void) {
  // All shared subsystem initialization (timer, logging, platform, buffer domain)
  // is now done by asciichat_shared_init() in src/main.c BEFORE options_init()

  // Ensure logging output is available for connection attempts (unless it was disabled with --quiet)
  if (!GET_OPTION(quiet)) {
    log_set_terminal_output(true);
  }
  log_truncate_if_large();

  // Display subsystem is now initialized by session_client_like_run()
  // No need to call display_init() here

  // Initialize capture subsystems
  int capture_result = capture_init();
  if (capture_result != 0) {
    log_fatal("Failed to initialize capture subsystem");
    return capture_result;
  }

  // Timed recordings capture call audio; immediate snapshots need only one frame.
  if (GET_OPTION(audio_enabled) && (!GET_OPTION(snapshot_mode) || GET_OPTION(snapshot_delay) > 0)) {
    if (audio_client_init() != 0) {
      NOTICE(WARNING, "AUDIO UNAVAILABLE", "Failed to initialize audio system");
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
  if (last_error == ERROR_CRYPTO_AUTH || last_error == ERROR_CRYPTO_KEY || last_error == ERROR_CRYPTO_VERIFICATION ||
      last_error == ERROR_CONFIG || last_error == ERROR_INVALID_PASSWORD || last_error == ERROR_INVALID_SIGNATURE ||
      last_error == ERROR_NETWORK_PROTOCOL || last_error == ERROR_INTERNAL)
    return false;
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
static asciichat_error_t client_send_settings(const options_t *values, uint32_t request, void *context) {
  return acip_settings_send(server_connection_get_transport((client_connection_t *)context), values, request,
                            UINT32_MAX);
}

static asciichat_error_t client_run_attempt(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                            void *user_data) {
  (void)capture; // Provided by session_client_like, used by protocol threads
  client_connection_t *connection = user_data;

  // Make the framework-created display context available to protocol threads
  display_set_context(display);
  connection->capture_context = capture_get_context();
  connection->display_context = display;
  connection->audio_context = audio_get_context();

  // Get the render loop's should_exit callback for monitoring
  bool (*render_should_exit)(void *) = session_client_like_get_render_should_exit();
  if (!render_should_exit) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Render should_exit callback not initialized");
  }

  asciichat_error_t connection_result = client_connection_start(
      connection, g_client_session.discovered_address ? g_client_session.discovered_address : "",
      (uint16_t)(g_client_session.discovered_port > 0 ? g_client_session.discovered_port : 27224));
  if (connection_result != ASCIICHAT_OK) {
    if (should_exit())
      return ERROR_NETWORK;
    LOG_ERRNO_IF_SET("Connection attempt failed");
    return connection_result;
  }

  // Log connection status
  if (!g_client_session.has_ever_connected) {
    log_info("Connected");
    g_client_session.has_ever_connected = true;
  } else {
    log_info("Reconnected");
  }

  // Monitor connection until it breaks or shutdown is requested
  bool snapshot_check_logged_once = false;
  uint64_t last_progress_log_ns = 0;
  uint64_t advertised_revision = OPTIONS_REVISION_ANY;
  while (!should_exit() && server_connection_is_active(connection)) {
    terminal_size_t previous = connection->advertised_dimensions;
    if (session_protocol_update_settings(&connection->advertised_dimensions, &advertised_revision, client_send_settings,
                                         connection) == ASCIICHAT_OK &&
        (previous.cols != connection->advertised_dimensions.cols ||
         previous.rows != connection->advertised_dimensions.rows)) {
      display_full_reset();
      log_set_terminal_output(false);
    }
    if (client_protocol_connection_lost(connection)) {
      log_debug("Connection lost detected");
      break;
    }

    // Check if snapshot_delay timer has fired (in case frames stopped arriving)
    // This is a safety check for when frame reception is sparse or stops entirely

    bool snapshot_mode_enabled = GET_OPTION(snapshot_mode);

    if (!snapshot_check_logged_once) {
      log_info("CLIENT_MAIN_LOOP: snapshot_mode=%d first_frame_time=%llu snapshot_delay=%.1f", snapshot_mode_enabled,
               (unsigned long long)atomic_load_u64(&connection->snapshot_first_frame_time_ns),
               GET_OPTION(snapshot_delay));
      snapshot_check_logged_once = true;
    }

    if (snapshot_mode_enabled && atomic_load_u64(&connection->snapshot_first_frame_time_ns) > 0) {
      uint64_t current_time_ns = time_get_ns();
      double elapsed =
          time_ns_to_s(time_elapsed_ns(atomic_load_u64(&connection->snapshot_first_frame_time_ns), current_time_ns));
      double snapshot_delay = GET_OPTION(snapshot_delay);

      // Log progress every 500ms
      if (current_time_ns - last_progress_log_ns > 500 * 1000 * 1000) {
        log_debug("[MAIN_LOOP_SNAPSHOT_CHECK] elapsed=%.3f target=%.3f first_frame_ns=%llu current_ns=%llu", elapsed,
                  snapshot_delay, (unsigned long long)atomic_load_u64(&connection->snapshot_first_frame_time_ns),
                  (unsigned long long)current_time_ns);
        last_progress_log_ns = current_time_ns;
      }

      if (elapsed >= snapshot_delay) {
        char duration_str[32];
        time_pretty((uint64_t)(elapsed * 1e9), -1, duration_str, sizeof(duration_str));
        log_info("🎬 SNAPSHOT TIMEOUT FIRED (IN_MAIN_LOOP): Snapshot captured after %s! (target=%.3f)", duration_str,
                 snapshot_delay);
        signal_exit();
        break; // Exit loop, proper shutdown will follow
      }
    }

    platform_sleep_us(100 * US_PER_MS_INT);
  }

  if (should_exit()) {
    log_debug("Shutdown requested, cleaning up connection");
  } else {
    log_info("[ELSE_BRANCH] Connection lost path (should_exit=0, connection_active=0)");
    // Connection lost - check if snapshot_delay timer has fired before exiting

    if (GET_OPTION(snapshot_mode) && atomic_load_u64(&connection->snapshot_first_frame_time_ns) > 0) {
      uint64_t current_time_ns = time_get_ns();
      double elapsed =
          time_ns_to_s(time_elapsed_ns(atomic_load_u64(&connection->snapshot_first_frame_time_ns), current_time_ns));
      double snapshot_delay = GET_OPTION(snapshot_delay);

      log_info("[CONNECTION_LOSS_SNAPSHOT_CHECK] elapsed=%.3f target=%.3f (checking if timer fired)", elapsed,
               snapshot_delay);

      if (elapsed >= snapshot_delay) {
        char duration_str[32];
        time_pretty((uint64_t)(elapsed * 1e9), -1, duration_str, sizeof(duration_str));
        log_info("🎬 SNAPSHOT TIMEOUT FIRED (ON_CONNECTION_LOSS): Snapshot captured after %s! (target=%.3f)",
                 duration_str, snapshot_delay);
        signal_exit();
        // Don't return yet, allow normal cleanup to proceed below
      }
    }

    if (!should_exit()) {
      splash_intro_start(display);
      log_debug("Connection lost, preparing for reconnection attempt");
    }
  }

  // A snapshot is complete once its first frame has been rendered. The peer
  // may close the connection while the snapshot shutdown is propagating, but
  // that is a successful snapshot rather than a reconnectable network error.

  if (should_exit()) {
    return ASCIICHAT_OK;
  }

  // The receive worker publishes the rejection before marking the connection lost.
  asciichat_error_t remote_error = server_connection_get_remote_error(connection);
  if (remote_error != ASCIICHAT_OK)
    return SET_ERRNO(remote_error, "Server rejected connection: %s", asciichat_error_string(remote_error));
  return ERROR_NETWORK;
}

/* Every attempt, including dialing/startup failure, leaves through this owner. */
static asciichat_error_t client_run(session_capture_ctx_t *capture, session_display_ctx_t *display, void *user_data) {
  (void)user_data;
  const char *address = g_client_session.discovered_address;
  if (!g_websocket_runtime && address && (strncmp(address, "ws://", 5) == 0 || strncmp(address, "wss://", 6) == 0)) {
    asciichat_error_t runtime_result = acip_websocket_runtime_create(&g_websocket_runtime);
    if (runtime_result != ASCIICHAT_OK)
      return runtime_result;
  }
  client_connection_t *connection = NULL;
  asciichat_error_t result = client_connection_create(&connection);
  if (result != ASCIICHAT_OK) {
    return result;
  }
  asciichat_errno_scope_t attempt_errors = errno_checkpoint();
  result = client_run_attempt(capture, display, connection);
  asciichat_error_t cleanup = client_connection_destroy(&connection);
  while (cleanup != ASCIICHAT_OK) {
    // Keep borrowed application devices and display alive until all users stop.
    LOG_ERRNO_IF_SET("Waiting for connection workers before application cleanup");
    platform_sleep_ms(100);
    cleanup = client_connection_destroy(&connection);
  }
  if (should_exit() && result == ERROR_NETWORK) {
    // Local cancellation can arrive while a remote disconnect is being cleaned up.
    errno_scope_end(attempt_errors, ASCIICHAT_ERRNO_DISMISSED);
    return ASCIICHAT_OK;
  }
  return result;
}

/* Runs once after splash startup, before capture/audio and the retry loop. */
static asciichat_error_t client_select_destination(void *user_data) {
  (void)user_data;
  const options_t *opts = options_get();
  if (!opts)
    return SET_ERRNO(ERROR_INVALID_STATE, "Client options are unavailable during LAN discovery");
  if (!opts->lan_discovery)
    return ASCIICHAT_OK;
  bool default_destination =
      !opts->address_explicit && strcmp(opts->address, OPT_ADDRESS_DEFAULT) == 0 && opts->port == OPT_PORT_INT_DEFAULT;
  if (opts->address_explicit || !default_destination)
    return ASCIICHAT_OK;

  ui_mdns_config_t config = {.timeout_ms = 2000, .max_servers = 20, .quiet = true};
  int count = 0;
  ui_mdns_server_t *servers = ui_mdns_query(&config, &count);
  if (count == 0) {
    ui_mdns_free_results(servers);
    return SET_ERRNO(ERROR_NETWORK, "No ascii-chat servers found on the local network");
  }
  uint64_t deadline = ui_input_deadline(30);
  ui_mdns_server_t selected;
  int selection = ui_mdns_select_live(servers, count, deadline, &selected);
  ui_mdns_free_results(servers);
  if (selection < 0) {
    return SET_ERRNO(ERROR_NETWORK, "LAN server selection cancelled");
  }
  static char selected_address[OPTIONS_BUFF_SIZE];
  SAFE_STRNCPY(selected_address, ui_mdns_get_best_address(&selected), sizeof(selected_address));
  asciichat_error_t result = options_set_string("address", selected_address);
  if (result == ASCIICHAT_OK)
    result = options_set_int("port", selected.port);
  if (result != ASCIICHAT_OK) {
    return result;
  }
  g_client_session.discovered_address = selected_address;
  g_client_session.discovered_port = selected.port;
  log_debug("LAN discovery: Selected server '%s' at %s:%d", selected.name, selected_address,
            g_client_session.discovered_port);
  return ASCIICHAT_OK;
}

static int client_initialize_devices(void) {

  // Initialize client-specific systems (NOT shared with session_client_like)
  // This includes application devices and the application client context.
  asciichat_errno_scope_t init_scope = errno_scope_begin();
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
      errno_scope_end(init_scope, ASCIICHAT_ERRNO_DISMISSED);
      ASSERT_NO_ERRNO_SINCE(init_scope);
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
  } else {
    ASSERT_NO_ERRNO_SINCE(init_scope);
    errno_scope_end(init_scope, ASCIICHAT_ERRNO_HANDLED);
  }

  return 0;
}

static asciichat_error_t client_prepare_destination(void *user_data) {
  asciichat_error_t result = client_select_destination(user_data);
  if (result != ASCIICHAT_OK)
    return result;
  return (asciichat_error_t)client_initialize_devices();
}

int client_main(void) {
  // Register cleanup function for graceful shutdown
  (void)atexit(shutdown_client);

  // Register client interrupt callback (socket shutdown on SIGTERM/Ctrl+C)
  // Global signal handlers (SIGTERM, SIGPIPE, Ctrl+C) are set up in setup_signal_handlers() in src/main.c
  set_interrupt_callback(NULL); // Signal handlers publish application exit only.

  /* ========================================================================
   * Client-Specific: LAN/Session Discovery
   * ========================================================================
   * This phase discovers the server to connect to via:
   * - LAN discovery (mDNS)
   * - Session string lookup (ACDS)
   * - Direct address/port
   */

  // =========================================================================
  // Client-Specific: Server Address Resolution
  // =========================================================================
  // Client mode supports:
  // 1. Direct positional address/port - handled by options
  // 2. Interactive default LAN discovery or --scan - prepared after splash startup
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

  // Configure session_client_like with client-specific settings
  session_client_like_config_t config = {
      .run_fn = client_run,
      .prepare_fn = client_prepare_destination,
      .run_user_data = NULL,
      .kind = SESSION_CLIENT_LIKE_KIND_CLIENT,
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

  log_debug("About to call session_client_like_run() with %d attempts", reconnect_attempts);
  asciichat_error_t session_result = session_client_like_run(&config);
  log_debug("session_client_like_run() returned %d", session_result);

  // Cleanup session log buffer (used by splash screen in session_client_like)
  // NOTE: session_log_buffer_destroy() is already called in session_client_like_run()
  // during its cleanup phase, so we don't call it here to avoid double-destroy of mutex

  log_debug("ascii-chat client shutting down");

  // IMPORTANT: Stop worker threads and join them BEFORE memory report
  // atexit(shutdown_client) won't run if interrupted by SIGTERM, so call explicitly
  log_debug("About to call shutdown_client()");
  fflush(stderr);
  shutdown_client();
  log_debug("shutdown_client() returned");
  fflush(stderr);

  // Cleanup remaining shared subsystems (buffer domain, platform, etc.)
  log_debug("About to call asciichat_shared_destroy()");
  asciichat_shared_destroy();

  // Log final session statistics before exit
  // Note: Frame count is logged in client_connection_stop() if connection was established
  // This is a fallback in case the connection never started
  return session_result;
}

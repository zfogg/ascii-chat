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
#include "mirror.h"
#include "server.h"
#include "protocol.h"
#include "display.h"
#include "capture.h"
#include "audio.h"
#include "os/webcam.h"

#include "platform/abstraction.h"
#include "platform/init.h"
#include "platform/terminal.h"
#include "common.h"
#include "options.h"
#include "buffer_pool.h"
#include "palette.h"
#include "network/network.h"
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
  // Only handle Ctrl+C and Ctrl+Break events
  if (event != CONSOLE_CTRL_C && event != CONSOLE_CTRL_BREAK) {
    return false;
  }

  // If this is the second Ctrl-C, force exit
  static volatile int ctrl_c_count = 0;
  ctrl_c_count++;

  if (ctrl_c_count > 1) {
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
  if (auto_width && auto_height) {
    update_dimensions_to_terminal_size();

    // Send new size to server if connected
    if (server_connection_is_active()) {
      if (threaded_send_terminal_size_with_auto_detect(opt_width, opt_height) < 0) {
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

  // Now safe to cleanup server connection (socket already closed by protocol_stop_connection)
  server_connection_cleanup();

  // Cleanup capture subsystems (capture thread already stopped by protocol_stop_connection)
  capture_cleanup();
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
  data_buffer_pool_cleanup_global();

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
    const char *custom_chars = opt_palette_custom_set ? opt_palette_custom : NULL;
    if (apply_palette_config(opt_palette_type, custom_chars) != 0) {
      log_error("Failed to apply palette configuration");
      return 1;
    }

    // Initialize logging with appropriate settings
    char *validated_log_file = NULL;
    if (strlen(opt_log_file) > 0) {
      asciichat_error_t log_path_result =
          path_validate_user_path(opt_log_file, PATH_ROLE_LOG_FILE, &validated_log_file);
      if (log_path_result != ASCIICHAT_OK || !validated_log_file || strlen(validated_log_file) == 0) {
        // Invalid log file path, fall back to default and warn
        (void)fprintf(stderr, "WARNING: Invalid log file path specified, using default 'client.log'\n");
        log_init("client.log", LOG_DEBUG, true);
      } else {
        log_init(validated_log_file, LOG_DEBUG, true);
      }
      SAFE_FREE(validated_log_file);
    } else {
      log_init("client.log", LOG_DEBUG, true);
    }

    // Initialize memory debugging if enabled
#ifdef DEBUG_MEMORY
    debug_memory_set_quiet_mode(opt_quiet || opt_snapshot_mode);
    if (!opt_snapshot_mode) {
      (void)atexit(debug_memory_report);
    }
#endif

    // Initialize global shared buffer pool
    data_buffer_pool_init_global();
    (void)atexit(data_buffer_pool_cleanup_global);
  }

  // Ensure logging output is available for connection attempts
  log_set_terminal_output(true);
  log_truncate_if_large();

  // Initialize display subsystem
  if (display_init() != 0) {
    log_fatal("Failed to initialize display subsystem");
    return ERROR_DISPLAY;
  }

  // Initialize server connection management
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
  if (opt_audio_enabled) {
    if (audio_client_init() != 0) {
      log_fatal("Failed to initialize audio system");
      return ERROR_AUDIO;
    }
  }

  return 0;
}

#ifdef USE_MIMALLOC_DEBUG
// Wrapper function for mi_stats_print to use with atexit()
// mi_stats_print takes a parameter, but atexit requires void(void)
extern void mi_stats_print(void *out);
static void print_mimalloc_stats(void) {
  mi_stats_print(NULL); // NULL = print to stderr
}
#endif

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
  if (opt_show_capabilities) {
    terminal_capabilities_t caps = detect_terminal_capabilities();
    caps = apply_color_mode_override(caps);
    print_terminal_capabilities(&caps);
    return 0;
  }

  // Mirror mode: view webcam locally without connecting to server
  if (opt_mirror_mode) {
    return mirror_main();
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

#ifdef USE_MIMALLOC_DEBUG
  // Register mimalloc stats printer at exit
  (void)atexit(print_mimalloc_stats);
#endif

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
  while (!should_exit()) {
    // Handle connection establishment or reconnection
    int connection_result = server_connection_establish(opt_address, strtoint_safe(opt_port), reconnect_attempt,
                                                        first_connection, has_ever_connected);

    if (connection_result != 0) {
      // Check for authentication failure (code -2) - exit immediately without retry
      if (connection_result == -2) {
        // Detailed error message already printed by crypto handshake code
        return 1;
      }

      // In snapshot mode, exit immediately on connection failure - no retries
      // Snapshot mode is for quick single-frame captures, not persistent connections
      if (opt_snapshot_mode) {
        log_error("Connection failed in snapshot mode - exiting without retry");
        return 1;
      }

      // Connection failed - increment attempt counter and retry
      reconnect_attempt++;

      if (has_ever_connected) {
        display_full_reset();
        log_set_terminal_output(true);
      } else {
        // Add newline to separate from ASCII art display for first-time connection failures
        printf("\n");
      }

      if (has_ever_connected) {
        log_info("Reconnection attempt #%d...", reconnect_attempt);
      } else {
        log_info("Connection attempt #%d...", reconnect_attempt);
      }

      // Continue retrying forever until user cancels (Ctrl+C)
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
      if (opt_snapshot_mode) {
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
    if (opt_snapshot_mode) {
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

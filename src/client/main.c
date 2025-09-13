/**
 * @file main.c
 * @brief ASCII-Chat Client Main Entry Point
 *
 * This module serves as the main entry point for the ASCII-Chat client application.
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
 * @date 2025
 * @version 2.0
 */

#include "main.h"
#include "server.h"
#include "protocol.h"
#include "display.h"
#include "capture.h"
#include "audio.h"
#include "os/audio.h"
#include "keepalive.h"
#include "os/webcam.h"
#include "image2ascii/image.h"

#include "platform/abstraction.h"
#include "platform/init.h"
#include "common.h"
#include "options.h"
#include "buffer_pool.h"
#include "palette.h"

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>

/* ============================================================================
 * Global State Variables
 * ============================================================================ */

/** Global flag indicating shutdown has been requested */
static atomic_bool g_should_exit = false;

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
 * Signal handler for SIGINT (Ctrl-C)
 *
 * Implements double-tap behavior: first SIGINT requests graceful shutdown,
 * second SIGINT within the same session forces immediate exit.
 *
 * @param sigint The signal number (unused)
 */
static void sigint_handler(int sigint) {
  (void)(sigint);

  // If this is the second Ctrl-C, force exit
  static int sigint_count = 0;
  sigint_count++;

  if (sigint_count > 1) {
    if (!opt_quiet) {
      printf("\nForce quit!\n");
    }
    _exit(1); // Force immediate exit
  }

  if (!opt_quiet) {
    printf("\nShutdown requested... (Press Ctrl-C again to force quit)\n");
  }

  // Signal all subsystems to shutdown
  atomic_store(&g_should_exit, true);

  // Trigger server connection module to close socket
  server_connection_shutdown();
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
      if (server_send_terminal_capabilities(opt_width, opt_height) < 0) {
        log_warn("Failed to send terminal capabilities to server: %s", network_error_string(errno));
      } else {
        display_full_reset();
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

  // Shutdown server connection and all associated threads
  server_connection_cleanup();

  // Cleanup capture subsystems
  capture_cleanup();
  audio_cleanup();

  // Cleanup display and terminal state
  display_cleanup();

  // Cleanup core systems
  data_buffer_pool_cleanup_global();
  log_destroy();

  log_info("Client shutdown complete");
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
static int initialize_client_systems() {
  // Initialize platform-specific functionality (Winsock, etc)
  if (platform_init() != 0) {
    fprintf(stderr, "FATAL: Failed to initialize platform\n");
    return 1;
  }
  atexit(platform_cleanup);

  // Initialize palette based on command line options
  const char *custom_chars = opt_palette_custom_set ? opt_palette_custom : NULL;
  if (apply_palette_config(opt_palette_type, custom_chars) != 0) {
    log_error("Failed to apply palette configuration");
    return 1;
  }

  // Initialize display subsystem
  if (display_init() != 0) {
    log_fatal("Failed to initialize display subsystem");
    return ASCIICHAT_ERR_DISPLAY;
  }

  // Initialize logging with appropriate settings
  const char *log_filename = (strlen(opt_log_file) > 0) ? opt_log_file : "client.log";
  log_init(log_filename, LOG_DEBUG);

  // Control terminal log output based on quiet flag and TTY detection
  log_set_terminal_output(display_has_tty() && !opt_quiet && !opt_snapshot_mode);
  log_truncate_if_large();

  // Initialize memory debugging if enabled
#ifdef DEBUG_MEMORY
  debug_memory_set_quiet_mode(opt_quiet || opt_snapshot_mode);
  if (!opt_snapshot_mode) {
    atexit(debug_memory_report);
  }
#endif

  // Initialize global shared buffer pool
  data_buffer_pool_init_global();
  atexit(data_buffer_pool_cleanup_global);

  // Initialize server connection management
  if (server_connection_init() != 0) {
    log_fatal("Failed to initialize server connection");
    return ASCIICHAT_ERR_NETWORK;
  }

  // Initialize capture subsystems
  if (capture_init() != 0) {
    log_fatal("Failed to initialize capture subsystem");
    return ASCIICHAT_ERR_WEBCAM;
  }

  // Initialize audio if enabled
  if (opt_audio_enabled) {
    if (audio_client_init() != 0) {
      log_fatal("Failed to initialize audio system");
      return ASCIICHAT_ERR_AUDIO;
    }
  }

  return 0;
}

/**
 * Main application entry point
 *
 * Orchestrates the complete client lifecycle:
 * 1. Command line parsing and option validation
 * 2. System initialization and resource allocation
 * 3. Signal handler registration
 * 4. Main connection/reconnection loop
 * 5. Graceful shutdown and cleanup
 *
 * The main loop implements robust connection management with exponential
 * backoff reconnection attempts. Each connection cycle spawns all necessary
 * worker threads and monitors their health.
 *
 * @param argc Command line argument count
 * @param argv Command line argument vector
 * @return 0 on success, error code on failure
 */
int main(int argc, char *argv[]) {
  // Parse command line options first
  options_init(argc, argv, true);

  // Handle --show-capabilities flag (exit after showing capabilities)
  if (opt_show_capabilities) {
    terminal_capabilities_t caps = detect_terminal_capabilities();
    caps = apply_color_mode_override(caps);
    print_terminal_capabilities(&caps);
    return 0;
  }

  // Initialize all client subsystems
  if (initialize_client_systems() != 0) {
    return 1;
  }

  // Register cleanup function for graceful shutdown
  atexit(shutdown_client);

  // Install signal handlers for graceful shutdown and terminal resize
  signal(SIGINT, sigint_handler);
  signal(SIGWINCH, sigwinch_handler);

#ifndef _WIN32
  // Ignore SIGPIPE - we'll handle write errors ourselves (not available on Windows)
  signal(SIGPIPE, SIG_IGN);
#endif

  // Perform initial terminal reset if running interactively
  if (!opt_snapshot_mode && display_has_tty()) {
    display_full_reset();
  }

  log_info("ASCII-Chat client started");

  /* ========================================================================
   * Main Connection Loop
   * ========================================================================
   */

  int reconnect_attempt = 0;
  bool first_connection = true;

  while (!should_exit()) {
    // Handle connection establishment or reconnection
    int connection_result =
        server_connection_establish(opt_address, strtoint_safe(opt_port), reconnect_attempt, first_connection);

    if (connection_result != 0) {
      // Connection failed - increment attempt counter and retry
      reconnect_attempt++;
      if (reconnect_attempt == 1) {
        // Clear console on first reconnection attempt
        display_full_reset();
      }
      continue;
    }

    // Connection successful - reset counters and flags
    reconnect_attempt = 0;
    first_connection = false;

    log_info("Connected successfully, starting worker threads");

    // Start all worker threads for this connection
    if (protocol_start_connection() != 0) {
      log_error("Failed to start connection protocols");
      server_connection_close();
      continue;
    }

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

      usleep(100 * 1000); // 0.1 second monitoring interval
    }

    if (should_exit()) {
      log_info("Shutdown requested, exiting main loop");
      break;
    }

    // Connection broken - clean up this connection and prepare for reconnect
    log_info("Connection lost, cleaning up for reconnection");

    protocol_stop_connection();
    server_connection_close();

    log_info("Cleanup complete, will attempt reconnection");
  }

  log_info("ASCII-Chat client shutting down");
  return 0;
}

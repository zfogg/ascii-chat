/**
 * @file server_like.h
 * @ingroup session
 * @brief Shared initialization and lifecycle management for server-like modes (server, discovery-service)
 *
 * Provides a unified interface for running server-like modes that share common
 * initialization and shutdown patterns: logging setup, signal handling, keepawake
 * management, status screen lifecycle, listener setup, thread pool management,
 * and graceful shutdown with proper cleanup ordering.
 *
 * Each mode registers mode-specific callbacks to handle server initialization,
 * the main server loop, and exit handling. The shared layer handles all boilerplate
 * for session management, rendering, and resource cleanup.
 *
 * ## Modes Supported
 *
 * - **Server mode**: Video broadcast server with multiple clients
 * - **Discovery service (ACDS)**: Session signaling and discovery WebRTC server
 *
 * ## Shared Responsibilities
 *
 * This layer automatically handles:
 * - Terminal output management (logging, stderr redirection if needed)
 * - Signal handling (SIGINT/SIGTERM for graceful shutdown)
 * - Keepawake system (platform sleep prevention)
 * - Status screen lifecycle and updates
 * - Server socket/listener setup coordination
 * - Session log buffer initialization and cleanup
 * - Proper cleanup ordering (critical for resource management)
 *
 * ## Mode-Specific Responsibilities
 *
 * Mode files provide:
 * - Initialization callback (`init_fn`) that sets up mode-specific state
 * - Main server loop callback (`run_fn`) that processes client connections
 * - Optional custom exit condition for additional shutdown criteria
 * - Status update callback for live status screen updates
 *
 * ## Memory and Lifecycle
 *
 * All resources created by mode's init_fn are owned by the mode. The session_server_like_run()
 * framework provides the container and orchestration but does not manage mode-specific
 * resources. Modes are responsible for their own resource cleanup.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * Mode-specific server initialization callback
 *
 * Called after shared initialization is complete (logging, keepawake, status screen
 * initialized). The mode sets up its server listeners and connection handling here.
 * This is where server opens TCP/WebSocket listeners or discovery service registers.
 *
 * Typical pattern:
 * ```c
 * asciichat_error_t init_result = mode_specific_init();
 * if (init_result != ASCIICHAT_OK) {
 *   return init_result;
 * }
 * return ASCIICHAT_OK;
 * ```
 *
 * @param user_data User data passed through from config.init_user_data
 * @return ASCIICHAT_OK on success, or error code to abort (cleanup still runs)
 */
typedef asciichat_error_t (*session_server_like_init_fn)(void *user_data);

/**
 * Mode-specific server main loop callback
 *
 * Called after initialization is complete. The mode runs its main server loop here,
 * accepting connections, processing client data, and handling the server-specific
 * protocol. Returns when the server should exit.
 *
 * Typical pattern:
 * ```c
 * while (!should_exit()) {
 *   // server protocol loop: accept connections, process clients, etc.
 *   status_screen_update_fn(...);  // Update status display periodically
 * }
 * return ASCIICHAT_OK;
 * ```
 *
 * @param user_data User data passed through from config.run_user_data
 * @return ASCIICHAT_OK, or error code to abort (cleanup still runs)
 */
typedef asciichat_error_t (*session_server_like_run_fn)(void *user_data);

/**
 * Optional callback for updating server status display
 *
 * Called periodically from mode's run_fn to update the status screen with
 * current server statistics (connected clients, bandwidth, etc).
 *
 * @param user_data User data passed through from config.status_update_user_data
 */
typedef void (*session_server_like_status_update_fn)(void *user_data);

/* ============================================================================
 * Configuration Structure
 * ============================================================================ */

/**
 * Configuration for session_server_like_run()
 *
 * All logging and display settings are read automatically from GET_OPTION()
 * inside the function. Callers only provide the mode-specific callbacks.
 */
typedef struct session_server_like_config {
  /* ================================================================ */
  /* Required                                                           */
  /* ================================================================ */

  /** Mode-specific initialization callback (required, never NULL) */
  session_server_like_init_fn init_fn;

  /** User data passed to init_fn */
  void *init_user_data;

  /** Mode-specific main loop callback (required, never NULL) */
  session_server_like_run_fn run_fn;

  /** User data passed to run_fn */
  void *run_user_data;

  /* ================================================================ */
  /* Optional                                                           */
  /* ================================================================ */

  /**
   * Optional status update callback for live display updates.
   * Called periodically from run_fn to update status screen.
   * NULL = no status screen updates (display shows only logs).
   */
  session_server_like_status_update_fn status_update_fn;

  /** User data for status_update_fn */
  void *status_update_user_data;

  /**
   * Optional additional exit condition. Called alongside the global should_exit().
   * The loop exits when EITHER should_exit() OR custom_should_exit() returns true.
   *
   * Use for modes that need extra exit logic:
   *   - discovery-service: exit when role changes or session ends
   *
   * NULL = use only the global should_exit().
   */
  bool (*custom_should_exit)(void *user_data);

  /** User data for custom_should_exit callback */
  void *exit_user_data;
} session_server_like_config_t;

/* ============================================================================
 * Entry Point
 * ============================================================================ */

/**
 * Run a server-like mode with fully shared initialization and teardown
 *
 * This function orchestrates the complete lifecycle of server-like modes:
 *
 * ## Initialization (automatic from GET_OPTION())
 *
 * **Terminal and Logging:**
 * - Forces stderr when stdout is piped (prevents ASCII corruption)
 * - Disables terminal logging during status screen rendering
 * - Re-enables logging on exit
 *
 * **Keepawake:**
 * - Validates --keepawake / --no-keepawake mutual exclusivity
 * - Calls platform_enable_keepawake() unless --no-keepawake set
 * - Disables keepawake on exit
 *
 * **Signal Handling:**
 * - Sets up SIGINT/SIGTERM handlers for graceful shutdown
 *
 * **Session Log Buffer:**
 * - Initializes session_log_buffer for status screen log display
 *
 * **Status Screen:**
 * - Initializes status screen UI
 * - Starts rendering with live log feed
 *
 * **Mode Initialization:**
 * - Calls config->init_fn to set up mode-specific state
 * - If init_fn fails, cleanup runs and error is returned
 *
 * ## Mode-Specific Loop
 *
 * Calls config->run_fn once all initialization is complete. The mode runs
 * its server protocol loop here, accepting connections and serving clients.
 *
 * ## Cleanup (always runs, even on error)
 *
 * Cleanup sequence (order matters):
 *   1. Stop accepting new connections (mode's responsibility during run_fn exit)
 *   2. Disconnect all current clients (mode's responsibility)
 *   3. Close all listeners (mode's responsibility)
 *   4. Re-enable terminal logging
 *   5. Destroy status screen
 *   6. session_log_buffer_destroy()
 *   7. platform_disable_keepawake()
 *
 * @param config  Mode configuration. Must not be NULL. init_fn and run_fn must not be NULL.
 * @return ASCIICHAT_OK on success, or the first error code from initialization
 *         or from init_fn/run_fn. Cleanup always runs regardless of return value.
 */
asciichat_error_t session_server_like_run(const session_server_like_config_t *config);

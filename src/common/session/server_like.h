/**
 * @file server_like.h
 * @ingroup session
 * @brief Shared initialization and teardown for server-like modes (server, discovery-service)
 *
 * Provides a unified interface for running server-like modes that share common
 * initialization patterns: socket binding, key loading, status screen management,
 * keyboard input handling, and clean shutdown.
 *
 * Each mode registers mode-specific callbacks for initialization, the main
 * accept loop, status gathering, and signal-based interruption. The shared layer
 * handles all the boilerplate.
 *
 * ## Modes Supported
 *
 * - **Server mode**: TCP/WebSocket server with audio mixing and H.265 encoding
 * - **Discovery-Service mode**: ACDS (ASCII Chat Discovery Service) server
 *
 * ## Shared Responsibilities
 *
 * This layer automatically handles:
 * - Keepawake system (platform sleep prevention)
 * - Signal handler registration (SIGINT, SIGTERM, SIGPIPE)
 * - Status screen lifecycle and animation
 * - Keyboard input and grep-mode toggle
 * - Proper cleanup ordering
 * - Terminal logging restoration
 *
 * ## Mode-Specific Responsibilities
 *
 * Mode files provide:
 * - `init_fn`: Create sockets, load keys, start worker threads
 * - `run_fn`: Blocking accept/event loop (tcp_server_run, acds_server_run, etc.)
 * - `interrupt_fn`: Async-signal-safe callback to close sockets on SIGINT/SIGTERM
 * - `status_fn`: Optional callback to gather and display live status at FPS rate
 *
 * ## Memory and Lifecycle
 *
 * All global state created by init_fn (sockets, threads, etc.) is owned by
 * the mode. The shared layer does not allocate on behalf of modes.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/ui/status.h>
#include <stdbool.h>

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * Mode-specific initialization callback.
 *
 * Called once after shared setup (keepawake, logging) completes.
 * Should create sockets, load keys, start worker threads—everything the mode
 * needs before its main accept loop starts.
 *
 * @param user_data Opaque pointer from config.init_user_data
 * @return ASCIICHAT_OK, or error code to abort (cleanup still runs)
 *
 * @ingroup session
 */
typedef asciichat_error_t (*session_server_like_init_fn)(void *user_data);

/**
 * Mode-specific blocking main loop.
 *
 * Called after init_fn succeeds. Blocks until the server shuts down.
 * Typical implementations:
 *   - Server mode:    tcp_server_run(&g_tcp_server)
 *   - Discovery-Service mode:      acds_server_run(&g_server)
 *
 * @param user_data Opaque pointer from config.run_user_data
 * @return ASCIICHAT_OK, or error code (cleanup always runs)
 *
 * @ingroup session
 */
typedef asciichat_error_t (*session_server_like_run_fn)(void *user_data);

/**
 * Async-signal-safe interrupt callback.
 *
 * Called from signal handler context (SIGINT / SIGTERM). Must only use
 * async-signal-safe operations. Responsible for closing listening sockets
 * to unblock the blocking run_fn (accept loop or poll).
 *
 * Examples:
 *   - Server mode: close g_tcp_server listen sockets + websocket cancel
 *   - Discovery-Service mode: set g_server->tcp_server.running = false + acds_signal_exit()
 *
 * @ingroup session
 */
typedef void (*session_server_like_interrupt_fn)(void);

/**
 * Status screen data callback.
 *
 * Called at GET_OPTION(fps) Hz by the status screen thread.
 * Should fill *out_status with current state for ui_status_display().
 * If NULL, the status screen is not shown even if --status-screen is set.
 *
 * The callback receives a zeroed ui_status_t and must populate ALL fields
 * it wants displayed (mode_name, addresses, port, connected_count,
 * session_string, start_time, etc.). No static fields are inherited
 * from the config — status_fn owns the entire ui_status_t.
 *
 * @param user_data   Opaque pointer from config.status_user_data
 * @param out_status  Output struct to populate (pre-zeroed by caller, not NULL)
 *
 * @ingroup session
 */
typedef void (*session_server_like_status_fn)(void *user_data, ui_status_t *out_status);

/* ============================================================================
 * Configuration Structure
 * ============================================================================ */

/**
 * Configuration for session_server_like_run()
 *
 * All logging, terminal, and signal settings are read automatically from
 * GET_OPTION() inside the function. Callers only provide the callbacks and
 * user data below.
 *
 * @ingroup session
 */
typedef struct session_server_like_config {
  /* ================================================================ */
  /* Required                                                           */
  /* ================================================================ */

  /**
   * Mode-specific initialization callback.
   * (required, never NULL)
   */
  session_server_like_init_fn init_fn;

  /** User data passed to init_fn */
  void *init_user_data;

  /**
   * Mode-specific blocking main loop callback.
   * (required, never NULL)
   */
  session_server_like_run_fn run_fn;

  /** User data passed to run_fn */
  void *run_user_data;

  /**
   * Async-signal-safe interrupt callback.
   * (required, never NULL)
   *
   * Installed via set_interrupt_callback() wrapper so SIGINT/SIGTERM
   * reaches it without each mode registering its own signal handler.
   */
  session_server_like_interrupt_fn interrupt_fn;

  /* ================================================================ */
  /* Status Screen (optional)                                           */
  /* ================================================================ */

  /**
   * Status data callback.
   * (optional, NULL = no status screen even if --status-screen is set)
   *
   * Decoupled from tcp_server_t so both server (has tcp_server_t) and
   * discovery-service (embeds tcp_server_t inside acds_server_t) can
   * provide status without type constraints.
   */
  session_server_like_status_fn status_fn;

  /** User data passed to status_fn */
  void *status_user_data;

} session_server_like_config_t;

/* ============================================================================
 * Entry Point
 * ============================================================================ */

/**
 * Run a server-like mode with unified initialization and shutdown
 *
 * This function orchestrates the complete lifecycle of server-like modes by
 * handling common boilerplate (keepawake, signal handlers) while delegating
 * mode-specific logic to callbacks. Status screen threading and keyboard
 * handling are the responsibility of individual modes.
 *
 * ## Initialization Sequence
 *
 * 1. **Keepawake Validation & Setup:**
 *    - Validates mutual exclusivity of --keepawake and --no-keepawake
 *    - Enables keepawake if requested (default: off for server modes)
 *
 * 2. **Signal Handler Registration:**
 *    - Stores mode's interrupt_fn in static scope for signal context
 *    - Registers SIGINT and SIGTERM to call server_like_signal_wrapper()
 *    - Ignores SIGPIPE (TCP/WebSocket friendly)
 *    - interrupt_fn must be async-signal-safe and close listening sockets
 *
 * 3. **Mode-Specific Initialization:**
 *    - Calls config->init_fn(config->init_user_data)
 *    - If init_fn fails, returns error code and proceeds to cleanup
 *
 * ## Main Run Loop
 *
 * Calls config->run_fn(config->run_user_data), which blocks until server
 * shuts down. This is typically an accept loop (tcp_server_run, acds_server_run, etc.).
 *
 * ## Cleanup (always runs, even on error)
 *
 * - Disables keepawake if it was enabled during init
 *
 * ## Notes
 *
 * - Status screen threading is **not** managed by this layer. Modes that need
 *   status display should implement their own status_fn callback and thread
 *   management within run_fn.
 * - The config->status_fn field is available for mode-specific use but is
 *   not invoked by server_like itself.
 *
 * @param config  Mode configuration. Must not be NULL. init_fn, run_fn,
 *                and interrupt_fn must not be NULL.
 * @return ASCIICHAT_OK on success, or the first error code from initialization
 *         or from run_fn. Cleanup always runs regardless of return value.
 *
 * @ingroup session
 */
asciichat_error_t session_server_like_run(const session_server_like_config_t *config);

/**
 * @file client_like.h
 * @ingroup session
 * @brief Shared initialization and teardown for client-like modes (mirror, client, discovery)
 *
 * Provides a unified interface for running client-like modes that share common
 * initialization patterns: media source selection, FPS probing, audio initialization,
 * display creation, splash screen management, keepawake handling, and cleanup.
 *
 * Each mode registers mode-specific callbacks to handle initialization, the main
 * render loop, and keyboard input. The shared layer handles all the boilerplate.
 *
 * ## Modes Supported
 *
 * - **Mirror mode**: Local webcam/media playback without networking
 * - **Client mode**: Network client with per-connection initialization
 * - **Discovery mode**: P2P mode with role negotiation
 *
 * ## Shared Responsibilities
 *
 * This layer automatically handles:
 * - Terminal output management (stdout piping detection)
 * - Keepawake system (platform sleep prevention)
 * - Splash screen lifecycle and animation
 * - Media source selection and FPS probing
 * - Audio initialization and lifecycle
 * - Display context creation and management
 * - Render loop execution
 * - Proper cleanup ordering (critical for PortAudio)
 *
 * ## Mode-Specific Responsibilities
 *
 * Mode files provide:
 * - Main loop callback (`run_fn`) that calls session_render_loop() or custom loop
 * - Optional network interrupt callback for socket shutdown
 * - Optional custom exit condition for additional shutdown criteria
 * - Keyboard handler for interactive controls
 * - Terminal behavior preferences (newline on exit)
 *
 * ## Memory and Lifecycle
 *
 * All allocations (capture, display, audio) are owned and cleaned up by
 * session_client_like_run(). Mode callbacks receive initialized, ready-to-use
 * contexts and should not attempt to free them.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <ascii-chat/session/capture.h>
#include <ascii-chat/session/display.h>
#include <ascii-chat/session/keyboard_handler.h>
#include <ascii-chat/session/render.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>

/* Forward declarations - callers include full headers themselves */
typedef struct tcp_client tcp_client_t;
typedef struct websocket_client websocket_client_t;
/* discovery_session is defined in src/discovery/session.h as an opaque type
   Discovery mode should include that header and cast to/from void * */

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * Mode-specific main loop callback
 *
 * Called after all shared initialization is complete (capture opened, audio
 * started, display ready, splash done). The mode runs its main loop here and
 * returns when finished. The shared teardown runs after this returns.
 *
 * Typical pattern:
 * ```c
 * // For mirror/discovery participant:
 * return session_render_loop(capture, display, exit_check_fn, NULL, NULL,
 *                            keyboard_handler, user_data);
 *
 * // For client mode (protocol-driven):
 * while (!should_exit()) {
 *   // protocol loop...
 * }
 * return ASCIICHAT_OK;
 * ```
 *
 * @param capture   Initialized capture context. Never NULL.
 * @param display   Initialized display context. Never NULL.
 * @param user_data User data passed through from config.run_user_data
 * @return ASCIICHAT_OK, or error code to abort (cleanup still runs)
 */
typedef asciichat_error_t (*session_client_like_run_fn)(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                                        void *user_data);

/* ============================================================================
 * Configuration Structure
 * ============================================================================ */

/**
 * Configuration for session_client_like_run()
 *
 * All media, display, audio, FPS, and snapshot settings are read automatically
 * from GET_OPTION() inside the function. Callers only provide the fields below
 * that differ per mode.
 */
typedef struct session_client_like_config {
  /* ================================================================ */
  /* Required                                                           */
  /* ================================================================ */

  /** Mode-specific main loop callback (required, never NULL) */
  session_client_like_run_fn run_fn;

  /** User data passed to run_fn */
  void *run_user_data;

  /* ================================================================ */
  /* Networking (NULL for mirror mode)                                  */
  /* ================================================================ */

  /**
   * Active TCP transport for this session. When non-NULL, teardown will
   * shut it down gracefully as part of the cleanup sequence.
   * NULL for mirror mode and discovery participant mode.
   */
  tcp_client_t *tcp_client;

  /**
   * Active WebSocket transport for this session. When non-NULL, teardown
   * will close it as part of the cleanup sequence.
   * NULL for mirror mode and TCP-only client mode.
   */
  websocket_client_t *websocket_client;

  /**
   * Active discovery session for this connection (opaque void *).
   * When non-NULL, events will be processed periodically inside the shared exit check,
   * and the session will be stopped during teardown.
   * NULL for mirror mode and TCP client mode.
   * Cast from discovery_session_t * when used (include src/discovery/session.h).
   */
  void *discovery;

  /* ================================================================ */
  /* Exit Condition                                                     */
  /* ================================================================ */

  /**
   * Optional additional exit condition. Called in the exit check logic
   * alongside the global should_exit(). The loop exits when EITHER
   * should_exit() OR custom_should_exit() returns true.
   *
   * Use this for modes that need extra exit logic:
   *   - discovery: also exit when discovery_session_is_active() becomes false
   *
   * NULL = use only the global should_exit().
   */
  bool (*custom_should_exit)(void *user_data);

  /** User data for custom_should_exit callback */
  void *exit_user_data;

  /* ================================================================ */
  /* Reconnection Logic (for client/discovery retry loops)             */
  /* ================================================================ */

  /**
   * Maximum reconnection attempts. Controls retry behavior:
   *   -1 = unlimited retries (keep trying forever)
   *    0 = no retries (single attempt only, exit on failure)
   *   >0 = retry up to N times
   *
   * Default: 0 (no retries). Client mode typically sets to -1.
   * Discovery mode may set custom value based on role.
   *
   * When run_fn returns non-OK, session_client_like_run() checks this
   * to decide whether to retry or exit.
   */
  int max_reconnect_attempts;

  /**
   * Optional callback to determine if should attempt reconnection.
   *
   * Called when run_fn returns with error code. Can implement custom
   * reconnection logic:
   *   - discovery: return false when role changes to prevent retry
   *   - client: always return true (handled by max_reconnect_attempts)
   *
   * Should return:
   *   true  = attempt reconnection
   *   false = exit immediately
   *
   * NULL = always attempt reconnection (unless max_reconnect_attempts reached).
   */
  bool (*should_reconnect_callback)(asciichat_error_t last_error, int attempt_number, void *user_data);

  /** User data for should_reconnect_callback */
  void *reconnect_user_data;

  /**
   * Delay in milliseconds before attempting reconnection.
   * Applied after each failed attempt. 0 = no delay.
   *
   * Can be a fixed value (e.g., 1000 for 1 second) or exponential
   * backoff. Modes with backoff can use should_reconnect_callback
   * to calculate delay based on attempt number.
   */
  unsigned int reconnect_delay_ms;

  /* ================================================================ */
  /* Keyboard Handler                                                   */
  /* ================================================================ */

  /**
   * Keyboard handler for interactive controls (seek, volume, pause, help).
   * Passed directly to session_render_loop(). NULL = no keyboard handling.
   *
   * Mirror and discovery modes use session_handle_keyboard_input().
   * Client mode may pass NULL (server drives display).
   */
  session_keyboard_handler_fn keyboard_handler;

  /* ================================================================ */
  /* Terminal Behavior                                                  */
  /* ================================================================ */

  /**
   * When true, write a bare '\n' to STDOUT_FILENO on exit if stdout is a
   * TTY. This separates the last rendered ASCII frame from the shell prompt.
   *
   * Mirror mode: true.
   * Client and discovery modes: false (server/host manages cursor).
   */
  bool print_newline_on_tty_exit;
} session_client_like_config_t;

/* ============================================================================
 * Entry Point
 * ============================================================================ */
/**
 * Get the render loop should_exit callback set by session_client_like_run().
 *
 * This is used by mode-specific run_fn callbacks to obtain the proper should_exit
 * callback for passing to session_render_loop(). The callback checks both the
 * global should_exit() flag and any mode-specific custom_should_exit condition.
 *
 * @return Function pointer to the render should_exit callback, or NULL if
 *         session_client_like_run() has not been called yet.
 */
bool (*session_client_like_get_render_should_exit(void))(void *);

/**
 * Get the TCP client created by session_client_like_run() (if applicable).
 *
 * Returns the TCP client created for direct TCP connections (non-WebSocket).
 * Only valid after session_client_like_run() is called and during run_fn execution.
 * May be NULL if WebSocket is being used instead.
 *
 * @return Pointer to tcp_client_t, or NULL if not created or using WebSocket
 */
tcp_client_t *session_client_like_get_tcp_client(void);

/**
 * Get the WebSocket client created by session_client_like_run() (if applicable).
 *
 * Returns the WebSocket client created for WebSocket connections.
 * Only valid after session_client_like_run() is called and during run_fn execution.
 * May be NULL if TCP is being used instead.
 *
 * @return Pointer to websocket_client_t, or NULL if not created or using TCP
 */
websocket_client_t *session_client_like_get_websocket_client(void);

/**
 * Run a client-like mode with fully shared initialization and teardown
 *
 * This function orchestrates the complete lifecycle of client-like modes:
 *
 * ## Initialization (automatic from GET_OPTION())
 *
 * **Terminal:**
 * - Forces stderr when stdout is piped (terminal_should_force_stderr)
 * - Disables terminal logging during rendering, re-enables on exit
 * - Handles snapshot mode log visibility
 *
 * **Keepawake:**
 * - Validates --keepawake / --no-keepawake mutual exclusivity
 * - Calls platform_enable_keepawake() unless --no-keepawake set
 * - Disables keepawake on exit
 *
 * **Splash Screen:**
 * - Creates temporary display context
 * - Calls splash_intro_start() to show splash animation
 * - Holds splash visible during media probing and audio init
 * - Calls splash_intro_done() when capture and display are ready
 *
 * **Media / Webcam (session_capture_ctx_t):**
 * - Selects source type from options (priority order):
 *     1. --url  → MEDIA_SOURCE_FILE (FFmpeg handles http/rtsp/rtmp)
 *     2. --file → MEDIA_SOURCE_FILE; "-" → MEDIA_SOURCE_STDIN
 *     3. --test-pattern → MEDIA_SOURCE_TEST
 *     4. (default) → MEDIA_SOURCE_WEBCAM
 * - FPS: uses GET_OPTION(fps) if > 0, otherwise probes with temporary media source
 * - Applies GET_OPTION(media_seek_timestamp) as initial seek
 * - Applies GET_OPTION(media_loop) for file looping
 * - Calls session_capture_create() to open webcam or media file
 * - Destroys capture on exit
 *
 * **Audio (audio_context_t):**
 * - Skipped entirely for immediate snapshots (snapshot_mode && snapshot_delay == 0)
 * - Probes for audio via media_source_has_audio() on capture's source
 * - Allocates and initializes audio_context_t when audio available
 * - Links audio context to capture and media source
 * - Sets playback_only based on audio_should_enable_microphone()
 * - Disables jitter buffering for local file playback
 * - Calls audio_start_duplex() after splash_intro_done()
 * - Stops, destroys, and frees audio on exit
 *
 * **Display (session_display_ctx_t):**
 * - Calls session_display_create() with snapshot and color settings
 * - Passes audio context if initialized
 * - Destroys display on exit
 *
 * ## Mode-Specific Loop
 *
 * Calls config->run_fn(capture, display, config->run_user_data) once all
 * initialization is complete. This is where each mode calls session_render_loop()
 * or runs its own protocol-thread-driven loop.
 *
 * ## Cleanup (always runs, even on error)
 *
 * Cleanup sequence (order is critical):
 *   1. Re-enable terminal logging
 *   2. audio_terminate_portaudio_final() - Free PortAudio device resources FIRST
 *   3. audio_stop_duplex() + audio_destroy() + SAFE_FREE
 *   4. session_display_destroy()
 *   5. session_capture_destroy()
 *   6. webcam_destroy() - Free cached webcam images and test patterns
 *   7. session_log_buffer_destroy()
 *   8. platform_disable_keepawake()
 *   9. Print '\n' to STDOUT_FILENO if print_newline_on_tty_exit && TTY
 *
 * @param config  Mode configuration. Must not be NULL. run_fn must not be NULL.
 * @return ASCIICHAT_OK on success, or the first error code from initialization
 *         or from run_fn. Cleanup always runs regardless of return value.
 */
asciichat_error_t session_client_like_run(const session_client_like_config_t *config);

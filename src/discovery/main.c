/**
 * @file discovery/main.c
 * @ingroup discovery_main
 * @brief ascii-chat Discovery Mode Entry Point
 *
 * Discovery mode enables zero-configuration video chat where participants
 * can dynamically become hosts based on NAT quality. This implements the
 * "one command to start, one command to join" philosophy.
 *
 * ## Discovery Flow
 *
 * 1. Parse session string (word-word-word format)
 * 2. Query ACDS for session information
 * 3. Assess NAT quality (STUN test)
 * 4. Either:
 *    a. Connect to existing host as participant
 *    b. Become host if no host exists or better NAT quality
 * 5. Handle dynamic role changes during session
 *
 * ## Implementation Architecture
 *
 * Discovery mode uses lib/session APIs for media handling:
 * - session_host_t: For rendering and broadcasting when elected host
 * - session_participant_t: For capture and display when not host
 * - discovery_session_t: For ACDS connection and host negotiation
 *
 * No network protocol code duplication - all media handling via lib/session.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0
 */

#include "main.h"
#include "../main.h" // Global exit API
#include "session.h"
#include "session/capture.h"
#include "session/display.h"
#include "session/host.h"
#include "session/render.h"
#include "session/keyboard_handler.h"
#include "session/client_like.h"

#include <stdio.h>

#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/common.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/network/acip/acds.h>

/* ============================================================================
 * Global Discovery Session
 * ============================================================================ */

/** Global discovery session created in discovery_main() and used by discovery_run() */
static discovery_session_t *g_discovery = NULL;

/* ============================================================================
 * State Change Callback
 * ============================================================================ */

/**
 * @brief Handle discovery session state changes
 *
 * @param new_state The new discovery state
 * @param user_data Pointer to discovery_session_t (unused, logged via session APIs)
 */
static void on_discovery_state_change(discovery_state_t new_state, void *user_data) {
  (void)user_data; // Unused

  const char *state_names[] = {"INIT",         "CONNECTING_ACDS", "CREATING_SESSION", "JOINING_SESSION",
                               "WAITING_PEER", "NEGOTIATING",     "STARTING_HOST",    "CONNECTING_HOST",
                               "ACTIVE",       "MIGRATING",       "FAILED",           "ENDED"};

  if (new_state >= 0 && new_state < (int)(sizeof(state_names) / sizeof(state_names[0]))) {
    log_info("Discovery state: %s", state_names[new_state]);
  }
}

/**
 * @brief Handle session ready (session string available for sharing)
 *
 * @param session_string The session string to share with peer
 * @param user_data Unused
 */
static void on_session_ready(const char *session_string, void *user_data) {
  (void)user_data; // Unused

  if (session_string && session_string[0]) {
    log_info("Session ready! Share this with your peer: %s", session_string);
  }
}

/**
 * @brief Handle discovery errors
 *
 * @param error Error code
 * @param message Error message
 * @param user_data Unused
 */
static void on_discovery_error(asciichat_error_t error, const char *message, void *user_data) {
  (void)user_data; // Unused

  log_error("Discovery error (%d): %s", error, message ? message : "Unknown");
  signal_exit();
}

/**
 * @brief Exit condition callback for render loop (participant mode)
 *
 * Called by render loop to check if it should exit. Also processes
 * discovery session events to keep negotiation responsive.
 *
 * @param user_data Pointer to discovery_session_t
 * @return true if render loop should exit
 */
static bool discovery_participant_render_should_exit(void *user_data) {
  discovery_session_t *discovery = (discovery_session_t *)user_data;

  // Check global exit flag first
  if (should_exit()) {
    return true;
  }

  // Process discovery session events (keep NAT negotiation responsive)
  // 50ms timeout for responsiveness
  asciichat_error_t result = discovery_session_process(discovery, 50 * NS_PER_MS_INT);
  if (result != ASCIICHAT_OK && result != ERROR_NETWORK_TIMEOUT) {
    log_error("Discovery session process failed: %d", result);
    signal_exit();
    return true;
  }

  // Exit if session is no longer active
  if (!discovery_session_is_active(discovery)) {
    // Still negotiating - continue loop
    return false;
  }

  return false;
}

/**
 * Adapter function for session capture exit callback
 * Converts from void*->bool signature to match session_capture_should_exit_fn
 *
 * @param user_data Unused (NULL)
 * @return true if capture should exit
 */
static bool discovery_capture_should_exit_adapter(void *user_data) {
  (void)user_data; // Unused parameter
  return should_exit();
}

/**
 * Discovery mode keyboard handler callback
 *
 * Enables interactive media controls during participant role.
 * Passes keyboard input to the session handler with capture context.
 *
 * @param capture Capture context for media source control
 * @param key Keyboard key code
 * @param user_data Unused (NULL)
 */
static void discovery_keyboard_handler(session_capture_ctx_t *capture, int key, void *user_data) {
  (void)user_data; // Unused parameter
  // Discovery mode doesn't have a display context yet, pass NULL for display
  // Help screen will be silently ignored (no-op)
  session_handle_keyboard_input(capture, NULL, (keyboard_key_t)key);
}

/* ============================================================================
 * Discovery Run Callback (for session_client_like)
 * ============================================================================ */

/**
 * Discovery mode main loop callback for session_client_like framework
 *
 * Handles role negotiation (host/participant) and media flow based on role.
 * The framework calls this after shared initialization (capture/display/audio).
 *
 * @param capture   Initialized capture context (provided by session_client_like)
 * @param display   Initialized display context (provided by session_client_like)
 * @param user_data Unused (NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t discovery_run(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                       void *user_data) {
  (void)user_data; // Unused

  asciichat_error_t result = ASCIICHAT_OK;
  bool snapshot_mode = GET_OPTION(snapshot_mode);

  // Wait for session to become active (host negotiation complete)
  // This processes ACDS events until we have a determined role
  // Note: Don't check should_exit() here - discovery requires peer connection even with --snapshot
  // The snapshot delay will be honored once we transition to active state and start capturing
  while (true) {
    result = discovery_session_process(g_discovery, 50 * NS_PER_MS_INT);
    if (result != ASCIICHAT_OK && result != ERROR_NETWORK_TIMEOUT) {
      log_error("Discovery session process failed: %d", result);
      return result;
    }

    if (discovery_session_is_active(g_discovery)) {
      break; // Session is active, role is determined
    }
  }

  // In snapshot mode, proceed to role handling even if should_exit() is true
  // We'll check for exit conditions during the actual frame rendering phase
  if (should_exit() && !snapshot_mode) {
    log_info("Shutdown requested during discovery negotiation");
    return ASCIICHAT_OK;
  }

  // Session is active - run based on our role
  bool we_are_host = discovery_session_is_host(g_discovery);

  if (we_are_host) {
    // HOST ROLE: Capture own media, manage participants, mix and broadcast
    log_info("Hosting session - capturing and broadcasting");

    // Get the host context already created and started during discovery negotiation
    session_host_t *host = discovery_session_get_host(g_discovery);
    if (!host) {
      log_fatal("Failed to get session host from discovery");
      return ERROR_INVALID_STATE;
    }

    // Set display context so host renders frames locally
    asciichat_error_t display_result = session_host_set_display(host, display);
    if (display_result != ASCIICHAT_OK) {
      log_error("Failed to set display context on host: %d", display_result);
      // Non-fatal - host can still render frames for broadcasting, just not locally
    } else {
      log_info("Display context set on host for local frame rendering");
    }

    // The passed capture context is set up for network mode (participant receiving frames).
    // For host role, we need local media capture. Reuse the existing capture context
    // which should have test pattern, webcam, or file configured.
    // Note: session_client_like should have initialized this with the media source.

    // Add memory participant for host's own media
    uint32_t host_participant_id = session_host_add_memory_participant(host);
    if (host_participant_id == 0) {
      log_error("Failed to add memory participant for host");
      // Host context is owned by discovery session - don't destroy
      return ERROR_INVALID_STATE;
    }

    log_info("Host participating with ID %u", host_participant_id);

    // Start the render thread (handles video mixing and ASCII rendering for display)
    result = session_host_start_render(host);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to start render thread: %d", result);
      // Host context is owned by discovery session - don't destroy
      return result;
    }

    // Main loop: capture own media and keep discovery responsive
    while (discovery_session_is_active(g_discovery)) {
      // Exit conditions:
      // 1. Non-snapshot mode: check should_exit() (Ctrl+C, SIGTERM, any exit signal)
      // 2. Snapshot mode: exit when should_exit() is true AND we've rendered at least one frame
      //    (to allow the full snapshot_delay to elapse while ignoring the snapshot timeout in should_exit)
      // 3. Discovery session ended: always exit

      if (should_exit()) {
        // In snapshot mode, if we haven't rendered any frames yet, don't exit yet
        // Wait until at least one frame has been displayed
        if (!snapshot_mode || g_snapshot_first_frame_rendered) {
          log_debug("Exit condition met (snapshot_mode=%d, first_frame_rendered=%d)",
                    snapshot_mode, g_snapshot_first_frame_rendered ? 1 : 0);
          break;
        } else {
          log_debug_every(5 * NS_PER_SEC_INT, "Waiting for first frame (snapshot_mode=true, first_frame_rendered=false)");
        }
      }

      // Capture frame from local media (webcam, test pattern, file, etc.)
      // The capture context is set up during session_client_like_run() and handles all media types
      image_t *frame = session_capture_read_frame(capture);
      if (frame) {
        log_debug_every(5 * NS_PER_SEC_INT, "Captured frame, injecting into host");
        // Inject host's frame into mixer for broadcasting
        session_host_inject_frame(host, host_participant_id, frame);
      } else {
        log_debug_every(5 * NS_PER_SEC_INT, "No frame captured");
      }

      // Keep discovery session responsive (NAT negotiations, migrations)
      result = discovery_session_process(g_discovery, 10 * NS_PER_MS_INT);
      if (result != ASCIICHAT_OK && result != ERROR_NETWORK_TIMEOUT) {
        log_error("Discovery session process failed: %d", result);
        break;
      }

      // Frame rate limiting (60 FPS)
      session_capture_sleep_for_fps(capture);
    }

    log_debug("Host role main loop exited (snapshot_mode=%d, first_frame_rendered=%d, session_active=%d)",
              snapshot_mode, g_snapshot_first_frame_rendered ? 1 : 0, discovery_session_is_active(g_discovery) ? 1 : 0);

    // Stop render thread before returning (display will be destroyed by caller)
    session_host_stop_render(host);

    if (should_exit()) {
      return ASCIICHAT_OK;
    }

    // If we reach here, discovery session is no longer active (role change)
    if (!discovery_session_is_active(g_discovery)) {
      log_info("Session ended or role changed");
      return ASCIICHAT_OK;
    }
  } else {
    // PARTICIPANT ROLE: Just wait for host connection
    // Don't use capture/display for participant discovery mode
    (void)capture;  // Not used
    (void)display;  // Not used - don't render

    // Session is active and we're a participant. In discovery mode, participants
    // don't render anything - they just wait for the host. Return early to skip
    // any display rendering that would happen after discovery_run returns.
    return ASCIICHAT_OK;
  }

  return result;
}

/* ============================================================================
 * Main Discovery Mode Loop
 * ============================================================================ */

/**
 * @brief Run discovery mode main loop
 *
 * Handles session discovery, host negotiation, and media flow.
 *
 * @return 0 on success, non-zero error code on failure
 */
int discovery_main(void) {
  log_debug("discovery_main() starting");

  // Discovery-specific setup: initialize ACDS connection and session negotiation
  // Note: Shared setup (keepawake, splash, terminal, capture, display, audio)
  // is handled by session_client_like_run()

  // Get session string from options (command-line argument)
  const char *session_string = GET_OPTION(session_string);
  bool is_initiator = (session_string == NULL || session_string[0] == '\0');

  int port_int = GET_OPTION(port);

  log_debug("Discovery: is_initiator=%d, port=%d", is_initiator, port_int);

  // Validate mutually exclusive options: discovery_service_url vs discovery_port
  const char *service_url = GET_OPTION(discovery_service_url);
  if (service_url && service_url[0] != '\0') {
    int discovery_port = GET_OPTION(discovery_port);
    if (discovery_port != ACIP_DISCOVERY_DEFAULT_PORT) {
      log_fatal("--discovery-service-url and --discovery-service-port are mutually exclusive");
      return ERROR_USAGE;
    }
  }

  // Create discovery session configuration
  discovery_config_t discovery_config = {
      .acds_address = GET_OPTION(discovery_server),
      .acds_port = (uint16_t)GET_OPTION(discovery_port),
      .acds_url = service_url,
      .session_string = is_initiator ? NULL : session_string,
      .local_port = (uint16_t)port_int,
      .on_state_change = on_discovery_state_change,
      .on_session_ready = on_session_ready,
      .on_error = on_discovery_error,
      .callback_user_data = NULL,
      .should_exit_callback = discovery_capture_should_exit_adapter,
      .exit_callback_data = NULL,
  };

  // Create discovery session (stored in global for discovery_run() to access)
  g_discovery = discovery_session_create(&discovery_config);
  if (!g_discovery) {
    log_fatal("Failed to create discovery session");
    return ERROR_MEMORY;
  }

  // Start discovery session (connects to ACDS, creates/joins, initiates NAT negotiation)
  log_debug("Discovery: starting discovery session");
  asciichat_error_t result = discovery_session_start(g_discovery);
  if (result != ASCIICHAT_OK) {
    log_fatal("Failed to start discovery session: %d", result);
    discovery_session_destroy(g_discovery);
    g_discovery = NULL;
    return result;
  }

  // No network interrupt callback needed - discovery session handles its own shutdown
  set_interrupt_callback(NULL);

  /* ========================================================================
   * Configure and Run Shared Client-Like Session Framework
   * ========================================================================
   * session_client_like_run() handles all shared initialization:
   * - Terminal output management
   * - Keepawake system
   * - Splash screen lifecycle
   * - Media source selection
   * - FPS probing
   * - Audio initialization
   * - Display context creation
   * - Proper cleanup ordering
   *
   * Discovery mode provides:
   * - discovery_run() callback: NAT negotiation, role determination, media handling
   * - discovery_keyboard_handler: interactive controls for participant role
   */

  session_client_like_config_t config = {
      .run_fn = discovery_run,
      .run_user_data = NULL,
      .tcp_client = NULL,
      .websocket_client = NULL,
      .discovery = (void *)g_discovery, // Opaque pointer to discovery session
      .custom_should_exit = NULL,
      .exit_user_data = NULL,
      .keyboard_handler = discovery_keyboard_handler,
      .max_reconnect_attempts = 0, // Discovery doesn't retry - role is determined once
      .should_reconnect_callback = NULL,
      .reconnect_user_data = NULL,
      .reconnect_delay_ms = 0,
      .print_newline_on_tty_exit = false, // Server/participant manages cursor
  };

  log_debug("Discovery: calling session_client_like_run()");
  asciichat_error_t session_result = session_client_like_run(&config);
  log_debug("Discovery: session_client_like_run() returned %d", session_result);

  // Cleanup discovery session
  log_debug("Discovery: cleaning up");

  if (g_discovery) {
    discovery_session_stop(g_discovery);
    discovery_session_destroy(g_discovery);
    g_discovery = NULL;
  }

  return (session_result == ASCIICHAT_OK) ? 0 : (int)session_result;
}

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
#include "session.h"
#include "session/capture.h"
#include "session/display.h"

#include <stdatomic.h>
#include <stdio.h>
#include <signal.h>

#include "log/logging.h"
#include "options/options.h"
#include "options/common.h"
#include "platform/abstraction.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

/**
 * @brief Atomic flag indicating discovery mode should exit
 *
 * Set by signal handlers or error conditions to trigger graceful shutdown.
 */
static atomic_bool g_discovery_should_exit = false;

/* ============================================================================
 * Signal Handler
 * ============================================================================ */

/**
 * @brief Console control handler for Ctrl+C and related events
 *
 * @param event The control event that occurred
 * @return true if the event was handled
 */
static bool discovery_console_ctrl_handler(console_ctrl_event_t event) {
  if (event != CONSOLE_CTRL_C && event != CONSOLE_CTRL_BREAK) {
    return false;
  }

  // Use atomic instead of volatile for signal handler
  static _Atomic int ctrl_c_count = 0;
  int count = atomic_fetch_add(&ctrl_c_count, 1) + 1;

  if (count > 1) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#else
    _exit(1);
#endif
  }

  discovery_signal_exit();
  return true;
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * @brief Check if discovery mode should exit
 */
bool discovery_should_exit(void) {
  return atomic_load(&g_discovery_should_exit);
}

/**
 * @brief Signal discovery mode to exit
 */
void discovery_signal_exit(void) {
  atomic_store(&g_discovery_should_exit, true);
}

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
  discovery_signal_exit();
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
  log_info("Starting discovery mode");

  // Install console control-c handler
  platform_set_console_ctrl_handler(discovery_console_ctrl_handler);

#ifndef _WIN32
  platform_signal(SIGPIPE, SIG_IGN);
#endif

  // Get session string from options (command-line argument)
  const char *session_string = GET_OPTION(session_string);
  bool is_initiator = (session_string == NULL || session_string[0] == '\0');

  // Parse port from options (stored as string)
  int port_int = strtoint_safe(GET_OPTION(port));

  // Create discovery session configuration
  discovery_config_t discovery_config = {
      .acds_address = GET_OPTION(discovery_server),
      .acds_port = (uint16_t)GET_OPTION(discovery_port),
      .session_string = is_initiator ? NULL : session_string,
      .local_port = (uint16_t)port_int,
      .on_state_change = on_discovery_state_change,
      .on_session_ready = on_session_ready,
      .on_error = on_discovery_error,
      .callback_user_data = NULL,
  };

  // Create discovery session
  discovery_session_t *discovery = discovery_session_create(&discovery_config);
  if (!discovery) {
    log_fatal("Failed to create discovery session");
    return ERROR_MEMORY;
  }

  // Start discovery session (connects to ACDS, creates/joins, negotiates)
  asciichat_error_t result = discovery_session_start(discovery);
  if (result != ASCIICHAT_OK) {
    log_fatal("Failed to start discovery session: %d", result);
    discovery_session_destroy(discovery);
    return result;
  }

  // Create capture and display contexts from command-line options
  // Passing NULL config auto-initializes from GET_OPTION()
  // This handles: media files, stdin, webcam, test patterns, terminal capabilities, ANSI init
  session_capture_ctx_t *capture = session_capture_create(NULL);
  if (!capture) {
    log_fatal("Failed to initialize capture source");
    discovery_session_destroy(discovery);
    return ERROR_MEDIA_INIT;
  }

  session_display_ctx_t *display = session_display_create(NULL);
  if (!display) {
    log_fatal("Failed to initialize display");
    session_capture_destroy(capture);
    discovery_session_destroy(discovery);
    return ERROR_DISPLAY;
  }

  log_info("Discovery mode running - press Ctrl+C to exit");
  log_set_terminal_output(false);

  // Main loop: process discovery events and handle media
  while (!discovery_should_exit()) {
    // Process discovery session events (state transitions, negotiations, etc)
    result = discovery_session_process(discovery, 50); // 50ms timeout for responsiveness
    if (result != ASCIICHAT_OK && result != ERROR_NETWORK_TIMEOUT) {
      log_error("Discovery session process failed: %d", result);
      break;
    }

    // Check if session is active (host negotiation complete)
    if (!discovery_session_is_active(discovery)) {
      continue; // Still negotiating, keep processing events
    }

    // ★ Insight ─────────────────────────────────────
    // Discovery session now provides either host_ctx or participant_ctx
    // based on the negotiated role. This follows lib/session patterns
    // to avoid duplicating network protocol code.
    // ─────────────────────────────────────────────────

    // Session is active - handle media based on role
    // NOTE: The actual media handling (capture, display, rendering)
    // is abstracted in session_host_t and session_participant_t,
    // which internally manage WebRTC and network protocol details.
    // We just need to drive the main loop.

    // For now, just display a status message
    // (Actual frame rendering/display happens in host/participant contexts)
    static bool printed_status = false;
    if (!printed_status) {
      if (discovery_session_is_host(discovery)) {
        log_info("Hosting session - rendering and broadcasting");
      } else {
        log_info("Participant in session - displaying host's frames");
      }
      printed_status = true;
    }

    // Brief sleep to avoid busy loop
    platform_sleep_ms(10);
  }

  // Cleanup
  log_set_terminal_output(true);
  log_info("Discovery mode shutting down");

  session_display_destroy(display);
  session_capture_destroy(capture);
  discovery_session_stop(discovery);
  discovery_session_destroy(discovery);

  return 0;
}

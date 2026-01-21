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
 * ## Implementation Status
 *
 * This is a Phase 3 stub implementation. Full discovery logic will be
 * implemented in Phase 4 (Host Migration & Edge Cases).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0
 */

#include "main.h"

#include <stdatomic.h>
#include <stdio.h>

#include "common.h"
#include "asciichat_errno.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/rcu.h"
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

/**
 * @brief Discovery mode entry point
 *
 * @return 0 on success, non-zero error code on failure
 */
int discovery_main(void) {
  log_info("Discovery mode starting...");

  const char *session_string = GET_OPTION(session_string);
  bool is_initiator = (!session_string || session_string[0] == '\0');

  if (is_initiator) {
    log_info("Starting new session (will generate session string via ACDS)...");
  } else {
    log_info("Joining session: %s", session_string);
  }

  // TODO: Phase 4 implementation
  // 1. Connect to ACDS
  // 2. If initiator: SESSION_CREATE -> get session string, wait for joiners
  // 3. If joiner: SESSION_JOIN -> check if host established
  // 4. NAT quality detection and host negotiation
  // 5. Start as host or connect as participant

  log_warn("Discovery mode is not yet fully implemented (Phase 3 stub)");

  while (!discovery_should_exit()) {
    platform_sleep_usec(100000);
  }

  log_info("Discovery mode exiting...");
  return ASCIICHAT_OK;
}

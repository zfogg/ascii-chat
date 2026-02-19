/**
 * @file discovery/main.h
 * @ingroup discovery_main
 * @brief ascii-chat Discovery Mode Entry Point Header
 *
 * This header exposes the discovery mode entry point for the unified binary architecture.
 * The unified binary dispatches to discovery_main() when invoked as `ascii-chat discovery`.
 *
 * ## Discovery Mode Philosophy
 *
 * "ascii-chat should be as simple as making a phone call."
 *
 * Discovery mode enables zero-configuration video chat:
 * - **One command to start**: `ascii-chat swift-river-mountain`
 * - **Automatic host negotiation**: Best NAT quality participant becomes host
 * - **Dynamic role switching**: Participants can become hosts mid-session
 * - **Seamless failover**: If host disconnects, another participant takes over
 *
 * ## Mode Entry Point Contract
 *
 * Each mode entry point (server_main, client_main, discovery_main) must:
 * - Accept no arguments: `int mode_main(void)`
 * - Options are already parsed by main dispatcher (available via GET_OPTION())
 * - Return 0 on success, non-zero error code on failure
 * - Perform mode-specific initialization and main loop
 * - Perform complete cleanup before returning
 *
 * ## Implementation Notes
 *
 * Discovery mode combines client and server capabilities:
 * - Uses session library (lib/session/) for capture, display, hosting
 * - Connects to ACDS for session discovery and NAT quality assessment
 * - Can transition between participant and host roles dynamically
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Discovery mode entry point for unified binary
 *
 * This function implements the complete discovery mode lifecycle:
 * - Session discovery via ACDS or LAN mDNS
 * - NAT quality assessment for host selection
 * - Dynamic role switching (participant <-> host)
 * - Media streaming (video/audio capture and display)
 * - Graceful shutdown and cleanup
 *
 * Options are already parsed by the main dispatcher before this function
 * is called, so they are available via GET_OPTION().
 *
 * @return 0 on success, non-zero error code on failure
 *
 * @par Example
 * @code{.sh}
 * # Join a session (session string as positional argument):
 * ascii-chat discovery swift-river-mountain
 *
 * # Or more concisely (session strings are auto-detected):
 * ascii-chat swift-river-mountain
 * @endcode
 */
int discovery_main(void);

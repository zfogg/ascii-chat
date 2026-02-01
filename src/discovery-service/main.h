#pragma once

/**
 * @file acds/main.h
 * @brief 🔍 ascii-chat Discovery Service (acds) main entry point
 *
 * The discovery service provides session management and WebRTC signaling
 * for peer-to-peer ascii-chat connections. It uses the ACIP binary protocol
 * over raw TCP to coordinate session creation, lookup, and WebRTC SDP/ICE relay.
 *
 * ## Key Features
 *
 * - **Session Management**: Create, lookup, join, and leave sessions
 * - **Session Strings**: Memorable session identifiers (e.g., "swift-river-mountain")
 * - **WebRTC Signaling**: SDP offer/answer and ICE candidate relay
 * - **Identity Keys**: Ed25519 keys for session authentication
 * - **SQLite Persistence**: Session registry and rate limiting
 * - **Zero New Dependencies**: Reuses all ascii-chat infrastructure
 *
 * ## Protocol
 *
 * Uses ACIP binary protocol (extends packet_type_t 0x20-0xFF):
 * - SESSION_CREATE/CREATED - Create new session
 * - SESSION_LOOKUP/INFO - Find existing session
 * - SESSION_JOIN/JOINED - Join session
 * - WEBRTC_SDP - Relay SDP offers/answers
 * - WEBRTC_ICE - Relay ICE candidates
 *
 * ## Transport
 *
 * Raw TCP on port 27225 (configurable via --port):
 * - Reuses lib/network/ packet handling
 * - Same crypto handshake as main ascii-chat
 * - Same accept/send/recv patterns
 *
 * ## Command-Line Usage
 *
 * ```
 * acds [options] [address1] [address2]
 *
 * Options:
 *   --port PORT              Listen port (default: 27225)
 *   --database PATH          SQLite database path (default: ~/.config/ascii-chat/acds.db)
 *   --key PATH               Ed25519 identity key path (default: ~/.config/ascii-chat/acds_identity)
 *   --log-file FILE          Log file path (default: stderr)
 *   --log-level LEVEL        Log level (dev, debug, info, warn, error, fatal)
 *   --help                   Show this help
 *   --version                Show version
 *
 * Positional Arguments (Bind Addresses):
 *   address1                 IPv4 or IPv6 bind address (0-2 addresses)
 *   address2                 Second bind address (must be different IP version)
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include "common.h"
#include "network/webrtc/stun.h"
#include "network/webrtc/turn.h"

/**
 * @brief Discovery server configuration
 *
 * Stores all runtime configuration for the discovery server,
 * parsed from command-line arguments.
 */
typedef struct {
  int port;                     ///< TCP listen port (default 27225)
  char address[256];            ///< IPv4 bind address (empty = all interfaces)
  char address6[256];           ///< IPv6 bind address (empty = all interfaces)
  char database_path[512];      ///< SQLite database path
  char key_path[512];           ///< Ed25519 identity key file path
  char log_file[512];           ///< Log file path (empty = stderr)
  log_level_t log_level;        ///< Logging verbosity level
  bool require_server_identity; ///< Require servers to provide signed identity when creating sessions
  bool require_client_identity; ///< Require clients to provide signed identity when joining sessions
  bool require_server_verify;   ///< ACDS policy: require servers to verify client identity during handshake
  bool require_client_verify;   ///< ACDS policy: require clients to verify server identity during handshake

  // WebRTC connectivity servers
  uint8_t stun_count;            ///< Number of configured STUN servers (0-4)
  stun_server_t stun_servers[4]; ///< STUN server configurations
  uint8_t turn_count;            ///< Number of configured TURN servers (0-4)
  turn_server_t turn_servers[4]; ///< TURN server configurations
  char turn_secret[256];         ///< Shared secret for TURN credential generation (HMAC-SHA1)
} acds_config_t;

/**
 * @brief ACDS (discovery-service mode) entry point
 *
 * Expects options already parsed and shared initialization complete.
 * Called by main.c mode dispatcher after options_init() and asciichat_shared_init().
 *
 * @return Exit code (0 = success, non-zero = error)
 */
int acds_main(void);

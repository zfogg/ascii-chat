#pragma once

/**
 * @file acds/main.h
 * @brief 🔍 ASCII-Chat Discovery Service (acds) main entry point
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

/**
 * @brief Discovery server configuration
 *
 * Stores all runtime configuration for the discovery server,
 * parsed from command-line arguments.
 */
typedef struct {
  int port;                ///< TCP listen port (default 27225)
  char address[256];       ///< IPv4 bind address (empty = all interfaces)
  char address6[256];      ///< IPv6 bind address (empty = all interfaces)
  char database_path[512]; ///< SQLite database path
  char key_path[512];      ///< Ed25519 identity key file path
  char log_file[512];      ///< Log file path (empty = stderr)
  log_level_t log_level;   ///< Logging verbosity level
} acds_config_t;

/**
 * @brief Parse command-line arguments
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param config Output configuration structure
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse error
 */
asciichat_error_t acds_parse_args(int argc, char **argv, acds_config_t *config);

/**
 * @brief Print usage information
 *
 * @param program_name Program name (argv[0])
 */
void acds_print_usage(const char *program_name);

/**
 * @brief Print version information
 */
void acds_print_version(void);

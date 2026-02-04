/**
 * @file connection_state.h
 * @brief 🎯 Connection state machine for Phase 3 WebRTC fallback integration
 *
 * Defines the 13-state connection state machine for automatic fallback sequence:
 * 1. **Stage 1**: Direct TCP (3s timeout)
 * 2. **Stage 2**: WebRTC + STUN (8s timeout)
 * 3. **Stage 3**: WebRTC + TURN (15s timeout)
 *
 * The state machine tracks:
 * - Current connection stage and state within that stage
 * - Active transport (TCP or WebRTC)
 * - Timeout tracking for each stage
 * - Session context (session_id, participant_id for WebRTC)
 * - STUN/TURN server configuration
 * - CLI flags (prefer_webrtc, force_tcp, skip_stun, disable_turn)
 *
 * Each stage has 4 states: ATTEMPTING, SIGNALING (for WebRTC), CONNECTED, FAILED
 * Terminal states: IDLE (no connection), CONNECTED (success), FAILED (all stages exhausted)
 *
 * @date January 2026
 * @version 1.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/platform/abstraction.h>

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

struct tcp_client; // Forward declaration for TCP client instance

/* ============================================================================
 * Connection State Enumeration
 * ============================================================================ */

/**
 * @brief 13-state connection state machine
 *
 * States are grouped by stage:
 * - Initial/Terminal: IDLE, CONNECTED, DISCONNECTED, FAILED
 * - Stage 1 (TCP): ATTEMPTING_DIRECT_TCP, DIRECT_TCP_CONNECTED, DIRECT_TCP_FAILED
 * - Stage 2 (STUN): ATTEMPTING_WEBRTC_STUN, WEBRTC_STUN_SIGNALING, WEBRTC_STUN_CONNECTED, WEBRTC_STUN_FAILED
 * - Stage 3 (TURN): ATTEMPTING_WEBRTC_TURN, WEBRTC_TURN_SIGNALING, WEBRTC_TURN_CONNECTED, WEBRTC_TURN_FAILED
 */
typedef enum {
  // Initial and terminal states
  CONN_STATE_IDLE = 0,          ///< Not connected, no attempt in progress
  CONN_STATE_CONNECTED = 20,    ///< Successfully connected (any transport)
  CONN_STATE_DISCONNECTED = 21, ///< Clean disconnect (user initiated)
  CONN_STATE_FAILED = 22,       ///< All fallback stages exhausted

  // Stage 1: Direct TCP (3s timeout)
  CONN_STATE_ATTEMPTING_DIRECT_TCP = 1, ///< Attempting direct TCP connection
  CONN_STATE_DIRECT_TCP_CONNECTED = 2,  ///< Direct TCP connection established
  CONN_STATE_DIRECT_TCP_FAILED = 3,     ///< Direct TCP failed, falling back to STUN

  // Stage 2: WebRTC + STUN (8s timeout)
  CONN_STATE_ATTEMPTING_WEBRTC_STUN = 4, ///< Initiating WebRTC + STUN connection
  CONN_STATE_WEBRTC_STUN_SIGNALING = 5,  ///< Exchanging SDP/ICE candidates via ACDS
  CONN_STATE_WEBRTC_STUN_CONNECTED = 6,  ///< WebRTC + STUN connection established
  CONN_STATE_WEBRTC_STUN_FAILED = 7,     ///< STUN failed, falling back to TURN

  // Stage 3: WebRTC + TURN (15s timeout)
  CONN_STATE_ATTEMPTING_WEBRTC_TURN = 8, ///< Initiating WebRTC + TURN connection
  CONN_STATE_WEBRTC_TURN_SIGNALING = 9,  ///< Exchanging SDP/ICE candidates with TURN relay
  CONN_STATE_WEBRTC_TURN_CONNECTED = 10, ///< WebRTC + TURN connection established
  CONN_STATE_WEBRTC_TURN_FAILED = 11,    ///< All stages exhausted
} connection_state_t;

/* ============================================================================
 * Timeout Constants (in nanoseconds)
 * ============================================================================ */

#define CONN_TIMEOUT_DIRECT_TCP (3LL * NS_PER_SEC_INT)   ///< Stage 1: Direct TCP timeout (3s)
#define CONN_TIMEOUT_WEBRTC_STUN (8LL * NS_PER_SEC_INT)  ///< Stage 2: WebRTC+STUN timeout (8s)
#define CONN_TIMEOUT_WEBRTC_TURN (15LL * NS_PER_SEC_INT) ///< Stage 3: WebRTC+TURN timeout (15s)

/* ============================================================================
 * Session Context (for WebRTC connections)
 * ============================================================================ */

/**
 * @brief Session context for WebRTC signaling
 *
 * Passed from ACDS SESSION_JOINED callback to WebRTC connection handler.
 * Identifies the client's session and participant within that session.
 */
typedef struct {
  char session_string[128];   ///< Session string (e.g., "mystic-stone-obelisk")
  uint8_t session_id[16];     ///< Session UUID (binary)
  uint8_t participant_id[16]; ///< Client's participant UUID (binary)
  uint16_t server_port;       ///< Server port for connection
  char server_address[64];    ///< Server IP/hostname for connection
} connection_session_context_t;

/* ============================================================================
 * STUN/TURN Configuration
 * ============================================================================ */

/**
 * @brief STUN and TURN server configuration for WebRTC fallback
 */
typedef struct {
  char stun_server[256]; ///< STUN server address (e.g., "stun.l.google.com:19302")
  uint16_t stun_port;    ///< STUN server port

  char turn_server[256];   ///< TURN relay server address
  uint16_t turn_port;      ///< TURN relay port
  char turn_username[128]; ///< TURN username (from ACDS or defaults)
  char turn_password[256]; ///< TURN password (from ACDS or defaults)
} connection_stun_turn_config_t;

/* ============================================================================
 * Connection Attempt Context
 * ============================================================================ */

/**
 * @brief Master context for connection attempt with fallback
 *
 * Orchestrates the entire connection flow including:
 * - State machine tracking (current state, previous state)
 * - Timeout management (stage start time, elapsed time)
 * - Transport management (TCP socket, WebRTC peer connection)
 * - Session context (session_id, participant_id)
 * - Configuration (STUN/TURN servers, CLI flags)
 * - Statistics (retry count, stage transitions)
 *
 * Created at client startup, passed through all connection stages.
 * Persists across reconnection attempts to maintain state.
 */
typedef struct {
  // ─────────────────────────────────────────────────────────────
  // State Machine
  // ─────────────────────────────────────────────────────────────

  connection_state_t current_state;  ///< Current connection state
  connection_state_t previous_state; ///< Previous state (for debugging)

  // ─────────────────────────────────────────────────────────────
  // Timeout Tracking
  // ─────────────────────────────────────────────────────────────

  time_t stage_start_time;                ///< When current stage began
  uint32_t current_stage_timeout_seconds; ///< Timeout for current stage (3/8/15)

  // ─────────────────────────────────────────────────────────────
  // Active Transports
  // ─────────────────────────────────────────────────────────────

  acip_transport_t *tcp_transport;    ///< TCP transport (Stage 1) - may be NULL
  acip_transport_t *acds_transport;   ///< ACDS signaling transport (Stages 2/3) - may be NULL
  acip_transport_t *webrtc_transport; ///< WebRTC transport (Stages 2/3) - may be NULL
  acip_transport_t *active_transport; ///< Currently active transport (whichever succeeded)

  struct tcp_client *tcp_client_instance; ///< TCP client instance (Direct TCP only) - owned by context

  // ─────────────────────────────────────────────────────────────
  // WebRTC Session Context
  // ─────────────────────────────────────────────────────────────

  connection_session_context_t session_ctx; ///< Session context from ACDS
  webrtc_peer_manager_t *peer_manager;      ///< WebRTC peer manager (Stages 2/3)

  // ─────────────────────────────────────────────────────────────
  // WebRTC Connection Synchronization
  // ─────────────────────────────────────────────────────────────

  mutex_t webrtc_mutex;           ///< Mutex for WebRTC callback synchronization
  cond_t webrtc_ready_cond;       ///< Condition variable for on_transport_ready
  bool webrtc_transport_received; ///< Flag: transport_ready callback fired

  // ─────────────────────────────────────────────────────────────
  // STUN/TURN Configuration
  // ─────────────────────────────────────────────────────────────

  connection_stun_turn_config_t stun_turn_cfg; ///< STUN/TURN server config

  // ─────────────────────────────────────────────────────────────
  // Connection Preferences (from CLI flags)
  // ─────────────────────────────────────────────────────────────

  bool prefer_webrtc;       ///< --prefer-webrtc flag
  bool no_webrtc;           ///< --no-webrtc flag (disable WebRTC, TCP only)
  bool webrtc_skip_stun;    ///< --webrtc-skip-stun flag (skip Stage 2 STUN)
  bool webrtc_disable_turn; ///< --webrtc-disable-turn flag (skip Stage 3 TURN)

  // ─────────────────────────────────────────────────────────────
  // Statistics
  // ─────────────────────────────────────────────────────────────

  uint32_t reconnect_attempt; ///< Reconnection attempt number (1st, 2nd, etc.)
  uint32_t stage_failures;    ///< How many stages have failed
  uint32_t total_transitions; ///< Total state transitions (for metrics)

} connection_attempt_context_t;

/* ============================================================================
 * Function Declarations
 * ============================================================================ */

/**
 * @brief Initialize connection attempt context
 *
 * Sets up initial state, resets timeouts, and prepares for connection attempt.
 *
 * @param ctx Connection context to initialize
 * @param prefer_webrtc CLI flag: prefer WebRTC over TCP
 * @param no_webrtc CLI flag: disable WebRTC, use TCP only
 * @param webrtc_skip_stun CLI flag: skip STUN stage, go straight to TURN
 * @param webrtc_disable_turn CLI flag: disable TURN stage (use STUN as fallback)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t connection_context_init(connection_attempt_context_t *ctx, bool prefer_webrtc, bool no_webrtc,
                                          bool webrtc_skip_stun, bool webrtc_disable_turn);

/**
 * @brief Transition to next connection state
 *
 * Updates current/previous states, logs transition, and updates timeout if stage changed.
 * Validates state transitions to prevent invalid sequences.
 *
 * @param ctx Connection context
 * @param new_state Next state to transition to
 * @return ASCIICHAT_OK on success, error code on invalid transition
 */
asciichat_error_t connection_state_transition(connection_attempt_context_t *ctx, connection_state_t new_state);

/**
 * @brief Check if current stage has exceeded timeout
 *
 * Compares elapsed time since stage_start_time against current_stage_timeout_seconds.
 *
 * @param ctx Connection context
 * @return true if timeout exceeded, false otherwise
 */
bool connection_check_timeout(const connection_attempt_context_t *ctx);

/**
 * @brief Get human-readable state name for logging
 *
 * @param state Connection state
 * @return Static string representation (e.g., "DIRECT_TCP_CONNECTED")
 */
const char *connection_state_name(connection_state_t state);

/**
 * @brief Get current stage number (1, 2, or 3)
 *
 * @param state Connection state
 * @return Stage number (1-3), or 0 if idle/terminal state
 */
uint32_t connection_get_stage(connection_state_t state);

/**
 * @brief Cleanup connection attempt context
 *
 * Closes and releases any active transports, frees WebRTC peer manager.
 * Called on connection success, failure, or shutdown.
 *
 * @param ctx Connection context to cleanup
 */
void connection_context_cleanup(connection_attempt_context_t *ctx);

/**
 * @brief Attempt connection with 3-stage fallback sequence
 *
 * Orchestrates the complete connection attempt with automatic fallback:
 * 1. **Stage 1 (3s)**: Direct TCP connection
 * 2. **Stage 2 (8s)**: WebRTC + STUN (NAT traversal via hole punching)
 * 3. **Stage 3 (15s)**: WebRTC + TURN (relay fallback)
 *
 * Each stage has independent timeout. If a stage fails or times out,
 * the next stage begins automatically. On success, sets active_transport
 * in the context and returns ASCIICHAT_OK.
 *
 * @param ctx Connection context (initialized via connection_context_init)
 * @param server_address Server hostname or IP address
 * @param server_port Server port number
 * @param acds_server ACDS server hostname or IP address (for signaling)
 * @param acds_port ACDS server port number
 * @return ASCIICHAT_OK on successful connection (via any stage)
 * @return ERROR_* code on failure (all stages exhausted)
 *
 * @note ctx.active_transport will be set to the successful transport
 * @note ctx.current_state will be set to CONN_STATE_CONNECTED on success
 */
asciichat_error_t connection_attempt_with_fallback(connection_attempt_context_t *ctx, const char *server_address,
                                                   uint16_t server_port, const char *acds_server, uint16_t acds_port);

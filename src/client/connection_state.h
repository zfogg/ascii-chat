/**
 * @file connection_state.h
 * @brief Connection state machine for TCP client connections
 *
 * Defines the connection state machine for direct TCP connections.
 * Client mode uses direct TCP only - no WebRTC fallback.
 *
 * The state machine tracks:
 * - Current connection state
 * - Active TCP transport
 * - Timeout tracking for connection attempts
 *
 * States: IDLE, ATTEMPTING, CONNECTED, DISCONNECTED, FAILED
 *
 * @date January 2026
 * @version 2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/platform/abstraction.h>

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

struct tcp_client;       // Forward declaration for TCP client instance
struct websocket_client; // Forward declaration for WebSocket client instance

/* ============================================================================
 * Connection State Enumeration
 * ============================================================================ */

/**
 * @brief Simple TCP connection state machine
 *
 * States:
 * - IDLE: Not connected, no attempt in progress
 * - ATTEMPTING: TCP connection in progress
 * - CONNECTED: Successfully connected via TCP
 * - DISCONNECTED: Clean disconnect (user initiated)
 * - FAILED: Connection attempt failed
 */
typedef enum {
  CONN_STATE_IDLE = 0,         ///< Not connected, no attempt in progress
  CONN_STATE_ATTEMPTING = 1,   ///< Attempting TCP connection
  CONN_STATE_CONNECTED = 2,    ///< Successfully connected via TCP
  CONN_STATE_DISCONNECTED = 3, ///< Clean disconnect (user initiated)
  CONN_STATE_FAILED = 4,       ///< Connection attempt failed
} connection_state_t;

/* ============================================================================
 * Timeout Constants (in nanoseconds)
 * ============================================================================ */

#define CONN_TIMEOUT_TCP (3LL * NS_PER_SEC_INT) ///< TCP connection timeout (3s)

/* ============================================================================
 * Connection Attempt Context
 * ============================================================================ */

/**
 * @brief Context for TCP connection attempts
 *
 * Manages TCP connection state including:
 * - State machine tracking (current state, previous state)
 * - Timeout management
 * - TCP transport and client instance
 * - Statistics (retry count, transitions)
 *
 * Created at client startup, persists across reconnection attempts.
 */
typedef struct {
  // State Machine
  connection_state_t current_state;  ///< Current connection state
  connection_state_t previous_state; ///< Previous state (for debugging)

  // Timeout Tracking (in nanoseconds)
  uint64_t attempt_start_time_ns; ///< When current attempt began
  uint64_t timeout_ns;            ///< Timeout for connection attempt

  // Transport Instances (one will be non-NULL at a time)
  acip_transport_t *active_transport;          ///< Currently active transport
  struct tcp_client *tcp_client_instance;      ///< TCP client instance - owned by context
  struct websocket_client *ws_client_instance; ///< WebSocket client instance - owned by context

  // Statistics
  uint32_t reconnect_attempt; ///< Reconnection attempt number (1st, 2nd, etc.)
  uint32_t total_transitions; ///< Total state transitions (for metrics)

  // Reconnection Strategy
  bool is_reconnection; ///< True if reconnecting after successful connection (not first connect)

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
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t connection_context_init(connection_attempt_context_t *ctx);

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
 * @brief Check if connection attempt has exceeded timeout
 *
 * Compares elapsed time since attempt start against timeout.
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
 * @brief Cleanup connection attempt context
 *
 * Closes and releases any active transports, frees WebRTC peer manager.
 * Called on connection success, failure, or shutdown.
 *
 * @param ctx Connection context to cleanup
 */
void connection_context_cleanup(connection_attempt_context_t *ctx);

/**
 * @brief Attempt TCP connection
 *
 * Attempts a direct TCP connection to the specified server.
 * On success, sets active_transport in the context.
 *
 * @param ctx Connection context (initialized via connection_context_init)
 * @param server_address Server hostname or IP address
 * @param server_port Server port number
 * @param pre_created_tcp_client Optional pre-created TCP client (from framework). If NULL, creates one.
 * @return ASCIICHAT_OK on successful connection
 * @return ERROR_* code on failure
 *
 * @note ctx.active_transport will be set to the TCP transport on success
 * @note ctx.current_state will be set to CONN_STATE_CONNECTED on success
 */
asciichat_error_t connection_attempt_tcp(connection_attempt_context_t *ctx, const char *server_address,
                                         uint16_t server_port, struct tcp_client *pre_created_tcp_client);

/**
 * @brief Attempt WebSocket connection
 *
 * Attempts a WebSocket connection to the specified server (ws:// or wss://).
 * On success, sets active_transport in the context.
 *
 * @param ctx Connection context (initialized via connection_context_init)
 * @param ws_url WebSocket URL (e.g., "ws://localhost:27226" or "wss://server.com:27226")
 * @return ASCIICHAT_OK on successful connection
 * @return ERROR_* code on failure
 *
 * @note ctx.active_transport will be set to the WebSocket transport on success
 * @note ctx.current_state will be set to CONN_STATE_CONNECTED on success
 */
asciichat_error_t connection_attempt_websocket(connection_attempt_context_t *ctx, const char *ws_url);

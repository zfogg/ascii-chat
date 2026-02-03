/**
 * @file connection_attempt.c
 * @brief 🎯 Connection fallback orchestrator for Phase 3 WebRTC integration
 *
 * Implements the 3-stage connection fallback sequence:
 * 1. **Stage 1**: Direct TCP (3s timeout) - Fastest path for accessible servers
 * 2. **Stage 2**: WebRTC + STUN (8s timeout) - NAT traversal via hole punching
 * 3. **Stage 3**: WebRTC + TURN (15s timeout) - Last resort relay
 *
 * Features:
 * - State machine with 13 states tracking all stages
 * - Automatic timeout-based progression between stages
 * - CLI flags to override or force specific connection methods
 * - Proper resource cleanup on transitions and failures
 * - Detailed logging of state transitions and errors
 *
 * Integration Points:
 * - Called from src/client/main.c connection loop (replaces direct TCP attempt)
 * - Returns active transport when connection succeeds
 * - Maintains session context for WebRTC handshake via ACDS
 *
 * @date January 2026
 * @version 1.0
 */

#include "connection_state.h"
#include "server.h"
#include "protocol.h"
#include "webrtc.h"
#include "crypto.h"
#include "main.h"
#include <ascii-chat/crypto/discovery_keys.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/tcp/client.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/platform/abstraction.h>

#include <time.h>
#include <string.h>
#include <memory.h>
#include <stdio.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Wrapper for should_exit() to match parallel_connect callback signature
 *
 * The callback signature requires (void *user_data), but should_exit() takes
 * no parameters. This wrapper adapts between the two.
 *
 * @param user_data Unused parameter (required by callback signature)
 * @return true if exit was requested, false otherwise
 */
static bool should_exit_callback_wrapper(void *user_data) {
  (void)user_data; // Unused
  return should_exit();
}

/* ============================================================================
 * State Machine Helper Functions
 * ============================================================================ */

/**
 * @brief Get human-readable state name for logging
 */
const char *connection_state_name(connection_state_t state) {
  switch (state) {
  case CONN_STATE_IDLE:
    return "IDLE";
  case CONN_STATE_CONNECTED:
    return "CONNECTED";
  case CONN_STATE_DISCONNECTED:
    return "DISCONNECTED";
  case CONN_STATE_FAILED:
    return "FAILED";

  case CONN_STATE_ATTEMPTING_DIRECT_TCP:
    return "ATTEMPTING_DIRECT_TCP";
  case CONN_STATE_DIRECT_TCP_CONNECTED:
    return "DIRECT_TCP_CONNECTED";
  case CONN_STATE_DIRECT_TCP_FAILED:
    return "DIRECT_TCP_FAILED";

  case CONN_STATE_ATTEMPTING_WEBRTC_STUN:
    return "ATTEMPTING_WEBRTC_STUN";
  case CONN_STATE_WEBRTC_STUN_SIGNALING:
    return "WEBRTC_STUN_SIGNALING";
  case CONN_STATE_WEBRTC_STUN_CONNECTED:
    return "WEBRTC_STUN_CONNECTED";
  case CONN_STATE_WEBRTC_STUN_FAILED:
    return "WEBRTC_STUN_FAILED";

  case CONN_STATE_ATTEMPTING_WEBRTC_TURN:
    return "ATTEMPTING_WEBRTC_TURN";
  case CONN_STATE_WEBRTC_TURN_SIGNALING:
    return "WEBRTC_TURN_SIGNALING";
  case CONN_STATE_WEBRTC_TURN_CONNECTED:
    return "WEBRTC_TURN_CONNECTED";
  case CONN_STATE_WEBRTC_TURN_FAILED:
    return "WEBRTC_TURN_FAILED";

  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Get current stage number (1, 2, or 3) from state
 */
uint32_t connection_get_stage(connection_state_t state) {
  if (state >= CONN_STATE_ATTEMPTING_DIRECT_TCP && state <= CONN_STATE_DIRECT_TCP_FAILED) {
    return 1;
  }
  if (state >= CONN_STATE_ATTEMPTING_WEBRTC_STUN && state <= CONN_STATE_WEBRTC_STUN_FAILED) {
    return 2;
  }
  if (state >= CONN_STATE_ATTEMPTING_WEBRTC_TURN && state <= CONN_STATE_WEBRTC_TURN_FAILED) {
    return 3;
  }
  return 0; // Idle or terminal state
}

/* ============================================================================
 * Context Management
 * ============================================================================ */

/**
 * @brief Initialize connection attempt context
 */
asciichat_error_t connection_context_init(connection_attempt_context_t *ctx, bool prefer_webrtc, bool no_webrtc,
                                          bool webrtc_skip_stun, bool webrtc_disable_turn) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Context pointer is NULL");
  }

  // Reset context
  memset(ctx, 0, sizeof(connection_attempt_context_t));

  // Initialize state
  ctx->current_state = CONN_STATE_IDLE;
  ctx->previous_state = CONN_STATE_IDLE;

  // Set CLI preferences
  ctx->prefer_webrtc = prefer_webrtc;
  ctx->no_webrtc = no_webrtc;
  ctx->webrtc_skip_stun = webrtc_skip_stun;
  ctx->webrtc_disable_turn = webrtc_disable_turn;

  // Initialize timeout
  ctx->stage_start_time = time(NULL);
  ctx->current_stage_timeout_seconds = CONN_TIMEOUT_DIRECT_TCP;

  // Initialize counters
  ctx->reconnect_attempt = 1;
  ctx->stage_failures = 0;
  ctx->total_transitions = 0;

  log_debug(
      "Connection context initialized (prefer_webrtc=%d, no_webrtc=%d, webrtc_skip_stun=%d, webrtc_disable_turn=%d)",
      prefer_webrtc, no_webrtc, webrtc_skip_stun, webrtc_disable_turn);

  return ASCIICHAT_OK;
}

/**
 * @brief Cleanup connection attempt context
 */
void connection_context_cleanup(connection_attempt_context_t *ctx) {
  if (!ctx)
    return;

  // Destroy TCP client instance if created
  if (ctx->tcp_client_instance) {
    tcp_client_destroy(&ctx->tcp_client_instance);
    ctx->tcp_client_instance = NULL;
    log_debug("TCP client instance destroyed");
  }

  // Close TCP transport if still open
  if (ctx->tcp_transport) {
    acip_transport_close(ctx->tcp_transport);
    acip_transport_destroy(ctx->tcp_transport);
    ctx->tcp_transport = NULL;
  }

  // Close WebRTC transport if still open
  if (ctx->webrtc_transport) {
    acip_transport_close(ctx->webrtc_transport);
    acip_transport_destroy(ctx->webrtc_transport);
    ctx->webrtc_transport = NULL;
  }

  // Cleanup peer manager
  if (ctx->peer_manager) {
    webrtc_peer_manager_destroy(ctx->peer_manager);
    ctx->peer_manager = NULL;
    g_peer_manager = NULL; // Clear global reference
  }

  ctx->active_transport = NULL;
  log_debug("Connection context cleaned up");
}

/**
 * @brief Transition to next connection state with validation
 */
asciichat_error_t connection_state_transition(connection_attempt_context_t *ctx, connection_state_t new_state) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Context pointer is NULL");
  }

  // Store previous state
  ctx->previous_state = ctx->current_state;
  ctx->current_state = new_state;
  ctx->total_transitions++;

  // Update timeout based on new stage
  uint32_t new_stage = connection_get_stage(new_state);
  uint32_t old_stage = connection_get_stage(ctx->previous_state);

  if (new_stage != old_stage && new_stage > 0) {
    ctx->stage_start_time = time(NULL);
    switch (new_stage) {
    case 1:
      ctx->current_stage_timeout_seconds = CONN_TIMEOUT_DIRECT_TCP;
      break;
    case 2:
      ctx->current_stage_timeout_seconds = CONN_TIMEOUT_WEBRTC_STUN;
      break;
    case 3:
      ctx->current_stage_timeout_seconds = CONN_TIMEOUT_WEBRTC_TURN;
      break;
    default:
      break;
    }
  }

  log_debug("State transition: %s → %s (stage %u → %u)", connection_state_name(ctx->previous_state),
            connection_state_name(new_state), old_stage, new_stage);

  return ASCIICHAT_OK;
}

/**
 * @brief Check if current stage has exceeded timeout
 */
bool connection_check_timeout(const connection_attempt_context_t *ctx) {
  if (!ctx)
    return false;

  time_t elapsed = time(NULL) - ctx->stage_start_time;
  bool timeout_exceeded = elapsed > (time_t)ctx->current_stage_timeout_seconds;

  if (timeout_exceeded) {
    log_warn("Stage timeout exceeded: stage %u, elapsed %ld seconds > %u seconds limit",
             connection_get_stage(ctx->current_state), elapsed, ctx->current_stage_timeout_seconds);
  }

  return timeout_exceeded;
}

/* ============================================================================
 * Stage 1: Direct TCP Connection
 * ============================================================================ */

/**
 * @brief Attempt direct TCP connection (Stage 1, 3s timeout)
 *
 * Fast path for servers that are directly accessible (same network, no NAT).
 * Uses existing TCP connection logic from server.c.
 */
static asciichat_error_t attempt_direct_tcp(connection_attempt_context_t *ctx, const char *server_address,
                                            uint16_t server_port) {
  if (!ctx || !server_address) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Check if shutdown was requested before attempting TCP connection
  if (should_exit()) {
    return SET_ERRNO(ERROR_NETWORK, "TCP connection attempt aborted due to shutdown request");
  }

  log_info("Stage 1/3: Attempting direct TCP connection to %s:%u (3s timeout)", server_address, server_port);

  // Transition to attempting state
  asciichat_error_t result = connection_state_transition(ctx, CONN_STATE_ATTEMPTING_DIRECT_TCP);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Create TCP client
  tcp_client_t *tcp_client = tcp_client_create();
  if (!tcp_client) {
    log_error("Failed to create TCP client");
    connection_state_transition(ctx, CONN_STATE_DIRECT_TCP_FAILED);
    ctx->stage_failures++;
    return SET_ERRNO(ERROR_NETWORK, "TCP client creation failed");
  }

  // Set stage timeout for this attempt
  ctx->stage_start_time = time(NULL);
  ctx->current_stage_timeout_seconds = CONN_TIMEOUT_DIRECT_TCP;

  // Attempt TCP connection (reconnect_attempt is 0-based, convert for tcp_client_connect)
  int tcp_result = tcp_client_connect(tcp_client, server_address, server_port, (int)ctx->reconnect_attempt,
                                      ctx->reconnect_attempt == 0, ctx->reconnect_attempt > 0);

  if (tcp_result != 0) {
    log_debug("Direct TCP connection failed (tcp_client_connect returned %d)", tcp_result);
    tcp_client_destroy(&tcp_client);
    connection_state_transition(ctx, CONN_STATE_DIRECT_TCP_FAILED);
    ctx->stage_failures++;
    return SET_ERRNO(ERROR_NETWORK, "TCP connection failed after %u attempts", ctx->reconnect_attempt);
  }

  // Extract socket from TCP client for crypto handshake
  socket_t sockfd = tcp_client_get_socket(tcp_client);
  if (sockfd == INVALID_SOCKET_VALUE) {
    log_error("Failed to get socket from TCP client");
    tcp_client_destroy(&tcp_client);
    return SET_ERRNO(ERROR_NETWORK, "Invalid socket after TCP connection");
  }

  // Extract and set server IP for crypto context initialization
  // TCP client already resolved and connected to the server IP, stored in tcp_client->server_ip
  if (tcp_client->server_ip[0] != '\0') {
    server_connection_set_ip(tcp_client->server_ip);
    log_debug("Server IP extracted from TCP client: %s", tcp_client->server_ip);
  } else {
    log_warn("TCP client did not populate server_ip field");
  }

  // Initialize crypto context if encryption is enabled
  // This must happen AFTER setting server IP, as crypto init reads server IP/port
  if (!GET_OPTION(no_encrypt)) {
    log_debug("Initializing crypto context...");
    if (client_crypto_init() != 0) {
      log_error("Failed to initialize crypto context");
      tcp_client_destroy(&tcp_client);
      return SET_ERRNO(ERROR_CRYPTO, "Crypto initialization failed");
    }
    log_debug("Crypto context initialized successfully");

    // Perform crypto handshake with server
    log_debug("Performing crypto handshake with server...");
    if (client_crypto_handshake(sockfd) != 0) {
      log_error("Crypto handshake failed");
      tcp_client_destroy(&tcp_client);
      return SET_ERRNO(ERROR_NETWORK, "Crypto handshake failed");
    }
    log_debug("Crypto handshake completed successfully");
  }

  // Get crypto context after handshake
  const crypto_context_t *crypto_ctx = crypto_client_is_ready() ? crypto_client_get_context() : NULL;

  // Create ACIP transport for protocol-agnostic packet sending/receiving
  ctx->tcp_transport = acip_tcp_transport_create(sockfd, (crypto_context_t *)crypto_ctx);
  if (!ctx->tcp_transport) {
    log_error("Failed to create ACIP transport for Direct TCP");
    tcp_client_destroy(&tcp_client);
    return SET_ERRNO(ERROR_NETWORK, "Failed to create ACIP transport");
  }

  log_info("Direct TCP connection established to %s:%u", server_address, server_port);
  connection_state_transition(ctx, CONN_STATE_DIRECT_TCP_CONNECTED);
  ctx->active_transport = ctx->tcp_transport;

  // Store tcp_client in context for proper lifecycle management
  // It will be destroyed in connection_context_cleanup()
  ctx->tcp_client_instance = tcp_client;
  log_debug("TCP client instance stored in connection context");

  return ASCIICHAT_OK;
}

/* ============================================================================
 * WebRTC Transport Ready Callback
 * ============================================================================ */

/**
 * @brief Callback when WebRTC DataChannel is ready
 *
 * Called by peer_manager when the WebRTC connection succeeds and
 * the DataChannel is ready for use. Stores the transport and signals
 * the waiting thread via condition variable.
 *
 * @param transport WebRTC ACIP transport (ownership transferred)
 * @param participant_id Remote participant UUID
 * @param user_data Connection context pointer
 */
static void on_webrtc_transport_ready(acip_transport_t *transport, const uint8_t participant_id[16], void *user_data) {
  connection_attempt_context_t *ctx = (connection_attempt_context_t *)user_data;

  if (!ctx || !transport) {
    log_error("on_webrtc_transport_ready: Invalid parameters");
    return;
  }

  log_info("WebRTC transport ready (participant_id=%.*s)", 16, (const char *)participant_id);

  // Store transport and signal waiting thread
  mutex_lock(&ctx->webrtc_mutex);
  ctx->webrtc_transport = transport;
  ctx->webrtc_transport_received = true;
  cond_signal(&ctx->webrtc_ready_cond);
  mutex_unlock(&ctx->webrtc_mutex);
}

/* ============================================================================
 * Stage 2: WebRTC + STUN Connection
 * ============================================================================ */

/**
 * @brief Attempt WebRTC + STUN connection (Stage 2, 8s timeout)
 *
 * NAT traversal using STUN hole punching. Requires:
 * 1. Join ACDS session to get session_id and peer credentials
 * 2. Create WebRTC peer connection with STUN servers
 * 3. Exchange SDP/ICE candidates via ACDS signaling relay
 * 4. Wait for data channel connection within 8s
 */
static asciichat_error_t attempt_webrtc_stun(connection_attempt_context_t *ctx, const char *server_address,
                                             const char *acds_server, uint16_t acds_port) {
  if (!ctx || !server_address || !acds_server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Skip if STUN explicitly disabled or we're disabling WebRTC only
  if (ctx->webrtc_skip_stun || ctx->no_webrtc) {
    log_debug("Skipping WebRTC+STUN (webrtc_skip_stun=%d, no_webrtc=%d)", ctx->webrtc_skip_stun, ctx->no_webrtc);
    return SET_ERRNO(ERROR_NETWORK, "STUN stage disabled per CLI flags");
  }

  log_info("Stage 2/3: Attempting WebRTC + STUN connection via %s:%u (8s timeout)", acds_server, acds_port);

  // Transition to attempting state
  asciichat_error_t result = connection_state_transition(ctx, CONN_STATE_ATTEMPTING_WEBRTC_STUN);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Set stage timeout
  ctx->stage_start_time = time(NULL);
  ctx->current_stage_timeout_seconds = CONN_TIMEOUT_WEBRTC_STUN;

  // ─────────────────────────────────────────────────────────────
  // Step 1: Connect to ACDS server
  // ─────────────────────────────────────────────────────────────

  acds_client_t acds_client = {0};
  acds_client_config_t acds_config = {0};
  SAFE_STRNCPY(acds_config.server_address, acds_server, sizeof(acds_config.server_address));
  acds_config.server_port = acds_port;
  acds_config.timeout_ms = 5000;                                   // 5s timeout for ACDS connection
  acds_config.should_exit_callback = should_exit_callback_wrapper; // Check for graceful shutdown during connection
  acds_config.callback_data = NULL;

  // ACDS key verification (optional in debug builds, only if --discovery-service-key is provided)
  if (strlen(GET_OPTION(discovery_service_key)) > 0) {
    log_info("Verifying ACDS server key for %s...", acds_config.server_address);
    uint8_t acds_pubkey[32];
    asciichat_error_t verify_result =
        discovery_keys_verify(acds_config.server_address, GET_OPTION(discovery_service_key), acds_pubkey);
    if (verify_result != ASCIICHAT_OK) {
      log_error("ACDS key verification failed for %s", acds_config.server_address);
      return SET_ERRNO(ERROR_CRYPTO_VERIFICATION, "ACDS key verification failed");
    }
    log_info("ACDS server key verified successfully");
  }
#ifndef NDEBUG
  else {
    log_debug("Skipping ACDS key verification (debug build, no --discovery-service-key provided)");
  }
#endif

  result = acds_client_connect(&acds_client, &acds_config);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to connect to ACDS server %s:%u: %d", acds_server, acds_port, result);
    return SET_ERRNO(ERROR_NETWORK, "ACDS connection failed");
  }

  // ─────────────────────────────────────────────────────────────
  // Step 2: Join ACDS session (use session context from discovery)
  // ─────────────────────────────────────────────────────────────

  // Check if we have session information from prior ACDS discovery
  if (ctx->session_ctx.session_string[0] == '\0') {
    log_warn("No session context available - ACDS discovery may have failed or not been performed");
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "Session context not available (discovery required before WebRTC)");
  }

  acds_session_join_params_t join_params = {0};
  join_params.session_string = ctx->session_ctx.session_string;

  // Add password if configured (required for password-protected sessions)
  const options_t *opts = options_get();
  if (opts && opts->password[0] != '\0') {
    join_params.has_password = true;
    SAFE_STRNCPY(join_params.password, opts->password, sizeof(join_params.password));
  }

  acds_session_join_result_t join_result = {0};
  result = acds_session_join(&acds_client, &join_params, &join_result);
  if (result != ASCIICHAT_OK || !join_result.success) {
    log_warn("Failed to join ACDS session: %d (%s)", join_result.error_code,
             join_result.error_message[0] ? join_result.error_message : "unknown error");
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "ACDS session join failed");
  }

  // Store session context for WebRTC signaling
  memcpy(ctx->session_ctx.session_id, join_result.session_id, sizeof(join_result.session_id));
  memcpy(ctx->session_ctx.participant_id, join_result.participant_id, sizeof(join_result.participant_id));
  ctx->session_ctx.server_port = join_result.server_port;
  SAFE_STRNCPY(ctx->session_ctx.server_address, join_result.server_address, sizeof(ctx->session_ctx.server_address));

  log_debug("Joined ACDS session: session_id=%02x%02x%02x%02x..., participant_id=%02x%02x%02x%02x...",
            join_result.session_id[0], join_result.session_id[1], join_result.session_id[2], join_result.session_id[3],
            join_result.participant_id[0], join_result.participant_id[1], join_result.participant_id[2],
            join_result.participant_id[3]);

  // ─────────────────────────────────────────────────────────────
  // Step 3: Create WebRTC peer manager with STUN servers
  // ─────────────────────────────────────────────────────────────

  // Configure STUN servers from options (or defaults if not set)
  stun_server_t stun_servers[4] = {0}; // Support up to 4 STUN servers
  int stun_count = stun_servers_parse(GET_OPTION(stun_servers), OPT_ENDPOINT_STUN_SERVERS_DEFAULT, stun_servers, 4);
  if (stun_count <= 0) {
    log_warn("Failed to parse STUN servers, using defaults");
    stun_count =
        stun_servers_parse(OPT_ENDPOINT_STUN_SERVERS_DEFAULT, OPT_ENDPOINT_STUN_SERVERS_DEFAULT, stun_servers, 4);
  }

  // Initialize synchronization primitives for transport_ready callback
  ctx->webrtc_transport_received = false;

  webrtc_peer_manager_config_t pm_config = {
      .role = WEBRTC_ROLE_JOINER, // Client joins, server creates
      .stun_servers = stun_servers,
      .stun_count = stun_count,
      .turn_servers = NULL,
      .turn_count = 0,
      .on_transport_ready = on_webrtc_transport_ready, // Callback when DataChannel ready
      .user_data = (void *)ctx,
      .crypto_ctx = NULL, // TODO: Get crypto context from client
  };

  // Get signaling callbacks (returns struct, not pointer)
  webrtc_signaling_callbacks_t signaling_callbacks = webrtc_get_signaling_callbacks();

  // Create ACIP transport wrapper for ACDS signaling
  // This transport is used to send SDP/ICE messages via ACDS relay
  ctx->acds_transport = acip_tcp_transport_create(acds_client.socket, NULL);
  if (!ctx->acds_transport) {
    log_error("Failed to create ACDS transport wrapper");
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "Failed to create ACDS transport");
  }

  // Set ACDS transport for signaling (SDP/ICE will be sent via this)
  webrtc_set_acds_transport(ctx->acds_transport);

  // Set session context (session_id, participant_id) for signaling
  webrtc_set_session_context(ctx->session_ctx.session_id, ctx->session_ctx.participant_id);

  result = webrtc_peer_manager_create(&pm_config, &signaling_callbacks, &ctx->peer_manager);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to create WebRTC peer manager: %d", result);
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "WebRTC peer manager creation failed");
  }

  // Set global peer manager for ACIP handlers to receive incoming SDP/ICE
  g_peer_manager = ctx->peer_manager;

  // ─────────────────────────────────────────────────────────────
  // Step 4: Initiate WebRTC connection (send SDP offer)
  // ─────────────────────────────────────────────────────────────

  // Broadcast SDP offer to all session participants (recipient_id = all zeros)
  // The server will receive this and respond with its own SDP answer
  uint8_t broadcast_recipient[16] = {0};
  result = webrtc_peer_manager_connect(ctx->peer_manager, ctx->session_ctx.session_id, broadcast_recipient);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to initiate WebRTC connection: %d", result);
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "WebRTC connection initiation failed");
  }

  // ─────────────────────────────────────────────────────────────
  // Step 5-7: Exchange SDP/ICE and wait for connection
  // ─────────────────────────────────────────────────────────────

  connection_state_transition(ctx, CONN_STATE_WEBRTC_STUN_SIGNALING);

  // Get client callbacks for receiving SDP/ICE responses from server
  const acip_client_callbacks_t *callbacks = protocol_get_acip_callbacks();
  if (!callbacks) {
    log_error("Failed to get ACIP client callbacks for WebRTC signaling");
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_INVALID_STATE, "Missing ACIP callbacks");
  }

  // Receive-and-wait loop: Process incoming ACDS signaling packets while waiting for DataChannel
  // The peer_manager automatically sends SDP offer via send_sdp callback after creation.
  // We need to receive the SDP answer and ICE candidates from the server via ACDS.
  time_t wait_start = time(NULL);
  time_t timeout_seconds = CONN_TIMEOUT_WEBRTC_STUN;
  bool connection_successful = false;

  while ((time(NULL) - wait_start) < timeout_seconds) {
    // Check if transport is ready (set by on_transport_ready callback)
    mutex_lock(&ctx->webrtc_mutex);
    if (ctx->webrtc_transport_received) {
      connection_successful = true;
      mutex_unlock(&ctx->webrtc_mutex);
      break;
    }
    mutex_unlock(&ctx->webrtc_mutex);

    // Receive one ACDS packet (SDP answer, ICE candidate, etc.)
    // This dispatches to on_webrtc_sdp/on_webrtc_ice in client/protocol.c
    // which forward to the peer_manager for processing
    asciichat_error_t recv_result = acip_client_receive_and_dispatch(ctx->acds_transport, callbacks);

    if (recv_result != ASCIICHAT_OK) {
      // Check if WebRTC succeeded before treating ACDS errors as fatal
      mutex_lock(&ctx->webrtc_mutex);
      bool transport_ready = ctx->webrtc_transport_received;
      mutex_unlock(&ctx->webrtc_mutex);

      if (transport_ready) {
        // WebRTC connection succeeded, ACDS errors are no longer relevant
        log_debug("ACDS receive error after WebRTC success - signaling complete");
        connection_successful = true;
        break;
      }

      if (recv_result == ERROR_NETWORK) {
        log_warn("ACDS connection closed during WebRTC signaling");
        break;
      } else if (recv_result == ERROR_CRYPTO) {
        log_error("ACDS crypto error during WebRTC signaling");
        break;
      }
      // Other errors are non-fatal, continue waiting
      log_debug("ACDS receive error (non-fatal): %s", asciichat_error_string(recv_result));
    }
  }

  if (!connection_successful) {
    log_warn("WebRTC+STUN connection timed out after %ld seconds", timeout_seconds);
    connection_state_transition(ctx, CONN_STATE_WEBRTC_STUN_FAILED);
    ctx->stage_failures++;
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "WebRTC+STUN connection timeout");
  }

  log_info("WebRTC+STUN connection established");
  log_info("WebRTC connection established"); // For test script detection
  connection_state_transition(ctx, CONN_STATE_WEBRTC_STUN_CONNECTED);
  ctx->active_transport = ctx->webrtc_transport;

  // Clean up ACDS client (signaling relay is separate from data transport)
  acds_client_disconnect(&acds_client);

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Stage 3: WebRTC + TURN Connection
 * ============================================================================ */

/**
 * @brief Attempt WebRTC + TURN connection (Stage 3, 15s timeout)
 *
 * Relay-based connection for restrictive networks. Requires:
 * 1. Join ACDS session (gets TURN credentials from server)
 * 2. Create WebRTC peer connection with TURN relay
 * 3. Exchange SDP/ICE candidates via ACDS signaling relay
 * 4. Wait for data channel connection within 15s
 *
 * This is the final fallback - guaranteed to work if TURN server is reachable.
 */
static asciichat_error_t attempt_webrtc_turn(connection_attempt_context_t *ctx, const char *server_address,
                                             const char *acds_server, uint16_t acds_port) {
  if (!ctx || !server_address || !acds_server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Skip if TURN explicitly disabled
  if (ctx->webrtc_disable_turn) {
    log_debug("Skipping WebRTC+TURN (webrtc_disable_turn=true)");
    return SET_ERRNO(ERROR_NETWORK, "TURN stage disabled per CLI flags");
  }

  log_info("Stage 3/3: Attempting WebRTC + TURN connection via %s:%u (15s timeout)", acds_server, acds_port);

  // Transition to attempting state
  asciichat_error_t result = connection_state_transition(ctx, CONN_STATE_ATTEMPTING_WEBRTC_TURN);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Set stage timeout
  ctx->stage_start_time = time(NULL);
  ctx->current_stage_timeout_seconds = CONN_TIMEOUT_WEBRTC_TURN;

  // ─────────────────────────────────────────────────────────────
  // Step 1: Connect to ACDS server
  // ─────────────────────────────────────────────────────────────

  acds_client_t acds_client = {0};
  acds_client_config_t acds_config = {0};
  SAFE_STRNCPY(acds_config.server_address, acds_server, sizeof(acds_config.server_address));
  acds_config.server_port = acds_port;
  acds_config.timeout_ms = 5000;                                   // 5s timeout for ACDS connection
  acds_config.should_exit_callback = should_exit_callback_wrapper; // Check for graceful shutdown during connection
  acds_config.callback_data = NULL;

  // ACDS key verification (optional in debug builds, only if --discovery-service-key is provided)
  if (strlen(GET_OPTION(discovery_service_key)) > 0) {
    log_info("Verifying ACDS server key for %s...", acds_config.server_address);
    uint8_t acds_pubkey[32];
    asciichat_error_t verify_result =
        discovery_keys_verify(acds_config.server_address, GET_OPTION(discovery_service_key), acds_pubkey);
    if (verify_result != ASCIICHAT_OK) {
      log_error("ACDS key verification failed for %s", acds_config.server_address);
      return SET_ERRNO(ERROR_CRYPTO_VERIFICATION, "ACDS key verification failed");
    }
    log_info("ACDS server key verified successfully");
  }
#ifndef NDEBUG
  else {
    log_debug("Skipping ACDS key verification (debug build, no --discovery-service-key provided)");
  }
#endif

  result = acds_client_connect(&acds_client, &acds_config);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to connect to ACDS server %s:%u for TURN: %d", acds_server, acds_port, result);
    return SET_ERRNO(ERROR_NETWORK, "ACDS connection failed for TURN");
  }

  // ─────────────────────────────────────────────────────────────
  // Step 2: Join ACDS session to get TURN credentials
  // ─────────────────────────────────────────────────────────────

  // Check if we have session information from prior ACDS discovery
  if (ctx->session_ctx.session_string[0] == '\0') {
    log_warn("No session context available for TURN stage (ACDS discovery required before WebRTC)");
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "Session context not available");
  }

  // Re-join same session to get TURN credentials from server
  // In production, ACDS would return TURN credentials on first join;
  // we're re-joining here to ensure we have them
  acds_session_join_params_t join_params = {0};
  join_params.session_string = ctx->session_ctx.session_string;

  // Add password if configured (required for password-protected sessions)
  const options_t *opts_turn = options_get();
  if (opts_turn && opts_turn->password[0] != '\0') {
    join_params.has_password = true;
    SAFE_STRNCPY(join_params.password, opts_turn->password, sizeof(join_params.password));
  }

  acds_session_join_result_t join_result = {0};
  result = acds_session_join(&acds_client, &join_params, &join_result);
  if (result != ASCIICHAT_OK || !join_result.success) {
    log_warn("Failed to re-join ACDS session for TURN: %d (%s)", join_result.error_code,
             join_result.error_message[0] ? join_result.error_message : "unknown error");
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "ACDS session re-join failed for TURN");
  }

  // Store TURN server credentials from ACDS response
  // Note: ACDS response should include TURN server, username, and password
  // For now we use ascii-chat's TURN server - in production this comes from server
  ctx->stun_turn_cfg.turn_port = OPT_TURN_SERVER_PORT; // Standard TURN port
  SAFE_STRNCPY(ctx->stun_turn_cfg.turn_server, OPT_TURN_SERVER_HOST, sizeof(ctx->stun_turn_cfg.turn_server));
  SAFE_STRNCPY(ctx->stun_turn_cfg.turn_username, "client", sizeof(ctx->stun_turn_cfg.turn_username));
  SAFE_STRNCPY(ctx->stun_turn_cfg.turn_password, "ephemeral-credential", sizeof(ctx->stun_turn_cfg.turn_password));

  log_debug("Retrieved TURN credentials: server=%s:%u, username=%s", ctx->stun_turn_cfg.turn_server,
            ctx->stun_turn_cfg.turn_port, ctx->stun_turn_cfg.turn_username);

  // ─────────────────────────────────────────────────────────────
  // Step 3: Create WebRTC peer manager with TURN relay
  // ─────────────────────────────────────────────────────────────

  // Configure STUN + TURN servers from options (or defaults if not set)
  stun_server_t stun_servers_turn[4] = {0}; // Support up to 4 STUN servers
  int stun_count_turn =
      stun_servers_parse(GET_OPTION(stun_servers), OPT_ENDPOINT_STUN_SERVERS_DEFAULT, stun_servers_turn, 4);
  if (stun_count_turn <= 0) {
    log_warn("Failed to parse STUN servers for TURN stage, using defaults");
    stun_count_turn =
        stun_servers_parse(OPT_ENDPOINT_STUN_SERVERS_DEFAULT, OPT_ENDPOINT_STUN_SERVERS_DEFAULT, stun_servers_turn, 4);
  }

  // Build TURN URL from configuration
  char turn_url[128] = {0};
  safe_snprintf(turn_url, sizeof(turn_url), "turn:%s:%u", ctx->stun_turn_cfg.turn_server, ctx->stun_turn_cfg.turn_port);

  turn_server_t turn_server = {0};
  turn_server.url_len = strlen(turn_url);
  SAFE_STRNCPY(turn_server.url, turn_url, sizeof(turn_server.url));
  turn_server.username_len = strlen(ctx->stun_turn_cfg.turn_username);
  SAFE_STRNCPY(turn_server.username, ctx->stun_turn_cfg.turn_username, sizeof(turn_server.username));
  turn_server.credential_len = strlen(ctx->stun_turn_cfg.turn_password);
  SAFE_STRNCPY(turn_server.credential, ctx->stun_turn_cfg.turn_password, sizeof(turn_server.credential));

  turn_server_t turn_servers[] = {turn_server};

  // Initialize synchronization primitives for transport_ready callback
  ctx->webrtc_transport_received = false;

  webrtc_peer_manager_config_t pm_config = {
      .role = WEBRTC_ROLE_JOINER,        // Client joins, server creates
      .stun_servers = stun_servers_turn, // Also try STUN during TURN stage
      .stun_count = stun_count_turn,
      .turn_servers = turn_servers,
      .turn_count = 1,
      .on_transport_ready = on_webrtc_transport_ready, // Callback when DataChannel ready
      .user_data = (void *)ctx,
      .crypto_ctx = NULL, // TODO: Get crypto context from client
  };

  // Get signaling callbacks (returns struct, not pointer)
  webrtc_signaling_callbacks_t signaling_callbacks_turn = webrtc_get_signaling_callbacks();

  // Create ACIP transport wrapper for ACDS signaling
  // This transport is used to send SDP/ICE messages via ACDS relay
  ctx->acds_transport = acip_tcp_transport_create(acds_client.socket, NULL);
  if (!ctx->acds_transport) {
    log_error("Failed to create ACDS transport wrapper for TURN");
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "Failed to create ACDS transport");
  }

  // Set ACDS transport for signaling (SDP/ICE will be sent via this)
  webrtc_set_acds_transport(ctx->acds_transport);

  // Set session context (session_id, participant_id) for signaling
  webrtc_set_session_context(ctx->session_ctx.session_id, ctx->session_ctx.participant_id);

  result = webrtc_peer_manager_create(&pm_config, &signaling_callbacks_turn, &ctx->peer_manager);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to create WebRTC peer manager for TURN: %d", result);
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "WebRTC peer manager creation failed");
  }

  // Set global peer manager for ACIP handlers to receive incoming SDP/ICE
  g_peer_manager = ctx->peer_manager;

  // Initiate WebRTC connection with TURN
  // Use broadcast recipient (all zeros) to connect to all session participants
  uint8_t broadcast_recipient_turn[16] = {0};
  result = webrtc_peer_manager_connect(ctx->peer_manager, ctx->session_ctx.session_id, broadcast_recipient_turn);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to initiate WebRTC+TURN connection: %d", result);
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK, "WebRTC+TURN connection initiation failed");
  }

  // ─────────────────────────────────────────────────────────────
  // Step 4-7: Exchange SDP/ICE and wait for connection
  // ─────────────────────────────────────────────────────────────

  connection_state_transition(ctx, CONN_STATE_WEBRTC_TURN_SIGNALING);

  // Get client callbacks for receiving SDP/ICE responses from server
  const acip_client_callbacks_t *callbacks = protocol_get_acip_callbacks();
  if (!callbacks) {
    log_error("Failed to get ACIP client callbacks for TURN signaling");
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_INVALID_STATE, "Missing ACIP callbacks");
  }

  // Receive-and-wait loop: Process incoming ACDS signaling packets while waiting for DataChannel
  // (Must actively receive SDP answer + ICE candidates from server or connection will never establish)
  time_t wait_start = time(NULL);
  time_t timeout_seconds = CONN_TIMEOUT_WEBRTC_TURN;
  bool connection_successful = false;

  while ((time(NULL) - wait_start) < timeout_seconds) {
    // Check if transport is ready (set by on_transport_ready callback)
    mutex_lock(&ctx->webrtc_mutex);
    if (ctx->webrtc_transport_received) {
      connection_successful = true;
      mutex_unlock(&ctx->webrtc_mutex);
      break;
    }
    mutex_unlock(&ctx->webrtc_mutex);

    // Receive one ACDS packet (SDP answer, ICE candidate, etc.)
    // This dispatches to on_webrtc_sdp/on_webrtc_ice in client/protocol.c
    asciichat_error_t recv_result = acip_client_receive_and_dispatch(ctx->acds_transport, callbacks);

    if (recv_result != ASCIICHAT_OK) {
      if (recv_result == ERROR_NETWORK) {
        log_warn("ACDS connection closed during WebRTC TURN signaling");
        break;
      } else if (recv_result == ERROR_CRYPTO) {
        log_error("ACDS crypto error during WebRTC TURN signaling");
        break;
      }
      // Non-fatal errors (e.g., timeout on receive): log and continue
      log_debug("ACDS receive error (non-fatal): %s", asciichat_error_string(recv_result));
    }

    // Yield CPU to avoid busy-wait (10ms sleep)
    platform_sleep_ms(10);
  }

  if (!connection_successful) {
    log_warn("WebRTC+TURN connection timed out after %ld seconds", timeout_seconds);
    connection_state_transition(ctx, CONN_STATE_WEBRTC_TURN_FAILED);
    ctx->stage_failures++;
    acds_client_disconnect(&acds_client);
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "WebRTC+TURN connection timeout");
  }

  log_info("WebRTC+TURN connection established");
  connection_state_transition(ctx, CONN_STATE_WEBRTC_TURN_CONNECTED);
  ctx->active_transport = ctx->webrtc_transport;

  // Clean up ACDS client (signaling relay is separate from data transport)
  acds_client_disconnect(&acds_client);

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Main Orchestrator
 * ============================================================================ */

/**
 * @brief Orchestrate connection attempt with automatic fallback
 *
 * Implements 3-stage fallback sequence:
 * 1. **Direct TCP** (3s) - Fastest for accessible servers
 * 2. **WebRTC + STUN** (8s) - NAT traversal
 * 3. **WebRTC + TURN** (15s) - Last resort relay
 *
 * Each stage is attempted until success or timeout. On timeout, falls back to next stage.
 * Returns ASCIICHAT_OK and sets ctx->active_transport when connection succeeds.
 * Returns error code when all stages fail.
 *
 * Called from src/client/main.c in the connection loop.
 * Replaces direct TCP connection attempt with automatic fallback.
 *
 * @param ctx Connection context (initialized by caller)
 * @param server_address Server IP/hostname
 * @param server_port Server port
 * @param acds_server ACDS discovery server address
 * @param acds_port ACDS discovery server port
 * @return ASCIICHAT_OK on successful connection, error code otherwise
 */
asciichat_error_t connection_attempt_with_fallback(connection_attempt_context_t *ctx, const char *server_address,
                                                   uint16_t server_port, const char *acds_server, uint16_t acds_port) {
  if (!ctx || !server_address || !acds_server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Check if shutdown was requested before starting connection attempt
  // Note: If SIGTERM arrives during a blocking TCP connect(), it won't interrupt
  // the syscall directly. The connect will continue until it times out (~3s) or
  // succeeds, then this check will catch the exit flag. This is expected behavior
  // for signal handling with blocking I/O.
  if (should_exit()) {
    return SET_ERRNO(ERROR_NETWORK, "Connection attempt aborted due to shutdown request");
  }

  log_info("=== Connection attempt %u: %s:%u (fallback strategy: TCP → STUN → TURN) ===", ctx->reconnect_attempt,
           server_address, server_port);

  asciichat_error_t result = ASCIICHAT_OK;

  // ─────────────────────────────────────────────────────────────
  // Stage 1: Direct TCP (3s timeout)
  // ─────────────────────────────────────────────────────────────

  if (!ctx->prefer_webrtc && !ctx->no_webrtc) {
    // Normal path: try TCP first unless WebRTC preferred
    result = attempt_direct_tcp(ctx, server_address, server_port);
    if (result == ASCIICHAT_OK) {
      log_info("Connection succeeded via Direct TCP");
      connection_state_transition(ctx, CONN_STATE_CONNECTED);
      return ASCIICHAT_OK;
    }

    // Check if timeout (fall back to next stage)
    if (connection_check_timeout(ctx)) {
      log_info("Stage 1 timeout, proceeding to Stage 2 (WebRTC+STUN)");
    } else {
      // Actual failure (not just timeout) - could be local error
      log_warn("Stage 1 failed immediately, proceeding to Stage 2");
    }
  } else if (ctx->no_webrtc) {
    // TCP-only mode - don't try WebRTC at all
    result = attempt_direct_tcp(ctx, server_address, server_port);
    if (result == ASCIICHAT_OK) {
      log_info("Connection succeeded via Direct TCP (--no-webrtc)");
      connection_state_transition(ctx, CONN_STATE_CONNECTED);
      return ASCIICHAT_OK;
    }
    log_error("Direct TCP failed with --no-webrtc flag");
    connection_state_transition(ctx, CONN_STATE_FAILED);
    return result;
  }

  // ─────────────────────────────────────────────────────────────
  // Stage 2: WebRTC + STUN (8s timeout)
  // ─────────────────────────────────────────────────────────────

  // Check if shutdown was requested before proceeding to next stage
  if (should_exit()) {
    connection_state_transition(ctx, CONN_STATE_FAILED);
    return SET_ERRNO(ERROR_NETWORK, "Connection attempt aborted due to shutdown request");
  }

  result = attempt_webrtc_stun(ctx, server_address, acds_server, acds_port);
  if (result == ASCIICHAT_OK) {
    log_info("Connection succeeded via WebRTC+STUN");
    connection_state_transition(ctx, CONN_STATE_CONNECTED);
    return ASCIICHAT_OK;
  }

  if (result == ERROR_NETWORK) {
    log_debug("WebRTC+STUN stage skipped per CLI flags");
  } else if (connection_check_timeout(ctx)) {
    log_info("Stage 2 timeout, proceeding to Stage 3 (WebRTC+TURN)");
  } else {
    log_warn("Stage 2 failed immediately, proceeding to Stage 3");
  }

  // ─────────────────────────────────────────────────────────────
  // Stage 3: WebRTC + TURN (15s timeout)
  // ─────────────────────────────────────────────────────────────

  // Check if shutdown was requested before proceeding to final stage
  if (should_exit()) {
    connection_state_transition(ctx, CONN_STATE_FAILED);
    return SET_ERRNO(ERROR_NETWORK, "Connection attempt aborted due to shutdown request");
  }

  result = attempt_webrtc_turn(ctx, server_address, acds_server, acds_port);
  if (result == ASCIICHAT_OK) {
    log_info("Connection succeeded via WebRTC+TURN");
    connection_state_transition(ctx, CONN_STATE_CONNECTED);
    return ASCIICHAT_OK;
  }

  if (result == ERROR_NETWORK) {
    log_debug("WebRTC+TURN stage skipped per CLI flags");
  } else if (connection_check_timeout(ctx)) {
    log_error("Stage 3 timeout - all fallback stages exhausted");
  } else {
    log_error("Stage 3 failed - all fallback stages exhausted");
  }

  // ─────────────────────────────────────────────────────────────
  // All stages failed
  // ─────────────────────────────────────────────────────────────

  connection_state_transition(ctx, CONN_STATE_FAILED);
  return SET_ERRNO(ERROR_NETWORK, "All fallback stages exhausted (TCP: failed, STUN: %s, TURN: %s)",
                   ctx->webrtc_skip_stun ? "skipped" : "failed", ctx->webrtc_disable_turn ? "skipped" : "failed");
}

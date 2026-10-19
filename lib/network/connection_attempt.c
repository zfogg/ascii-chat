#include <ascii-chat/ui/notice.h>
#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/stats/runtime.h>
/**
 * @file connection_attempt.c
 * @brief Connection state machine and attempt logic for TCP/WebSocket clients
 *
 * Implements direct TCP and WebSocket connections for client mode.
 * Manages connection state transitions, timeouts, and transport lifecycle.
 *
 * Features:
 * - Direct TCP connection to server
 * - WebSocket connection support
 * - Timeout management
 * - Crypto handshake integration
 * - ACIP transport creation
 * - Proper resource cleanup
 *
 * Integration Points:
 * - Called from src/client/main.c connection loop
 * - Returns active transport when connection succeeds
 *
 * @date January 2026
 * @version 2.0
 */

#include <ascii-chat/network/connection_attempt.h>
#include <ascii-chat/network/connection_endpoint.h>
#include <ascii-chat/network/connection_handle.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/tcp/client.h>
#include <ascii-chat/network/websocket/client.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/app_callbacks.h>

#include <string.h>
#include <memory.h>

static void destroy_tcp_client_owner(void *owner) {
  tcp_client_t *tcp_client = (tcp_client_t *)owner;
  tcp_client_destroy(&tcp_client);
}

static void destroy_websocket_client_owner(void *owner) {
  websocket_client_t *ws_client = (websocket_client_t *)owner;
  websocket_client_destroy(&ws_client);
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
  case CONN_STATE_ATTEMPTING:
    return "ATTEMPTING";
  case CONN_STATE_CONNECTED:
    return "CONNECTED";
  case CONN_STATE_DISCONNECTED:
    return "DISCONNECTED";
  case CONN_STATE_FAILED:
    return "FAILED";
  default:
    return "UNKNOWN";
  }
}

/* ============================================================================
 * Context Management
 * ============================================================================ */

/**
 * @brief Initialize connection attempt context
 */
asciichat_error_t connection_context_init(connection_attempt_context_t *ctx) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Context pointer is NULL");
  }

  // Reset context
  memset(ctx, 0, sizeof(connection_attempt_context_t));
  connection_handle_init(&ctx->connection);

  // Initialize state
  ctx->current_state = CONN_STATE_IDLE;
  ctx->previous_state = CONN_STATE_IDLE;

  // Initialize timeout
  ctx->attempt_start_time_ns = time_get_realtime_ns();
  ctx->timeout_ns = CONN_TIMEOUT_TCP;

  // Initialize counters
  ctx->reconnect_attempt = 1;
  ctx->total_transitions = 0;

  log_debug("Connection context initialized");

  return ASCIICHAT_OK;
}

/**
 * @brief Cleanup connection attempt context
 */
void connection_context_cleanup(connection_attempt_context_t *ctx) {
  if (!ctx)
    return;

  connection_handle_cleanup(&ctx->connection);

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

  log_debug("State transition: %s → %s", connection_state_name(ctx->previous_state), connection_state_name(new_state));

  return ASCIICHAT_OK;
}

/**
 * @brief Check if connection attempt has exceeded timeout
 */
bool connection_check_timeout(const connection_attempt_context_t *ctx) {
  if (!ctx)
    return false;

  uint64_t elapsed_ns = time_get_realtime_ns() - ctx->attempt_start_time_ns;
  bool timeout_exceeded = elapsed_ns > ctx->timeout_ns;

  if (timeout_exceeded) {
    char elapsed_str[32], timeout_str[32];
    time_pretty(elapsed_ns, -1, elapsed_str, sizeof(elapsed_str));
    time_pretty(ctx->timeout_ns, -1, timeout_str, sizeof(timeout_str));
    log_warn("Connection timeout exceeded: elapsed %s > %s limit", elapsed_str, timeout_str);
  }

  return timeout_exceeded;
}

/* ============================================================================
 * TCP Connection
 * ============================================================================ */

/**
 * @brief Attempt direct TCP connection
 *
 * Connects to server via TCP, performs crypto handshake if enabled,
 * and creates ACIP transport for protocol communication.
 */
static asciichat_error_t connection_attempt_tcp_measured(connection_attempt_context_t *ctx, const char *server_address,
                                                         uint16_t server_port) {
  if (!ctx || !ctx->ops.set_ip || !ctx->ops.set_port || !ctx->ops.set_mode || !ctx->ops.init || !ctx->ops.handshake ||
      !ctx->ops.is_ready || !ctx->ops.get_context)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Connection attempt requires per-instance callbacks");
  log_info("=== connection_attempt_tcp CALLED: address='%s', port=%u ===", server_address, server_port);

  if (!ctx || !server_address) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Check if shutdown was requested before attempting connection (via callback if available)
  if (APP_CALLBACK_BOOL(should_exit)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection attempt aborted due to shutdown request");
  }

  // Normalize the endpoint once so TCP and WebSocket paths use the same rules.
  connection_endpoint_t endpoint = {0};
  asciichat_error_t endpoint_result = connection_endpoint_resolve(server_address, server_port, &endpoint);
  if (endpoint_result != ASCIICHAT_OK) {
    return endpoint_result;
  }

  log_debug("connection_attempt_tcp: server_address='%s', normalized=%s host=%s port=%u", server_address,
            connection_endpoint_protocol_name(endpoint.protocol), endpoint.host, endpoint.port);

  if (endpoint.protocol == CONNECTION_ENDPOINT_WEBSOCKET) {
    // WebSocket connection path
    const char *ws_url = endpoint.input;

    // Set server IP/port for crypto handshake known_hosts verification
    ctx->ops.set_ip(ctx->context, endpoint.host);
    ctx->ops.set_port(ctx->context, endpoint.port);
    log_debug("Set server IP=%s, port=%u for WebSocket crypto handshake", endpoint.host, endpoint.port);

    log_info("Attempting WebSocket connection to %s", ws_url);

    // Transition to attempting state
    asciichat_error_t result = connection_state_transition(ctx, CONN_STATE_ATTEMPTING);
    if (result != ASCIICHAT_OK) {
      return result;
    }

    // Set timeout for this attempt
    ctx->attempt_start_time_ns = time_get_realtime_ns();
    ctx->timeout_ns = CONN_TIMEOUT_TCP; // Use same timeout as TCP

    const crypto_policy_t policy = {
        .transport_secure = strncmp(ws_url, "wss://", 6) == 0,
        .encrypt = GET_OPTION(encrypt_enabled),
        .no_encrypt = GET_OPTION(no_encrypt),
        .no_auth = GET_OPTION(no_auth),
        .has_password = GET_OPTION(password)[0] != '\0',
        .has_identity = GET_OPTION(encrypt_key)[0] != '\0' || GET_OPTION(num_identity_keys) > 0,
        .has_verification_key = GET_OPTION(server_key)[0] != '\0',
    };
    uint8_t crypto_mode;
    asciichat_error_t policy_result = crypto_policy_resolve(&policy, &crypto_mode);
    if (policy_result != ASCIICHAT_OK)
      return policy_result;

    if (!policy.transport_secure && !ACIP_CRYPTO_HAS_ENCRYPT(crypto_mode))
      NOTICE(DANGER, "ENCRYPTION DISABLED", "--no-encrypt is enabled. This connection is not encrypted.");

    log_debug("WebSocket crypto mode computed: 0x%02x (encrypt=%d, auth=%d)", crypto_mode,
              ACIP_CRYPTO_HAS_ENCRYPT(crypto_mode), ACIP_CRYPTO_HAS_AUTH(crypto_mode));

    // Initialize negotiation even in NONE mode so the server can enforce its policy.
    const crypto_context_t *crypto_ctx = NULL;

    log_debug("Initializing crypto context for WebSocket with mode 0x%02x...", crypto_mode);

    ctx->ops.set_mode(ctx->context, crypto_mode);

    if (ctx->ops.init(ctx->context) != 0) {
      log_error("Failed to initialize crypto context");
      return SET_ERRNO(ERROR_CRYPTO, "Crypto initialization failed");
    }
    log_debug("Crypto context initialized successfully");

    // Get crypto context if ready
    if (ctx->ops.is_ready(ctx->context)) {
      crypto_ctx = ctx->ops.get_context(ctx->context);
    }

    // Create WebSocket client instance
    websocket_client_t *ws_client = websocket_client_create("discovery_connection");
    if (!ws_client) {
      log_error("Failed to create WebSocket client");
      connection_state_transition(ctx, CONN_STATE_FAILED);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket client creation failed");
    }

    // Connect via WebSocket
    acip_transport_t *transport = websocket_client_connect(ws_client, ws_url, (crypto_context_t *)crypto_ctx);
    if (!transport) {
      log_error("Failed to create WebSocket ACIP transport");
      websocket_client_destroy(&ws_client);
      connection_state_transition(ctx, CONN_STATE_FAILED);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket connection failed");
    }

    // CRITICAL: Wait for the WebSocket handshake to actually complete
    // The transport is created but the TLS/crypto handshake happens in the service thread
    log_info("WebSocket transport created, waiting for TLS handshake completion...");
    const int MAX_HANDSHAKE_WAIT_ITERATIONS = 300; // 300 * 50ms = 15 seconds
    int handshake_wait_count = 0;
    while (!acip_transport_is_connected(transport) && handshake_wait_count < MAX_HANDSHAKE_WAIT_ITERATIONS) {
      platform_sleep_us(50000); // Sleep 50ms between checks
      handshake_wait_count++;
      if (handshake_wait_count % 20 == 0) {
        log_info("WebSocket handshake waiting... (%d seconds elapsed)", handshake_wait_count / 20);
      }
    }

    if (!acip_transport_is_connected(transport)) {
      log_error("WebSocket TLS handshake failed or timed out after 15 seconds");
      websocket_client_destroy(&ws_client);
      connection_state_transition(ctx, CONN_STATE_FAILED);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket handshake timeout");
    }

    asciichat_error_t handshake_result = ctx->ops.handshake(ctx->context, transport);
    if (handshake_result != ASCIICHAT_OK) {
      websocket_client_destroy(&ws_client);
      return handshake_result;
    }
    if (ctx->ops.is_ready(ctx->context))
      transport->crypto_ctx = ctx->ops.get_context(ctx->context);
    log_info("WebSocket connection established to %s", ws_url);
    connection_state_transition(ctx, CONN_STATE_CONNECTED);
    ctx->connection.transport = transport;
    ctx->connection.transport_type = ACIP_TRANSPORT_WEBSOCKET;
    ctx->connection.owner = ws_client;
    ctx->connection.owner_destroy = destroy_websocket_client_owner;

    return ASCIICHAT_OK;
  }

  // TCP connection path (original logic)
  log_info("Attempting TCP connection to %s:%u (3s timeout)", endpoint.host, endpoint.port);

  // Transition to attempting state
  asciichat_error_t result = connection_state_transition(ctx, CONN_STATE_ATTEMPTING);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Create TCP client internally so connection ownership stays in this layer.
  tcp_client_t *tcp_client = tcp_client_create();
  if (!tcp_client) {
    log_error("Failed to create TCP client");
    connection_state_transition(ctx, CONN_STATE_FAILED);
    return SET_ERRNO(ERROR_NETWORK, "TCP client creation failed");
  }

  // Set timeout for this attempt
  ctx->attempt_start_time_ns = time_get_realtime_ns();
  ctx->timeout_ns = CONN_TIMEOUT_TCP;

  // Attempt TCP connection (reconnect_attempt is 0-based, convert for tcp_client_connect)
  // Use effective_address/port which may be parsed from tcp:// URL scheme
  int tcp_result = tcp_client_connect(tcp_client, endpoint.host, endpoint.port, (int)ctx->reconnect_attempt,
                                      ctx->reconnect_attempt == 0, ctx->reconnect_attempt > 0);

  if (tcp_result != 0) {
    log_debug("TCP connection failed (tcp_client_connect returned %d)", tcp_result);
    tcp_client_destroy(&tcp_client);
    connection_state_transition(ctx, CONN_STATE_FAILED);
    return SET_ERRNO(ERROR_NETWORK, "TCP connection failed after %u attempts", ctx->reconnect_attempt);
  }

  // Extract socket from TCP client for crypto handshake
  socket_t sockfd = tcp_client_get_socket(tcp_client);
  if (sockfd == INVALID_SOCKET_VALUE) {
    log_error("Failed to get socket from TCP client");
    tcp_client_destroy(&tcp_client);
    return SET_ERRNO(ERROR_NETWORK, "Invalid socket after TCP connection");
  }

  // Note: TCP client stores server IP in tcp_client->server_ip for potential crypto context use
  if (tcp_client->server_ip[0] != '\0') {
    log_debug("Server IP available from TCP client: %s", tcp_client->server_ip);
    ctx->ops.set_ip(ctx->context, tcp_client->server_ip);
  } else {
    log_warn("TCP client did not populate server_ip field");
  }

  const crypto_policy_t policy = {
      .transport_secure = false,
      .encrypt = GET_OPTION(encrypt_enabled),
      .no_encrypt = GET_OPTION(no_encrypt),
      .no_auth = GET_OPTION(no_auth),
      .has_password = GET_OPTION(password)[0] != '\0',
      .has_identity = GET_OPTION(encrypt_key)[0] != '\0' || GET_OPTION(num_identity_keys) > 0,
      .has_verification_key = GET_OPTION(server_key)[0] != '\0',
  };
  uint8_t crypto_mode;
  asciichat_error_t policy_result = crypto_policy_resolve(&policy, &crypto_mode);
  if (policy_result != ASCIICHAT_OK) {
    tcp_client_destroy(&tcp_client);
    return policy_result;
  }

  if (!policy.transport_secure && !ACIP_CRYPTO_HAS_ENCRYPT(crypto_mode))
    NOTICE(DANGER, "ENCRYPTION DISABLED", "--no-encrypt is enabled. This connection is not encrypted.");

  log_debug("TCP crypto mode computed: 0x%02x (encrypt=%d, auth=%d)", crypto_mode, ACIP_CRYPTO_HAS_ENCRYPT(crypto_mode),
            ACIP_CRYPTO_HAS_AUTH(crypto_mode));

  // Setup crypto operations via callbacks
  const crypto_context_t *crypto_ctx = NULL;
  acip_transport_t *transport = NULL;

  {
    ctx->ops.set_mode(ctx->context, crypto_mode);

    // Initialize crypto context if mode requires handshake (not ACIP_CRYPTO_NONE)
    // This must happen AFTER setting server IP, as crypto init reads server IP/port
    log_debug("Initializing crypto context...");
    if (ctx->ops.init(ctx->context) != 0) {
      log_error("Failed to initialize crypto context");
      tcp_client_destroy(&tcp_client);
      return SET_ERRNO(ERROR_CRYPTO, "Crypto initialization failed");
    }
    log_debug("Crypto context initialized successfully");

    // Create transport before handshake (starts with NULL crypto, set after)
    transport = acip_tcp_transport_create("connection", sockfd, NULL);
    if (!transport) {
      log_error("Failed to create ACIP transport for TCP");
      tcp_client_destroy(&tcp_client);
      return SET_ERRNO(ERROR_NETWORK, "Failed to create ACIP transport");
    }

    // Perform crypto handshake with server
    log_debug("Performing crypto handshake with server...");
    asciichat_errno_scope_t handshake_scope = asciichat_errno_checkpoint();
    asciichat_error_t handshake_result = ctx->ops.handshake(ctx->context, transport);
    if (handshake_result != ASCIICHAT_OK) {
      acip_transport_destroy(transport);
      tcp_client_destroy(&tcp_client);
      return SET_ERRNO(handshake_result, "Crypto handshake failed");
    }
    ASSERT_NO_ERRNO_SINCE(handshake_scope);
    asciichat_errno_scope_end(handshake_scope, ASCIICHAT_ERRNO_HANDLED);
    log_debug("Crypto handshake completed successfully");

    // Get crypto context after handshake and set it on the transport
    if (ctx->ops.is_ready(ctx->context)) {
      crypto_ctx = ctx->ops.get_context(ctx->context);
      transport->crypto_ctx = (crypto_context_t *)crypto_ctx;
    }
  }

  log_info("TCP connection established to %s:%u", endpoint.host, endpoint.port);
  connection_state_transition(ctx, CONN_STATE_CONNECTED);
  ctx->connection.transport = transport;
  ctx->connection.transport_type = ACIP_TRANSPORT_TCP;
  ctx->connection.owner = tcp_client;
  ctx->connection.owner_destroy = destroy_tcp_client_owner;
  log_debug("TCP owner stored in connection context for cleanup");

  return ASCIICHAT_OK;
}

asciichat_error_t connection_attempt_tcp(connection_attempt_context_t *ctx, const char *server_address,
                                         uint16_t server_port) {
  uint64_t started = time_get_ns();
  stats_runtime_connection_state("connecting");
  asciichat_error_t result = connection_attempt_tcp_measured(ctx, server_address, server_port);
  stats_duration_record(stats_runtime_scope(), STATS_DURATION_CONNECTION_SETUP, time_get_ns() - started);
  stats_runtime_connection_state(result == ASCIICHAT_OK ? "connected" : "connection failed");
  return result;
}

/**
 * @brief Attempt WebSocket connection (ws:// or wss://)
 *
 * Connects to server via WebSocket, performs crypto handshake if enabled,
 * and creates ACIP transport for protocol communication.
 */
static asciichat_error_t connection_attempt_websocket_measured(connection_attempt_context_t *ctx, const char *ws_url) {
  // URL dispatch and security negotiation are identical to the general connector.
  return connection_attempt_tcp_measured(ctx, ws_url, 0);
}

asciichat_error_t connection_attempt_websocket(connection_attempt_context_t *ctx, const char *ws_url) {
  uint64_t started = time_get_ns();
  stats_runtime_connection_state("connecting");
  asciichat_error_t result = connection_attempt_websocket_measured(ctx, ws_url);
  stats_duration_record(stats_runtime_scope(), STATS_DURATION_CONNECTION_SETUP, time_get_ns() - started);
  stats_runtime_connection_state(result == ASCIICHAT_OK ? "connected" : "connection failed");
  return result;
}

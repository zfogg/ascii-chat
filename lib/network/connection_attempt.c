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

// Forward declare session function (avoids header include issues)
struct websocket_client;
void session_client_like_set_websocket_client(struct websocket_client *ws_client);

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

  // Destroy TCP client instance if created
  if (ctx->tcp_client_instance) {
    tcp_client_destroy(&ctx->tcp_client_instance);
    ctx->tcp_client_instance = NULL;
    log_debug("TCP client instance destroyed");
  }

  // Note: Do NOT destroy the WebSocket client instance here. The WebSocket client
  // is owned by the session layer (passed via config->websocket_client and also
  // stored in g_websocket_client). The session cleanup will destroy it.
  // We just clear the reference to prevent double-free.
  if (ctx->ws_client_instance) {
    ctx->ws_client_instance = NULL;
    ctx->active_transport = NULL;
    log_debug("WebSocket client reference cleared (ownership remains with session)");
  }

  // Close active transport if still open (only if not already destroyed by WebSocket client)
  if (ctx->active_transport) {
    acip_transport_close(ctx->active_transport);
    acip_transport_destroy(ctx->active_transport);
    ctx->active_transport = NULL;
    log_debug("Transport connection closed");
  }

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
asciichat_error_t connection_attempt_tcp(connection_attempt_context_t *ctx, const char *server_address,
                                         uint16_t server_port, struct tcp_client *pre_created_tcp_client) {
  log_info("=== connection_attempt_tcp CALLED: address='%s', port=%u, pre_created=%p ===", server_address, server_port,
           (void *)pre_created_tcp_client);

  if (!ctx || !server_address) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Check if shutdown was requested before attempting connection (via callback if available)
  if (APP_CALLBACK_BOOL(should_exit)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection attempt aborted due to shutdown request");
  }

  // Check for WebSocket URL - handle separately from TCP
  log_debug("connection_attempt_tcp: server_address='%s', port=%u", server_address, server_port);

  if (url_is_websocket(server_address)) {
    // WebSocket connection path
    const char *ws_url = server_address;

    // Parse for debug logging and server IP extraction
    url_parts_t url_parts = {0};
    if (url_parse(server_address, &url_parts) == ASCIICHAT_OK) {
      log_debug("WebSocket URL parsed: host=%s, port=%d, scheme=%s", url_parts.host, url_parts.port, url_parts.scheme);

      // Set server IP for crypto handshake
      // This ensures the crypto context has proper server address info for known_hosts verification
      APP_CALLBACK_VOID_STR(server_connection_set_ip, url_parts.host);
      log_debug("Set server IP to %s from WebSocket URL", url_parts.host);
    }

    log_info("Attempting WebSocket connection to %s", ws_url);

    // Transition to attempting state
    asciichat_error_t result = connection_state_transition(ctx, CONN_STATE_ATTEMPTING);
    if (result != ASCIICHAT_OK) {
      url_parts_destroy(&url_parts);
      return result;
    }

    // Set timeout for this attempt
    ctx->attempt_start_time_ns = time_get_realtime_ns();
    ctx->timeout_ns = CONN_TIMEOUT_TCP; // Use same timeout as TCP

    // Compute crypto mode from CLI options
    // Determine if client has authentication material (keys or password)
    bool has_auth_material =
        (GET_OPTION(encrypt_key)[0] != '\0' || GET_OPTION(num_identity_keys) > 0 || GET_OPTION(password)[0] != '\0');
    bool no_encrypt = GET_OPTION(no_encrypt);
    bool no_auth = GET_OPTION(no_auth);

    uint8_t crypto_mode;
    if (!no_encrypt && !no_auth && !has_auth_material)
      crypto_mode = ACIP_CRYPTO_ENCRYPT; // default: encrypt only
    else if (!no_encrypt && !no_auth && has_auth_material)
      crypto_mode = ACIP_CRYPTO_FULL; // full: encrypt + auth
    else if (no_encrypt && !no_auth && has_auth_material)
      crypto_mode = ACIP_CRYPTO_AUTH; // auth-only mode
    else
      crypto_mode = ACIP_CRYPTO_NONE; // no crypto

    log_debug("WebSocket crypto mode computed: 0x%02x (encrypt=%d, auth=%d)", crypto_mode,
              ACIP_CRYPTO_HAS_ENCRYPT(crypto_mode), ACIP_CRYPTO_HAS_AUTH(crypto_mode));

    // Set server IP and port for crypto handshake known_hosts verification
    // This is required for the crypto layer to properly validate server identity
    APP_CALLBACK_VOID_STR(server_connection_set_ip, url_parts.host);
    APP_CALLBACK_VOID_INT(server_connection_set_port, url_parts.port);
    log_debug("Set server IP=%s, port=%d for WebSocket crypto handshake", url_parts.host, url_parts.port);

    // Set crypto mode before initialization via callback
    const crypto_context_t *crypto_ctx = NULL;

    if (crypto_mode != ACIP_CRYPTO_NONE) {
      log_debug("Initializing crypto context for WebSocket...");

      APP_CALLBACK_VOID_UINT8(client_crypto_set_mode, crypto_mode);

      if (APP_CALLBACK_INT(client_crypto_init) != 0) {
        log_error("Failed to initialize crypto context");
        url_parts_destroy(&url_parts);
        return SET_ERRNO(ERROR_CRYPTO, "Crypto initialization failed");
      }
      log_debug("Crypto context initialized successfully");

      // Get crypto context if ready
      if (APP_CALLBACK_BOOL(crypto_client_is_ready)) {
        crypto_ctx = APP_CALLBACK_PTR(crypto_client_get_context);
      }
    }

    // Create WebSocket client instance
    websocket_client_t *ws_client = websocket_client_create("discovery_connection");
    if (!ws_client) {
      log_error("Failed to create WebSocket client");
      connection_state_transition(ctx, CONN_STATE_FAILED);
      url_parts_destroy(&url_parts);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket client creation failed");
    }

    // Connect via WebSocket
    acip_transport_t *transport = websocket_client_connect(ws_client, ws_url, (crypto_context_t *)crypto_ctx);
    if (!transport) {
      log_error("Failed to create WebSocket ACIP transport");
      websocket_client_destroy(&ws_client);
      connection_state_transition(ctx, CONN_STATE_FAILED);
      url_parts_destroy(&url_parts);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket connection failed");
    }

    log_info("WebSocket connection established to %s", ws_url);
    connection_state_transition(ctx, CONN_STATE_CONNECTED);
    ctx->active_transport = transport;
    ctx->ws_client_instance = ws_client;

    // Update global websocket client so session_client_like_run() detects network mode
    session_client_like_set_websocket_client(ws_client);

    url_parts_destroy(&url_parts);
    return ASCIICHAT_OK;
  }

  // TCP connection path (original logic)
  log_info("Attempting TCP connection to %s:%u (3s timeout)", server_address, server_port);

  // Transition to attempting state
  asciichat_error_t result = connection_state_transition(ctx, CONN_STATE_ATTEMPTING);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Use pre-created TCP client if provided, otherwise create one
  tcp_client_t *tcp_client = pre_created_tcp_client;
  bool created_tcp_client = false;
  if (!tcp_client) {
    tcp_client = tcp_client_create();
    created_tcp_client = true;
    if (!tcp_client) {
      log_error("Failed to create TCP client");
      connection_state_transition(ctx, CONN_STATE_FAILED);
      return SET_ERRNO(ERROR_NETWORK, "TCP client creation failed");
    }
    log_debug("Created TCP client locally (not pre-created by framework)");
  } else {
    log_debug("Using pre-created TCP client from framework");
  }

  // Set timeout for this attempt
  ctx->attempt_start_time_ns = time_get_realtime_ns();
  ctx->timeout_ns = CONN_TIMEOUT_TCP;

  // Attempt TCP connection (reconnect_attempt is 0-based, convert for tcp_client_connect)
  int tcp_result = tcp_client_connect(tcp_client, server_address, server_port, (int)ctx->reconnect_attempt,
                                      ctx->reconnect_attempt == 0, ctx->reconnect_attempt > 0);

  if (tcp_result != 0) {
    log_debug("TCP connection failed (tcp_client_connect returned %d)", tcp_result);
    if (created_tcp_client) {
      tcp_client_destroy(&tcp_client);
    }
    connection_state_transition(ctx, CONN_STATE_FAILED);
    return SET_ERRNO(ERROR_NETWORK, "TCP connection failed after %u attempts", ctx->reconnect_attempt);
  }

  // Extract socket from TCP client for crypto handshake
  socket_t sockfd = tcp_client_get_socket(tcp_client);
  if (sockfd == INVALID_SOCKET_VALUE) {
    log_error("Failed to get socket from TCP client");
    if (created_tcp_client) {
      tcp_client_destroy(&tcp_client);
    }
    return SET_ERRNO(ERROR_NETWORK, "Invalid socket after TCP connection");
  }

  // Note: TCP client stores server IP in tcp_client->server_ip for potential crypto context use
  if (tcp_client->server_ip[0] != '\0') {
    log_debug("Server IP available from TCP client: %s", tcp_client->server_ip);
    APP_CALLBACK_VOID_STR(server_connection_set_ip, tcp_client->server_ip);
  } else {
    log_warn("TCP client did not populate server_ip field");
  }

  // Compute crypto mode from CLI options
  // Determine if client has authentication material (keys or password)
  bool has_auth_material =
      (GET_OPTION(encrypt_key)[0] != '\0' || GET_OPTION(num_identity_keys) > 0 || GET_OPTION(password)[0] != '\0');
  bool no_encrypt = GET_OPTION(no_encrypt);
  bool no_auth = GET_OPTION(no_auth);

  uint8_t crypto_mode;
  if (!no_encrypt && !no_auth && !has_auth_material)
    crypto_mode = ACIP_CRYPTO_ENCRYPT; // default: encrypt only
  else if (!no_encrypt && !no_auth && has_auth_material)
    crypto_mode = ACIP_CRYPTO_FULL; // full: encrypt + auth
  else if (no_encrypt && !no_auth && has_auth_material)
    crypto_mode = ACIP_CRYPTO_AUTH; // auth-only mode
  else
    crypto_mode = ACIP_CRYPTO_NONE; // no crypto

  log_debug("TCP crypto mode computed: 0x%02x (encrypt=%d, auth=%d)", crypto_mode, ACIP_CRYPTO_HAS_ENCRYPT(crypto_mode),
            ACIP_CRYPTO_HAS_AUTH(crypto_mode));

  // Setup crypto operations via callbacks
  const crypto_context_t *crypto_ctx = NULL;

  if (crypto_mode != ACIP_CRYPTO_NONE) {
    APP_CALLBACK_VOID_UINT8(client_crypto_set_mode, crypto_mode);

    // Initialize crypto context if mode requires handshake (not ACIP_CRYPTO_NONE)
    // This must happen AFTER setting server IP, as crypto init reads server IP/port
    log_debug("Initializing crypto context...");
    if (APP_CALLBACK_INT(client_crypto_init) != 0) {
      log_error("Failed to initialize crypto context");
      if (created_tcp_client) {
        tcp_client_destroy(&tcp_client);
      }
      return SET_ERRNO(ERROR_CRYPTO, "Crypto initialization failed");
    }
    log_debug("Crypto context initialized successfully");

    // Perform crypto handshake with server
    log_debug("Performing crypto handshake with server...");
    if (APP_CALLBACK_INT_SOCKET(client_crypto_handshake, sockfd) != 0) {
      log_error("Crypto handshake failed");
      if (created_tcp_client) {
        tcp_client_destroy(&tcp_client);
      }
      return SET_ERRNO(ERROR_NETWORK, "Crypto handshake failed");
    }
    log_debug("Crypto handshake completed successfully");

    // Get crypto context after handshake
    if (APP_CALLBACK_BOOL(crypto_client_is_ready)) {
      crypto_ctx = APP_CALLBACK_PTR(crypto_client_get_context);
    }
  }

  // Create ACIP transport for protocol-agnostic packet sending/receiving
  acip_transport_t *transport = acip_tcp_transport_create("connection", sockfd, (crypto_context_t *)crypto_ctx);
  if (!transport) {
    log_error("Failed to create ACIP transport for TCP");
    if (created_tcp_client) {
      tcp_client_destroy(&tcp_client);
    }
    return SET_ERRNO(ERROR_NETWORK, "Failed to create ACIP transport");
  }

  log_info("TCP connection established to %s:%u", server_address, server_port);
  connection_state_transition(ctx, CONN_STATE_CONNECTED);
  ctx->active_transport = transport;

  // Store tcp_client in context for proper lifecycle management only if we created it locally
  // If it's pre-created by the framework, the framework will manage its lifecycle
  if (created_tcp_client) {
    ctx->tcp_client_instance = tcp_client;
    log_debug("TCP client instance stored in connection context for cleanup");
  } else {
    log_debug("Using framework-managed TCP client, not storing in context");
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Attempt WebSocket connection (ws:// or wss://)
 *
 * Connects to server via WebSocket, performs crypto handshake if enabled,
 * and creates ACIP transport for protocol communication.
 */
asciichat_error_t connection_attempt_websocket(connection_attempt_context_t *ctx, const char *ws_url) {
  log_info("=== connection_attempt_websocket CALLED: url='%s' ===", ws_url);

  if (!ctx || !ws_url) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Check if shutdown was requested (via callback if available)
  if (APP_CALLBACK_BOOL(should_exit)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection attempt aborted due to shutdown request");
  }

  log_info("Attempting WebSocket connection to %s", ws_url);

  // Transition to attempting state
  asciichat_error_t result = connection_state_transition(ctx, CONN_STATE_ATTEMPTING);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Set timeout for this attempt
  ctx->attempt_start_time_ns = time_get_realtime_ns();
  ctx->timeout_ns = CONN_TIMEOUT_TCP;

  // Initialize crypto context if encryption is enabled
  const crypto_context_t *crypto_ctx = NULL;
  if (!GET_OPTION(no_encrypt)) {
    log_debug("Initializing crypto context for WebSocket...");

    if (APP_CALLBACK_INT(client_crypto_init) != 0) {
      log_error("Failed to initialize crypto context");
      return SET_ERRNO(ERROR_CRYPTO, "Crypto initialization failed");
    }
    log_debug("Crypto context initialized successfully");

    // Get crypto context if ready
    if (APP_CALLBACK_BOOL(crypto_client_is_ready)) {
      crypto_ctx = APP_CALLBACK_PTR(crypto_client_get_context);
    }
  }

  // Create WebSocket client instance
  websocket_client_t *ws_client = websocket_client_create("connection");
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

  log_info("WebSocket connection established to %s", ws_url);
  connection_state_transition(ctx, CONN_STATE_CONNECTED);
  ctx->active_transport = transport;
  ctx->ws_client_instance = ws_client;
  log_debug("WebSocket client instance stored in connection context");

  return ASCIICHAT_OK;
}

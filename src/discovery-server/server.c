/**
 * @file acds/server.c
 * @brief 🌐 Discovery server TCP connection manager
 *
 * Uses lib/network/tcp_server abstraction for:
 * - Dual-stack IPv4/IPv6 binding
 * - Per-client handler threads
 * - select()-based accept loop
 *
 * ACDS-specific functionality:
 * - SQLite as single source of truth for sessions
 * - ACIP packet dispatch to session/signaling handlers
 */

#include "discovery-server/server.h"
#include "discovery/database.h"
#include "discovery/session.h"
#include "discovery-server/signaling.h"
#include "network/acip/acds.h"
#include "network/acip/acds_handlers.h"
#include "network/acip/acds_client.h"
#include "network/acip/send.h"
#include "network/acip/transport.h"
#include "network/rate_limit/rate_limit.h"
#include "network/rate_limit/sqlite.h"
#include "network/errors.h"
#include "log/logging.h"
#include "platform/socket.h"
#include "network/network.h"
#include "buffer_pool.h"
#include "network/tcp/server.h"
#include "util/ip.h"
#include <string.h>
#include <time.h>

/**
 * @brief Background thread for periodic cleanup
 *
 * Wakes up every 5 minutes to:
 * - Remove old rate limit events from the database
 * - Clean up expired sessions
 */
static void *cleanup_thread_func(void *arg) {
  acds_server_t *server = (acds_server_t *)arg;
  if (!server) {
    return NULL;
  }

  log_info("Cleanup thread started (rate limits + expired sessions)");

  while (!atomic_load(&server->shutdown)) {
    // Sleep for 5 minutes (or until shutdown)
    for (int i = 0; i < 300 && !atomic_load(&server->shutdown); i++) {
      platform_sleep_ms(1000); // Sleep 1 second at a time for responsive shutdown
    }

    if (atomic_load(&server->shutdown)) {
      break;
    }

    // Run rate limit cleanup (delete events older than 1 hour)
    log_debug("Running rate limit cleanup...");
    asciichat_error_t result = rate_limiter_cleanup(server->rate_limiter, 3600);
    if (result != ASCIICHAT_OK) {
      log_warn("Rate limit cleanup failed");
    }

    // Run expired session cleanup
    log_debug("Running expired session cleanup...");
    database_session_cleanup_expired(server->db);
  }

  log_info("Cleanup thread exiting");
  return NULL;
}

asciichat_error_t acds_server_init(acds_server_t *server, const acds_config_t *config) {
  if (!server || !config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or config is NULL");
  }

  memset(server, 0, sizeof(*server));
  memcpy(&server->config, config, sizeof(acds_config_t));

  // Open database (SQLite as single source of truth)
  asciichat_error_t result = database_init(config->database_path, &server->db);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Initialize rate limiter with SQLite backend
  server->rate_limiter = rate_limiter_create_sqlite(NULL); // NULL = externally managed DB
  if (!server->rate_limiter) {
    database_close(server->db);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create rate limiter");
  }

  // Set the database handle for the rate limiter
  rate_limiter_set_sqlite_db(server->rate_limiter, server->db);

  // Configure TCP server
  tcp_server_config_t tcp_config = {
      .port = config->port,
      .ipv4_address = (config->address[0] != '\0') ? config->address : NULL,
      .ipv6_address = (config->address6[0] != '\0') ? config->address6 : NULL,
      .bind_ipv4 = (config->address[0] != '\0') || (config->address[0] == '\0' && config->address6[0] == '\0'),
      .bind_ipv6 = (config->address6[0] != '\0') || (config->address[0] == '\0' && config->address6[0] == '\0'),
      .accept_timeout_sec = 1,
      .client_handler = acds_client_handler,
      .user_data = server,
  };

  // Initialize TCP server
  result = tcp_server_init(&server->tcp_server, &tcp_config);
  if (result != ASCIICHAT_OK) {
    rate_limiter_destroy(server->rate_limiter);
    database_close(server->db);
    return result;
  }

  // Initialize background worker thread pool
  atomic_store(&server->shutdown, false);
  server->worker_pool = thread_pool_create("acds_workers");
  if (!server->worker_pool) {
    log_warn("Failed to create worker thread pool");
    tcp_server_shutdown(&server->tcp_server);
    rate_limiter_destroy(server->rate_limiter);
    database_close(server->db);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create worker thread pool");
  }

  // Spawn cleanup thread in worker pool
  if (thread_pool_spawn(server->worker_pool, cleanup_thread_func, server, 0, "cleanup") != ASCIICHAT_OK) {
    log_warn("Failed to spawn cleanup thread (continuing without cleanup)");
  }

  log_info("Discovery server initialized successfully");
  return ASCIICHAT_OK;
}

asciichat_error_t acds_server_run(acds_server_t *server) {
  if (!server) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server is NULL");
  }

  log_info("Discovery server accepting connections on port %d", server->config.port);

  // Delegate to TCP server abstraction
  return tcp_server_run(&server->tcp_server);
}

void acds_server_shutdown(acds_server_t *server) {
  if (!server) {
    return;
  }

  // Signal shutdown to worker threads
  atomic_store(&server->shutdown, true);

  // Shutdown TCP server (closes listen sockets, stops accept loop)
  tcp_server_shutdown(&server->tcp_server);

  // Wait for all client handler threads to exit
  size_t remaining_clients;
  int shutdown_attempts = 0;
  const int max_shutdown_attempts = 100; // 10 seconds (100 * 100ms)

  while ((remaining_clients = tcp_server_get_client_count(&server->tcp_server)) > 0 &&
         shutdown_attempts < max_shutdown_attempts) {
    log_debug("Waiting for %zu client handler threads to exit (attempt %d/%d)", remaining_clients,
              shutdown_attempts + 1, max_shutdown_attempts);
    platform_sleep_ms(100);
    shutdown_attempts++;
  }

  if (remaining_clients > 0) {
    log_warn("Server shutdown: %zu client handler threads still running after 10 seconds", remaining_clients);
  } else if (shutdown_attempts > 0) {
    log_debug("All client handler threads exited gracefully");
  }

  // Stop and destroy worker thread pool (cleanup thread, etc.)
  if (server->worker_pool) {
    thread_pool_destroy(server->worker_pool);
    server->worker_pool = NULL;
    log_debug("Worker thread pool stopped");
  }

  // Destroy rate limiter
  if (server->rate_limiter) {
    rate_limiter_destroy(server->rate_limiter);
    server->rate_limiter = NULL;
  }

  // Close database
  if (server->db) {
    database_close(server->db);
    server->db = NULL;
  }

  log_info("Server shutdown complete");
}

// =============================================================================
// ACIP Transport Helper Macros for ACDS
// =============================================================================
// ACDS uses plain TCP without encryption (discovery service)
// These macros simplify creating temporary transports for responses

#define ACDS_CREATE_TRANSPORT(socket, transport_var)                                                                   \
  acip_transport_t *transport_var = acip_tcp_transport_create(socket, NULL);                                           \
  if (!transport_var) {                                                                                                \
    log_error("Failed to create ACDS transport");                                                                      \
    return;                                                                                                            \
  }

#define ACDS_DESTROY_TRANSPORT(transport_var) acip_transport_destroy(transport_var)

// =============================================================================
// ACIP Callback Wrappers for ACDS
// =============================================================================
// These callbacks are invoked by acip_handle_acds_packet() via O(1) array dispatch.
// Each callback implements: Rate Limit → Crypto Verify → Business Logic → DB Save

static void acds_on_session_create(const acip_session_create_t *req, int client_socket, const char *client_ip,
                                   void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("SESSION_CREATE packet from %s", client_ip);

  // Create ACIP transport for responses
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  // Get client data for multi-key session state tracking
  void *client_data_ptr = NULL;
  if (tcp_server_get_client(&server->tcp_server, client_socket, &client_data_ptr) != ASCIICHAT_OK || !client_data_ptr) {
    acip_send_error(transport, ERROR_INVALID_PARAM, "Client data not found");
    ACDS_DESTROY_TRANSPORT(transport);
    return;
  }
  acds_client_data_t *client_data = (acds_client_data_t *)client_data_ptr;

  // Check if identity_pubkey is all zeros (finalize sentinel)
  bool is_zero_key = true;
  for (size_t i = 0; i < 32; i++) {
    if (req->identity_pubkey[i] != 0) {
      is_zero_key = false;
      break;
    }
  }

  // === MULTI-KEY PROTOCOL: Finalize session ===
  if (is_zero_key) {
    if (!client_data->in_multikey_session_create) {
      // Special case: If identity verification is not required, allow single zero-key session creation
      if (!server->config.require_server_identity) {
        log_debug(
            "SESSION_CREATE with zero key from %s: identity verification not required, treating as anonymous session",
            client_ip);
        // Store the zero key as first key and proceed to finalize immediately
        memcpy(&client_data->pending_session, req, sizeof(acip_session_create_t));
        memcpy(client_data->pending_session_keys[0], req->identity_pubkey, 32);
        client_data->num_pending_keys = 1;
        client_data->in_multikey_session_create = true;
        // Fall through to finalize logic below
      } else {
        acip_send_error(transport, ERROR_INVALID_PARAM, "Zero key received but not in multi-key session creation mode");
        ACDS_DESTROY_TRANSPORT(transport);
        return;
      }
    }

    log_info("SESSION_CREATE finalize from %s: %zu identity key(s)", client_ip, client_data->num_pending_keys);

    // Create session in database with all collected keys
    // For now, database_session_create only supports single key, so use first key
    // TODO: Extend database schema to support multiple keys per session
    acip_session_created_t resp;
    memset(&resp, 0, sizeof(resp));

    asciichat_error_t create_result =
        database_session_create(server->db, &client_data->pending_session, &server->config, &resp);
    if (create_result == ASCIICHAT_OK) {
      // Build complete payload: fixed response + variable STUN/TURN servers
      size_t stun_size = (size_t)resp.stun_count * sizeof(stun_server_t);
      size_t turn_size = (size_t)resp.turn_count * sizeof(turn_server_t);
      size_t total_size = sizeof(resp) + stun_size + turn_size;

      uint8_t *payload = SAFE_MALLOC(total_size, uint8_t *);
      if (!payload) {
        acip_send_error(transport, ERROR_MEMORY, "Out of memory building response");
        client_data->in_multikey_session_create = false;
        client_data->num_pending_keys = 0;
        ACDS_DESTROY_TRANSPORT(transport);
        return;
      }

      // Copy fixed response
      memcpy(payload, &resp, sizeof(resp));

      // Append STUN servers
      if (resp.stun_count > 0) {
        memcpy(payload + sizeof(resp), server->config.stun_servers, stun_size);
      }

      // Append TURN servers
      if (resp.turn_count > 0) {
        memcpy(payload + sizeof(resp) + stun_size, server->config.turn_servers, turn_size);
      }

      // Send complete response with variable-length data
      packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_CREATED, payload, total_size);
      SAFE_FREE(payload);

      log_info("Session created: %.*s (UUID: %02x%02x..., %zu keys, %d STUN, %d TURN servers)", resp.session_string_len,
               resp.session_string, resp.session_id[0], resp.session_id[1], client_data->num_pending_keys,
               resp.stun_count, resp.turn_count);
    } else {
      // Error - send error response using proper error code
      acip_send_error(transport, create_result, "Failed to create session");
      log_warn("Session creation failed for %s: %s", client_ip, asciichat_error_string(create_result));
    }

    // Clear multi-key state
    client_data->in_multikey_session_create = false;
    client_data->num_pending_keys = 0;

    ACDS_DESTROY_TRANSPORT(transport);
    return;
  }

  // === MULTI-KEY PROTOCOL: Add key to pending session ===
  if (client_data->in_multikey_session_create) {
    // Subsequent SESSION_CREATE with non-zero key - add to pending keys

    // Validate we haven't exceeded max keys
    if (client_data->num_pending_keys >= MAX_IDENTITY_KEYS) {
      acip_send_error(transport, ERROR_INVALID_PARAM, "Maximum identity keys exceeded");
      ACDS_DESTROY_TRANSPORT(transport);
      return;
    }

    // Validate key is different from all existing keys
    for (size_t i = 0; i < client_data->num_pending_keys; i++) {
      if (memcmp(client_data->pending_session_keys[i], req->identity_pubkey, 32) == 0) {
        acip_send_error(transport, ERROR_INVALID_PARAM, "Duplicate identity key");
        ACDS_DESTROY_TRANSPORT(transport);
        return;
      }
    }

    // Add key to pending array
    memcpy(client_data->pending_session_keys[client_data->num_pending_keys], req->identity_pubkey, 32);
    client_data->num_pending_keys++;

    log_debug("SESSION_CREATE key #%zu from %s (pubkey: %02x%02x...)", client_data->num_pending_keys, client_ip,
              req->identity_pubkey[0], req->identity_pubkey[1]);

    // Don't send response yet - client will send more keys or zero-key to finalize
    ACDS_DESTROY_TRANSPORT(transport);
    return;
  }

  // === MULTI-KEY PROTOCOL: Start new session ===
  // First SESSION_CREATE with non-zero key - start multi-key mode

  // Rate limiting check (only on first SESSION_CREATE)
  if (!check_and_record_rate_limit(server->rate_limiter, client_ip, RATE_EVENT_SESSION_CREATE, client_socket,
                                   "SESSION_CREATE")) {
    ACDS_DESTROY_TRANSPORT(transport);
    return;
  }

  // Cryptographic identity verification (if required)
  if (server->config.require_server_identity) {
    // Validate timestamp (5 minute window)
    if (!acds_validate_timestamp(req->timestamp, 300)) {
      log_warn("SESSION_CREATE rejected from %s: invalid timestamp (replay attack protection)", client_ip);
      acip_send_error(transport, ERROR_CRYPTO_VERIFICATION, "Timestamp validation failed - too old or in the future");
      ACDS_DESTROY_TRANSPORT(transport);
      return;
    }

    // Verify Ed25519 signature
    asciichat_error_t verify_result = acds_verify_session_create(
        req->identity_pubkey, req->timestamp, req->capabilities, req->max_participants, req->signature);

    if (verify_result != ASCIICHAT_OK) {
      log_warn("SESSION_CREATE rejected from %s: invalid signature (identity verification failed)", client_ip);
      acip_send_error(transport, ERROR_CRYPTO_VERIFICATION, "Identity signature verification failed");
      ACDS_DESTROY_TRANSPORT(transport);
      return;
    }

    log_debug("SESSION_CREATE signature verified from %s (pubkey: %02x%02x...)", client_ip, req->identity_pubkey[0],
              req->identity_pubkey[1]);
  }

  // Reachability verification for Direct TCP sessions
  if (req->session_type == SESSION_TYPE_DIRECT_TCP) {
    // Auto-detect public IP if server_address is empty
    if (req->server_address[0] == '\0') {
      SAFE_STRNCPY(req->server_address, client_ip, sizeof(req->server_address));
      log_info("SESSION_CREATE from %s: auto-detected server address (bind was 0.0.0.0)", client_ip);
    }

    // Verify server_address matches connection source
    if (strcmp(req->server_address, client_ip) != 0) {
      log_warn("SESSION_CREATE rejected from %s: server_address '%s' does not match actual connection IP", client_ip,
               req->server_address);
      acip_send_error(transport, ERROR_INVALID_PARAM,
                      "Direct TCP sessions require server_address to match your actual IP");
      ACDS_DESTROY_TRANSPORT(transport);
      return;
    }
    log_debug("SESSION_CREATE reachability verified: %s matches connection source", req->server_address);
  }

  // Store pending session data and first key
  memcpy(&client_data->pending_session, req, sizeof(acip_session_create_t));
  memcpy(client_data->pending_session_keys[0], req->identity_pubkey, 32);
  client_data->num_pending_keys = 1;
  client_data->in_multikey_session_create = true;

  log_info("SESSION_CREATE started from %s: multi-key mode (key #1 stored, waiting for more or zero-key finalize)",
           client_ip);

  // Don't send response yet - wait for more keys or zero-key finalize
  ACDS_DESTROY_TRANSPORT(transport);
}

static void acds_on_session_lookup(const acip_session_lookup_t *req, int client_socket, const char *client_ip,
                                   void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("SESSION_LOOKUP packet from %s", client_ip);

  // Create ACIP transport for responses
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  // Rate limiting check
  if (!check_and_record_rate_limit(server->rate_limiter, client_ip, RATE_EVENT_SESSION_LOOKUP, client_socket,
                                   "SESSION_LOOKUP")) {
    return;
  }

  acip_session_info_t resp;
  memset(&resp, 0, sizeof(resp));

  // Null-terminate session string for lookup
  char session_string[49] = {0};
  size_t copy_len =
      (req->session_string_len < sizeof(session_string) - 1) ? req->session_string_len : sizeof(session_string) - 1;
  memcpy(session_string, req->session_string, copy_len);

  asciichat_error_t lookup_result = database_session_lookup(server->db, session_string, &server->config, &resp);
  if (lookup_result == ASCIICHAT_OK) {
    acip_send_session_info(transport, &resp);
    log_info("Session lookup for '%s' from %s: %s", session_string, client_ip, resp.found ? "found" : "not found");
  } else {
    acip_send_error(transport, lookup_result, "Session lookup failed");
    log_warn("Session lookup failed for %s: %s", client_ip, asciichat_error_string(lookup_result));
  }
}

static void acds_on_session_join(const acip_session_join_t *req, int client_socket, const char *client_ip,
                                 void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("SESSION_JOIN packet from %s", client_ip);

  // Create ACIP transport for responses
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  // Rate limiting check
  if (!check_and_record_rate_limit(server->rate_limiter, client_ip, RATE_EVENT_SESSION_JOIN, client_socket,
                                   "SESSION_JOIN")) {
    return;
  }

  // Cryptographic identity verification (if required)
  if (server->config.require_client_identity) {
    // Validate timestamp (5 minute window)
    if (!acds_validate_timestamp(req->timestamp, 300)) {
      log_warn("SESSION_JOIN rejected from %s: invalid timestamp (replay attack protection)", client_ip);
      acip_session_joined_t error_resp;
      memset(&error_resp, 0, sizeof(error_resp));
      error_resp.success = 0;
      error_resp.error_code = ERROR_CRYPTO_VERIFICATION;
      SAFE_STRNCPY(error_resp.error_message, "Timestamp validation failed", sizeof(error_resp.error_message));
      acip_send_session_joined(transport, &error_resp);
      return;
    }

    // Verify Ed25519 signature
    asciichat_error_t verify_result =
        acds_verify_session_join(req->identity_pubkey, req->timestamp, req->session_string, req->signature);

    if (verify_result != ASCIICHAT_OK) {
      log_warn("SESSION_JOIN rejected from %s: invalid signature (identity verification failed)", client_ip);
      acip_session_joined_t error_resp;
      memset(&error_resp, 0, sizeof(error_resp));
      error_resp.success = 0;
      error_resp.error_code = ERROR_CRYPTO_VERIFICATION;
      SAFE_STRNCPY(error_resp.error_message, "Identity signature verification failed",
                   sizeof(error_resp.error_message));
      acip_send_session_joined(transport, &error_resp);
      return;
    }

    log_debug("SESSION_JOIN signature verified from %s (pubkey: %02x%02x...)", client_ip, req->identity_pubkey[0],
              req->identity_pubkey[1]);
  }

  acip_session_joined_t resp;
  memset(&resp, 0, sizeof(resp));

  asciichat_error_t join_result = database_session_join(server->db, req, &server->config, &resp);
  if (join_result == ASCIICHAT_OK && resp.success) {
    acip_send_session_joined(transport, &resp);

    // Update client data in registry (update in place)
    void *retrieved_data = NULL;
    if (tcp_server_get_client(&server->tcp_server, client_socket, &retrieved_data) == ASCIICHAT_OK && retrieved_data) {
      acds_client_data_t *client_data = (acds_client_data_t *)retrieved_data;
      memcpy(client_data->session_id, resp.session_id, 16);
      memcpy(client_data->participant_id, resp.participant_id, 16);
      client_data->joined_session = true;
    }

    log_info("Client %s joined session (participant %02x%02x...)", client_ip, resp.participant_id[0],
             resp.participant_id[1]);
  } else {
    acip_send_session_joined(transport, &resp);
    log_warn("Session join failed for %s: %s", client_ip, resp.error_message);
  }
}

static void acds_on_session_leave(const acip_session_leave_t *req, int client_socket, const char *client_ip,
                                  void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("SESSION_LEAVE packet from %s", client_ip);

  // Create ACIP transport for responses
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  asciichat_error_t leave_result = database_session_leave(server->db, req->session_id, req->participant_id);
  if (leave_result == ASCIICHAT_OK) {
    log_info("Client %s left session", client_ip);

    // Update client data to mark as not joined
    void *retrieved_data = NULL;
    if (tcp_server_get_client(&server->tcp_server, client_socket, &retrieved_data) == ASCIICHAT_OK && retrieved_data) {
      acds_client_data_t *client_data = (acds_client_data_t *)retrieved_data;
      client_data->joined_session = false;
    }
  } else {
    acip_send_error(transport, leave_result, asciichat_error_string(leave_result));
    log_warn("Session leave failed for %s: %s", client_ip, asciichat_error_string(leave_result));
  }
}

static void acds_on_webrtc_sdp(const acip_webrtc_sdp_t *sdp, size_t payload_len, int client_socket,
                               const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("WEBRTC_SDP packet from %s", client_ip);

  // Create ACIP transport for responses
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  // Validation is done in the library handler (lib/network/acip/acds_handlers.c)
  asciichat_error_t relay_result = signaling_relay_sdp(server->db, &server->tcp_server, sdp, payload_len);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "SDP relay failed");
    log_warn("SDP relay failed from %s: %s", client_ip, asciichat_error_string(relay_result));
  }
}

static void acds_on_webrtc_ice(const acip_webrtc_ice_t *ice, size_t payload_len, int client_socket,
                               const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("WEBRTC_ICE packet from %s", client_ip);

  // Create ACIP transport for responses
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  // Validation is done in the library handler (lib/network/acip/acds_handlers.c)
  asciichat_error_t relay_result = signaling_relay_ice(server->db, &server->tcp_server, ice, payload_len);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "ICE relay failed");
    log_warn("ICE relay failed from %s: %s", client_ip, asciichat_error_string(relay_result));
  }
}

static void acds_on_discovery_ping(const void *payload, size_t payload_len, int client_socket, const char *client_ip,
                                   void *app_ctx) {
  (void)payload;
  (void)payload_len;
  (void)app_ctx;

  // Create ACIP transport for PONG response
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  // Send PONG response
  log_debug("PING from %s, sending PONG", client_ip);
  acip_send_pong(transport);

  ACDS_DESTROY_TRANSPORT(transport);
}

// Global ACIP callback structure for ACDS
static const acip_acds_callbacks_t g_acds_callbacks = {
    .on_session_create = acds_on_session_create,
    .on_session_lookup = acds_on_session_lookup,
    .on_session_join = acds_on_session_join,
    .on_session_leave = acds_on_session_leave,
    .on_webrtc_sdp = acds_on_webrtc_sdp,
    .on_webrtc_ice = acds_on_webrtc_ice,
    .on_discovery_ping = acds_on_discovery_ping,
    .app_ctx = NULL // Set dynamically to server instance
};

void *acds_client_handler(void *arg) {
  tcp_client_context_t *ctx = (tcp_client_context_t *)arg;
  if (!ctx) {
    log_error("Client handler: NULL context");
    return NULL;
  }

  acds_server_t *server = (acds_server_t *)ctx->user_data;
  socket_t client_socket = ctx->client_socket;

  // Get client IP for logging
  char client_ip[INET6_ADDRSTRLEN] = {0};
  tcp_client_context_get_ip(ctx, client_ip, sizeof(client_ip));

  log_info("Client handler started for %s", client_ip);

  // Register client in TCP server registry with allocated client data
  acds_client_data_t *client_data = SAFE_MALLOC(sizeof(acds_client_data_t), acds_client_data_t *);
  if (!client_data) {
    tcp_server_reject_client(client_socket, "Failed to allocate client data");
    SAFE_FREE(ctx);
    return NULL;
  }
  memset(client_data, 0, sizeof(*client_data));
  client_data->joined_session = false;

  if (tcp_server_add_client(&server->tcp_server, client_socket, client_data) != ASCIICHAT_OK) {
    SAFE_FREE(client_data);
    tcp_server_reject_client(client_socket, "Failed to register client in registry");
    SAFE_FREE(ctx);
    return NULL;
  }

  log_debug("Client %s registered (socket=%d, total=%zu)", client_ip, client_socket,
            tcp_server_get_client_count(&server->tcp_server));

  // TODO: Crypto handshake (when crypto support is added)
  // For now, accept plain connections

  // Main packet processing loop
  while (atomic_load(&server->tcp_server.running)) {
    packet_type_t packet_type;
    void *payload = NULL;
    size_t payload_size = 0;

    // Receive packet (blocking with system timeout)
    int result = receive_packet(client_socket, &packet_type, &payload, &payload_size);
    if (result < 0) {
      // Connection error - client disconnected
      log_info("Client %s disconnected", client_ip);
      if (payload) {
        buffer_pool_free(NULL, payload, payload_size);
      }
      break;
    }

    log_debug("Received packet type 0x%02X from %s, length=%zu", packet_type, client_ip, payload_size);

    // Multi-key session creation protocol: block non-PING/PONG/SESSION_CREATE messages
    if (client_data->in_multikey_session_create) {
      bool allowed =
          (packet_type == PACKET_TYPE_ACIP_SESSION_CREATE || packet_type == PACKET_TYPE_ACIP_DISCOVERY_PING ||
           packet_type == PACKET_TYPE_PING || packet_type == PACKET_TYPE_PONG);

      if (!allowed) {
        log_warn("Client %s sent packet type 0x%02X during multi-key session creation - only SESSION_CREATE/PING/PONG "
                 "allowed",
                 client_ip, packet_type);

        // Send error response
        acip_transport_t *error_transport = acip_tcp_transport_create(client_socket, NULL);
        if (error_transport) {
          acip_send_error(error_transport, ERROR_INVALID_PARAM,
                          "Only SESSION_CREATE/PING/PONG allowed during multi-key session creation");
          ACDS_DESTROY_TRANSPORT(error_transport);
        }

        // Free payload and continue
        if (payload) {
          buffer_pool_free(NULL, payload, payload_size);
        }
        continue;
      }
    }

    // O(1) ACIP array-based dispatch
    // Set server context for callbacks
    acip_acds_callbacks_t callbacks = g_acds_callbacks;
    callbacks.app_ctx = server;

    asciichat_error_t dispatch_result =
        acip_handle_acds_packet(NULL, packet_type, payload, payload_size, client_socket, client_ip, &callbacks);

    if (dispatch_result != ASCIICHAT_OK) {
      log_warn("ACIP handler failed for packet type 0x%02X from %s: %s", packet_type, client_ip,
               asciichat_error_string(dispatch_result));
    }

    // Free payload (allocated by receive_packet via buffer_pool_alloc)
    if (payload) {
      buffer_pool_free(NULL, payload, payload_size);
    }
  }

  // Cleanup
  tcp_server_remove_client(&server->tcp_server, client_socket);
  log_debug("Client %s unregistered (total=%zu)", client_ip, tcp_server_get_client_count(&server->tcp_server));

  socket_close(client_socket);
  SAFE_FREE(ctx);

  log_info("Client handler finished for %s", client_ip);
  return NULL;
}

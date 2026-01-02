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
 * - Session registry management
 * - SQLite persistence
 * - ACIP packet dispatch to session/signaling handlers
 */

#include "acds/server.h"
#include "acds/database.h"
#include "acds/session.h"
#include "acds/signaling.h"
#include "network/acip/acds.h"
#include "network/acip/acds_handlers.h"
#include "network/acip/client.h"
#include "network/acip/send.h"
#include "network/acip/transport.h"
#include "network/rate_limit/rate_limit.h"
#include "network/rate_limit/sqlite.h"
#include "network/errors.h"
#include "log/logging.h"
#include "platform/socket.h"
#include "network/network.h"
#include "network/tcp_server.h"
#include "util/ip.h"
#include <string.h>
#include <time.h>

/**
 * @brief Background thread for periodic rate limit cleanup
 *
 * Wakes up every 5 minutes to remove old rate limit events from the database.
 * This prevents the rate_events table from growing unbounded.
 */
static void *cleanup_thread_func(void *arg) {
  acds_server_t *server = (acds_server_t *)arg;
  if (!server) {
    return NULL;
  }

  log_info("Rate limit cleanup thread started");

  while (!atomic_load(&server->shutdown)) {
    // Sleep for 5 minutes (or until shutdown)
    for (int i = 0; i < 300 && !atomic_load(&server->shutdown); i++) {
      platform_sleep_ms(1000); // Sleep 1 second at a time for responsive shutdown
    }

    if (atomic_load(&server->shutdown)) {
      break;
    }

    // Run cleanup (delete events older than 1 hour)
    log_debug("Running rate limit cleanup...");
    asciichat_error_t result = rate_limiter_cleanup(server->rate_limiter, 3600);
    if (result != ASCIICHAT_OK) {
      log_warn("Rate limit cleanup failed");
    }
  }

  log_info("Rate limit cleanup thread exiting");
  return NULL;
}

asciichat_error_t acds_server_init(acds_server_t *server, const acds_config_t *config) {
  if (!server || !config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or config is NULL");
  }

  memset(server, 0, sizeof(*server));
  memcpy(&server->config, config, sizeof(acds_config_t));

  // Initialize session registry
  server->sessions = SAFE_MALLOC(sizeof(session_registry_t), session_registry_t *);
  if (!server->sessions) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate session registry");
  }

  asciichat_error_t result = session_registry_init(server->sessions);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(server->sessions);
    return result;
  }

  // Open database
  result = database_init(config->database_path, &server->db);
  if (result != ASCIICHAT_OK) {
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
    return result;
  }

  // Load sessions from database
  result = database_load_sessions(server->db, server->sessions);
  if (result != ASCIICHAT_OK) {
    log_warn("Failed to load sessions from database (continuing anyway)");
  }

  // Initialize rate limiter with SQLite backend
  server->rate_limiter = rate_limiter_create_sqlite(NULL); // NULL = externally managed DB
  if (!server->rate_limiter) {
    database_close(server->db);
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
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
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
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
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create worker thread pool");
  }

  // Spawn rate limit cleanup thread in worker pool
  if (thread_pool_spawn(server->worker_pool, cleanup_thread_func, server, 0, "rate_limit_cleanup") != ASCIICHAT_OK) {
    log_warn("Failed to spawn rate limit cleanup thread (continuing without cleanup)");
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

  // TODO: Wait for all client handler threads to exit

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

  // Destroy session registry
  if (server->sessions) {
    session_registry_destroy(server->sessions);
    SAFE_FREE(server->sessions);
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

  // Rate limiting check
  if (!check_and_record_rate_limit(server->rate_limiter, client_ip, RATE_EVENT_SESSION_CREATE, client_socket,
                                   "SESSION_CREATE")) {
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
  // WebRTC sessions don't need this since they use P2P mesh with STUN/TURN
  if (req->session_type == SESSION_TYPE_DIRECT_TCP) {
    // Verify that the server is actually reachable at the IP it claims
    // Compare the claimed server address with the actual connection source
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

  acip_session_created_t resp;
  memset(&resp, 0, sizeof(resp));

  asciichat_error_t create_result = session_create(server->sessions, req, &server->config, &resp);
  if (create_result == ASCIICHAT_OK) {
    // Build complete payload: fixed response + variable STUN/TURN servers
    size_t stun_size = (size_t)resp.stun_count * sizeof(stun_server_t);
    size_t turn_size = (size_t)resp.turn_count * sizeof(turn_server_t);
    size_t total_size = sizeof(resp) + stun_size + turn_size;

    uint8_t *payload = SAFE_MALLOC(total_size, uint8_t *);
    if (!payload) {
      acip_send_error(transport, ERROR_MEMORY, "Out of memory building response");
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

    log_info("Session created: %.*s (UUID: %02x%02x..., %d STUN, %d TURN servers)", resp.session_string_len,
             resp.session_string, resp.session_id[0], resp.session_id[1], resp.stun_count, resp.turn_count);

    // Save to database - look up session entry first
    rwlock_rdlock(&server->sessions->lock);
    session_entry_t *session = NULL;
    HASH_FIND(hh, server->sessions->sessions, resp.session_string, strlen(resp.session_string), session);
    if (session) {
      database_save_session(server->db, session);
    }
    rwlock_rdunlock(&server->sessions->lock);
  } else {
    // Error - send error response using proper error code
    acip_send_error(transport, create_result, "Failed to create session");
    log_warn("Session creation failed for %s: %s", client_ip, asciichat_error_string(create_result));
  }

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

  asciichat_error_t lookup_result = session_lookup(server->sessions, session_string, &server->config, &resp);
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

  asciichat_error_t join_result = session_join(server->sessions, req, &resp);
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

    // Save to database - look up session entry first
    rwlock_rdlock(&server->sessions->lock);
    session_entry_t *session_iter, *session_tmp;
    session_entry_t *joined_session = NULL;
    HASH_ITER(hh, server->sessions->sessions, session_iter, session_tmp) {
      if (memcmp(session_iter->session_id, resp.session_id, 16) == 0) {
        joined_session = session_iter;
        break;
      }
    }
    if (joined_session) {
      database_save_session(server->db, joined_session);
    }
    rwlock_rdunlock(&server->sessions->lock);
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

  asciichat_error_t leave_result = session_leave(server->sessions, req->session_id, req->participant_id);
  if (leave_result == ASCIICHAT_OK) {
    log_info("Client %s left session", client_ip);

    // Update client data to mark as not joined
    void *retrieved_data = NULL;
    if (tcp_server_get_client(&server->tcp_server, client_socket, &retrieved_data) == ASCIICHAT_OK && retrieved_data) {
      acds_client_data_t *client_data = (acds_client_data_t *)retrieved_data;
      client_data->joined_session = false;
    }

    // Save updated session to database - look up session entry first
    rwlock_rdlock(&server->sessions->lock);
    session_entry_t *session_iter, *session_tmp;
    session_entry_t *left_session = NULL;
    HASH_ITER(hh, server->sessions->sessions, session_iter, session_tmp) {
      if (memcmp(session_iter->session_id, req->session_id, 16) == 0) {
        left_session = session_iter;
        break;
      }
    }
    if (left_session) {
      database_save_session(server->db, left_session);
    }
    rwlock_rdunlock(&server->sessions->lock);
  } else {
    acip_send_error(transport, leave_result, asciichat_error_string(leave_result));
    log_warn("Session leave failed for %s: %s", client_ip, asciichat_error_string(leave_result));
  }
}

static void acds_on_webrtc_sdp(const acip_webrtc_sdp_t *sdp, int client_socket, const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("WEBRTC_SDP packet from %s", client_ip);

  // Create ACIP transport for responses
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  // Calculate payload size (header + SDP string)
  size_t payload_size = sizeof(acip_webrtc_sdp_t) + sdp->sdp_len;

  asciichat_error_t relay_result = signaling_relay_sdp(server->sessions, &server->tcp_server, sdp, payload_size);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "SDP relay failed");
    log_warn("SDP relay failed from %s: %s", client_ip, asciichat_error_string(relay_result));
  }
}

static void acds_on_webrtc_ice(const acip_webrtc_ice_t *ice, int client_socket, const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("WEBRTC_ICE packet from %s", client_ip);

  // Create ACIP transport for responses
  ACDS_CREATE_TRANSPORT(client_socket, transport);

  // Calculate payload size (header + candidate string)
  size_t payload_size = sizeof(acip_webrtc_ice_t) + ice->candidate_len;

  asciichat_error_t relay_result = signaling_relay_ice(server->sessions, &server->tcp_server, ice, payload_size);
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
        SAFE_FREE(payload);
      }
      break;
    }

    log_debug("Received packet type 0x%02X from %s, length=%zu", packet_type, client_ip, payload_size);

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

    // Free payload
    if (payload) {
      SAFE_FREE(payload);
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

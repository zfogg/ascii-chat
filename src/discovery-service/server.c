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

#include "discovery-service/server.h"
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/session.h>
#include <ascii-chat/discovery/identity.h>
#include <ascii-chat/crypto/handshake/server.h>
#include "discovery-service/signaling.h"
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/acds_handlers.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/rate_limit/rate_limit.h>
#include <ascii-chat/network/rate_limit/sqlite.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

/**
 * @brief Find or create migration context for a session
 *
 * Searches active_migrations for the given session_id. If found, returns the context.
 * If not found and create=true, creates a new entry. Returns NULL if not found and
 * create=false, or if max migrations reached.
 *
 * @param server ACDS server instance
 * @param session_id Session UUID to find
 * @param create If true, create new entry if not found
 * @return Pointer to migration context, or NULL if not found/created
 */

/**
 * @brief Get current time in nanoseconds
 * @return Current time in nanoseconds (using unified timing API)
 */
static uint64_t get_current_time_ns(void) {
  return time_get_ns();
}

static migration_context_t *find_or_create_migration(acds_server_t *server, const uint8_t session_id[16], bool create) {
  if (!server || !session_id) {
    return NULL;
  }

  // Search for existing migration
  for (size_t i = 0; i < server->num_active_migrations; i++) {
    if (memcmp(server->active_migrations[i].session_id, session_id, 16) == 0) {
      return &server->active_migrations[i];
    }
  }

  // Not found
  if (!create) {
    return NULL;
  }

  // Create new entry if space available
  if (server->num_active_migrations >= 32) {
    log_warn("Too many active migrations (max 32)");
    return NULL;
  }

  migration_context_t *ctx = &server->active_migrations[server->num_active_migrations];
  memcpy(ctx->session_id, session_id, 16);
  ctx->migration_start_ns = get_current_time_ns();
  server->num_active_migrations++;

  return ctx;
}

/**
 * @brief Monitor host migrations and elect new hosts when ready
 *
 * Called periodically to check for completed migration windows.
 * When a migration window completes, elects the best new host.
 *
 * Algorithm:
 * 1. For each active migration, check if window expired
 * 2. When expired, elect best candidate using NAT quality comparison
 * 3. Update database with new host
 * 4. Remove migration from tracking
 *
 * @param server ACDS server instance
 * @param migration_window_ms Collection window duration (milliseconds)
 */
static void monitor_host_migrations(acds_server_t *server, uint64_t migration_timeout_ms) {
  if (!server || !server->db || server->num_active_migrations == 0) {
    return;
  }

  uint64_t now = get_current_time_ns();
  uint64_t migration_timeout_ns = migration_timeout_ms * NS_PER_MS_INT; // Convert ms to ns

  size_t i = 0;
  while (i < server->num_active_migrations) {
    migration_context_t *ctx = &server->active_migrations[i];

    // Check if migration has timed out (stuck for >migration_timeout_ns)
    uint64_t elapsed_ns = now - ctx->migration_start_ns;
    if (elapsed_ns < migration_timeout_ns) {
      i++;
      continue;
    }

    uint64_t elapsed_ms = elapsed_ns / NS_PER_MS_INT; // Convert ns to ms for logging
    log_warn("Host migration timeout for session %02x%02x... (elapsed %llu ms)", ctx->session_id[0], ctx->session_id[1],
             (unsigned long long)elapsed_ms);

    // Migration timed out - mark session as failed and clear migration state
    asciichat_error_t result = database_session_clear_host(server->db, ctx->session_id);
    if (result != ASCIICHAT_OK) {
      log_warn("Failed to clear host for timed-out migration: %s", asciichat_error_string(result));
    }

    // Remove this migration from tracking (shift remaining entries down)
    if (i < server->num_active_migrations - 1) {
      memmove(&server->active_migrations[i], &server->active_migrations[i + 1],
              (server->num_active_migrations - i - 1) * sizeof(migration_context_t));
    }
    server->num_active_migrations--;
  }
}

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

  while (!atomic_load_bool(&server->shutdown)) {
    // Sleep for 5 minutes (or until shutdown)
    // Use 100ms sleep intervals for responsive shutdown on timeout
    for (int i = 0; i < 3000 && !atomic_load_bool(&server->shutdown); i++) {
      APP_CALLBACK_VOID(platform_pump_events);
      platform_sleep_ms(100); // Sleep 100ms at a time for responsive shutdown
    }

    if (atomic_load_bool(&server->shutdown)) {
      break;
    }

    // Run rate limit cleanup (delete events older than 1 hour)
    log_debug("Running rate limit cleanup...");
    asciichat_error_t result = rate_limiter_prune(server->rate_limiter, 3600);
    if (result != ASCIICHAT_OK) {
      log_warn("Rate limit cleanup failed");
    }

    // Run expired session cleanup
    log_debug("Running expired session cleanup...");
    database_session_cleanup_expired(server->db);

    // Monitor host migrations (check for completed windows)
    log_debug("Checking for completed host migrations...");
    monitor_host_migrations(server, 5000); // 5-second collection window for testing
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

  // Initialize stats
  atomic_store_u64(&server->stats.bytes_sent, 0);
  atomic_store_u64(&server->stats.bytes_received, 0);
  atomic_store_u64(&server->stats.packets_sent, 0);
  atomic_store_u64(&server->stats.packets_received, 0);
  server->stats.start_time = time(NULL);

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
  atomic_store_bool(&server->shutdown, false);
  server->worker_pool = thread_pool_create("acds_workers");
  if (!server->worker_pool) {
    log_warn("Failed to create worker thread pool");
    tcp_server_destroy(&server->tcp_server);
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
  atomic_store_bool(&server->shutdown, true);

  // Shutdown TCP server (closes listen sockets, stops accept loop)
  tcp_server_destroy(&server->tcp_server);

  // Wait for all client handler threads to exit
  size_t remaining_clients;
  int shutdown_attempts = 0;
  const int max_shutdown_attempts = 100; // 10 seconds (100 * 100ms)

  while ((remaining_clients = tcp_server_get_client_count(&server->tcp_server)) > 0 &&
         shutdown_attempts < max_shutdown_attempts) {
    log_debug("Waiting for %zu client handler threads to exit (attempt %d/%d)", remaining_clients,
              shutdown_attempts + 1, max_shutdown_attempts);
    APP_CALLBACK_VOID(platform_pump_events);
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
// ACIP Transport Helper Macros for ACDS - REMOVED
// =============================================================================
// Transport is now passed to callbacks as a parameter, no need for temp creation

// =============================================================================
// Stats Tracking Helper
// =============================================================================

static inline void acds_track_packet_sent(acds_server_t *server, size_t payload_size) {
  if (server) {
    // Packet size includes header (same as receive)
    size_t total_packet_size = payload_size + 14; // 4 (magic) + 1 (type) + 2 (length) + 4 (CRC) + 3 (client ID)
    atomic_fetch_add_u64(&server->stats.bytes_sent, total_packet_size);
    atomic_fetch_add_u64(&server->stats.packets_sent, 1);
  }
}

// =============================================================================
// ACIP Callback Wrappers for ACDS
// =============================================================================
// These callbacks are invoked by acip_handle_acds_packet() via O(1) array dispatch.
// Each callback implements: Rate Limit → Crypto Verify → Business Logic → DB Save

static void acds_on_session_create(const acip_session_create_t *req, acip_transport_t *transport, const char *client_ip,
                                   void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  acds_client_data_t *client_data = (acds_client_data_t *)transport->user_data;

  log_debug("SESSION_CREATE packet from %s", client_ip);

  if (!client_data) {
    acip_send_error(transport, ERROR_INVALID_PARAM, "Client data not found");
    return;
  }

  // === MULTI-KEY PROTOCOL: Handle key by key_index ===
  // total_keys > 0 indicates multi-key mode
  // key_index == 0 starts a new sequence; subsequent keys are appended

  if (req->total_keys > 0) {
    // Multi-key mode: total_keys tells us how many to expect
    if (req->key_index == 0) {
      // First key in sequence - initialize state
      log_info("SESSION_CREATE starting multi-key sequence from %s: expecting %u key(s)", client_ip, req->total_keys);

      // Store the first packet's session parameters
      memcpy(&client_data->pending_session, req, sizeof(acip_session_create_t));

      // Allocate for keys (should already be sized for MAX_IDENTITY_KEYS, but validate bounds)
      if (req->total_keys > MAX_IDENTITY_KEYS) {
        log_error("Too many keys: %u (max %u)", req->total_keys, MAX_IDENTITY_KEYS);
        acip_send_error(transport, ERROR_INVALID_PARAM, "Too many keys");
        return;
      }

      client_data->num_pending_keys = 0;
      client_data->in_multikey_session_create = true;
    }

    // Store this key
    if (req->key_index >= req->total_keys) {
      log_error("Invalid key_index: %u >= total_keys: %u", req->key_index, req->total_keys);
      acip_send_error(transport, ERROR_INVALID_PARAM, "Invalid key index");
      client_data->in_multikey_session_create = false;
      client_data->num_pending_keys = 0;
      return;
    }

    memcpy(client_data->pending_session_keys[req->key_index], req->identity_pubkey, 32);
    client_data->num_pending_keys = req->key_index + 1;

    log_debug("SESSION_CREATE received key %u/%u from %s", req->key_index + 1, req->total_keys, client_ip);

    // Check if this is the last key
    bool is_final_key = (req->key_index == req->total_keys - 1);
    if (!is_final_key) {
      // More keys expected - wait for next packet
      log_debug("Waiting for remaining %u key(s)", req->total_keys - client_data->num_pending_keys);
      return;
    }

    // Final key received - finalize session
    log_info("SESSION_CREATE finalize from %s: received all %zu identity key(s)", client_ip,
             client_data->num_pending_keys);

    // Auto-detect server's public IP if not provided (empty address)
    if (client_data->pending_session.server_address[0] == '\0') {
      log_info("ACDS: Auto-detecting server public IP from connection source: %s", client_ip);
      SAFE_STRNCPY(client_data->pending_session.server_address, client_ip,
                   sizeof(client_data->pending_session.server_address));
      log_info("ACDS: Auto-detected server_address='%s'", client_data->pending_session.server_address);
    }

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
      packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_CREATED, payload, total_size, 0);
      acds_track_packet_sent(server, total_size);
      SAFE_FREE(payload);

      log_info("Session created: %.*s (UUID: %02x%02x..., %zu keys, %d STUN, %d TURN servers)", resp.session_string_len,
               resp.session_string, resp.session_id[0], resp.session_id[1], client_data->num_pending_keys,
               resp.stun_count, resp.turn_count);

      // Store session/participant IDs and mark session as established
      memcpy(client_data->session_id, resp.session_id, 16);
      memcpy(client_data->participant_id, resp.participant_id, 16);
      client_data->joined_session = true;
      client_data->session_established = true; // Signal handler to exit receive loop
    } else {
      // Error - send error response using proper error code
      acip_send_error(transport, create_result, "Failed to create session");
      log_warn("Session creation failed for %s: %s", client_ip, asciichat_error_string(create_result));
    }

    // Clear multi-key state
    client_data->in_multikey_session_create = false;
    client_data->num_pending_keys = 0;

    return;
  } else {
    // Legacy single-key mode (total_keys == 0)
    log_debug("SESSION_CREATE single-key mode from %s", client_ip);
    memcpy(&client_data->pending_session, req, sizeof(acip_session_create_t));
    memcpy(client_data->pending_session_keys[0], req->identity_pubkey, 32);
    client_data->num_pending_keys = 1;
    client_data->in_multikey_session_create = false;
    log_info("SESSION_CREATE finalize from %s: %zu identity key(s)", client_ip, client_data->num_pending_keys);

    // Auto-detect server's public IP if not provided (empty address)
    if (client_data->pending_session.server_address[0] == '\0') {
      log_info("ACDS: Auto-detecting server public IP from connection source: %s", client_ip);
      SAFE_STRNCPY(client_data->pending_session.server_address, client_ip,
                   sizeof(client_data->pending_session.server_address));
      log_info("ACDS: Auto-detected server_address='%s'", client_data->pending_session.server_address);
    }

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
      packet_send_via_transport(transport, PACKET_TYPE_ACIP_SESSION_CREATED, payload, total_size, 0);
      acds_track_packet_sent(server, total_size);
      SAFE_FREE(payload);

      log_info("Session created: %.*s (UUID: %02x%02x..., %zu keys, %d STUN, %d TURN servers)", resp.session_string_len,
               resp.session_string, resp.session_id[0], resp.session_id[1], client_data->num_pending_keys,
               resp.stun_count, resp.turn_count);

      // Store session/participant IDs and mark session as established
      memcpy(client_data->session_id, resp.session_id, 16);
      memcpy(client_data->participant_id, resp.participant_id, 16);
      client_data->joined_session = true;
      client_data->session_established = true; // Signal handler to exit receive loop
    } else {
      // Error - send error response using proper error code
      acip_send_error(transport, create_result, "Failed to create session");
      log_warn("Session creation failed for %s: %s", client_ip, asciichat_error_string(create_result));
    }

    // Clear multi-key state
    client_data->in_multikey_session_create = false;
    client_data->num_pending_keys = 0;

    return;
  }

  // === MULTI-KEY PROTOCOL: Add key to pending session ===
  if (client_data->in_multikey_session_create) {
    // Subsequent SESSION_CREATE with non-zero key - add to pending keys

    // Validate we haven't exceeded max keys
    if (client_data->num_pending_keys >= MAX_IDENTITY_KEYS) {
      acip_send_error(transport, ERROR_INVALID_PARAM, "Maximum identity keys exceeded");
      return;
    }

    // Validate key is different from all existing keys
    for (size_t i = 0; i < client_data->num_pending_keys; i++) {
      if (memcmp(client_data->pending_session_keys[i], req->identity_pubkey, 32) == 0) {
        acip_send_error(transport, ERROR_INVALID_PARAM, "Duplicate identity key");
        return;
      }
    }

    // Add key to pending array
    memcpy(client_data->pending_session_keys[client_data->num_pending_keys], req->identity_pubkey, 32);
    client_data->num_pending_keys++;

    log_debug("SESSION_CREATE key #%zu from %s (pubkey: %02x%02x...)", client_data->num_pending_keys, client_ip,
              req->identity_pubkey[0], req->identity_pubkey[1]);

    // Don't send response yet - client will send more keys or zero-key to finalize
    return;
  }

  // === MULTI-KEY PROTOCOL: Start new session ===
  // First SESSION_CREATE with non-zero key - start multi-key mode

  // Rate limiting check (only on first SESSION_CREATE)
  {
    bool allowed = false;
    asciichat_error_t rate_check =
        rate_limiter_check(server->rate_limiter, client_ip, RATE_EVENT_SESSION_CREATE, NULL, &allowed);
    if (rate_check != ASCIICHAT_OK || !allowed) {
      acip_send_error(transport, ERROR_RATE_LIMITED, "Rate limit exceeded. Please try again later.");
      log_warn("Rate limit exceeded for SESSION_CREATE from %s", client_ip);
      return;
    }
    rate_limiter_record(server->rate_limiter, client_ip, RATE_EVENT_SESSION_CREATE);
  }

  // Cryptographic identity verification (if required)
  if (server->config.require_server_identity) {
    // Validate timestamp (5 minute window)
    if (!acds_validate_timestamp(req->timestamp, 300)) {
      log_warn("SESSION_CREATE rejected from %s: invalid timestamp (replay attack protection)", client_ip);
      acip_send_error(transport, ERROR_CRYPTO_VERIFICATION, "Timestamp validation failed - too old or in the future");
      return;
    }

    // Verify Ed25519 signature
    asciichat_error_t verify_result = acds_verify_session_create(
        req->identity_pubkey, req->timestamp, req->capabilities, req->max_participants, req->signature);

    if (verify_result != ASCIICHAT_OK) {
      log_warn("SESSION_CREATE rejected from %s: invalid signature (identity verification failed)", client_ip);
      acip_send_error(transport, ERROR_CRYPTO_VERIFICATION, "Identity signature verification failed");
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
}

static void acds_on_session_lookup(const acip_session_lookup_t *req, acip_transport_t *transport, const char *client_ip,
                                   void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_debug("SESSION_LOOKUP packet from %s", client_ip);

  // Rate limiting check
  {
    bool allowed = false;
    asciichat_error_t rate_check =
        rate_limiter_check(server->rate_limiter, client_ip, RATE_EVENT_SESSION_LOOKUP, NULL, &allowed);
    if (rate_check != ASCIICHAT_OK || !allowed) {
      acip_send_error(transport, ERROR_RATE_LIMITED, "Rate limit exceeded. Please try again later.");
      log_warn("Rate limit exceeded for SESSION_LOOKUP from %s", client_ip);
      return;
    }
    rate_limiter_record(server->rate_limiter, client_ip, RATE_EVENT_SESSION_LOOKUP);
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

static void acds_on_session_join(const acip_session_join_t *req, acip_transport_t *transport, const char *client_ip,
                                 void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  acds_client_data_t *client_data = (acds_client_data_t *)transport->user_data;

  log_debug("SESSION_JOIN packet from %s", client_ip);

  // Rate limiting check
  {
    bool allowed = false;
    asciichat_error_t rate_check =
        rate_limiter_check(server->rate_limiter, client_ip, RATE_EVENT_SESSION_JOIN, NULL, &allowed);
    if (rate_check != ASCIICHAT_OK || !allowed) {
      acip_send_error(transport, ERROR_RATE_LIMITED, "Rate limit exceeded. Please try again later.");
      log_warn("Rate limit exceeded for SESSION_JOIN from %s", client_ip);
      return;
    }
    rate_limiter_record(server->rate_limiter, client_ip, RATE_EVENT_SESSION_JOIN);
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

    // Update client data (accessed via transport->user_data)
    if (client_data) {
      memcpy(client_data->session_id, resp.session_id, 16);
      memcpy(client_data->participant_id, resp.participant_id, 16);
      client_data->joined_session = true;
      client_data->session_established = true; // Signal handler to exit receive loop
    }

    log_info("Client %s joined session (participant %02x%02x...)", client_ip, resp.participant_id[0],
             resp.participant_id[1]);

    // Broadcast PARTICIPANT_JOINED notification to all other participants in the session
    acip_participant_joined_t participant_joined;
    memset(&participant_joined, 0, sizeof(participant_joined));
    memcpy(participant_joined.session_id, resp.session_id, 16);
    memcpy(participant_joined.new_participant_id, resp.participant_id, 16);
    memcpy(participant_joined.new_participant_pubkey, req->identity_pubkey, 32);
    // peer_count is the number of OTHER participants; total = peer_count + 1 (the new one)
    participant_joined.current_participant_count = resp.peer_count + 1;

    asciichat_error_t broadcast_result =
        signaling_broadcast(server->db, &server->tcp_server, resp.session_id, PACKET_TYPE_ACIP_PARTICIPANT_JOINED,
                            &participant_joined, sizeof(participant_joined), resp.participant_id);

    if (broadcast_result != ASCIICHAT_OK) {
      log_warn("Failed to broadcast PARTICIPANT_JOINED for session %02x%02x...: %d", resp.session_id[0],
               resp.session_id[1], broadcast_result);
    } else {
      log_debug("Broadcasted PARTICIPANT_JOINED to other participants in session");
    }
  } else {
    acip_send_session_joined(transport, &resp);
    log_warn("Session join failed for %s: %s", client_ip, resp.error_message);
  }
}

static void acds_on_session_leave(const acip_session_leave_t *req, acip_transport_t *transport, const char *client_ip,
                                  void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  acds_client_data_t *client_data = (acds_client_data_t *)transport->user_data;

  log_debug("SESSION_LEAVE packet from %s", client_ip);

  asciichat_error_t leave_result = database_session_leave(server->db, req->session_id, req->participant_id);
  if (leave_result == ASCIICHAT_OK) {
    log_info("Client %s left session", client_ip);

    // Update client data to mark as not joined
    if (client_data) {
      client_data->joined_session = false;
    }
  } else {
    acip_send_error(transport, leave_result, asciichat_error_string(leave_result));
    log_warn("Session leave failed for %s: %s", client_ip, asciichat_error_string(leave_result));
  }
}

static void acds_on_webrtc_sdp(const acip_webrtc_sdp_t *sdp, size_t payload_len, acip_transport_t *transport,
                               const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  (void)transport; // Transport not needed for relay

  log_debug("WEBRTC_SDP packet from %s", client_ip);

  // Validation is done in the library handler (lib/network/acip/acds_handlers.c)
  asciichat_error_t relay_result = signaling_relay_sdp(server->db, &server->tcp_server, sdp, payload_len);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "SDP relay failed");
    log_warn("SDP relay failed from %s: %s", client_ip, asciichat_error_string(relay_result));
  }
}

static void acds_on_webrtc_ice(const acip_webrtc_ice_t *ice, size_t payload_len, acip_transport_t *transport,
                               const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  (void)transport; // Transport not needed for relay

  log_debug("WEBRTC_ICE packet from %s", client_ip);

  // Validation is done in the library handler (lib/network/acip/acds_handlers.c)
  asciichat_error_t relay_result = signaling_relay_ice(server->db, &server->tcp_server, ice, payload_len);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "ICE relay failed");
    log_warn("ICE relay failed from %s: %s", client_ip, asciichat_error_string(relay_result));
  }
}

static void acds_on_network_quality(const void *payload, size_t payload_len, acip_transport_t *transport,
                                    const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  (void)transport; // Transport not needed for relay

  log_info("★ ACDS_ON_NETWORK_QUALITY: received from %s, payload_len=%zu", client_ip, payload_len);

  // Cast payload to acip_nat_quality_t for relay
  const acip_nat_quality_t *quality = (const acip_nat_quality_t *)payload;

  log_debug("NETWORK_QUALITY: session=%02x%02x..., participant=%02x%02x...", quality->session_id[0],
            quality->session_id[1], quality->participant_id[0], quality->participant_id[1]);

  // Validation and relay
  asciichat_error_t relay_result =
      signaling_relay_network_quality(server->db, &server->tcp_server, quality, payload_len);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "NETWORK_QUALITY relay failed");
    log_warn("NETWORK_QUALITY relay failed from %s: %s", client_ip, asciichat_error_string(relay_result));
  } else {
    log_info("★ ACDS_ON_NETWORK_QUALITY: relay completed successfully for %s", client_ip);
  }
}

static void acds_on_host_announcement(const acip_host_announcement_t *announcement, acip_transport_t *transport,
                                      const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_info("HOST_ANNOUNCEMENT from %s: host_id=%02x%02x..., address=%s:%u, conn_type=%d", client_ip,
           announcement->host_id[0], announcement->host_id[1], announcement->host_address, announcement->host_port,
           announcement->connection_type);

  // Update session host in database
  asciichat_error_t result =
      database_session_update_host(server->db, announcement->session_id, announcement->host_id,
                                   announcement->host_address, announcement->host_port, announcement->connection_type);

  if (result != ASCIICHAT_OK) {
    acip_send_error(transport, result, "Failed to update session host");
    log_warn("HOST_ANNOUNCEMENT failed from %s: %s", client_ip, asciichat_error_string(result));
    return;
  }

  // TODO: Broadcast HOST_DESIGNATED to all participants in the session
  // This requires iterating connected clients and sending to those in the same session
  // For now, new participants will get host info when they join

  log_info("Session host updated via HOST_ANNOUNCEMENT from %s", client_ip);
}

static void acds_on_host_lost(const acip_host_lost_t *host_lost, acip_transport_t *transport, const char *client_ip,
                              void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_info("HOST_LOST from %s: session=%02x%02x..., participant=%02x%02x..., last_host=%02x%02x..., reason=%u",
           client_ip, host_lost->session_id[0], host_lost->session_id[1], host_lost->participant_id[0],
           host_lost->participant_id[1], host_lost->last_host_id[0], host_lost->last_host_id[1],
           host_lost->disconnect_reason);

  // Start host migration (set in_migration flag in database for bookkeeping)
  asciichat_error_t result = database_session_start_migration(server->db, host_lost->session_id);
  if (result != ASCIICHAT_OK) {
    acip_send_error(transport, result, "Failed to start host migration");
    log_warn("HOST_LOST failed from %s: %s", client_ip, asciichat_error_string(result));
    return;
  }

  // Track migration for timeout detection
  // (Host already elected future host 5 minutes ago; participants will failover to pre-elected host)
  migration_context_t *migration = find_or_create_migration(server, host_lost->session_id, true);
  if (!migration) {
    acip_send_error(transport, ERROR_MEMORY, "Failed to track migration");
    log_warn("HOST_LOST: Failed to create migration context from %s", client_ip);
    return;
  }

  log_info("Migration tracking started for session %02x%02x... (participant %02x%02x...)", host_lost->session_id[0],
           host_lost->session_id[1], host_lost->participant_id[0], host_lost->participant_id[1]);

  // NOTE: No candidate collection needed - future host was pre-elected 5 minutes ago.
  // Participants already know who the new host is from the last FUTURE_HOST_ELECTED broadcast.
  // They will instantly failover to the pre-elected host.
  // ACDS just tracks migration timeout for cleanup.
}

// Global ACIP callback structure for ACDS
static const acip_acds_callbacks_t g_acds_callbacks = {
    .on_session_create = acds_on_session_create,
    .on_session_lookup = acds_on_session_lookup,
    .on_session_join = acds_on_session_join,
    .on_session_leave = acds_on_session_leave,
    .on_webrtc_sdp = acds_on_webrtc_sdp,
    .on_webrtc_ice = acds_on_webrtc_ice,
    .on_host_announcement = acds_on_host_announcement,
    .on_host_lost = acds_on_host_lost,
    .on_network_quality = acds_on_network_quality,
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
  client_data->handshake_complete = false;

  // Initialize crypto handshake context with client IP name
  char crypto_name[96];
  SAFE_SNPRINTF(crypto_name, sizeof(crypto_name), "crypto_discovery_client_%s", client_ip);
  asciichat_error_t handshake_result = crypto_handshake_init(crypto_name, &client_data->handshake_ctx, true);
  if (handshake_result != ASCIICHAT_OK) {
    log_error("Failed to initialize crypto handshake for client %s", client_ip);
    SAFE_FREE(client_data);
    tcp_server_reject_client(client_socket, "Failed to initialize crypto handshake");
    SAFE_FREE(ctx);
    return NULL;
  }

  // Set server identity keys for handshake
  client_data->handshake_ctx.server_public_key.type = KEY_TYPE_ED25519;
  client_data->handshake_ctx.server_private_key.type = KEY_TYPE_ED25519;
  memcpy(client_data->handshake_ctx.server_public_key.key, server->identity_public, 32);
  memcpy(client_data->handshake_ctx.server_private_key.key.ed25519, server->identity_secret, 64);

  if (tcp_server_add_client(&server->tcp_server, client_socket, client_data) != ASCIICHAT_OK) {
    SAFE_FREE(client_data);
    tcp_server_reject_client(client_socket, "Failed to register client in registry");
    SAFE_FREE(ctx);
    return NULL;
  }

  log_debug("Client %s registered (socket=%d, total=%zu)", client_ip, client_socket,
            tcp_server_get_client_count(&server->tcp_server));

  // Create transport for handshake and subsequent client communication
  acip_transport_t *transport = acip_tcp_transport_create("acds_tcp_client", client_socket, NULL);
  if (!transport) {
    log_error("Failed to create TCP transport for client %s", client_ip);
    tcp_server_remove_client(&server->tcp_server, client_socket);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Perform crypto handshake (three-step process)
  log_debug("Performing crypto handshake with client %s", client_ip);

  // Step -1: Receive and discard protocol version from client
  {
    packet_type_t pkt_type;
    void *pkt_data = NULL;
    size_t pkt_len = 0;
    void *alloc_buffer = NULL;

    handshake_result = packet_receive_via_transport(transport, &pkt_type, &pkt_data, &pkt_len, &alloc_buffer);
    if (handshake_result != ASCIICHAT_OK) {
      log_warn("Failed to receive PROTOCOL_VERSION from client %s", client_ip);
      acip_transport_destroy(transport);
      tcp_server_remove_client(&server->tcp_server, client_socket);
      SAFE_FREE(ctx);
      return NULL;
    }

    if (pkt_type != PACKET_TYPE_PROTOCOL_VERSION) {
      log_warn("Expected PROTOCOL_VERSION, got packet type %u from client %s", pkt_type, client_ip);
      buffer_pool_free(NULL, alloc_buffer, 0);
      acip_transport_destroy(transport);
      tcp_server_remove_client(&server->tcp_server, client_socket);
      SAFE_FREE(ctx);
      return NULL;
    }

    buffer_pool_free(NULL, alloc_buffer, 0);
    log_debug("Received PROTOCOL_VERSION from client %s", client_ip);
  }

  // Step 0: Send crypto parameters
  handshake_result = crypto_handshake_server_send_parameters(&client_data->handshake_ctx, transport);
  if (handshake_result != ASCIICHAT_OK) {
    log_warn("Crypto parameters send failed for client %s", client_ip);
    acip_transport_destroy(transport);
    tcp_server_remove_client(&server->tcp_server, client_socket);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Step 1: Start handshake (send server key, receive client key)
  handshake_result = crypto_handshake_server_start(&client_data->handshake_ctx, transport);
  if (handshake_result != ASCIICHAT_OK) {
    log_warn("Crypto handshake start failed for client %s", client_ip);
    acip_transport_destroy(transport);
    tcp_server_remove_client(&server->tcp_server, client_socket);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Step 2: Authentication challenge (if required)
  {
    packet_type_t packet_type;
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    int recv_result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
    if (recv_result != ASCIICHAT_OK) {
      log_warn("Failed to receive KEY_EXCHANGE_RESPONSE from client %s", client_ip);
      acip_transport_destroy(transport);
      tcp_server_remove_client(&server->tcp_server, client_socket);
      SAFE_FREE(ctx);
      return NULL;
    }
    handshake_result = crypto_handshake_server_auth_challenge(&client_data->handshake_ctx, transport, packet_type,
                                                              payload, payload_len);
    buffer_pool_free(NULL, payload, payload_len);
  }
  if (handshake_result != ASCIICHAT_OK) {
    log_warn("Crypto handshake auth challenge failed for client %s", client_ip);
    acip_transport_destroy(transport);
    tcp_server_remove_client(&server->tcp_server, client_socket);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Step 3: Complete handshake (verify and finalize) - skip if already complete
  if (client_data->handshake_ctx.state != CRYPTO_HANDSHAKE_READY) {
    packet_type_t packet_type = 0;
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    int recv_result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
    if (recv_result != ASCIICHAT_OK) {
      log_warn("Failed to receive AUTH_RESPONSE from client %s", client_ip);
      acip_transport_destroy(transport);
      tcp_server_remove_client(&server->tcp_server, client_socket);
      SAFE_FREE(ctx);
      return NULL;
    }
    handshake_result =
        crypto_handshake_server_complete(&client_data->handshake_ctx, transport, packet_type, payload, payload_len);
    buffer_pool_free(NULL, payload, payload_len);
    if (handshake_result != ASCIICHAT_OK) {
      log_warn("Crypto handshake complete failed for client %s", client_ip);
      acip_transport_destroy(transport);
      tcp_server_remove_client(&server->tcp_server, client_socket);
      SAFE_FREE(ctx);
      return NULL;
    }
  } else {
    log_debug("Crypto handshake already complete from auth_challenge step for client %s", client_ip);
  }

  client_data->handshake_complete = true;
  log_info("Crypto handshake complete for client %s", client_ip);

  // Store transport and registry ID in client data, and link back from transport
  client_data->transport = transport;
  client_data->registry_id = client_socket;
  transport->user_data = client_data;

  // Main packet processing loop
  uint64_t last_packet_time_ns = time_get_ns();
  const uint64_t idle_disconnect_ns = 60ULL * NS_PER_SEC_INT; // Disconnect after 60s idle

  while (atomic_load_bool(&server->tcp_server.running)) {
    packet_type_t packet_type;
    void *payload = NULL;
    size_t payload_size = 0;

    // Receive packet (blocking with system timeout)
    int result = receive_packet(client_socket, &packet_type, &payload, &payload_size);

    if (result >= 0) {
      last_packet_time_ns = time_get_ns();
      log_info("★ ACDS_CLIENT_HANDLER: Received packet type=%u from %s, payload_size=%zu", packet_type, client_ip,
               payload_size);

      // Track network stats (TCP packets)
      // Packet size includes header (6 bytes: magic 4 + type 1 + length 2 + CRC 4)
      size_t total_packet_size = payload_size + 14; // 4 (magic) + 1 (type) + 2 (length) + 4 (CRC) + 3 (client ID)
      atomic_fetch_add_u64(&server->stats.bytes_received, total_packet_size);
      atomic_fetch_add_u64(&server->stats.packets_received, 1);
    }

    if (result < 0) {
      // Check error context to distinguish timeout from actual disconnect
      asciichat_error_context_t err_ctx;
      bool has_context = HAS_ERRNO(&err_ctx);

      // Check if this is a timeout (non-fatal) or actual disconnect (fatal)
      asciichat_error_t error = GET_ERRNO();
      if (error == ERROR_NETWORK_TIMEOUT ||
          (error == ERROR_NETWORK && has_context && strstr(err_ctx.context_message, "timed out") != NULL)) {
        // Check if client has been idle too long (abrupt disconnect without FIN)
        uint64_t idle_ns = time_get_ns() - last_packet_time_ns;
        if (idle_ns >= idle_disconnect_ns) {
          log_info("Client %s: idle for %llu seconds, disconnecting", client_ip,
                   (unsigned long long)(idle_ns / NS_PER_SEC_INT));
          if (payload) {
            buffer_pool_free(NULL, payload, payload_size);
          }
          break;
        }
        log_debug("Client %s: receive timeout, continuing to wait for packets", client_ip);
        if (payload) {
          buffer_pool_free(NULL, payload, payload_size);
        }
        continue;
      }

      // Actual disconnect or fatal error
      log_info("Client %s disconnected", client_ip);
      if (payload) {
        buffer_pool_free(NULL, payload, payload_size);
      }
      break;
    }

    log_debug("Received packet type 0x%02X from %s, length=%zu", packet_type, client_ip, payload_size);

    // Multi-key session creation protocol: block non-PING/PONG/SESSION_CREATE messages
    if (client_data->in_multikey_session_create) {
      bool allowed = (packet_type == PACKET_TYPE_ACIP_SESSION_CREATE || packet_type == PACKET_TYPE_PING ||
                      packet_type == PACKET_TYPE_PONG);

      if (!allowed) {
        log_warn("Client %s sent packet type 0x%02X during multi-key session creation - only SESSION_CREATE/PING/PONG "
                 "allowed",
                 client_ip, packet_type);

        // Send error response using persistent transport
        acip_send_error(transport, ERROR_INVALID_PARAM,
                        "Only SESSION_CREATE/PING/PONG allowed during multi-key session creation");

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
        acip_handle_acds_packet(transport, packet_type, payload, payload_size, client_ip, &callbacks);

    if (dispatch_result != ASCIICHAT_OK) {
      log_warn("ACIP handler failed for packet type 0x%02X from %s: %s", packet_type, client_ip,
               asciichat_error_string(dispatch_result));
    }

    // Free payload (allocated by receive_packet via buffer_pool_alloc)
    if (payload) {
      buffer_pool_free(NULL, payload, payload_size);
    }

    // Keep handler running after session_established
    // Handler continues to receive and relay WebRTC signaling packets (SDP, ICE)
    // Broadcasts don't go through this socket - they use tcp_server_foreach_client() to write directly
  }

  // Cleanup - handler always cleans up since it runs until connection closes
  tcp_server_remove_client(&server->tcp_server, client_socket);
  log_debug("Client %s unregistered (total=%zu)", client_ip, tcp_server_get_client_count(&server->tcp_server));

  // Destroy transport before closing socket
  acip_transport_destroy(transport);
  socket_close(client_socket);

  SAFE_FREE(ctx);

  log_info("Client handler finished for %s", client_ip);
  return NULL;
}

// =============================================================================
// WebSocket Client Handler
// =============================================================================

void *acds_websocket_client_handler(void *arg) {
  websocket_client_context_t *ctx = (websocket_client_context_t *)arg;
  if (!ctx) {
    log_error("WebSocket client handler: NULL context");
    return NULL;
  }

  acds_server_t *server = (acds_server_t *)ctx->user_data;
  acip_transport_t *transport = ctx->transport;
  char *client_ip = ctx->client_ip;

  log_info("WebSocket client handler started for %s", client_ip);

  // Allocate client_data
  acds_client_data_t *client_data = SAFE_CALLOC(1, sizeof(acds_client_data_t), acds_client_data_t *);
  if (!client_data) {
    log_error("Failed to allocate client data for WebSocket client %s", client_ip);
    SAFE_FREE(ctx);
    return NULL;
  }

  client_data->transport = transport;
  client_data->joined_session = false;
  client_data->handshake_complete = false;
  transport->user_data = client_data;

  // Initialize crypto handshake context with client IP name
  char crypto_name[96];
  SAFE_SNPRINTF(crypto_name, sizeof(crypto_name), "crypto_discovery_websocket_%s", client_ip);
  asciichat_error_t hs_init_result = crypto_handshake_init(crypto_name, &client_data->handshake_ctx, true);
  if (hs_init_result != ASCIICHAT_OK) {
    log_error("Failed to initialize crypto handshake for WebSocket client %s", client_ip);
    SAFE_FREE(client_data);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Set server identity keys for handshake
  client_data->handshake_ctx.server_public_key.type = KEY_TYPE_ED25519;
  client_data->handshake_ctx.server_private_key.type = KEY_TYPE_ED25519;
  memcpy(client_data->handshake_ctx.server_public_key.key, server->identity_public, 32);
  memcpy(client_data->handshake_ctx.server_private_key.key.ed25519, server->identity_secret, 64);

  // Generate synthetic socket ID for tcp_server registry
  static _Atomic int g_ws_synthetic_id = INT_MAX;
  int synthetic_id = atomic_fetch_sub(&g_ws_synthetic_id, 1);
  client_data->registry_id = synthetic_id;

  // Register in tcp_server with synthetic ID (so callbacks can find client_data)
  if (tcp_server_add_client(&server->tcp_server, synthetic_id, client_data) != ASCIICHAT_OK) {
    log_error("Failed to register WebSocket client %s in registry", client_ip);
    SAFE_FREE(client_data);
    SAFE_FREE(ctx);
    return NULL;
  }

  log_debug("WebSocket client %s registered (synthetic_id=%d, total=%zu)", client_ip, synthetic_id,
            tcp_server_get_client_count(&server->tcp_server));

  // Determine if client authentication is required by checking server configuration
  bool auth_required = server->config.require_client_identity;
  ctx->auth_required = auth_required;

  // Skip crypto handshake only for wss:// connections WITHOUT authentication requirement
  // For plain ws://, always do crypto handshake
  // For wss:// WITH auth requirement, always do crypto handshake
  bool skip_handshake = ctx->is_secure && !auth_required;

  if (skip_handshake) {
    client_data->handshake_complete = true;
    log_info("WebSocket connection from %s - skipping crypto handshake (TLS encryption used, no auth required)",
             client_ip);
  } else {
    log_info("WebSocket connection from %s - proceeding with crypto handshake%s", client_ip,
             auth_required ? " (client authentication required)" : "");

    // Perform crypto handshake (three-step process)
    log_debug("Performing crypto handshake with WebSocket client %s", client_ip);

    // Step -1: Receive and discard protocol version from client
    {
      packet_type_t pkt_type;
      void *pkt_data = NULL;
      size_t pkt_len = 0;
      void *alloc_buffer = NULL;

      asciichat_error_t hs_result =
          packet_receive_via_transport(transport, &pkt_type, &pkt_data, &pkt_len, &alloc_buffer);
      if (hs_result != ASCIICHAT_OK) {
        log_warn("Failed to receive PROTOCOL_VERSION from WebSocket client %s", client_ip);
        buffer_pool_free(NULL, alloc_buffer, 0);
        tcp_server_remove_client(&server->tcp_server, synthetic_id);
        SAFE_FREE(ctx);
        return NULL;
      }

      if (pkt_type != PACKET_TYPE_PROTOCOL_VERSION) {
        log_warn("Expected PROTOCOL_VERSION, got packet type %u from WebSocket client %s", pkt_type, client_ip);
        buffer_pool_free(NULL, alloc_buffer, 0);
        tcp_server_remove_client(&server->tcp_server, synthetic_id);
        SAFE_FREE(ctx);
        return NULL;
      }

      buffer_pool_free(NULL, alloc_buffer, 0);
      log_debug("Received PROTOCOL_VERSION from WebSocket client %s", client_ip);
    }

    // Step 0: Send crypto parameters
    asciichat_error_t hs_result = crypto_handshake_server_send_parameters(&client_data->handshake_ctx, transport);
    if (hs_result != ASCIICHAT_OK) {
      log_warn("Crypto parameters send failed for WebSocket client %s", client_ip);
      tcp_server_remove_client(&server->tcp_server, synthetic_id);
      SAFE_FREE(ctx);
      return NULL;
    }

    // Step 1: Start handshake (send server key, receive client key)
    hs_result = crypto_handshake_server_start(&client_data->handshake_ctx, transport);
    if (hs_result != ASCIICHAT_OK) {
      log_warn("Crypto handshake start failed for WebSocket client %s", client_ip);
      tcp_server_remove_client(&server->tcp_server, synthetic_id);
      SAFE_FREE(ctx);
      return NULL;
    }

    // Step 2: Authentication challenge (if required)
    {
      packet_type_t pkt_type;
      void *pkt_data = NULL;
      size_t pkt_len = 0;
      void *alloc_buffer = NULL;

      hs_result = packet_receive_via_transport(transport, &pkt_type, &pkt_data, &pkt_len, &alloc_buffer);
      if (hs_result != ASCIICHAT_OK) {
        log_warn("Failed to receive KEY_EXCHANGE_RESPONSE from WebSocket client %s", client_ip);
        buffer_pool_free(NULL, alloc_buffer, 0);
        tcp_server_remove_client(&server->tcp_server, synthetic_id);
        SAFE_FREE(ctx);
        return NULL;
      }

      hs_result =
          crypto_handshake_server_auth_challenge(&client_data->handshake_ctx, transport, pkt_type, pkt_data, pkt_len);
      buffer_pool_free(NULL, alloc_buffer, 0);
    }
    if (hs_result != ASCIICHAT_OK) {
      log_warn("Crypto handshake auth challenge failed for WebSocket client %s", client_ip);
      tcp_server_remove_client(&server->tcp_server, synthetic_id);
      SAFE_FREE(ctx);
      return NULL;
    }

    // Step 3: Complete handshake (verify and finalize) - skip if already complete
    if (client_data->handshake_ctx.state != CRYPTO_HANDSHAKE_READY) {
      packet_type_t pkt_type = 0;
      void *pkt_data = NULL;
      size_t pkt_len = 0;
      void *alloc_buffer = NULL;

      hs_result = packet_receive_via_transport(transport, &pkt_type, &pkt_data, &pkt_len, &alloc_buffer);
      if (hs_result != ASCIICHAT_OK) {
        log_warn("Failed to receive AUTH_RESPONSE from WebSocket client %s", client_ip);
        buffer_pool_free(NULL, alloc_buffer, 0);
        tcp_server_remove_client(&server->tcp_server, synthetic_id);
        SAFE_FREE(ctx);
        return NULL;
      }

      hs_result = crypto_handshake_server_complete(&client_data->handshake_ctx, transport, pkt_type, pkt_data, pkt_len);
      buffer_pool_free(NULL, alloc_buffer, 0);
      if (hs_result != ASCIICHAT_OK) {
        log_warn("Crypto handshake complete failed for WebSocket client %s", client_ip);
        tcp_server_remove_client(&server->tcp_server, synthetic_id);
        SAFE_FREE(ctx);
        return NULL;
      }
    }

    client_data->handshake_complete = true;
    log_debug("Crypto handshake completed for WebSocket client %s", client_ip);
  }

  // Main packet processing loop using transport recv
  while (atomic_load_bool(&server->tcp_server.running)) {
    void *recv_buffer = NULL;
    size_t recv_len = 0;
    void *alloc_buffer = NULL;

    asciichat_error_t recv_result = acip_transport_recv(transport, &recv_buffer, &recv_len, &alloc_buffer);
    if (recv_result != ASCIICHAT_OK) {
      // Check if this is a timeout or disconnect
      if (recv_result == ERROR_NETWORK_TIMEOUT) {
        log_debug("WebSocket client %s: receive timeout, continuing to wait for packets", client_ip);
        if (alloc_buffer) {
          buffer_pool_free(NULL, alloc_buffer, 0);
        }
        continue;
      }

      // Actual disconnect or fatal error
      log_info("WebSocket client %s disconnected", client_ip);
      if (alloc_buffer) {
        buffer_pool_free(NULL, alloc_buffer, 0);
      }
      break;
    }

    // Parse packet header from received data
    if (recv_len < sizeof(packet_header_t)) {
      log_warn("WebSocket client %s: received packet too small (%zu bytes)", client_ip, recv_len);
      if (alloc_buffer) {
        buffer_pool_free(NULL, alloc_buffer, 0);
      }
      continue;
    }

    const packet_header_t *header = (const packet_header_t *)recv_buffer;
    packet_type_t packet_type = (packet_type_t)NET_TO_HOST_U16(header->type);
    size_t payload_size = NET_TO_HOST_U32(header->length);
    const void *payload = (const uint8_t *)recv_buffer + sizeof(packet_header_t);

    // Validate payload size
    if (recv_len < sizeof(packet_header_t) + payload_size) {
      log_warn("WebSocket client %s: packet size mismatch (expected %zu, got %zu)", client_ip,
               sizeof(packet_header_t) + payload_size, recv_len);
      if (alloc_buffer) {
        buffer_pool_free(NULL, alloc_buffer, 0);
      }
      continue;
    }

    log_debug("Received packet type 0x%02X from WebSocket client %s, length=%zu", packet_type, client_ip, payload_size);

    // Multi-key session creation protocol: block non-PING/PONG/SESSION_CREATE messages
    if (client_data->in_multikey_session_create) {
      bool allowed = (packet_type == PACKET_TYPE_ACIP_SESSION_CREATE || packet_type == PACKET_TYPE_PING ||
                      packet_type == PACKET_TYPE_PONG);

      if (!allowed) {
        log_warn("WebSocket client %s sent packet type 0x%02X during multi-key session creation - only "
                 "SESSION_CREATE/PING/PONG allowed",
                 client_ip, packet_type);
        acip_send_error(transport, ERROR_INVALID_PARAM,
                        "Only SESSION_CREATE/PING/PONG allowed during multi-key session creation");
        if (alloc_buffer) {
          buffer_pool_free(NULL, alloc_buffer, 0);
        }
        continue;
      }
    }

    // O(1) ACIP array-based dispatch
    acip_acds_callbacks_t callbacks = g_acds_callbacks;
    callbacks.app_ctx = server;

    asciichat_error_t dispatch_result =
        acip_handle_acds_packet(transport, packet_type, payload, payload_size, client_ip, &callbacks);

    if (dispatch_result != ASCIICHAT_OK) {
      log_warn("ACIP handler failed for packet type 0x%02X from WebSocket client %s: %s", packet_type, client_ip,
               asciichat_error_string(dispatch_result));
    }

    if (alloc_buffer) {
      buffer_pool_free(NULL, alloc_buffer, 0);
    }
  }

  // Cleanup
  tcp_server_remove_client(&server->tcp_server, synthetic_id);
  log_debug("WebSocket client %s unregistered (total=%zu)", client_ip,
            tcp_server_get_client_count(&server->tcp_server));

  // Transport is owned by WebSocket server, don't destroy it here

  log_info("WebSocket client handler finished for %s", client_ip);

  SAFE_FREE(ctx);

  return NULL;
}

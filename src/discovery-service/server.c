#include <ascii-chat/otel.h>
#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/stats/runtime.h>
/* Discovery application state, session persistence, and ACIP callbacks.
 * Connection establishment and execution belong to the shared network runtime. */

#include "discovery-service/server.h"
#include "common/session/server_like.h"
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
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include <string.h>
#include <ascii-chat/network/packet/decoder.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/crypto/handshake/server.h>

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
    bool still_migrating = database_session_is_migration_ready(server->db, ctx->session_id, 0);
    asciichat_error_t result =
        still_migrating ? database_session_clear_host(server->db, ctx->session_id) : ASCIICHAT_OK;
    if (still_migrating)
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MIGRATION_FAILURES, 1);
    if (result != ASCIICHAT_OK) {
      log_warn("Failed to clear host for timed-out migration: %s", asciichat_error_format(NULL, result));
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
typedef struct acds_handoff_entry {
  consensus_handoff_t state;
  struct acds_handoff_entry *next;
} acds_handoff_entry_t;

static asciichat_error_t acds_handoff_tick(acds_server_t *, consensus_handoff_t *);

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
      mutex_lock(&server->handoff_mutex);
      for (acds_handoff_entry_t *entry = server->handoffs; entry; entry = entry->next) {
        asciichat_error_t tick_result = acds_handoff_tick(server, &entry->state);
        if (tick_result != ASCIICHAT_OK)
          log_warn("Periodic handoff failed %s", asciichat_error_format(NULL, tick_result));
      }
      mutex_unlock(&server->handoff_mutex);
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
  ASCII_OTEL_SCOPE(DISCOVERY_INIT);
  if (!server || !config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "server or config is NULL");
  }

  memset(server, 0, sizeof(*server));
  asciichat_error_t lock_result = mutex_init(&server->handoff_mutex, "acds_handoff");
  if (lock_result != ASCIICHAT_OK)
    return lock_result;
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

  // Get TCP server from server_like (it owns the lifecycle)
  server->network_server = session_server_like_get_network_server();
  if (!server->network_server) {
    rate_limiter_destroy(server->rate_limiter);
    database_close(server->db);
    return SET_ERRNO(ERROR_INVALID_STATE, "TCP server not available from server_like");
  }

  // Initialize background worker thread pool
  atomic_store_bool(&server->shutdown, false);
  server->worker_pool = thread_pool_create("acds_workers");
  if (!server->worker_pool) {
    log_warn("Failed to create worker thread pool");
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

void acds_server_shutdown(acds_server_t *server) {
  ASCII_OTEL_SCOPE(DISCOVERY_SHUTDOWN);
  if (!server) {
    return;
  }

  // Signal shutdown to worker threads
  atomic_store_bool(&server->shutdown, true);

  // Stop and destroy worker thread pool (cleanup thread, etc.)
  if (server->worker_pool) {
    thread_pool_destroy(server->worker_pool);
    server->worker_pool = NULL;
    log_debug("Worker thread pool stopped");
  }

  mutex_lock(&server->handoff_mutex);
  while (server->handoffs) {
    acds_handoff_entry_t *entry = server->handoffs;
    server->handoffs = entry->next;
    SAFE_FREE(entry);
  }
  mutex_unlock(&server->handoff_mutex);

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
  ASCII_OTEL_SCOPE(DISCOVERY_CREATE);
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
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_SESSION_CREATES, 1);
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
      log_warn("Session creation failed for %s: %s", client_ip, asciichat_error_format(NULL, create_result));
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
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_SESSION_CREATES, 1);
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
      log_warn("Session creation failed for %s: %s", client_ip, asciichat_error_format(NULL, create_result));
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
  ASCII_OTEL_SCOPE(DISCOVERY_LOOKUP);
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
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_SESSION_LOOKUPS, 1);
    acip_send_session_info(transport, &resp);
    log_info("Session lookup for '%s' from %s: %s", session_string, client_ip, resp.found ? "found" : "not found");
  } else {
    acip_send_error(transport, lookup_result, "Session lookup failed");
    log_warn("Session lookup failed for %s: %s", client_ip, asciichat_error_format(NULL, lookup_result));
  }
}

static void acds_on_session_join(const acip_session_join_t *req, acip_transport_t *transport, const char *client_ip,
                                 void *app_ctx) {
  ASCII_OTEL_SCOPE(DISCOVERY_JOIN);
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
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_SESSION_JOINS, 1);
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
        signaling_broadcast(server->db, server->network_server, resp.session_id, PACKET_TYPE_ACIP_PARTICIPANT_JOINED,
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

static consensus_handoff_t *acds_handoff_find(acds_server_t *, const uint8_t[16], bool);
static asciichat_error_t acds_handoff_tick(acds_server_t *, consensus_handoff_t *);

static asciichat_error_t acds_remove_client_from_session(acds_server_t *server, acds_client_data_t *client_data) {
  if (!server || !client_data || !client_data->joined_session) {
    return ASCIICHAT_OK;
  }

  session_entry_t *session = database_session_find_by_id(server->db, client_data->session_id);
  bool was_host = false;
  if (session) {
    const uint8_t *host_id = session->host_established ? session->host_participant_id : session->initiator_id;
    was_host = memcmp(host_id, client_data->participant_id, sizeof(client_data->participant_id)) == 0;
    session_entry_destroy(session);
  }

  asciichat_error_t result = database_session_leave(server->db, client_data->session_id, client_data->participant_id);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(result, "Failed to remove disconnected participant from session");
  }

  client_data->joined_session = false;
  mutex_lock(&server->handoff_mutex);
  consensus_handoff_t *handoff = acds_handoff_find(server, client_data->session_id, false);
  if (handoff) {
    consensus_handoff_leave(handoff, client_data->participant_id);
    asciichat_error_t handoff_result = acds_handoff_tick(server, handoff);
    if (handoff_result != ASCIICHAT_OK)
      log_warn("Handoff after departure failed %s", asciichat_error_format(NULL, handoff_result));
    if (!handoff->count) {
      acds_handoff_entry_t **link = &server->handoffs;
      while (*link && &(*link)->state != handoff)
        link = &(*link)->next;
      if (*link) {
        acds_handoff_entry_t *entry = *link;
        *link = entry->next;
        SAFE_FREE(entry);
      }
    }
  }
  mutex_unlock(&server->handoff_mutex);

  // A closed connection can no longer send SESSION_LEAVE. Notify peers after
  // deleting the participant so reconnects do not try signaling this socket.
  session = database_session_find_by_id(server->db, client_data->session_id);
  if (session) {
    acip_participant_left_t participant_left = {0};
    memcpy(participant_left.session_id, client_data->session_id, sizeof(participant_left.session_id));
    memcpy(participant_left.left_participant_id, client_data->participant_id,
           sizeof(participant_left.left_participant_id));
    participant_left.was_host = was_host ? 1 : 0;
    participant_left.remaining_count = session->current_participants;
    result = signaling_broadcast(server->db, server->network_server, client_data->session_id,
                                 PACKET_TYPE_ACIP_PARTICIPANT_LEFT, &participant_left, sizeof(participant_left),
                                 client_data->participant_id);
    if (result != ASCIICHAT_OK) {
      log_warn("Failed to notify peers that participant left: %s", asciichat_error_format(NULL, result));
    }
    session_entry_destroy(session);
  }

  return ASCIICHAT_OK;
}

static void acds_on_session_leave(const acip_session_leave_t *req, acip_transport_t *transport, const char *client_ip,
                                  void *app_ctx) {
  ASCII_OTEL_SCOPE(DISCOVERY_LEAVE);
  acds_server_t *server = (acds_server_t *)app_ctx;
  acds_client_data_t *client_data = (acds_client_data_t *)transport->user_data;

  log_debug("SESSION_LEAVE packet from %s", client_ip);

  if (!client_data || !client_data->joined_session ||
      memcmp(req->session_id, client_data->session_id, sizeof(client_data->session_id)) != 0 ||
      memcmp(req->participant_id, client_data->participant_id, sizeof(client_data->participant_id)) != 0) {
    acip_send_error(transport, ERROR_INVALID_PARAM, "SESSION_LEAVE does not match this connection");
    log_warn("Rejected SESSION_LEAVE from %s because it did not match the joined participant", client_ip);
    return;
  }

  asciichat_error_t leave_result = acds_remove_client_from_session(server, client_data);
  if (leave_result == ASCIICHAT_OK) {
    log_info("Client %s left session", client_ip);
  } else {
    acip_send_error(transport, leave_result, asciichat_error_string(leave_result));
    log_warn("Session leave failed for %s: %s", client_ip, asciichat_error_format(NULL, leave_result));
  }
}

static void acds_on_webrtc_sdp(const acip_webrtc_sdp_t *sdp, size_t payload_len, acip_transport_t *transport,
                               const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  (void)transport; // Transport not needed for relay

  log_debug("WEBRTC_SDP packet from %s", client_ip);

  // Validation is done in the library handler (lib/network/acip/acds_handlers.c)
  asciichat_error_t relay_result = signaling_relay_sdp(server->db, server->network_server, sdp, payload_len);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "SDP relay failed");
    log_warn("SDP relay failed from %s: %s", client_ip, asciichat_error_format(NULL, relay_result));
  }
}

static void acds_on_webrtc_ice(const acip_webrtc_ice_t *ice, size_t payload_len, acip_transport_t *transport,
                               const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  (void)transport; // Transport not needed for relay

  log_debug("WEBRTC_ICE packet from %s", client_ip);

  // Validation is done in the library handler (lib/network/acip/acds_handlers.c)
  asciichat_error_t relay_result = signaling_relay_ice(server->db, server->network_server, ice, payload_len);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "ICE relay failed");
    log_warn("ICE relay failed from %s: %s", client_ip, asciichat_error_format(NULL, relay_result));
  }
}

static void acds_on_network_quality(const void *payload, size_t payload_len, acip_transport_t *transport,
                                    const char *client_ip, void *app_ctx) {
  acds_server_t *server = (acds_server_t *)app_ctx;
  (void)transport; // Transport not needed for relay

  log_debug_every(US_PER_SEC_INT, "★ ACDS_ON_NETWORK_QUALITY: received from %s, payload_len=%zu", client_ip,
                  payload_len);

  // Cast payload to acip_nat_quality_t for relay
  const acip_nat_quality_t *quality = (const acip_nat_quality_t *)payload;

  log_debug("NETWORK_QUALITY: session=%02x%02x..., participant=%02x%02x...", quality->session_id[0],
            quality->session_id[1], quality->participant_id[0], quality->participant_id[1]);

  // Validation and relay
  asciichat_error_t relay_result =
      signaling_relay_network_quality(server->db, server->network_server, quality, payload_len);
  if (relay_result != ASCIICHAT_OK) {
    acip_send_error(transport, relay_result, "NETWORK_QUALITY relay failed");
    log_warn("NETWORK_QUALITY relay failed from %s: %s", client_ip, asciichat_error_format(NULL, relay_result));
  } else {
    log_debug_every(US_PER_SEC_INT, "★ ACDS_ON_NETWORK_QUALITY: relay completed successfully for %s", client_ip);
  }
}

static void acds_on_host_announcement(const acip_host_announcement_t *announcement, acip_transport_t *transport,
                                      const char *client_ip, void *app_ctx) {
  ASCII_OTEL_SCOPE(DISCOVERY_HOST_ANNOUNCE);
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_info("HOST_ANNOUNCEMENT from %s: host_id=%02x%02x..., address=%s:%u, conn_type=%d", client_ip,
           announcement->host_id[0], announcement->host_id[1], announcement->host_address, announcement->host_port,
           announcement->connection_type);

  acds_client_data_t *client = transport ? transport->user_data : NULL;
  if (!client || !client->joined_session || memcmp(client->participant_id, announcement->host_id, 16) ||
      memcmp(client->session_id, announcement->session_id, 16)) {
    acip_send_error(transport, ERROR_NETWORK_PROTOCOL, "Host announcement identity mismatch");
    return;
  }
  mutex_lock(&server->handoff_mutex);
  consensus_handoff_t *handoff = acds_handoff_find(server, announcement->session_id, false);
  bool authorized = !handoff || !memcmp(handoff->host, announcement->host_id, 16);
  mutex_unlock(&server->handoff_mutex);
  if (!authorized) {
    acip_send_error(transport, ERROR_NETWORK_PROTOCOL, "Obsolete host announcement");
    return;
  }

  // Update session host in database
  asciichat_error_t result =
      database_session_update_host(server->db, announcement->session_id, announcement->host_id,
                                   announcement->host_address, announcement->host_port, announcement->connection_type);

  if (result != ASCIICHAT_OK) {
    acip_send_error(transport, result, "Failed to update session host");
    log_warn("HOST_ANNOUNCEMENT failed from %s: %s", client_ip, asciichat_error_format(NULL, result));
    return;
  }

  // TODO: Broadcast HOST_DESIGNATED to all participants in the session
  // This requires iterating connected clients and sending to those in the same session
  // For now, new participants will get host info when they join

  log_info("Session host updated via HOST_ANNOUNCEMENT from %s", client_ip);
}

static void acds_on_host_lost(const acip_host_lost_t *host_lost, acip_transport_t *transport, const char *client_ip,
                              void *app_ctx) {
  ASCII_OTEL_SCOPE(DISCOVERY_HOST_LOST);
  acds_server_t *server = (acds_server_t *)app_ctx;

  log_info("HOST_LOST from %s: session=%02x%02x..., participant=%02x%02x..., last_host=%02x%02x..., reason=%u",
           client_ip, host_lost->session_id[0], host_lost->session_id[1], host_lost->participant_id[0],
           host_lost->participant_id[1], host_lost->last_host_id[0], host_lost->last_host_id[1],
           host_lost->disconnect_reason);

  // Start host migration (set in_migration flag in database for bookkeeping)
  asciichat_error_t result = database_session_start_migration(server->db, host_lost->session_id);
  if (result != ASCIICHAT_OK) {
    acip_send_error(transport, result, "Failed to start host migration");
    log_warn("HOST_LOST failed from %s: %s", client_ip, asciichat_error_format(NULL, result));
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

static consensus_handoff_t *acds_handoff_find(acds_server_t *server, const uint8_t session[16], bool create) {
  for (acds_handoff_entry_t *entry = server->handoffs; entry; entry = entry->next)
    if (!memcmp(entry->state.session, session, 16))
      return &entry->state;
  if (!create)
    return NULL;
  session_entry_t *session_entry = database_session_find_by_id(server->db, session);
  if (!session_entry)
    return NULL;
  acds_handoff_entry_t *entry = SAFE_CALLOC(1, sizeof(*entry), acds_handoff_entry_t *);
  consensus_handoff_init(&entry->state, session,
                         session_entry->host_established ? session_entry->host_participant_id
                                                         : session_entry->initiator_id);
  session_entry_destroy(session_entry);
  entry->next = server->handoffs;
  server->handoffs = entry;
  return &entry->state;
}
static asciichat_error_t acds_handoff_broadcast(acds_server_t *server, consensus_handoff_message_t *message) {
  uint8_t bytes[CONSENSUS_HANDOFF_HEADER];
  consensus_handoff_encode(message, bytes);
  log_info("Handoff action=%u generation=%u round=%u candidate=%02x%02x", message->kind, message->generation,
           message->sequence, message->participant[0], message->participant[1]);
  return signaling_broadcast(server->db, server->network_server, message->session,
                             (packet_type_t)CONSENSUS_HANDOFF_PACKET, bytes, sizeof(bytes), NULL);
}
static asciichat_error_t acds_handoff_tick(acds_server_t *server, consensus_handoff_t *state) {
  consensus_handoff_message_t action;
  if (!consensus_handoff_tick(state, time_get_ns() / NS_PER_MS_INT, &action))
    return ASCIICHAT_OK;
  ASCII_OTEL_SCOPE(DISCOVERY_HANDOFF);
  if (action.kind == HANDOFF_COMMIT) {
    asciichat_error_t result = database_session_update_host(server->db, state->session, action.participant, "",
                                                            action.port, ACIP_CONNECTION_TYPE_STUN);
    if (result != ASCIICHAT_OK)
      return result;
  }
  if (action.kind == HANDOFF_FAILED) {
    asciichat_error_t result = database_session_clear_host(server->db, state->session);
    if (result != ASCIICHAT_OK)
      return result;
  }
  return acds_handoff_broadcast(server, &action);
}
static asciichat_error_t acds_on_handoff(const void *data, size_t length, acip_transport_t *transport, void *ctx) {
  acds_server_t *server = ctx;
  acds_client_data_t *client = transport ? transport->user_data : NULL;
  consensus_handoff_message_t message;
  asciichat_error_t result = consensus_handoff_decode(data, length, &message);
  if (result != ASCIICHAT_OK)
    return result;
  if (!client || !client->joined_session || memcmp(client->session_id, message.session, 16) ||
      memcmp(client->participant_id, message.participant, 16))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Handoff transport identity mismatch");
  mutex_lock(&server->handoff_mutex);
  consensus_handoff_t *state = acds_handoff_find(server, message.session, true);
  if (!state) {
    mutex_unlock(&server->handoff_mutex);
    return SET_ERRNO(ERROR_INVALID_STATE, "No handoff session");
  }
  if ((message.kind == HANDOFF_READY || message.kind == HANDOFF_REJECT) &&
      (message.generation != state->generation || message.sequence != state->round ||
       memcmp(message.participant, state->successor, 16))) {
    mutex_unlock(&server->handoff_mutex);
    return ASCIICHAT_OK; // Membership changes can overtake a preparation acknowledgment.
  }
  result = consensus_handoff_join(state, message.participant);
  if (result == ASCIICHAT_OK)
    result = consensus_handoff_receive(state, &message, length, time_get_ns() / NS_PER_MS_INT);
  if (result == ASCIICHAT_OK && message.kind == HANDOFF_READY && state->ready) {
    message.kind = HANDOFF_PLAN;
    result = acds_handoff_broadcast(server, &message);
  }
  if (result == ASCIICHAT_OK)
    result = acds_handoff_tick(server, state);
  mutex_unlock(&server->handoff_mutex);
  return result;
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
    .on_handoff = acds_on_handoff,
    .app_ctx = NULL // Set dynamically to server instance
};

static asciichat_error_t acds_connection_open(network_connection_t *connection, void *arg) {
  acds_server_t *server = arg;
  acds_client_data_t *client = SAFE_CALLOC(1, sizeof(*client), acds_client_data_t *);
  if (!client)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate discovery participant");
  network_connection_set_context(connection, client);
  client->transport = network_connection_transport(connection);
  client->transport->user_data = client;
  crypto_handshake_context_t *handshake = network_connection_handshake(connection);
  asciichat_error_t result = crypto_handshake_init("discovery", handshake, true);
  if (result != ASCIICHAT_OK)
    return result;
  client->negotiation = (network_negotiation_t){.protocol = NETWORK_PROTOCOL_DISCOVERY,
                                                .handshake = handshake,
                                                .initialized = true,
                                                .secure = network_connection_secure(connection),
                                                .require_auth = server->config.require_client_identity,
                                                .require_encryption = GET_OPTION(encrypt_enabled),
                                                .forbid_encryption = GET_OPTION(no_encrypt)};
  handshake->require_client_auth = server->config.require_client_identity;
  handshake->allow_unlisted_client_identity = server->config.require_client_identity;
  handshake->server_public_key.type = KEY_TYPE_ED25519;
  handshake->server_private_key.type = KEY_TYPE_ED25519;
  memcpy(handshake->server_public_key.key, server->identity_public, sizeof(server->identity_public));
  memcpy(handshake->server_private_key.key.ed25519, server->identity_secret, sizeof(server->identity_secret));
  return ASCIICHAT_OK;
}
static asciichat_error_t acds_connection_packet(network_connection_t *connection, packet_type_t type,
                                                const void *payload, size_t size, void *arg) {
  acds_server_t *server = arg;
  acds_client_data_t *client = network_connection_context(connection);
  acip_transport_t *transport = network_connection_transport(connection);
  if (network_connection_state(connection) == NETWORK_NEGOTIATING) {
    asciichat_error_t result = network_negotiation_receive(&client->negotiation, transport, type, payload, size);
    if (result == ASCIICHAT_OK && client->negotiation.ready)
      network_connection_ready(connection);
    return result;
  }
  atomic_fetch_add_u64(&server->stats.bytes_received, size + sizeof(packet_header_t));
  atomic_fetch_add_u64(&server->stats.packets_received, 1);
  if (client->in_multikey_session_create && type != PACKET_TYPE_ACIP_SESSION_CREATE && type != PACKET_TYPE_PING &&
      type != PACKET_TYPE_PONG)
    return acip_send_error(transport, ERROR_INVALID_PARAM,
                           "Only SESSION_CREATE/PING/PONG allowed during session creation");
  acip_acds_callbacks_t callbacks = g_acds_callbacks;
  callbacks.app_ctx = server;
  return acip_handle_acds_packet(transport, type, payload, size, network_connection_peer(connection), &callbacks);
}
static asciichat_error_t acds_connection_closing(network_connection_t *connection, void *arg) {
  acds_client_data_t *client = network_connection_context(connection);
  if (client && client->joined_session && acds_remove_client_from_session(arg, client) != ASCIICHAT_OK)
    log_warn("Failed to remove disconnected discovery participant");
  return ASCIICHAT_OK;
}
static void acds_connection_closed(network_connection_t *connection, void *arg) {
  (void)arg;
  acds_client_data_t *client = network_connection_context(connection);
  if (!client)
    return;
  SAFE_FREE(client);
  network_connection_set_context(connection, NULL);
}
const network_connection_callbacks_t acds_network_callbacks = {acds_connection_open, acds_connection_packet,
                                                               acds_connection_closing, acds_connection_closed};

/**
 * @file lib/network/mdns/discovery.c
 * @brief Parallel mDNS and ACDS session discovery implementation
 * @ingroup network_discovery
 *
 * This module provides automated discovery of ascii-chat servers via parallel
 * mDNS (local LAN) and ACDS (internet) lookups.
 *
 * Architecture:
 * - mDNS discovery: discovery_mdns_query() - core implementation (this module)
 * - ACDS discovery: ACDS client implementation for centralized lookup
 * - Parallel coordination: Concurrent threads with "race to success" semantics
 * - TUI wrapper: discovery_tui.c calls discovery_mdns_query() for interactive selection
 */

#include <ascii-chat/network/mdns/discovery.h>
#include <ascii-chat/network/mdns/discovery_tui.h> // For discovery_tui_server_t struct only
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/util/time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// ============================================================================
// Thread Coordination
// ============================================================================

/** Thread-safe result sharing between discovery threads */
typedef struct {
  mutex_t lock;
  cond_t signal;
  discovery_result_t *result;
  bool mdns_done;
  bool acds_done;
  bool found;
} discovery_thread_state_t;

// ============================================================================
// Utility Functions
// ============================================================================

void pubkey_to_hex(const uint8_t pubkey[32], char hex_out[65]) {
  if (!pubkey || !hex_out)
    return;
  for (int i = 0; i < 32; i++) {
    safe_snprintf(hex_out + (i * 2), 3, "%02x", pubkey[i]);
  }
  hex_out[64] = '\0';
}

asciichat_error_t hex_to_pubkey(const char *hex_str, uint8_t pubkey_out[32]) {
  if (!hex_str || !pubkey_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid hex_str or pubkey_out pointer");
    return ERROR_INVALID_PARAM;
  }

  if (strlen(hex_str) != 64) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Hex string must be exactly 64 characters");
    return ERROR_INVALID_PARAM;
  }

  for (int i = 0; i < 32; i++) {
    char hex_byte[3];
    hex_byte[0] = hex_str[i * 2];
    hex_byte[1] = hex_str[i * 2 + 1];
    hex_byte[2] = '\0';

    if (!isxdigit(hex_byte[0]) || !isxdigit(hex_byte[1])) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid hex character in string");
      return ERROR_INVALID_PARAM;
    }

    pubkey_out[i] = (uint8_t)strtol(hex_byte, NULL, 16);
  }

  return ASCIICHAT_OK;
}

// NOTE: is_session_string() has been moved to lib/discovery/strings.c with enhanced
// validation against cached wordlists. See that module for the implementation.

// ============================================================================
// TXT Record Parsing
// ============================================================================
// mDNS Query Implementation (Core Module)
// ============================================================================

/**
 * @brief Internal state for collecting discovered services
 */
typedef struct {
  discovery_tui_server_t *servers; ///< Array of discovered servers
  int count;                       ///< Number of servers discovered so far
  int capacity;                    ///< Allocated capacity
  int64_t start_time_ms;           ///< When discovery started (for timeout)
  int timeout_ms;                  ///< Discovery timeout in milliseconds
  bool query_complete;             ///< Set when discovery completes
} mdns_query_state_t;

/**
 * @brief mDNS callback for discovered services
 * Called by mDNS library when a service is discovered.
 */
static void discovery_mdns_callback(const asciichat_mdns_discovery_t *discovery, void *user_data) {
  mdns_query_state_t *state = (mdns_query_state_t *)user_data;
  if (!state || !discovery) {
    return;
  }

  // Check if we've exceeded capacity
  if (state->count >= state->capacity) {
    log_warn("mDNS: Reached maximum server capacity (%d)", state->capacity);
    return;
  }

  // Check if timeout exceeded
  int64_t elapsed = (int64_t)time_ns_to_ms(time_get_ns()) - state->start_time_ms;
  if (elapsed > state->timeout_ms) {
    state->query_complete = true;
    return;
  }

  // Only accept services of the right type
  if (strstr(discovery->type, "_ascii-chat._tcp") == NULL) {
    return;
  }

  // Check if we already have this server (avoid duplicates)
  for (int i = 0; i < state->count; i++) {
    if (strcmp(state->servers[i].name, discovery->name) == 0 && state->servers[i].port == discovery->port) {
      // Update TTL if newer
      if (discovery->ttl > state->servers[i].ttl) {
        state->servers[i].ttl = discovery->ttl;
      }
      return; // Already have this server
    }
  }

  // Add new server to our array
  discovery_tui_server_t *server = &state->servers[state->count];
  memset(server, 0, sizeof(discovery_tui_server_t));

  // Copy service information
  SAFE_STRNCPY(server->name, discovery->name, sizeof(server->name));
  SAFE_STRNCPY(server->ipv4, discovery->ipv4, sizeof(server->ipv4));
  SAFE_STRNCPY(server->ipv6, discovery->ipv6, sizeof(server->ipv6));
  server->port = discovery->port;
  server->ttl = discovery->ttl;

  // Prefer IPv4 address as the primary address, fall back to hostname
  if (discovery->ipv4[0] != '\0') {
    SAFE_STRNCPY(server->address, discovery->ipv4, sizeof(server->address));
  } else if (discovery->host[0] != '\0') {
    SAFE_STRNCPY(server->address, discovery->host, sizeof(server->address));
  } else if (discovery->ipv6[0] != '\0') {
    SAFE_STRNCPY(server->address, discovery->ipv6, sizeof(server->address));
  }

  state->count++;
  log_debug("mDNS: Found server '%s' at %s:%u", discovery->name, server->address, discovery->port);
}

/**
 * @brief Public mDNS query function used by both parallel discovery and TUI wrapper
 *
 * @param timeout_ms Query timeout in milliseconds
 * @param max_servers Maximum servers to discover
 * @param quiet If true, suppresses progress messages
 * @param out_count Output: number of servers discovered
 * @return Array of discovered servers, or NULL on error. Use discovery_mdns_free() to free.
 */
discovery_tui_server_t *discovery_mdns_query(int timeout_ms, int max_servers, bool quiet, int *out_count) {
  if (!out_count) {
    SET_ERRNO(ERROR_INVALID_PARAM, "out_count pointer is NULL");
    return NULL;
  }

  *out_count = 0;

  // Apply defaults
  if (timeout_ms <= 0) {
    timeout_ms = 2000;
  }
  if (max_servers <= 0) {
    max_servers = 20;
  }

  // Allocate state for collecting servers
  mdns_query_state_t state;
  memset(&state, 0, sizeof(state));
  state.capacity = max_servers;
  state.timeout_ms = timeout_ms;
  state.start_time_ms = (int64_t)time_ns_to_ms(time_get_ns());

  // Allocate server array
  state.servers = SAFE_MALLOC((size_t)state.capacity * sizeof(discovery_tui_server_t), discovery_tui_server_t *);
  if (!state.servers) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate mDNS discovery server array");
    return NULL;
  }
  memset(state.servers, 0, state.capacity * sizeof(discovery_tui_server_t));

  if (!quiet) {
    log_info("mDNS: Searching for ascii-chat servers on local network (timeout: %dms)", state.timeout_ms);
    printf("🔍 Searching for ascii-chat servers on LAN...\n");
  }

  // Initialize mDNS
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  if (!mdns) {
    log_warn("mDNS: Failed to initialize mDNS - discovery unavailable");
    SAFE_FREE(state.servers);
    return NULL;
  }

  // Start mDNS query for _ascii-chat._tcp services
  asciichat_error_t query_result =
      asciichat_mdns_query(mdns, "_ascii-chat._tcp.local", discovery_mdns_callback, &state);

  if (query_result != ASCIICHAT_OK) {
    log_info("mDNS: Query failed - no servers found via service discovery");
    asciichat_mdns_shutdown(mdns);
    SAFE_FREE(state.servers);
    return NULL;
  }

  // Poll for responses until timeout
  int64_t deadline = state.start_time_ms + state.timeout_ms;
  while (!state.query_complete && (int64_t)time_ns_to_ms(time_get_ns()) < deadline) {
    int poll_timeout = (int)(deadline - (int64_t)time_ns_to_ms(time_get_ns()));
    if (poll_timeout < 0) {
      poll_timeout = 0;
    }
    if (poll_timeout > 100) {
      poll_timeout = 100; // Check every 100ms
    }
    asciichat_mdns_update(mdns, poll_timeout);
  }

  // Cleanup mDNS
  asciichat_mdns_shutdown(mdns);

  if (!quiet) {
    if (state.count > 0) {
      printf("✅ Found %d ascii-chat server%s on LAN\n", state.count, state.count == 1 ? "" : "s");
      log_info("mDNS: Found %d server(s)", state.count);
    } else {
      printf("❌ No ascii-chat servers found on LAN\n");
      log_info("mDNS: No servers found");
    }
  }

  *out_count = state.count;
  return state.servers;
}

/**
 * @brief Free memory from mDNS discovery results
 */
void discovery_mdns_free(discovery_tui_server_t *servers) {
  SAFE_FREE(servers);
}

// ============================================================================
// mDNS Discovery Thread
// ============================================================================

/** Context for mDNS discovery thread */
typedef struct {
  const char *session_string;
  discovery_thread_state_t *state;
  const uint8_t *expected_pubkey;
  uint32_t timeout_ms;
} mdns_thread_context_t;

/** Thread function for mDNS discovery */
static void *mdns_thread_fn(void *arg) {
  mdns_thread_context_t *ctx = (mdns_thread_context_t *)arg;
  if (!ctx)
    return NULL;

  // Call the public mDNS query function in this module
  int discovered_count = 0;
  discovery_tui_server_t *discovered_servers =
      discovery_mdns_query((int)(ctx->timeout_ms > 0 ? ctx->timeout_ms : 2000), 20, true, &discovered_count);

  if (!discovered_servers || discovered_count == 0) {
    log_debug("mDNS: No servers found (this is normal if no servers are on LAN)");
    if (discovered_servers) {
      discovery_mdns_free(discovered_servers);
    }
    mutex_lock(&ctx->state->lock);
    ctx->state->mdns_done = true;
    cond_signal(&ctx->state->signal);
    mutex_unlock(&ctx->state->lock);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Search for server matching our session string
  for (int i = 0; i < discovered_count; i++) {
    discovery_tui_server_t *server = &discovered_servers[i];

    if (strcmp(server->name, ctx->session_string) == 0) {
      // Found a match!
      mutex_lock(&ctx->state->lock);
      {
        if (!ctx->state->found) {
          ctx->state->found = true;
          ctx->state->result->success = true;
          ctx->state->result->source = DISCOVERY_SOURCE_MDNS;
          ctx->state->result->server_port = server->port;

          const char *best_addr = (server->ipv4[0] != '\0') ? server->ipv4 : server->ipv6;
          strncpy(ctx->state->result->server_address, best_addr, sizeof(ctx->state->result->server_address) - 1);
          ctx->state->result->server_address[sizeof(ctx->state->result->server_address) - 1] = '\0';

          strncpy(ctx->state->result->mdns_service_name, server->name,
                  sizeof(ctx->state->result->mdns_service_name) - 1);
          ctx->state->result->mdns_service_name[sizeof(ctx->state->result->mdns_service_name) - 1] = '\0';

          log_info("mDNS: Found session '%s' at %s:%d", ctx->session_string, ctx->state->result->server_address,
                   ctx->state->result->server_port);
          cond_signal(&ctx->state->signal);
        }
      }
      mutex_unlock(&ctx->state->lock);
      break;
    }
  }

  discovery_mdns_free(discovered_servers);

  mutex_lock(&ctx->state->lock);
  ctx->state->mdns_done = true;
  cond_signal(&ctx->state->signal);
  mutex_unlock(&ctx->state->lock);
  SAFE_FREE(ctx);
  return NULL;
}

// ============================================================================
// ACDS Discovery Thread
// ============================================================================

/** Context for ACDS discovery thread */
typedef struct {
  const char *session_string;
  discovery_thread_state_t *state;
  const discovery_config_t *config;
} acds_thread_context_t;

/** Thread function for ACDS discovery */
static void *acds_thread_fn(void *arg) {
  acds_thread_context_t *ctx = (acds_thread_context_t *)arg;
  if (!ctx)
    return NULL;

  acds_client_t client;
  memset(&client, 0, sizeof(client));

  acds_client_config_t client_config;
  memset(&client_config, 0, sizeof(client_config));

  strncpy(client_config.server_address, ctx->config->acds_server, sizeof(client_config.server_address) - 1);
  client_config.server_port = ctx->config->acds_port;
  client_config.timeout_ms = ctx->config->acds_timeout_ms;

  // Connect to ACDS server
  asciichat_error_t conn_err = acds_client_connect(&client, &client_config);
  if (conn_err != ASCIICHAT_OK) {
    log_debug("ACDS: Failed to connect to %s:%d", ctx->config->acds_server, ctx->config->acds_port);
    mutex_lock(&ctx->state->lock);
    ctx->state->acds_done = true;
    cond_signal(&ctx->state->signal);
    mutex_unlock(&ctx->state->lock);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Look up session
  acds_session_lookup_result_t lookup_result;
  memset(&lookup_result, 0, sizeof(lookup_result));

  asciichat_error_t lookup_err = acds_session_lookup(&client, ctx->session_string, &lookup_result);
  if (lookup_err != ASCIICHAT_OK) {
    log_debug("ACDS: Session lookup failed for '%s'", ctx->session_string);
    acds_client_disconnect(&client);
    mutex_lock(&ctx->state->lock);
    ctx->state->acds_done = true;
    cond_signal(&ctx->state->signal);
    mutex_unlock(&ctx->state->lock);
    SAFE_FREE(ctx);
    return NULL;
  }

  if (!lookup_result.found) {
    log_debug("ACDS: Session '%s' not found", ctx->session_string);
    acds_client_disconnect(&client);
    mutex_lock(&ctx->state->lock);
    ctx->state->acds_done = true;
    cond_signal(&ctx->state->signal);
    mutex_unlock(&ctx->state->lock);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Verify pubkey if provided
  if (ctx->config->expected_pubkey) {
    if (memcmp(lookup_result.host_pubkey, ctx->config->expected_pubkey, 32) != 0) {
      log_warn("ACDS: Session found but pubkey mismatch (MITM?)");
      acds_client_disconnect(&client);
      mutex_lock(&ctx->state->lock);
      ctx->state->acds_done = true;
      cond_signal(&ctx->state->signal);
      mutex_unlock(&ctx->state->lock);
      SAFE_FREE(ctx);
      return NULL;
    }
  }

  // Join session to get server connection details
  acds_session_join_params_t join_params;
  memset(&join_params, 0, sizeof(join_params));
  join_params.session_string = ctx->session_string;

  if (ctx->config->client_pubkey) {
    memcpy(join_params.identity_pubkey, ctx->config->client_pubkey, 32);
  }
  if (ctx->config->client_seckey) {
    memcpy(join_params.identity_seckey, ctx->config->client_seckey, 64);
  }
  if (ctx->config->password) {
    join_params.has_password = true;
    strncpy(join_params.password, ctx->config->password, sizeof(join_params.password) - 1);
  }

  acds_session_join_result_t join_result;
  memset(&join_result, 0, sizeof(join_result));

  asciichat_error_t join_err = acds_session_join(&client, &join_params, &join_result);
  acds_client_disconnect(&client);

  if (join_err != ASCIICHAT_OK || !join_result.success) {
    log_debug("ACDS: Session join failed: %s", join_result.error_message);
    mutex_lock(&ctx->state->lock);
    ctx->state->acds_done = true;
    cond_signal(&ctx->state->signal);
    mutex_unlock(&ctx->state->lock);
    SAFE_FREE(ctx);
    return NULL;
  }

  // Found it! Populate result
  mutex_lock(&ctx->state->lock);
  {
    if (!ctx->state->found) {
      ctx->state->found = true;
      ctx->state->result->success = true;
      ctx->state->result->source = DISCOVERY_SOURCE_ACDS;

      memcpy(ctx->state->result->host_pubkey, lookup_result.host_pubkey, 32);
      memcpy(ctx->state->result->session_id, join_result.session_id, 16);
      memcpy(ctx->state->result->participant_id, join_result.participant_id, 16);

      strncpy(ctx->state->result->server_address, join_result.server_address,
              sizeof(ctx->state->result->server_address) - 1);
      ctx->state->result->server_port = join_result.server_port;

      log_info("ACDS: Found session '%s' at %s:%d", ctx->session_string, ctx->state->result->server_address,
               ctx->state->result->server_port);
      cond_signal(&ctx->state->signal);
    }
  }
  mutex_unlock(&ctx->state->lock);

  SAFE_FREE(ctx);
  return NULL;
}

// ============================================================================
// Main Discovery Function
// ============================================================================

void discovery_config_init_defaults(discovery_config_t *config) {
  if (!config)
    return;

  memset(config, 0, sizeof(*config));

  // Check if debug or release build
#ifdef NDEBUG
  // Release: use internet ACDS
  strncpy(config->acds_server, "discovery.ascii-chat.com", sizeof(config->acds_server) - 1);
#else
  // Debug: use local ACDS
  strncpy(config->acds_server, "127.0.0.1", sizeof(config->acds_server) - 1);
#endif

  config->acds_port = OPT_ACDS_PORT_INT_DEFAULT;
  config->mdns_timeout_ms = 2000;
  config->acds_timeout_ms = 5000;
  config->insecure_mode = false;
  config->expected_pubkey = NULL;
}

asciichat_error_t discover_session_parallel(const char *session_string, const discovery_config_t *config,
                                            discovery_result_t *result) {
  if (!session_string || !config || !result) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to discover_session_parallel");
    return ERROR_INVALID_PARAM;
  }

  // Validate session string format
  if (!is_session_string(session_string)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session string format");
    return ERROR_INVALID_PARAM;
  }

  memset(result, 0, sizeof(*result));
  log_info("Discovery: Looking up session '%s'", session_string);

  // Initialize thread state
  discovery_thread_state_t state;
  memset(&state, 0, sizeof(state));
  state.result = result;
  mutex_init(&state.lock);
  cond_init(&state.signal);

  // Determine which discovery methods to use
  bool use_mdns = true;
  bool use_acds = config->expected_pubkey != NULL || config->insecure_mode;

  if (!use_acds) {
    log_debug("Discovery: mDNS-only mode (no --server-key and no --acds-insecure)");
  }

  // Spawn mDNS thread
  asciichat_thread_t mdns_thread;
  asciichat_thread_init(&mdns_thread);

  if (use_mdns) {
    mdns_thread_context_t *mdns_ctx = SAFE_MALLOC(sizeof(*mdns_ctx), mdns_thread_context_t *);
    if (!mdns_ctx) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate mDNS context");
      mutex_destroy(&state.lock);
      cond_destroy(&state.signal);
      return ERROR_MEMORY;
    }

    mdns_ctx->session_string = session_string;
    mdns_ctx->state = &state;
    mdns_ctx->expected_pubkey = config->expected_pubkey;

    int thread_err = asciichat_thread_create(&mdns_thread, mdns_thread_fn, mdns_ctx);
    if (thread_err != 0) {
      log_warn("Discovery: Failed to spawn mDNS thread");
      SAFE_FREE(mdns_ctx);
      use_mdns = false;
    }
  }

  // Spawn ACDS thread
  asciichat_thread_t acds_thread;
  asciichat_thread_init(&acds_thread);

  if (use_acds) {
    acds_thread_context_t *acds_ctx = SAFE_MALLOC(sizeof(*acds_ctx), acds_thread_context_t *);
    if (!acds_ctx) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate ACDS context");
      if (use_mdns && asciichat_thread_is_initialized(&mdns_thread)) {
        asciichat_thread_join(&mdns_thread, NULL);
      }
      mutex_destroy(&state.lock);
      cond_destroy(&state.signal);
      return ERROR_MEMORY;
    }

    acds_ctx->session_string = session_string;
    acds_ctx->state = &state;
    acds_ctx->config = config;

    int thread_err = asciichat_thread_create(&acds_thread, acds_thread_fn, acds_ctx);
    if (thread_err != 0) {
      log_warn("Discovery: Failed to spawn ACDS thread");
      SAFE_FREE(acds_ctx);
      use_acds = false;
    }
  }

  // Wait for result (mDNS timeout 2s + ACDS timeout 5s max)
  uint32_t wait_timeout_ms = config->acds_timeout_ms + 1000;
  mutex_lock(&state.lock);
  {
    uint32_t elapsed_ms = 0;
    while (!state.found && elapsed_ms < wait_timeout_ms) {
      // Check if both threads are done
      if (state.mdns_done && state.acds_done) {
        break;
      }

      // Wait with timeout
      cond_timedwait(&state.signal, &state.lock, 500 * NS_PER_MS_INT);
      elapsed_ms += 500;
    }
  }
  mutex_unlock(&state.lock);

  // Join threads
  if (use_mdns && asciichat_thread_is_initialized(&mdns_thread)) {
    asciichat_thread_join(&mdns_thread, NULL);
  }
  if (use_acds && asciichat_thread_is_initialized(&acds_thread)) {
    asciichat_thread_join(&acds_thread, NULL);
  }

  // Cleanup
  mutex_destroy(&state.lock);
  cond_destroy(&state.signal);

  // Check result
  if (!result->success) {
    SET_ERRNO(ERROR_NOT_FOUND, "Session '%s' not found (mDNS/ACDS timeout)", session_string);
    return ERROR_NOT_FOUND;
  }

  log_info("Discovery: Session '%s' discovered via %s", session_string,
           result->source == DISCOVERY_SOURCE_MDNS ? "mDNS" : "ACDS");

  return ASCIICHAT_OK;
}

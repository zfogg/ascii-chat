/**
 * @file lan_discovery.c
 * @brief LAN service discovery implementation for ascii-chat client
 */

#include "network/mdns/lan_discovery.h"
#include "common.h"
#include "log/logging.h"
#include "mdns.h"
#include "platform/abstraction.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Internal state for collecting discovered services
 */
typedef struct {
  lan_discovered_server_t *servers; ///< Array of discovered servers
  int count;                        ///< Number of servers discovered so far
  int capacity;                     ///< Allocated capacity
  int64_t start_time_ms;            ///< When discovery started (for timeout)
  int timeout_ms;                   ///< Discovery timeout in milliseconds
  bool query_complete;              ///< Set when discovery completes
} lan_discovery_state_t;

/**
 * @brief Get current time in milliseconds since epoch
 */
static int64_t get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

/**
 * @brief mDNS callback for discovered services
 *
 * Called by mDNS library when a service is discovered.
 * Collects service information into the state array.
 */
static void lan_discovery_mdns_callback(const asciichat_mdns_discovery_t *discovery, void *user_data) {
  lan_discovery_state_t *state = (lan_discovery_state_t *)user_data;
  if (!state || !discovery) {
    return;
  }

  // Check if we've exceeded capacity
  if (state->count >= state->capacity) {
    log_warn("LAN discovery: Reached maximum server capacity (%d)", state->capacity);
    return;
  }

  // Check if timeout exceeded
  int64_t elapsed = get_time_ms() - state->start_time_ms;
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
    // Check if same name and port
    if (strcmp(state->servers[i].name, discovery->name) == 0 && state->servers[i].port == discovery->port) {
      // Update TTL if newer
      if (discovery->ttl > state->servers[i].ttl) {
        state->servers[i].ttl = discovery->ttl;
      }
      return; // Already have this server
    }
  }

  // Add new server to our array
  lan_discovered_server_t *server = &state->servers[state->count];
  memset(server, 0, sizeof(lan_discovered_server_t));

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
  log_debug("LAN discovery: Found server '%s' at %s:%u", discovery->name, server->address, discovery->port);
}

/**
 * @brief Get default discovery configuration
 */
static void lan_discovery_config_set_defaults(lan_discovery_config_t *config) {
  if (!config) {
    return;
  }
  if (config->timeout_ms <= 0) {
    config->timeout_ms = 2000; // Default 2 seconds
  }
  if (config->max_servers <= 0) {
    config->max_servers = 20; // Default max 20 servers
  }
}

/**
 * @brief Implement LAN discovery query
 */
lan_discovered_server_t *lan_discovery_query(const lan_discovery_config_t *config, int *out_count) {
  if (!out_count) {
    SET_ERRNO(ERROR_INVALID_PARAM, "out_count pointer is NULL");
    return NULL;
  }

  *out_count = 0;

  // Create config with defaults
  lan_discovery_config_t effective_config;
  if (config) {
    effective_config = *config;
  } else {
    memset(&effective_config, 0, sizeof(effective_config));
  }
  lan_discovery_config_set_defaults(&effective_config);

  // Allocate state for collecting servers
  lan_discovery_state_t state;
  memset(&state, 0, sizeof(state));
  state.capacity = effective_config.max_servers;
  state.timeout_ms = effective_config.timeout_ms;
  state.start_time_ms = get_time_ms();

  // Allocate server array
  state.servers = SAFE_MALLOC((size_t)state.capacity * sizeof(lan_discovered_server_t), lan_discovered_server_t *);
  if (!state.servers) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate LAN discovery server array");
    return NULL;
  }
  memset(state.servers, 0, state.capacity * sizeof(lan_discovered_server_t));

  if (!effective_config.quiet) {
    log_info("LAN discovery: Searching for ASCII-Chat servers on local network (timeout: %dms)", state.timeout_ms);
    printf("🔍 Searching for ASCII-Chat servers on LAN...\n");
  }

  // Initialize mDNS
  asciichat_mdns_t *mdns = asciichat_mdns_init();
  if (!mdns) {
    log_warn("LAN discovery: Failed to initialize mDNS - LAN discovery unavailable");
    SAFE_FREE(state.servers);
    return NULL;
  }

  // Start mDNS query for _ascii-chat._tcp services
  asciichat_error_t query_result =
      asciichat_mdns_query(mdns, "_ascii-chat._tcp.local", lan_discovery_mdns_callback, &state);

  if (query_result != ASCIICHAT_OK) {
    log_warn("LAN discovery: Failed to start mDNS query: %s", asciichat_error_string(query_result));
    asciichat_mdns_shutdown(mdns);
    SAFE_FREE(state.servers);
    return NULL;
  }

  // Poll for responses until timeout
  int64_t deadline = state.start_time_ms + state.timeout_ms;
  while (!state.query_complete && get_time_ms() < deadline) {
    // Process any pending mDNS responses
    int poll_timeout = (int)(deadline - get_time_ms());
    if (poll_timeout < 0) {
      poll_timeout = 0;
    }
    if (poll_timeout > 100) {
      poll_timeout = 100; // Check every 100ms for cancellation/timeout
    }

    asciichat_mdns_update(mdns, poll_timeout);
  }

  // Cleanup mDNS
  asciichat_mdns_shutdown(mdns);

  if (!effective_config.quiet) {
    if (state.count > 0) {
      printf("✅ Found %d ASCII-Chat server%s on LAN\n", state.count, state.count == 1 ? "" : "s");
      log_info("LAN discovery: Found %d server(s)", state.count);
    } else {
      printf("❌ No ASCII-Chat servers found on LAN\n");
      log_info("LAN discovery: No servers found");
    }
  }

  *out_count = state.count;
  return state.servers;
}

/**
 * @brief Free results from LAN discovery
 */
void lan_discovery_free_results(lan_discovered_server_t *servers) {
  SAFE_FREE(servers);
}

/**
 * @brief Interactive server selection
 */
int lan_discovery_prompt_selection(const lan_discovered_server_t *servers, int count) {
  if (!servers || count <= 0) {
    return -1;
  }

  // Display available servers
  printf("\nAvailable ASCII-Chat servers on LAN:\n");
  for (int i = 0; i < count; i++) {
    const lan_discovered_server_t *srv = &servers[i];
    const char *addr = lan_discovery_get_best_address(srv);
    printf("  %d. %s (%s:%u)\n", i + 1, srv->name, addr, srv->port);
  }

  // Prompt for selection
  printf("\nSelect server (1-%d) or press Enter to cancel: ", count);
  fflush(stdout);

  // Read user input
  char input[32];
  if (fgets(input, sizeof(input), stdin) == NULL) {
    printf("\n");
    return -1; // EOF or error
  }

  // Check for empty input (Enter pressed)
  if (input[0] == '\n' || input[0] == '\r' || input[0] == '\0') {
    return -1; // User cancelled
  }

  // Parse input as number
  char *endptr;
  long selection = strtol(input, &endptr, 10);

  // Validate input
  if (selection < 1 || selection > count) {
    printf("⚠️  Invalid selection. Please enter a number between 1 and %d\n", count);
    return lan_discovery_prompt_selection(servers, count); // Re-prompt
  }

  return (int)(selection - 1); // Convert to 0-based index
}

/**
 * @brief Get best address for a server
 */
const char *lan_discovery_get_best_address(const lan_discovered_server_t *server) {
  if (!server) {
    return "";
  }

  // Prefer IPv4 > name > IPv6
  if (server->ipv4[0] != '\0') {
    return server->ipv4;
  }
  if (server->name[0] != '\0') {
    return server->name;
  }
  if (server->ipv6[0] != '\0') {
    return server->ipv6;
  }

  return server->address; // Fallback to address field
}

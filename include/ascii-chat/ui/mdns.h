/**
 * @file ui/mdns.h
 * @brief mDNS service discovery UI with terminal_screen rendering
 *
 * Implements interactive mDNS-based discovery of ascii-chat servers on the local network.
 * Uses the shared terminal_screen infrastructure for consistent UI rendering with other modules.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Log Management
 * ========================================================================== */

/**
 * @brief Initialize log buffer for mDNS discovery UI
 *
 * Delegates to terminal_screen log initialization.
 * Call once at startup before rendering mDNS discovery screen.
 */
void ui_mdns_log_init(void);

/**
 * @brief Destroy mDNS discovery log buffer
 *
 * Delegates to terminal_screen log cleanup.
 */
void ui_mdns_log_destroy(void);

/**
 * @brief Clear mDNS discovery log buffer
 *
 * Delegates to terminal_screen log clear.
 * Useful when starting fresh mDNS discovery screen.
 */
void ui_mdns_log_clear(void);

/**
 * @brief Append message to mDNS discovery log buffer
 *
 * Delegates to the logging system which automatically captures to session log buffer.
 *
 * @param message Log message to append
 */
void ui_mdns_log_append(const char *message);

/**
 * @brief Discovered server information from mDNS
 */
typedef struct {
  char name[256];           ///< Service instance name (e.g., "swift-river-canyon")
  char session_string[256]; ///< Session identity advertised in TXT metadata
  char address[256];        ///< Server address (IPv4, IPv6, or hostname)
  uint16_t port;            ///< Server port number
  char ipv4[16];            ///< IPv4 address (if available)
  char ipv6[46];            ///< IPv6 address (if available)
  uint32_t ttl;             ///< TTL remaining (seconds)
} ui_mdns_server_t;

/**
 * @brief Configuration for TUI discovery
 */
typedef struct {
  int timeout_ms;  ///< Maximum time to wait for responses (default: 2000)
  int max_servers; ///< Maximum servers to collect (default: 20)
  bool quiet;      ///< Suppress discovery messages (default: false)
} ui_mdns_config_t;

/**
 * @brief Discover ascii-chat servers on the local network via mDNS
 *
 * Performs an mDNS query for _ascii-chat._tcp services on the local network.
 * Collects responses and returns discovered servers to the caller.
 *
 * **Behavior:**
 * - Sends multicast mDNS query for _ascii-chat._tcp.local services
 * - Waits for responses for the specified timeout period
 * - Collects all discovered servers with their addresses and ports
 * - Returns array of discovered servers
 *
 * **Memory Management:**
 * - Caller is responsible for freeing the returned array with ui_mdns_free_results()
 * - Returned servers are valid until ui_mdns_free_results() is called
 *
 * **Error Handling:**
 * - Returns NULL and sets errno on initialization failure
 * - Partial results returned if mDNS init fails but discovery was attempted
 * - Network errors logged but don't prevent continuation
 *
 * **Threading:**
 * - Blocking operation - waits for full timeout period
 * - Safe to call from main thread
 * - Does not spawn background threads
 *
 * @param config Discovery configuration (NULL uses defaults)
 * @param out_count Output parameter: number of discovered servers
 * @return Array of discovered servers, or NULL on error
 *         Must be freed with ui_mdns_free_results()
 *
 * @note Timeout includes network round-trip time, so 2000ms allows ~1.5s of actual waiting
 * @note Returns empty array (non-NULL with count=0) if no servers found, not NULL
 *
 * Example:
 * @code
 * ui_mdns_config_t config = {.timeout_ms = 2000, .max_servers = 20};
 * int count = 0;
 * ui_mdns_server_t *servers = ui_mdns_query(&config, &count);
 *
 * if (servers && count > 0) {
 *     for (int i = 0; i < count; i++) {
 *         printf("%d: %s (%s:%d)\n", i+1, servers[i].name, servers[i].address, servers[i].port);
 *     }
 *     // User selects server...
 *     ui_mdns_free_results(servers);
 * }
 * @endcode
 */
ui_mdns_server_t *ui_mdns_query(const ui_mdns_config_t *config, int *out_count);

/**
 * @brief Free results from TUI discovery query
 *
 * Releases memory allocated by ui_mdns_query().
 *
 * @param servers Pointer to server array (safe to pass NULL)
 *
 * @note Safe to call multiple times or with NULL pointer
 */
void ui_mdns_free_results(ui_mdns_server_t *servers);

/**
 * Interactive controller picker; Enter selects the highlighted row, Esc/q cancels.
 * Redirected input never chooses a server implicitly. Returns -1 when a terminal
 * is unavailable, or UI_MDNS_REFRESH when the user requests a new query.
 */
int ui_mdns_prompt_selection(const ui_mdns_server_t *servers, int count);

/**
 * Shared controller list: arrows/j/k move, Enter selects, digits choose by number,
 * Esc/q cancels. Resizes retain the selection and scroll it into view. A refresh
 * request returns UI_MDNS_REFRESH for the caller to query and republish results.
 * Returns -1 without waiting when no interactive terminal is available.
 */
int ui_mdns_select(const ui_mdns_server_t *servers, int count);

/** Refresh requests return this value; cancellation returns -1. */
#define UI_MDNS_REFRESH (-2)
/** Select with a caller-owned absolute monotonic deadline, retained across refreshes. */
int ui_mdns_select_until(const ui_mdns_server_t *servers, int count, uint64_t deadline);

/**
 * @brief Get best address representation for a discovered server
 *
 * Returns the most suitable address representation (IPv4 > IPv6 > hostname)
 * for connecting to a discovered server.
 *
 * **Selection Priority:**
 * 1. IPv4 address (most universal)
 * 2. IPv6 address
 * 3. Advertised hostname (service labels are never connection destinations)
 *
 * @param server Discovered server
 * @return Pointer to best address string (points to field in server struct)
 */
const char *ui_mdns_get_best_address(const ui_mdns_server_t *server);

#ifdef __cplusplus
}
#endif

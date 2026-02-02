/**
 * @file discovery_tui.h
 * @brief TUI-based service discovery for ascii-chat client
 *
 * Implements interactive mDNS-based discovery of ascii-chat servers on the local network.
 * Provides a terminal UI for browsing and selecting available servers without manual IP entry.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Discovered server information from mDNS
 */
typedef struct {
  char name[256];    ///< Service instance name (e.g., "swift-river-canyon")
  char address[256]; ///< Server address (IPv4, IPv6, or hostname)
  uint16_t port;     ///< Server port number
  char ipv4[16];     ///< IPv4 address (if available)
  char ipv6[46];     ///< IPv6 address (if available)
  uint32_t ttl;      ///< TTL remaining (seconds)
} discovery_tui_server_t;

/**
 * @brief Configuration for TUI discovery
 */
typedef struct {
  int timeout_ms;  ///< Maximum time to wait for responses (default: 2000)
  int max_servers; ///< Maximum servers to collect (default: 20)
  bool quiet;      ///< Suppress discovery messages (default: false)
} discovery_tui_config_t;

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
 * - Caller is responsible for freeing the returned array with lan_discovery_free_results()
 * - Returned servers are valid until lan_discovery_free_results() is called
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
 *         Must be freed with lan_discovery_free_results()
 *
 * @note Timeout includes network round-trip time, so 2000ms allows ~1.5s of actual waiting
 * @note Returns empty array (non-NULL with count=0) if no servers found, not NULL
 *
 * Example:
 * @code
 * discovery_tui_config_t config = {.timeout_ms = 2000, .max_servers = 20};
 * int count = 0;
 * discovery_tui_server_t *servers = discovery_tui_query(&config, &count);
 *
 * if (servers && count > 0) {
 *     for (int i = 0; i < count; i++) {
 *         printf("%d: %s (%s:%d)\n", i+1, servers[i].name, servers[i].address, servers[i].port);
 *     }
 *     // User selects server...
 *     discovery_tui_free_results(servers);
 * }
 * @endcode
 */
discovery_tui_server_t *discovery_tui_query(const discovery_tui_config_t *config, int *out_count);

/**
 * @brief Free results from TUI discovery query
 *
 * Releases memory allocated by discovery_tui_query().
 *
 * @param servers Pointer to server array (safe to pass NULL)
 *
 * @note Safe to call multiple times or with NULL pointer
 */
void discovery_tui_free_results(discovery_tui_server_t *servers);

/**
 * @brief Display discovered servers to user and prompt for selection
 *
 * Shows a numbered list of discovered servers and prompts user to select one.
 * Handles invalid input with re-prompting.
 *
 * **User Experience:**
 * ```
 * Available ascii-chat servers on LAN:
 *   1. swift-river-canyon (192.168.1.100:27224)
 *   2. quiet-mountain-lake (192.168.1.101:27224)
 *   3. gentle-forest-breeze (192.168.1.102:27224)
 *
 * Select server (1-3) or press Enter to cancel: _
 * ```
 *
 * **Behavior:**
 * - Displays each server with instance name and best-guess address
 * - Shows IPv4 address if available, falls back to IPv6 or hostname
 * - Handles non-numeric input with error message and re-prompt
 * - Returns -1 if user presses Enter/Ctrl+C to cancel
 * - Returns 0-based index of selected server
 *
 * @param servers Array of discovered servers
 * @param count Number of servers in array
 * @return Index of selected server (0 to count-1), or -1 to cancel
 *
 * @note This function performs interactive I/O - may not be suitable for automated contexts
 * @note For automated selection, use servers[0] directly instead
 */
int discovery_tui_prompt_selection(const discovery_tui_server_t *servers, int count);

/**
 * @brief TUI-based server selection with formatted display
 *
 * Displays discovered servers in a formatted terminal UI with:
 * - Clear screen and boxed display
 * - Server list with numbering and addresses
 * - "No results" message if no servers available
 * - Interactive numeric selection prompt
 *
 * **Display Example (3 servers):**
 * ```
 * ╭─ 🔍 ascii-chat Server Discovery ────────────╮
 * │
 * │ Found 3 servers on your local network:
 * │
 * │   [1] swift-river-canyon     192.168.1.100:27224
 * │   [2] quiet-mountain-lake    192.168.1.101:27224
 * │   [3] gentle-forest-breeze   192.168.1.102:27224
 * │
 * ╰────────────────────────────────────────────╯
 *
 * Enter server number (1-3) or press Enter to cancel:
 * ```
 *
 * **No Results Display:**
 * ```
 * ╭─ 🔍 ascii-chat Server Discovery ─╮
 * │
 * │   No servers found on local network
 * │
 * │   Make sure an ascii-chat server is running on your LAN
 * │   Or provide a server address manually: ascii-chat client <address>
 * │
 * ╰─────────────────────────────────────────╯
 * ```
 *
 * @param servers Array of discovered servers
 * @param count Number of servers (0 for "no results")
 * @return 0-based index of selected server, or -1 to cancel
 */
int discovery_tui_select(const discovery_tui_server_t *servers, int count);

/**
 * @brief Get best address representation for a discovered server
 *
 * Returns the most suitable address representation (IPv4 > hostname > IPv6)
 * for connecting to a discovered server.
 *
 * **Selection Priority:**
 * 1. IPv4 address (most universal)
 * 2. Service name (if IPv4 not available)
 * 3. IPv6 address (last resort)
 *
 * @param server Discovered server
 * @return Pointer to best address string (points to field in server struct)
 */
const char *discovery_tui_get_best_address(const discovery_tui_server_t *server);

#ifdef __cplusplus
}
#endif

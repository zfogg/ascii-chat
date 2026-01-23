/**
 * @file lib/network/mdns/discovery.h
 * @brief Parallel mDNS and ACDS session discovery
 * @ingroup network_discovery
 *
 * Implements concurrent lookup on both mDNS (local LAN) and ACDS (internet discovery)
 * with "race to success" semantics - whichever discovery method finds the session first is used.
 *
 * **Three Usage Modes:**
 *
 * 1. **mDNS-only (safest, no ACDS)**
 *    - Input: session string only
 *    - Searches mDNS for TXT record containing session_string
 *    - No ACDS lookup, no network calls
 *    - Use case: Local LAN connections where server is on same network
 *    - Command: `ascii-chat swift-river-mountain`
 *
 * 2. **Verified ACDS (parallel with pubkey check)**
 *    - Input: session string + expected server pubkey
 *    - Searches mDNS (timeout 2s) AND ACDS (timeout 5s) in parallel threads
 *    - Verifies discovered server pubkey matches expected key in BOTH sources
 *    - Requires explicit `--server-key` flag with Ed25519 public key
 *    - Command: `ascii-chat --server-key $pubkey swift-river-mountain`
 *
 * 3. **Insecure ACDS (parallel without verification)**
 *    - Input: session string only
 *    - Searches mDNS (timeout 2s) AND ACDS (timeout 5s) in parallel threads
 *    - No pubkey verification (MITM-vulnerable, requires explicit `--acds-insecure` flag)
 *    - Use case: Test/development environments only
 *    - Command: `ascii-chat --acds-insecure swift-river-mountain`
 *
 * **Thread Architecture:**
 *
 * Both lookups run concurrently:
 *
 * ```
 * discover_session_parallel()
 *   │
 *   ├─ Thread A (mDNS):
 *   │   └─ Query TXT records, parse session_string
 *   │   └─ Extract host_pubkey from TXT
 *   │   └─ Return: IPv4/IPv6, port, pubkey (2s timeout)
 *   │
 *   ├─ Thread B (ACDS):
 *   │   └─ Connect to ACDS server
 *   │   └─ SESSION_LOOKUP request
 *   │   └─ Return: host_pubkey, session_info (5-8s timeout)
 *   │   └─ SESSION_JOIN to get server_address/port
 *   │
 *   └─ Whichever completes first wins, other is cancelled
 * ```
 *
 * **Security Model:**
 *
 * - **mDNS verification**: TXT record contains `host_pubkey=<hex>`, client checks against `--server-key`
 * - **ACDS verification**: ACDS stores `host_pubkey`, SESSION_LOOKUP returns it, client verifies
 * - **Cross-verification**: If both succeed, MUST have same pubkey (prevents split-brain)
 * - **TOFU on first use**: When no `--server-key` specified, mDNS-only mode (trust first TXT record seen)
 *
 * @author Claude <claude@anthropic.com>
 * @date January 2026
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "common.h"
#include "discovery/strings.h" // For is_session_string()
#include "network/mdns/discovery_tui.h" // For discovery_tui_server_t

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Discovery Result
// ============================================================================

/**
 * @brief Result from session discovery
 *
 * Returned by `discover_session_parallel()` with connection info for the discovered server.
 */
typedef struct {
  bool success;             ///< Discovery succeeded
  uint8_t host_pubkey[32];  ///< Ed25519 public key of discovered server
  char server_address[256]; ///< Server IP or hostname
  uint16_t server_port;     ///< Server port (typically 27224)
  enum {
    DISCOVERY_SOURCE_MDNS = 0, ///< Found via mDNS (local LAN)
    DISCOVERY_SOURCE_ACDS = 1, ///< Found via ACDS (internet)
  } source;                    ///< Which discovery method found the server

  // For mDNS results
  char mdns_service_name[256]; ///< mDNS service instance name (e.g., "swift-river-mountain")
  char mdns_hostname[256];     ///< mDNS hostname (e.g., "mycomputer.local")

  // For ACDS results (if applicable)
  uint8_t session_id[16];     ///< ACDS session UUID
  uint8_t participant_id[16]; ///< Assigned participant ID (from SESSION_JOIN)

  // Error details
  asciichat_error_t error; ///< Error code if !success
  char error_message[256]; ///< Human-readable error (if !success)
} discovery_result_t;

/**
 * @brief Session discovery configuration
 */
typedef struct {
  // Verification options
  const uint8_t *expected_pubkey; ///< Expected server pubkey (NULL = no verification)
  bool insecure_mode;             ///< Allow no verification (--acds-insecure flag)

  // Connection details
  char acds_server[256]; ///< ACDS server address (e.g., "localhost" or "discovery.ascii-chat.com")
  uint16_t acds_port;    ///< ACDS server port (default: 27225)

  // Timeouts
  uint32_t mdns_timeout_ms; ///< mDNS search timeout (default: 2000ms)
  uint32_t acds_timeout_ms; ///< ACDS lookup timeout (default: 5000ms)

  // Client identity (for SESSION_JOIN)
  const uint8_t *client_pubkey; ///< Client's Ed25519 public key (can be NULL)
  const uint8_t *client_seckey; ///< Client's Ed25519 secret key (can be NULL)
  const char *password;         ///< Optional session password (NULL if none)
} discovery_config_t;

/**
 * @brief Initialize discovery config with defaults
 *
 * Sets sensible defaults:
 * - ACDS server: localhost:27225 (debug), discovery.ascii-chat.com (release)
 * - Timeouts: mdns=2000ms, acds=5000ms
 * - Verification: None (expected_pubkey=NULL)
 * - Insecure mode: false
 *
 * @param config Configuration to initialize
 */
void discovery_config_init_defaults(discovery_config_t *config);

// ============================================================================
// Parallel Discovery
// ============================================================================

/**
 * @brief Look up session in parallel on mDNS and ACDS
 *
 * Spawns two concurrent threads to search both mDNS (local LAN) and ACDS (internet).
 * Returns immediately when either finds the session, cancelling the other search.
 *
 * **Verification Logic:**
 *
 * If `config->expected_pubkey` is set:
 * - mDNS result: Verify TXT record contains matching `host_pubkey`
 * - ACDS result: Verify SESSION_LOOKUP response contains matching `host_pubkey`
 * - If both find result: Verify they have same pubkey (cross-check)
 * - On mismatch: Return ERROR_CRYPTO_VERIFY_FAILED
 *
 * If `config->expected_pubkey` is NULL but `config->insecure_mode` is true:
 * - Skip all verification, accept first result found
 * - WARNING: Vulnerable to MITM attacks!
 *
 * If `config->expected_pubkey` is NULL and `config->insecure_mode` is false:
 * - Search mDNS only (ACDS thread not started)
 * - Use TOFU (Trust On First Use): accept first mDNS result found
 *
 * **Thread Lifecycle:**
 *
 * - Both threads start immediately
 * - Whichever completes first signals success via condition variable
 * - Other thread is joined (cleaned up)
 * - Function returns with result
 *
 * @param session_string Session string to find (e.g., "swift-river-mountain")
 * @param config Discovery configuration
 * @param result Discovery result (output)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note This function BLOCKS until one discovery method succeeds or both timeout
 * @note Not thread-safe for concurrent calls - use only one discovery at a time
 */
asciichat_error_t discover_session_parallel(const char *session_string, const discovery_config_t *config,
                                            discovery_result_t *result);

// ============================================================================
// mDNS Query API (Core Module)
// ============================================================================

/**
 * @brief Discover ascii-chat servers on local network via mDNS
 *
 * Queries for mDNS _ascii-chat._tcp services and returns discovered servers.
 * This is the core discovery function used by both:
 * - Parallel discovery threads (discover_session_parallel)
 * - TUI wrapper (discovery_tui_query)
 *
 * @param timeout_ms Query timeout in milliseconds (default: 2000)
 * @param max_servers Maximum servers to discover (default: 20)
 * @param quiet If true, suppresses progress messages
 * @param out_count Output: number of servers discovered
 * @return Array of discovered servers, or NULL on error. Use discovery_mdns_free() to free.
 */
discovery_tui_server_t *discovery_mdns_query(int timeout_ms, int max_servers, bool quiet, int *out_count);

/**
 * @brief Free memory from mDNS discovery results
 */
void discovery_mdns_free(discovery_tui_server_t *servers);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert Ed25519 public key to hex string
 *
 * Converts 32-byte binary pubkey to lowercase hex (64 characters).
 *
 * @param pubkey Binary public key (32 bytes)
 * @param hex_out Output hex string buffer (must be at least 65 bytes)
 */
void pubkey_to_hex(const uint8_t pubkey[32], char hex_out[65]);

/**
 * @brief Convert hex string to Ed25519 public key
 *
 * Converts 64-character hex string to 32-byte binary pubkey.
 *
 * @param hex_str Hex string (64 characters)
 * @param pubkey_out Output binary pubkey (32 bytes)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM if invalid hex
 */
asciichat_error_t hex_to_pubkey(const char *hex_str, uint8_t pubkey_out[32]);

#ifdef __cplusplus
}
#endif

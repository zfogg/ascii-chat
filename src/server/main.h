/**
 * @file server/main.h
 * @ingroup server_main
 * @brief ascii-chat Server Mode Entry Point Header
 *
 * This header exposes the server mode entry point for the unified binary architecture.
 * The unified binary dispatches to server_main() when invoked as `ascii-chat server`.
 *
 * ## Unified Binary Architecture
 *
 * The ascii-chat application uses a single binary with multiple operating modes:
 * - **ascii-chat server** - Run as server (multi-client connection manager)
 * - **ascii-chat client** - Run as client (connects to server, streams video/audio)
 *
 * This design provides several benefits:
 * - Simplified distribution (single binary to install)
 * - Reduced disk space (shared library code)
 * - Easier testing (one binary to build and deploy)
 * - Consistent versioning across modes
 *
 * ## Mode Entry Point Contract
 *
 * Each mode entry point (server_main, client_main) must:
 * - Accept no arguments: `int mode_main(void)`
 * - Options are already parsed by main dispatcher (available via global opt_* variables)
 * - Return 0 on success, non-zero error code on failure
 * - Perform mode-specific initialization and main loop
 * - Perform complete cleanup before returning
 *
 * ## Implementation Notes
 *
 * The server_main() function is the original main() from src/server/main.c,
 * adapted to the new dispatcher pattern. Common initialization (options parsing,
 * logging setup, lock debugging) now happens in src/main.c before dispatch.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 * @version 2.0
 */

#pragma once

#include <ascii-chat/network/rate_limit/rate_limit.h>
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/crypto/key_types.h>
#include "session/host.h"
#include <ascii-chat/options/options.h>
#include "stats.h"
#include "client.h"

/* ============================================================================
 * Global Server State
 * ============================================================================ */

/**
 * @brief Global connection rate limiter
 *
 * Shared rate limiter instance used by packet handlers to prevent DoS attacks.
 * Tracks connection attempts and packet rates per IP address.
 */
extern rate_limiter_t *g_rate_limiter;

/**
 * @brief Global client manager
 *
 * Registry for all connected clients. Used to track client state, routing,
 * and lifecycle management across server threads.
 */
extern client_manager_t g_client_manager;

/**
 * @brief Read-write lock protecting client manager
 *
 * Protects concurrent access to g_client_manager from server threads.
 * Use rwlock_rdlock() for reads, rwlock_wrlock() for writes.
 */
extern rwlock_t g_client_manager_rwlock;

/**
 * @brief Server shutdown flag
 *
 * Atomic boolean used to signal all threads to exit gracefully.
 * Set to true when server receives SIGINT or other shutdown signal.
 */
extern atomic_bool g_server_should_exit;

/**
 * @brief Global audio mixer
 *
 * Volatile pointer to the multi-client audio mixer instance.
 * Used to mix audio from multiple clients for playback.
 */
extern mixer_t *volatile g_audio_mixer;

/**
 * @brief Server encryption enabled flag
 *
 * Indicates whether end-to-end encryption is enabled for this server.
 */
extern bool g_server_encryption_enabled;

/**
 * @brief Server's private key
 *
 * Server's Ed25519 private key used for digital signatures and key exchange.
 */
extern private_key_t g_server_private_key;

/**
 * @brief Array of server identity keys
 *
 * Additional server identity keys for multi-key support.
 * Size is limited by MAX_IDENTITY_KEYS.
 */
extern private_key_t g_server_identity_keys[MAX_IDENTITY_KEYS];

/**
 * @brief Number of server identity keys in use
 *
 * Tracks how many keys in g_server_identity_keys are valid.
 */
extern size_t g_num_server_identity_keys;

/**
 * @brief Whitelist of allowed client public keys
 *
 * Array of allowed client Ed25519 public keys for authentication.
 * Size is limited by MAX_CLIENTS.
 */
extern public_key_t g_client_whitelist[MAX_CLIENTS];

/**
 * @brief Number of whitelisted clients
 *
 * Tracks how many keys in g_client_whitelist are valid.
 */
extern size_t g_num_whitelisted_clients;

/**
 * @brief Server context - encapsulates all server state
 *
 * This structure holds all server-wide state that was previously stored in
 * global variables. It's passed to client handlers via tcp_server user_data,
 * reducing global state and improving modularity.
 *
 * DESIGN RATIONALE:
 * =================
 * - Reduces global state: All server state in one place
 * - Improves testability: Can create multiple independent server instances
 * - Better encapsulation: Clear ownership of resources
 * - Thread-safe: Context is read-only after initialization
 *
 * LIFETIME:
 * =========
 * - Created in server_main() before tcp_server_init()
 * - Passed to tcp_server via config.user_data
 * - Available to client handlers via tcp_client_context_t.user_data
 * - Destroyed in server_main() after tcp_server_destroy()
 *
 * @ingroup server_main
 */
typedef struct server_context_t {
  // TCP server instance
  tcp_server_t *tcp_server; ///< TCP server managing connections

  // Rate limiting
  rate_limiter_t *rate_limiter; ///< Connection and packet rate limiter

  // Client management
  client_manager_t *client_manager; ///< Client registry and state
  rwlock_t *client_manager_rwlock;  ///< RW lock protecting client manager

  // Server lifecycle
  atomic_bool *server_should_exit; ///< Shutdown flag

  // Audio mixing
  mixer_t *audio_mixer; ///< Multi-client audio mixer

  // Statistics
  server_stats_t *stats; ///< Server statistics
  mutex_t *stats_mutex;  ///< Mutex protecting stats

  // Cryptography
  bool encryption_enabled;           ///< Whether encryption is enabled
  private_key_t *server_private_key; ///< Server's private key
  public_key_t *client_whitelist;    ///< Whitelisted client public keys
  size_t num_whitelisted_clients;    ///< Number of whitelisted clients

  // Session library integration
  session_host_t *session_host; ///< Session host for discovery mode support
} server_context_t;

/**
 * @brief Server mode entry point for unified binary
 *
 * This function implements the complete server lifecycle including:
 * - Server-specific initialization (crypto, shutdown callback)
 * - Network socket setup and binding
 * - Main connection accept loop
 * - Client lifecycle management
 * - Graceful shutdown and cleanup
 *
 * Options are already parsed by the main dispatcher before this function
 * is called, so they are available via global opt_* variables.
 *
 * @return 0 on success, non-zero error code on failure
 *
 * @par Example
 * @code{.sh}
 * # Invoked by dispatcher after options are parsed:
 * ascii-chat server --port 8080
 * # Options parsed in main.c, then server_main() called
 * @endcode
 */
int server_main(void);

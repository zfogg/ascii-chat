/**
 * @file server_like.h
 * @ingroup session
 * @brief Shared initialization, networking, and teardown for server-like modes
 *
 * Provides a unified lifecycle for modes that act as servers (accepting client
 * connections). Both the main ascii-chat server and the discovery-service (ACDS)
 * use this layer, analogous to how client_like serves mirror/client/discovery.
 *
 * ## Modes Supported
 *
 * - **Server mode**: TCP/WebSocket server with audio mixing and H.265 encoding
 * - **Discovery-Service mode**: ACDS session discovery and WebRTC signaling
 *
 * ## Shared Responsibilities
 *
 * This layer owns and manages:
 * - TCP server lifecycle (init, accept loop, destroy)
 * - WebSocket server lifecycle (init, event loop thread, destroy)
 * - mDNS context and service advertisement
 * - UPnP port mapping
 * - Per-client crypto handshake (single shared implementation)
 * - Signal handler registration (SIGINT, SIGTERM, SIGPIPE)
 * - Keepawake system (platform sleep prevention)
 * - Status screen thread with keyboard input and grep mode
 * - File descriptor limit raising
 * - Proper cleanup ordering
 *
 * ## Mode-Specific Responsibilities
 *
 * Mode files provide callbacks for:
 * - `init_fn`: Mode-specific setup (crypto keys, rate limiter, mixer, DB, etc.)
 * - `interrupt_fn`: Async-signal-safe shutdown (close sockets, set exit flag)
 * - `cleanup_fn`: Mode-specific teardown (clients, threads, resources)
 * - `status_fn`: Optional status screen data population
 *
 * ## Memory and Lifecycle
 *
 * server_like owns TCP server, WebSocket server, mDNS context, and UPnP context.
 * Modes access these via accessors (e.g., session_server_like_get_tcp_server()).
 * Mode-specific state (clients, mixer, DB) is owned by the mode.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/mdns/mdns.h>
#include <ascii-chat/network/nat/upnp.h>
#include <ascii-chat/network/tcp/server.h>
#include <ascii-chat/network/websocket/server.h>
#include <ascii-chat/ui/status.h>
#include <stdbool.h>

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * Mode-specific initialization callback.
 *
 * Called once after shared infrastructure is up (TCP server bound, WebSocket
 * started, mDNS/UPnP configured). Should set up mode-specific state: crypto
 * keys, rate limiter, mixer, database, etc.
 *
 * The TCP server is accessible via session_server_like_get_tcp_server().
 *
 * @param user_data Opaque pointer from config.init_user_data
 * @return ASCIICHAT_OK, or error code to abort (cleanup still runs)
 */
typedef asciichat_error_t (*session_server_like_init_fn)(void *user_data);

/**
 * Async-signal-safe interrupt callback.
 *
 * Called from signal handler context (SIGINT / SIGTERM). Must only use
 * async-signal-safe operations. Responsible for:
 * 1. Setting the mode's shutdown flag (atomic)
 * 2. Closing TCP listen sockets to unblock accept loop
 * 3. Optionally stopping WebSocket server
 *
 * Use session_server_like_get_tcp_server() to access listen sockets.
 */
typedef void (*session_server_like_interrupt_fn)(void);

/**
 * Mode-specific teardown callback.
 *
 * Called after WebSocket server is stopped but before WebSocket/TCP servers
 * are destroyed. The TCP client registry is still available for iteration
 * (e.g., to disconnect all clients gracefully).
 *
 * @param user_data Opaque pointer from config.cleanup_user_data
 */
typedef void (*session_server_like_cleanup_fn)(void *user_data);

/**
 * Status screen data callback.
 *
 * Called at GET_OPTION(fps) Hz by the status screen thread.
 * Should fill *out_status with current state for ui_status_display().
 * If NULL in config, the status screen is not shown even if --status-screen is set.
 *
 * The callback receives a zeroed ui_status_t and must populate ALL fields
 * it wants displayed.
 *
 * @param user_data   Opaque pointer from config.status_user_data
 * @param out_status  Output struct to populate (pre-zeroed by caller)
 */
typedef void (*session_server_like_status_fn)(void *user_data, ui_status_t *out_status);

/* ============================================================================
 * Configuration Structure
 * ============================================================================ */

/**
 * Configuration for session_server_like_run().
 *
 * Network settings (port, address, WebSocket port, TLS paths) are read
 * from GET_OPTION() automatically. Callers provide callbacks, handler
 * functions, and feature toggles.
 */
typedef struct session_server_like_config {
  /* ================================================================ */
  /* Required Callbacks                                                */
  /* ================================================================ */

  /** Mode-specific initialization (required, never NULL). */
  session_server_like_init_fn init_fn;
  void *init_user_data;

  /** Async-signal-safe interrupt handler (required, never NULL). */
  session_server_like_interrupt_fn interrupt_fn;

  /* ================================================================ */
  /* Optional Callbacks                                                */
  /* ================================================================ */

  /** Mode-specific teardown (optional, NULL to skip). */
  session_server_like_cleanup_fn cleanup_fn;
  void *cleanup_user_data;

  /** Status screen data provider (optional, NULL = no status screen). */
  session_server_like_status_fn status_fn;
  void *status_user_data;

  /* ================================================================ */
  /* TCP Server                                                        */
  /* ================================================================ */

  /** Per-connection handler callback (required). */
  tcp_client_handler_fn tcp_handler;

  /** User data passed to each tcp_handler invocation. */
  void *tcp_user_data;

  /* ================================================================ */
  /* WebSocket Server (optional)                                       */
  /* ================================================================ */

  struct {
    /** Enable WebSocket server alongside TCP. */
    bool enabled;

    /** Per-connection handler callback (required if enabled). */
    websocket_client_handler_fn handler;

    /** User data passed to each WebSocket handler invocation. */
    void *user_data;
  } websocket;

  /* ================================================================ */
  /* mDNS (optional)                                                   */
  /* ================================================================ */

  struct {
    /** Enable mDNS service advertisement. */
    bool enabled;

    /** Service name (e.g., "ascii-chat-Server"). NULL for deferred advertisement. */
    const char *service_name;

    /** Service type (e.g., "_ascii-chat._tcp"). */
    const char *service_type;
  } mdns;

  /* ================================================================ */
  /* UPnP (optional)                                                   */
  /* ================================================================ */

  struct {
    /** Enable UPnP port mapping. */
    bool enabled;

    /** Description for port mapping (e.g., "ascii-chat Server"). */
    const char *description;
  } upnp;

  /* ================================================================ */
  /* System                                                            */
  /* ================================================================ */

  /** Raise file descriptor limit on startup. */
  bool raise_fd_limit;

  /** Target file descriptor limit (e.g., 65536). Ignored if raise_fd_limit is false. */
  int fd_limit_target;

} session_server_like_config_t;

/* ============================================================================
 * Entry Point
 * ============================================================================ */

/**
 * Run a server-like mode with unified lifecycle management.
 *
 * Orchestrates the complete lifecycle:
 *
 * ## Initialization Sequence
 * 1. Keepawake validation and setup
 * 2. File descriptor limit raising (if configured)
 * 3. Signal handler registration (SIGINT, SIGTERM, SIGPIPE)
 * 4. TCP server init (binds to GET_OPTION port/address)
 * 5. UPnP port mapping (if enabled)
 * 6. mDNS context init and optional immediate advertisement
 * 7. WebSocket server init and event loop thread (if enabled)
 * 8. Mode-specific init_fn()
 * 9. Status screen and keyboard threads (if status_fn provided)
 * 10. TCP accept loop (blocks until shutdown)
 *
 * ## Cleanup (always runs, in order)
 * 11. Join status screen and keyboard threads
 * 12. Stop WebSocket server (atomic stop, cancel, join with 500ms timeout)
 * 13. Mode-specific cleanup_fn() (TCP registry still available)
 * 14. Destroy WebSocket server
 * 15. Destroy TCP server
 * 16. Close UPnP port mapping
 * 17. Destroy mDNS context
 * 18. Disable keepawake
 *
 * @param config Mode configuration (must not be NULL)
 * @return ASCIICHAT_OK on success, or first error from init or accept loop
 */
asciichat_error_t session_server_like_run(const session_server_like_config_t *config);

/* ============================================================================
 * Accessors (for use by mode callbacks)
 * ============================================================================ */

/**
 * Get the TCP server instance owned by server_like.
 *
 * Available after tcp_server_init() completes (i.e., during init_fn and after).
 * Modes use this for client registry operations and to close listen sockets
 * in interrupt_fn.
 *
 * @return Pointer to TCP server, or NULL if not initialized
 */
tcp_server_t *session_server_like_get_tcp_server(void);

/**
 * Get the mDNS context owned by server_like.
 *
 * Available after mDNS init completes (i.e., during init_fn and after).
 * Use for deferred advertisement when service_name is not known at config time.
 *
 * @return Pointer to mDNS context, or NULL if not initialized or disabled
 */
asciichat_mdns_t *session_server_like_get_mdns_ctx(void);

/**
 * Get the UPnP context owned by server_like.
 *
 * Available after UPnP mapping completes (i.e., during init_fn and after).
 * Use for querying the public address.
 *
 * @return Pointer to UPnP context, or NULL if not initialized or disabled
 */
nat_upnp_context_t *session_server_like_get_upnp_ctx(void);

/**
 * Get the WebSocket server instance owned by server_like.
 *
 * Available after WebSocket init completes (i.e., during init_fn and after).
 *
 * @return Pointer to WebSocket server, or NULL if not initialized or disabled
 */
websocket_server_t *session_server_like_get_websocket_server(void);

/* ============================================================================
 * Helpers (for use by mode callbacks)
 * ============================================================================ */

/**
 * Advertise or re-advertise an mDNS service.
 *
 * Convenience wrapper for modes that need deferred or updated advertisement
 * (e.g., server mode advertises after ACDS registration provides session string).
 *
 * @param name    Service instance name
 * @param type    Service type (e.g., "_ascii-chat._tcp")
 * @param port    Service port
 * @param txt     Optional TXT records (NULL-terminated array), or NULL
 * @param txt_count Number of TXT records
 * @return ASCIICHAT_OK on success, error if mDNS is not initialized
 */
asciichat_error_t session_server_like_mdns_advertise(const char *name, const char *type, uint16_t port,
                                                     const char **txt, size_t txt_count);

/**
 * Perform the server-side crypto handshake on a transport.
 *
 * Runs the full handshake sequence: receive PROTOCOL_VERSION, send parameters,
 * key exchange, auth challenge, and completion. Both server and discovery-service
 * modes use this single implementation.
 *
 * The caller must initialize the handshake context (crypto_handshake_init or
 * crypto_handshake_init_with_password) and configure keys/whitelist on it
 * before calling this function.
 *
 * @param ctx       Initialized handshake context with keys/auth configured
 * @param transport ACIP transport for the client connection
 * @return ASCIICHAT_OK on success, error code on handshake failure
 */
asciichat_error_t session_server_like_handshake(crypto_handshake_context_t *ctx, acip_transport_t *transport);

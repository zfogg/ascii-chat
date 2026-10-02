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
 * - Signal handler registration (SIGINT, SIGTERM, SIGPIPE) with persistent shutdown flag
 * - Keepawake system (platform sleep prevention)
 * - Status screen thread with keyboard input and grep mode
 * - File descriptor limit raising
 * - Proper cleanup ordering with partial-init safety
 *
 * ## Mode-Specific Responsibilities
 *
 * Mode files provide callbacks for:
 * - `init_fn`: Mode-specific setup (crypto keys, rate limiter, mixer, DB, etc.)
 *   - Called AFTER network infrastructure is ready but BEFORE WebSocket thread starts
 *   - Can detect early shutdown via session_server_like_shutdown_requested()
 *   - Failure triggers clean shutdown (cleanup still runs)
 * - `interrupt_fn(int sig)`: Async-signal-safe shutdown (close sockets, set exit flag)
 *   - Called from signal handler (SIGINT/SIGTERM)
 *   - Must use only async-signal-safe operations
 *   - Receives signal number as parameter
 * - `cleanup_fn`: Mode-specific teardown (clients, threads, resources)
 *   - Called after WebSocket event loop stops
 *   - TCP registry still available for iteration
 *   - Only called if config provided (safe against NULL)
 * - `status_fn`: Optional status screen data population
 *   - Called at GET_OPTION(fps) Hz by dedicated status thread
 *   - Receives pre-zeroed ui_status_t struct to populate
 *   - NULL = no status screen even if --status-screen is set
 *
 * ## Memory and Lifecycle
 *
 * server_like owns TCP server, WebSocket server, mDNS context, and UPnP context.
 * Modes access these via accessors (e.g., session_server_like_get_tcp_server()).
 * Mode-specific state (clients, mixer, DB) is owned by the mode.
 *
 * CRITICAL SAFETY GUARANTEES:
 * - Cleanup operations only destroy resources that were successfully initialized
 * - Mode init failures do not cause undefined behavior during cleanup
 * - Early SIGINT/SIGTERM during init is preserved and detectable
 * - WebSocket callbacks do not run before mode init completes
 * - Signal handler type is correct (void(*)(int)) with no casts
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date April 2026 (Refactored)
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
 * Called from signal handler context (SIGINT / SIGTERM) with the signal number.
 * Must only use async-signal-safe operations. Responsible for:
 * 1. Setting the mode's shutdown flag (atomic)
 * 2. Closing TCP listen sockets to unblock accept loop
 * 3. Optionally stopping WebSocket server
 *
 * Use session_server_like_get_tcp_server() to access listen sockets.
 *
 * @param sig Signal number (SIGINT=2, SIGTERM=15)
 */
typedef void (*session_server_like_interrupt_fn)(int sig);

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
 * Orchestrates the complete lifecycle with proper initialization ordering to
 * ensure mode-specific state is ready before external clients can connect.
 *
 * ## Initialization Sequence
 * 1. Keepawake validation and setup
 * 2. File descriptor limit raising (if configured)
 * 3. Signal handler registration (SIGINT, SIGTERM, SIGPIPE) with shutdown persistence
 * 4. TCP server init (binds to GET_OPTION port/address)
 * 5. UPnP port mapping (if enabled)
 * 6. mDNS context init and optional immediate advertisement
 * 7. WebSocket server construction (object created, event loop thread deferred)
 * 8. Mode-specific init_fn() - mode initializes its state (crypto keys, rate limiter, DB, etc.)
 * 9. WebSocket event loop thread start (only if mode init succeeds)
 * 10. Status screen and keyboard threads (if status_fn provided)
 * 11. TCP accept loop (blocks until shutdown)
 *
 * CRITICAL ORDERING NOTES:
 * - WebSocket event loop thread ONLY starts after mode init succeeds (step 9, not step 7)
 * - This prevents clients from arriving before mode state is ready
 * - If SIGINT/SIGTERM arrives during init, subsequent steps are skipped
 * - Mode-specific init can check session_server_like_shutdown_requested() for early shutdowns
 *
 * ## Cleanup (always runs, in order, guarded against partial init)
 * 12. Join status screen and keyboard threads
 * 13. Stop WebSocket server (only if thread was started, atomic stop, cancel, join with 500ms timeout)
 * 14. Mode-specific cleanup_fn() (TCP registry still available, guarded by mode state flags)
 * 15. Destroy WebSocket server (only if successfully initialized)
 * 16. Destroy TCP server
 * 17. Close UPnP port mapping
 * 18. Destroy mDNS context
 * 19. Disable keepawake
 *
 * SAFETY GUARANTEES:
 * - Cleanup operations only touch resources that were successfully initialized
 * - Signal handler safely sets shutdown flag during early initialization
 * - Modes can detect early shutdown via session_server_like_shutdown_requested()
 * - Partial initialization failures do not trigger undefined behavior in cleanup
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
 * READINESS GUARANTEE: Available from step 4 onward (during init_fn, interrupt_fn, cleanup_fn).
 * Modes use this for client registry operations and to close listen sockets in interrupt_fn.
 * The TCP server is guaranteed to be valid during init_fn and in interrupt_fn (async-signal context).
 *
 * @return Pointer to TCP server (never NULL after step 4), or NULL if called before TCP init
 */
tcp_server_t *session_server_like_get_tcp_server(void);

/**
 * Get the mDNS context owned by server_like.
 *
 * READINESS GUARANTEE: Available from step 6 onward (during init_fn and after).
 * Use for deferred advertisement when service_name is not known at config time.
 * NULL if mDNS is disabled or initialization failed (non-fatal).
 *
 * @return Pointer to mDNS context, or NULL if not initialized or disabled
 */
asciichat_mdns_t *session_server_like_get_mdns_ctx(void);

/**
 * Get the UPnP context owned by server_like.
 *
 * READINESS GUARANTEE: Available from step 5 onward (during init_fn and after).
 * Use for querying the public address. NULL if port mapping failed or was disabled.
 * Failure to map UPnP is non-fatal; WebRTC fallback will be used.
 *
 * @return Pointer to UPnP context, or NULL if not initialized or disabled
 */
nat_upnp_context_t *session_server_like_get_upnp_ctx(void);

/**
 * Get the WebSocket server instance owned by server_like.
 *
 * READINESS GUARANTEE: WebSocket object is constructed at step 7, but the event loop
 * thread does NOT start until after mode init succeeds (step 9). Callbacks will not run
 * until after init_fn returns successfully. This prevents client connections before mode
 * state is ready.
 *
 * Available for configuration during init_fn, and for state queries after init_fn succeeds.
 * The WebSocket object is valid for the lifetime of the session_server_like_run() call.
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

/**
 * Check if a shutdown request was received during initialization.
 *
 * Modes can use this during init_fn to detect early SIGINT/SIGTERM signals
 * and exit gracefully instead of proceeding with partial initialization.
 *
 * @return true if SIGINT or SIGTERM was received, false otherwise
 */
bool session_server_like_shutdown_requested(void);

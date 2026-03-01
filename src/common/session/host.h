/**
 * @file session/host.h
 * @brief 🏠 Server-side session hosting abstraction
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * This header provides the server-side session hosting abstraction,
 * encapsulating client management, connection acceptance, and event handling
 * for session hosts.
 *
 * CORE FEATURES:
 * ==============
 * - Server lifecycle management (start, stop)
 * - Client connection handling (add, remove, lookup)
 * - Callback-based event notification
 * - Encryption and authentication support
 *
 * USAGE:
 * ======
 * @code{.c}
 * // Define callbacks
 * void on_client_join(session_host_t *h, uint32_t id, void *data) {
 *     printf("Client %u joined\n", id);
 * }
 *
 * void on_frame(session_host_t *h, uint32_t id, const image_t *frame, void *data) {
 *     // Process video frame from client
 * }
 *
 * // Create and start host
 * session_host_config_t config = {
 *     .port = 27224,
 *     .max_clients = 32,
 *     .callbacks = { .on_client_join = on_client_join, .on_frame_received = on_frame }
 * };
 * session_host_t *h = session_host_create(&config);
 * session_host_start(h);
 *
 * // Run until stopped...
 *
 * // Cleanup
 * session_host_destroy(h);
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/video/rgba/image.h>

/* ============================================================================
 * Session Host Types
 * ============================================================================ */

/**
 * @brief Opaque session host handle
 *
 * Manages server state, client connections, and event callbacks.
 * Created via session_host_create(), destroyed via session_host_destroy().
 *
 * @ingroup session
 */
typedef struct session_host session_host_t;

/**
 * @brief Client information structure
 *
 * Contains basic information about a connected client.
 *
 * @ingroup session
 */
typedef struct {
  /** @brief Unique client identifier */
  uint32_t client_id;

  /** @brief Client IP address */
  char ip_address[64];

  /** @brief Client port */
  int port;

  /** @brief Client is currently streaming video */
  bool video_active;

  /** @brief Client is currently streaming audio */
  bool audio_active;

  /** @brief Connection timestamp (Unix time) */
  uint64_t connected_at;
} session_host_client_info_t;

/**
 * @brief Callback function prototypes for session host events
 * @ingroup session
 */
typedef struct {
  /**
   * @brief Called when a client joins the session
   * @param h Host handle
   * @param client_id New client's ID
   * @param user_data User-provided context pointer
   */
  void (*on_client_join)(session_host_t *h, uint32_t client_id, void *user_data);

  /**
   * @brief Called when a client leaves the session
   * @param h Host handle
   * @param client_id Departing client's ID
   * @param user_data User-provided context pointer
   */
  void (*on_client_leave)(session_host_t *h, uint32_t client_id, void *user_data);

  /**
   * @brief Called when video frame received from a client
   * @param h Host handle
   * @param client_id Source client's ID
   * @param frame Video frame image
   * @param user_data User-provided context pointer
   */
  void (*on_frame_received)(session_host_t *h, uint32_t client_id, const image_t *frame, void *user_data);

  /**
   * @brief Called when audio samples received from a client
   * @param h Host handle
   * @param client_id Source client's ID
   * @param samples Audio sample buffer (float format)
   * @param count Number of samples
   * @param user_data User-provided context pointer
   */
  void (*on_audio_received)(session_host_t *h, uint32_t client_id, const float *samples, size_t count, void *user_data);

  /**
   * @brief Called when an error occurs
   * @param h Host handle
   * @param error Error code
   * @param message Error message
   * @param user_data User-provided context pointer
   */
  void (*on_error)(session_host_t *h, asciichat_error_t error, const char *message, void *user_data);
} session_host_callbacks_t;

/**
 * @brief Configuration for session host
 * @ingroup session
 */
typedef struct {
  /** @brief Port to listen on (default: 27224) */
  int port;

  /** @brief IPv4 address to bind to (NULL for any) */
  const char *ipv4_address;

  /** @brief IPv6 address to bind to (NULL for any) */
  const char *ipv6_address;

  /** @brief Maximum number of clients */
  int max_clients;

  /** @brief Enable encryption (default: true) */
  bool encryption_enabled;

  /** @brief Path to server identity key */
  const char *key_path;

  /** @brief Password for client authentication (optional) */
  const char *password;

  /** @brief Event callbacks */
  session_host_callbacks_t callbacks;

  /** @brief User-provided context pointer passed to callbacks */
  void *user_data;
} session_host_config_t;

/* ============================================================================
 * Session Host Lifecycle Functions
 * @{
 */

/**
 * @brief Create a new session host
 * @param config Host configuration (must not be NULL)
 * @return Pointer to host handle, or NULL on failure
 *
 * Creates a session host with the specified configuration.
 * The host is not started until session_host_start() is called.
 *
 * @note Call session_host_destroy() to free resources when done.
 * @note On failure, sets asciichat_errno with error details.
 *
 * @ingroup session
 */
session_host_t *session_host_create(const session_host_config_t *config);

/**
 * @brief Destroy session host and free resources
 * @param host Host to destroy (can be NULL)
 *
 * Stops if running, disconnects all clients, and releases all resources.
 * Safe to call with NULL.
 *
 * @ingroup session
 */
void session_host_destroy(session_host_t *host);

/** @} */

/* ============================================================================
 * Session Host Server Control Functions
 * @{
 */

/**
 * @brief Start accepting client connections
 * @param host Host handle (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Binds to the configured port and starts accepting connections.
 * This function may start background threads for connection handling.
 *
 * @ingroup session
 */
asciichat_error_t session_host_start(session_host_t *host);

/**
 * @brief Stop accepting connections and disconnect all clients
 * @param host Host handle (must not be NULL)
 *
 * Gracefully stops the server, disconnects all clients, and cleans up.
 *
 * @ingroup session
 */
void session_host_stop(session_host_t *host);

/**
 * @brief Check if host is running
 * @param host Host handle (can be NULL)
 * @return true if running, false otherwise
 *
 * @ingroup session
 */
bool session_host_is_running(session_host_t *host);

/** @} */

/* ============================================================================
 * Session Host Client Management Functions
 * @{
 */

/**
 * @brief Add a client from an accepted socket
 * @param host Host handle (must not be NULL)
 * @param socket Accepted client socket
 * @param ip Client IP address
 * @param port Client port
 * @return Assigned client ID on success, 0 on failure
 *
 * Registers a new client from an accepted socket connection. This is typically
 * called internally by the accept loop, but can be used for testing.
 *
 * @note on_client_join callback is invoked on successful registration.
 *
 * @ingroup session
 */
uint32_t session_host_add_client(session_host_t *host, socket_t socket, const char *ip, int port);

/**
 * @brief Add a memory participant (host's own media)
 * @param host Host handle (must not be NULL)
 * @return Assigned participant ID on success, 0 on failure
 *
 * Registers a memory participant for the host's own webcam/audio. This allows
 * the host to participate in the session without network loopback. Media is
 * injected directly into the mixer via session_host_inject_frame().
 *
 * @note on_client_join callback is invoked on successful registration.
 * @note Only one memory participant per host is supported.
 *
 * @ingroup session
 */
uint32_t session_host_add_memory_participant(session_host_t *host);

/**
 * @brief Inject a video frame from memory participant
 * @param host Host handle (must not be NULL)
 * @param participant_id Memory participant ID
 * @param frame Video frame image (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Injects a video frame directly into the mixer from a memory participant.
 * This bypasses network I/O and is used when the host participates in the
 * session with their own webcam.
 *
 * @note Frame data is copied, so caller retains ownership.
 *
 * @ingroup session
 */
asciichat_error_t session_host_inject_frame(session_host_t *host, uint32_t participant_id, const image_t *frame);

/**
 * @brief Inject audio samples from memory participant
 * @param host Host handle (must not be NULL)
 * @param participant_id Memory participant ID
 * @param samples Audio sample buffer (float format, must not be NULL)
 * @param count Number of samples
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Injects audio samples directly into the mixer from a memory participant.
 * This bypasses network I/O and is used when the host participates in the
 * session with their own audio.
 *
 * @note Sample data is copied, so caller retains ownership.
 *
 * @ingroup session
 */
asciichat_error_t session_host_inject_audio(session_host_t *host, uint32_t participant_id, const float *samples,
                                            size_t count);

/**
 * @brief Remove a client by ID
 * @param host Host handle (must not be NULL)
 * @param client_id Client ID to remove
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Disconnects and removes a client from the session.
 *
 * @note on_client_leave callback is invoked before removal.
 *
 * @ingroup session
 */
asciichat_error_t session_host_remove_client(session_host_t *host, uint32_t client_id);

/**
 * @brief Find a client by ID
 * @param host Host handle (must not be NULL)
 * @param client_id Client ID to find
 * @param info Output client info structure (must not be NULL)
 * @return ASCIICHAT_OK on success, ERROR_NOT_FOUND if not found
 *
 * Retrieves information about a connected client.
 *
 * @ingroup session
 */
asciichat_error_t session_host_find_client(session_host_t *host, uint32_t client_id, session_host_client_info_t *info);

/**
 * @brief Get number of connected clients
 * @param host Host handle (can be NULL)
 * @return Number of connected clients, 0 if NULL
 *
 * @ingroup session
 */
int session_host_get_client_count(session_host_t *host);

/**
 * @brief Get list of all connected client IDs
 * @param host Host handle (must not be NULL)
 * @param ids Output array for client IDs (must not be NULL)
 * @param max_ids Maximum number of IDs to return (size of ids array)
 * @return Number of IDs written to array
 *
 * @ingroup session
 */
int session_host_get_client_ids(session_host_t *host, uint32_t *ids, int max_ids);

/** @} */

/* ============================================================================
 * Session Host Broadcast Functions
 * @{
 */

/**
 * @brief Broadcast ASCII frame to all clients
 * @param host Host handle (must not be NULL)
 * @param frame ASCII frame data (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Sends an ASCII frame to all connected clients.
 *
 * @ingroup session
 */
asciichat_error_t session_host_broadcast_frame(session_host_t *host, const char *frame);

/**
 * @brief Send ASCII frame to a specific client
 * @param host Host handle (must not be NULL)
 * @param client_id Target client ID
 * @param frame ASCII frame data (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup session
 */
asciichat_error_t session_host_send_frame(session_host_t *host, uint32_t client_id, const char *frame);

/** @} */

/* ============================================================================
 * Session Host Render Thread Functions
 * @{
 */

/**
 * @brief Start media rendering thread (video mixing and audio distribution)
 * @param host Host handle (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts the render thread which collects video frames from all participants,
 * generates mixed ASCII frames, and broadcasts them to all clients. Also handles
 * audio mixing and distribution.
 *
 * @note The host must be running before starting the render thread.
 * @note Call session_host_stop_render() to stop the thread.
 *
 * @ingroup session
 */
asciichat_error_t session_host_start_render(session_host_t *host);

/**
 * @brief Stop media rendering thread
 * @param host Host handle (must not be NULL)
 *
 * Gracefully stops the render thread and cleans up audio resources.
 *
 * @ingroup session
 */
void session_host_stop_render(session_host_t *host);

/** @} */

/* ============================================================================
 * Session Host Transport Functions (WebRTC Integration)
 * @{
 */

/**
 * @brief Forward declaration of ACIP transport
 *
 * Opaque transport interface for send/receive operations.
 * Used for WebRTC DataChannels, WebSockets, and other transports.
 *
 * @see acip_transport_t in network/acip/transport.h
 *
 * @ingroup session
 */
typedef struct acip_transport acip_transport_t;

/**
 * @brief Set an alternative transport for a specific client in the host
 *
 * Allows replacing a client's default TCP socket transport with an alternative
 * transport such as WebRTC DataChannel. Once set, the host will use this
 * transport for send/receive operations with the specified client.
 *
 * @param host Host handle (must not be NULL)
 * @param client_id Client ID whose transport should be replaced
 * @param transport Alternative transport to use (can be NULL to clear)
 * @return ASCIICHAT_OK on success, ERROR_NOT_FOUND if client not found, other error on failure
 *
 * @note This is used for WebRTC integration when DataChannel becomes ready
 * @note Transport ownership remains with caller (host does not free it)
 * @note If both socket and transport exist, transport takes precedence
 *
 * @ingroup session
 */
asciichat_error_t session_host_set_client_transport(session_host_t *host, uint32_t client_id,
                                                    acip_transport_t *transport);

/**
 * @brief Get the current transport for a specific client in the host
 *
 * Returns the currently active transport for the client, if any. May return NULL
 * if only socket transport is in use.
 *
 * @param host Host handle (must not be NULL)
 * @param client_id Client ID whose transport should be retrieved
 * @return Transport pointer if set, NULL if using socket transport or client not found
 *
 * @ingroup session
 */
acip_transport_t *session_host_get_client_transport(session_host_t *host, uint32_t client_id);

/**
 * @brief Check if a specific client has an active alternative transport
 *
 * Convenient way to check if a client is using an alternative transport
 * (WebRTC, WebSocket, etc.) instead of raw TCP socket.
 *
 * @param host Host handle (must not be NULL)
 * @param client_id Client ID to check
 * @return true if alternative transport is set, false otherwise
 *
 * @ingroup session
 */
bool session_host_client_has_transport(session_host_t *host, uint32_t client_id);

/** @} */

/** @} */

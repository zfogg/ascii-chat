/**
 * @file networking/webrtc/webrtc.h
 * @brief WebRTC peer connection management for P2P ACIP transport
 * @ingroup webrtc
 *
 * Wraps libdatachannel to provide WebRTC DataChannel connectivity for
 * transporting ACIP packets in a star topology. The session creator acts as
 * the server, accepting WebRTC connections from all clients. The ACDS server
 * acts as a pure signaling relay for SDP/ICE exchange.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ascii-chat/common.h>
#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/network/webrtc/turn.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup webrtc WebRTC P2P Connections
 * @brief WebRTC DataChannel transport for ACIP packets
 * @{
 */

// Forward declarations
typedef struct webrtc_peer_connection webrtc_peer_connection_t;
typedef struct webrtc_data_channel webrtc_data_channel_t;

/**
 * @brief WebRTC connection state
 */
typedef enum {
  WEBRTC_STATE_NEW = 0,      ///< Connection created but not started
  WEBRTC_STATE_CONNECTING,   ///< ICE gathering/connection in progress
  WEBRTC_STATE_CONNECTED,    ///< DataChannel established and ready
  WEBRTC_STATE_DISCONNECTED, ///< Connection lost
  WEBRTC_STATE_FAILED,       ///< Connection failed (fatal)
  WEBRTC_STATE_CLOSED        ///< Connection closed cleanly
} webrtc_state_t;

/**
 * @brief ICE gathering state
 */
typedef enum {
  WEBRTC_GATHERING_NEW = 0,   ///< Not started
  WEBRTC_GATHERING_GATHERING, ///< Gathering candidates
  WEBRTC_GATHERING_COMPLETE   ///< All candidates gathered
} webrtc_gathering_state_t;

/**
 * @brief Callback for state changes
 * @param pc Peer connection
 * @param state New connection state
 * @param user_data User-provided context pointer
 */
typedef void (*webrtc_state_callback_t)(webrtc_peer_connection_t *pc, webrtc_state_t state, void *user_data);

/**
 * @brief Callback for local SDP offer/answer generation
 * @param pc Peer connection
 * @param sdp SDP string (null-terminated)
 * @param type SDP type ("offer" or "answer")
 * @param user_data User-provided context pointer
 *
 * Application should send this SDP to the remote peer via signaling channel (ACDS).
 */
typedef void (*webrtc_local_description_callback_t)(webrtc_peer_connection_t *pc, const char *sdp, const char *type,
                                                    void *user_data);

/**
 * @brief Callback for local ICE candidate discovery
 * @param pc Peer connection
 * @param candidate ICE candidate string (null-terminated)
 * @param mid Media stream ID (null-terminated)
 * @param user_data User-provided context pointer
 *
 * Application should send this candidate to the remote peer via signaling channel (ACDS).
 */
typedef void (*webrtc_local_candidate_callback_t)(webrtc_peer_connection_t *pc, const char *candidate, const char *mid,
                                                  void *user_data);

/**
 * @brief Callback for DataChannel open event
 * @param dc Data channel that opened
 * @param user_data User-provided context pointer
 */
typedef void (*webrtc_datachannel_open_callback_t)(webrtc_data_channel_t *dc, void *user_data);

/**
 * @brief Callback for DataChannel message received
 * @param dc Data channel
 * @param data Message data (binary)
 * @param size Message size in bytes
 * @param user_data User-provided context pointer
 */
typedef void (*webrtc_datachannel_message_callback_t)(webrtc_data_channel_t *dc, const uint8_t *data, size_t size,
                                                      void *user_data);

/**
 * @brief Callback for DataChannel error
 * @param dc Data channel
 * @param error Error message (null-terminated)
 * @param user_data User-provided context pointer
 */
typedef void (*webrtc_datachannel_error_callback_t)(webrtc_data_channel_t *dc, const char *error, void *user_data);

/**
 * @brief WebRTC configuration
 */
typedef struct {
  stun_server_t *stun_servers; ///< Array of STUN servers
  size_t stun_count;           ///< Number of STUN servers
  turn_server_t *turn_servers; ///< Array of TURN servers
  size_t turn_count;           ///< Number of TURN servers

  // Callbacks
  webrtc_state_callback_t on_state_change;
  webrtc_local_description_callback_t on_local_description;
  webrtc_local_candidate_callback_t on_local_candidate;
  webrtc_datachannel_open_callback_t on_datachannel_open;
  webrtc_datachannel_message_callback_t on_datachannel_message;
  webrtc_datachannel_error_callback_t on_datachannel_error;

  void *user_data; ///< Passed to all callbacks
} webrtc_config_t;

// ============================================================================
// Initialization and Cleanup
// ============================================================================

/**
 * @brief Initialize WebRTC library (libdatachannel)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Must be called once before creating any peer connections.
 * Thread-safe and idempotent (safe to call multiple times).
 */
asciichat_error_t webrtc_init(void);

/**
 * @brief Cleanup WebRTC library resources
 *
 * Should be called at program exit after all connections are closed.
 * Thread-safe and idempotent.
 */
void webrtc_cleanup(void);

// ============================================================================
// Peer Connection Management
// ============================================================================

/**
 * @brief Create a new WebRTC peer connection
 * @param config Configuration including ICE servers and callbacks
 * @param pc_out Output parameter for peer connection handle
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Creates a new peer connection with the specified configuration.
 * The connection starts in WEBRTC_STATE_NEW state.
 */
asciichat_error_t webrtc_create_peer_connection(const webrtc_config_t *config, webrtc_peer_connection_t **pc_out);

/**
 * @brief Close and destroy a peer connection
 * @param pc Peer connection to close
 *
 * Closes the connection gracefully and frees all resources.
 * Safe to call with NULL pointer.
 */
void webrtc_close_peer_connection(webrtc_peer_connection_t *pc);

/**
 * @brief Get current connection state
 * @param pc Peer connection
 * @return Current state
 */
webrtc_state_t webrtc_get_state(webrtc_peer_connection_t *pc);

/**
 * @brief Get user data pointer from connection
 * @param pc Peer connection
 * @return User data pointer (from webrtc_config_t)
 */
void *webrtc_get_user_data(webrtc_peer_connection_t *pc);

// ============================================================================
// SDP Offer/Answer Exchange
// ============================================================================

/**
 * @brief Create and set local SDP offer (for connection initiator)
 * @param pc Peer connection
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Triggers ICE gathering and local description callback with the SDP offer.
 * Use this when initiating a connection to a remote peer.
 */
asciichat_error_t webrtc_create_offer(webrtc_peer_connection_t *pc);

/**
 * @brief Set remote SDP offer/answer
 * @param pc Peer connection
 * @param sdp Remote SDP string (null-terminated)
 * @param type SDP type ("offer" or "answer")
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Sets the remote peer's SDP. If this is an offer, triggers automatic
 * answer generation via the local description callback.
 */
asciichat_error_t webrtc_set_remote_description(webrtc_peer_connection_t *pc, const char *sdp, const char *type);

// ============================================================================
// ICE Candidate Exchange
// ============================================================================

/**
 * @brief Add remote ICE candidate
 * @param pc Peer connection
 * @param candidate ICE candidate string (null-terminated)
 * @param mid Media stream ID (null-terminated, can be empty)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Adds a remote ICE candidate received via signaling channel.
 */
asciichat_error_t webrtc_add_remote_candidate(webrtc_peer_connection_t *pc, const char *candidate, const char *mid);

// ============================================================================
// DataChannel Management
// ============================================================================

/**
 * @brief Create a DataChannel (for connection initiator)
 * @param pc Peer connection
 * @param label Channel label (e.g., "acip")
 * @param dc_out Output parameter for data channel handle
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Creates a new DataChannel. The initiator should create the channel,
 * while the responder receives it via the datachannel callback.
 */
asciichat_error_t webrtc_create_datachannel(webrtc_peer_connection_t *pc, const char *label,
                                            webrtc_data_channel_t **dc_out);

/**
 * @brief Send data over DataChannel
 * @param dc Data channel
 * @param data Data buffer (binary)
 * @param size Data size in bytes
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Sends binary data over the DataChannel. Returns error if channel is not open.
 */
asciichat_error_t webrtc_datachannel_send(webrtc_data_channel_t *dc, const uint8_t *data, size_t size);

/**
 * @brief Check if DataChannel is open and ready
 * @param dc Data channel
 * @return true if open, false otherwise
 */
bool webrtc_datachannel_is_open(webrtc_data_channel_t *dc);

/**
 * @brief Set DataChannel open state (internal use)
 * @param dc Data channel
 * @param is_open Open state to set
 *
 * Internal function for transport layer to manually set open state when
 * callbacks are replaced after the DataChannel is already open.
 */
void webrtc_datachannel_set_open_state(webrtc_data_channel_t *dc, bool is_open);

/**
 * @brief Get DataChannel label
 * @param dc Data channel
 * @return Label string (null-terminated), or NULL on error
 */
const char *webrtc_datachannel_get_label(webrtc_data_channel_t *dc);

/**
 * @brief DataChannel callback structure
 *
 * Callbacks for DataChannel events (open, close, error, message).
 * Used with webrtc_datachannel_set_callbacks() to register per-channel callbacks.
 */
typedef struct {
  void (*on_open)(webrtc_data_channel_t *dc, void *user_data);                                     ///< Channel opened
  void (*on_close)(webrtc_data_channel_t *dc, void *user_data);                                    ///< Channel closed
  void (*on_error)(webrtc_data_channel_t *dc, const char *error, void *user_data);                 ///< Error occurred
  void (*on_message)(webrtc_data_channel_t *dc, const uint8_t *data, size_t len, void *user_data); ///< Message received
  void *user_data; ///< Passed to all callbacks
} webrtc_datachannel_callbacks_t;

/**
 * @brief Set DataChannel callbacks
 * @param dc Data channel
 * @param callbacks Callback structure
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Registers callbacks for DataChannel events. Any NULL callback pointer is ignored.
 */
asciichat_error_t webrtc_datachannel_set_callbacks(webrtc_data_channel_t *dc,
                                                   const webrtc_datachannel_callbacks_t *callbacks);

/**
 * @brief Close a DataChannel
 * @param dc Data channel to close
 *
 * Closes the channel gracefully. Safe to call with NULL pointer.
 */
void webrtc_close_datachannel(webrtc_data_channel_t *dc);

/**
 * @brief Close a DataChannel (alias for webrtc_close_datachannel)
 * @param dc Data channel to close
 *
 * Closes the channel gracefully. Safe to call with NULL pointer.
 */
static inline void webrtc_datachannel_close(webrtc_data_channel_t *dc) {
  webrtc_close_datachannel(dc);
}

/**
 * @brief Destroy a DataChannel and free resources
 * @param dc Data channel to destroy
 *
 * Frees all resources associated with the DataChannel.
 * Automatically closes the channel if still open.
 * Safe to call with NULL pointer.
 */
void webrtc_datachannel_destroy(webrtc_data_channel_t *dc);

/**
 * @brief Close a peer connection
 * @param pc Peer connection to close
 *
 * Closes the peer connection gracefully.
 * Safe to call with NULL pointer.
 */
void webrtc_peer_connection_close(webrtc_peer_connection_t *pc);

/**
 * @brief Destroy a peer connection and free resources
 * @param pc Peer connection to destroy
 *
 * Frees all resources associated with the peer connection.
 * Automatically closes the connection if still open.
 * Safe to call with NULL pointer.
 */
void webrtc_peer_connection_destroy(webrtc_peer_connection_t *pc);

/**
 * @brief Get the internal libdatachannel peer connection ID
 *
 * Helper function for C++ code that needs access to internal rtc_id
 * without exposing the full structure definition.
 *
 * @param pc Peer connection
 * @return libdatachannel peer connection ID, or -1 if pc is NULL
 */
int webrtc_get_rtc_id(webrtc_peer_connection_t *pc);

/** @} */ // end of webrtc group

#ifdef __cplusplus
}
#endif

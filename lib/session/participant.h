/**
 * @file session/participant.h
 * @brief 👤 Client-side session participation abstraction
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * This header provides the client-side session participation abstraction,
 * encapsulating connection management, media streaming, and event handling
 * for session participants.
 *
 * CORE FEATURES:
 * ==============
 * - Connection lifecycle management (connect, disconnect, reconnect)
 * - Video and audio stream control
 * - Callback-based event notification
 * - Encryption and authentication support
 *
 * USAGE:
 * ======
 * @code{.c}
 * // Define callbacks
 * void on_connected(session_participant_t *p, uint32_t id, void *data) {
 *     printf("Connected with ID %u\n", id);
 * }
 *
 * void on_frame(session_participant_t *p, const char *frame, void *data) {
 *     // Render ASCII frame
 * }
 *
 * // Create and connect
 * session_participant_config_t config = {
 *     .address = "127.0.0.1",
 *     .port = 27224,
 *     .callbacks = { .on_connected = on_connected, .on_frame_received = on_frame }
 * };
 * session_participant_t *p = session_participant_create(&config);
 * session_participant_connect(p);
 *
 * // Start streaming
 * session_participant_start_video(p);
 *
 * // Cleanup
 * session_participant_destroy(p);
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "asciichat_errno.h"
#include "settings.h"

/* ============================================================================
 * Session Participant Types
 * ============================================================================ */

/**
 * @brief Opaque session participant handle
 *
 * Manages connection state, media streams, and event callbacks.
 * Created via session_participant_create(), destroyed via session_participant_destroy().
 *
 * @ingroup session
 */
typedef struct session_participant session_participant_t;

/**
 * @brief Callback function prototypes for session participant events
 * @ingroup session
 */
typedef struct {
  /**
   * @brief Called when successfully connected to session
   * @param p Participant handle
   * @param client_id Assigned client ID from server
   * @param user_data User-provided context pointer
   */
  void (*on_connected)(session_participant_t *p, uint32_t client_id, void *user_data);

  /**
   * @brief Called when disconnected from session
   * @param p Participant handle
   * @param user_data User-provided context pointer
   */
  void (*on_disconnected)(session_participant_t *p, void *user_data);

  /**
   * @brief Called when ASCII frame received from server
   * @param p Participant handle
   * @param frame ASCII frame data (null-terminated)
   * @param user_data User-provided context pointer
   */
  void (*on_frame_received)(session_participant_t *p, const char *frame, void *user_data);

  /**
   * @brief Called when audio samples received from server
   * @param p Participant handle
   * @param samples Audio sample buffer (float format)
   * @param count Number of samples
   * @param user_data User-provided context pointer
   */
  void (*on_audio_received)(session_participant_t *p, const float *samples, size_t count, void *user_data);

  /**
   * @brief Called when session settings change
   * @param p Participant handle
   * @param settings New session settings
   * @param user_data User-provided context pointer
   */
  void (*on_settings_changed)(session_participant_t *p, const session_settings_t *settings, void *user_data);

  /**
   * @brief Called when an error occurs
   * @param p Participant handle
   * @param error Error code
   * @param message Error message
   * @param user_data User-provided context pointer
   */
  void (*on_error)(session_participant_t *p, asciichat_error_t error, const char *message, void *user_data);
} session_participant_callbacks_t;

/**
 * @brief Configuration for session participant
 * @ingroup session
 */
typedef struct {
  /** @brief Server address to connect to */
  const char *address;

  /** @brief Server port (default: 27224) */
  int port;

  /** @brief Enable encryption (default: true) */
  bool encryption_enabled;

  /** @brief Password for server authentication (optional) */
  const char *password;

  /** @brief Expected server key for verification (optional) */
  const char *server_key;

  /** @brief Enable audio streaming */
  bool enable_audio;

  /** @brief Enable video capture and streaming */
  bool enable_video;

  /** @brief Event callbacks */
  session_participant_callbacks_t callbacks;

  /** @brief User-provided context pointer passed to callbacks */
  void *user_data;
} session_participant_config_t;

/* ============================================================================
 * Session Participant Lifecycle Functions
 * @{
 */

/**
 * @brief Create a new session participant
 * @param config Participant configuration (must not be NULL)
 * @return Pointer to participant handle, or NULL on failure
 *
 * Creates a session participant with the specified configuration.
 * The participant is not connected until session_participant_connect() is called.
 *
 * @note Call session_participant_destroy() to free resources when done.
 * @note On failure, sets asciichat_errno with error details.
 *
 * @ingroup session
 */
session_participant_t *session_participant_create(const session_participant_config_t *config);

/**
 * @brief Destroy session participant and free resources
 * @param p Participant to destroy (can be NULL)
 *
 * Disconnects if connected, stops all streams, and releases all resources.
 * Safe to call with NULL.
 *
 * @ingroup session
 */
void session_participant_destroy(session_participant_t *p);

/** @} */

/* ============================================================================
 * Session Participant Connection Functions
 * @{
 */

/**
 * @brief Connect to session server
 * @param p Participant handle (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initiates connection to the configured server. Connection establishment
 * is synchronous but callbacks are invoked asynchronously.
 *
 * @note on_connected callback is invoked on successful connection.
 * @note on_error callback is invoked on failure.
 *
 * @ingroup session
 */
asciichat_error_t session_participant_connect(session_participant_t *p);

/**
 * @brief Disconnect from session server
 * @param p Participant handle (must not be NULL)
 *
 * Gracefully disconnects from the server and stops all streams.
 *
 * @note on_disconnected callback is invoked after disconnection.
 *
 * @ingroup session
 */
void session_participant_disconnect(session_participant_t *p);

/**
 * @brief Check if participant is connected
 * @param p Participant handle (can be NULL)
 * @return true if connected, false otherwise
 *
 * @ingroup session
 */
bool session_participant_is_connected(session_participant_t *p);

/**
 * @brief Get assigned client ID
 * @param p Participant handle (can be NULL)
 * @return Client ID if connected, 0 otherwise
 *
 * @ingroup session
 */
uint32_t session_participant_get_client_id(session_participant_t *p);

/** @} */

/* ============================================================================
 * Session Participant Media Control Functions
 * @{
 */

/**
 * @brief Start video capture and streaming
 * @param p Participant handle (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts capturing video from the webcam and streaming to the server.
 *
 * @ingroup session
 */
asciichat_error_t session_participant_start_video(session_participant_t *p);

/**
 * @brief Stop video capture and streaming
 * @param p Participant handle (must not be NULL)
 *
 * @ingroup session
 */
void session_participant_stop_video(session_participant_t *p);

/**
 * @brief Check if video is streaming
 * @param p Participant handle (can be NULL)
 * @return true if video is active, false otherwise
 *
 * @ingroup session
 */
bool session_participant_is_video_active(session_participant_t *p);

/**
 * @brief Start audio capture and streaming
 * @param p Participant handle (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Starts capturing audio from the microphone and streaming to the server.
 * Also starts audio playback for receiving audio from other participants.
 *
 * @ingroup session
 */
asciichat_error_t session_participant_start_audio(session_participant_t *p);

/**
 * @brief Stop audio capture and streaming
 * @param p Participant handle (must not be NULL)
 *
 * @ingroup session
 */
void session_participant_stop_audio(session_participant_t *p);

/**
 * @brief Check if audio is streaming
 * @param p Participant handle (can be NULL)
 * @return true if audio is active, false otherwise
 *
 * @ingroup session
 */
bool session_participant_is_audio_active(session_participant_t *p);

/** @} */

/* ============================================================================
 * Session Participant Settings Functions
 * @{
 */

/**
 * @brief Get current session settings
 * @param p Participant handle (must not be NULL)
 * @param settings Output settings structure (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup session
 */
asciichat_error_t session_participant_get_settings(session_participant_t *p, session_settings_t *settings);

/**
 * @brief Request session settings update (if permitted)
 * @param p Participant handle (must not be NULL)
 * @param settings New settings to request (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Requests the host to update session settings. The host may accept
 * or reject the request.
 *
 * @note Actual settings change is signaled via on_settings_changed callback.
 *
 * @ingroup session
 */
asciichat_error_t session_participant_request_settings(session_participant_t *p, const session_settings_t *settings);

/** @} */

/** @} */

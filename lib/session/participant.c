/**
 * @file session/participant.c
 * @brief 👤 Client-side session participation implementation
 * @ingroup session
 *
 * Implements the session participant abstraction for client-side connection
 * management and media streaming.
 *
 * NOTE: This is a stub implementation that provides the API structure.
 * Full implementation will integrate with existing client code in a future phase.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "participant.h"
#include "common.h"
#include "asciichat_errno.h"
#include "platform/socket.h"

#include <string.h>

/* ============================================================================
 * Session Participant Context Structure
 * ============================================================================ */

/**
 * @brief Internal session participant structure
 *
 * Contains connection state, media stream state, and callback configuration.
 */
struct session_participant {
  /** @brief Server address */
  char address[256];

  /** @brief Server port */
  int port;

  /** @brief Encryption enabled */
  bool encryption_enabled;

  /** @brief Password (if any) */
  char password[256];

  /** @brief Server key for verification (if any) */
  char server_key[512];

  /** @brief Audio enabled */
  bool enable_audio;

  /** @brief Video enabled */
  bool enable_video;

  /** @brief Event callbacks */
  session_participant_callbacks_t callbacks;

  /** @brief User data for callbacks */
  void *user_data;

  /** @brief Connection socket */
  socket_t socket;

  /** @brief Currently connected */
  bool connected;

  /** @brief Assigned client ID */
  uint32_t client_id;

  /** @brief Video streaming active */
  bool video_active;

  /** @brief Audio streaming active */
  bool audio_active;

  /** @brief Current session settings */
  session_settings_t settings;

  /** @brief Context is initialized */
  bool initialized;
};

/* ============================================================================
 * Session Participant Lifecycle Functions
 * ============================================================================ */

session_participant_t *session_participant_create(const session_participant_config_t *config) {
  if (!config) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_create: NULL config");
    return NULL;
  }

  // Allocate participant
  session_participant_t *p = SAFE_CALLOC(1, sizeof(session_participant_t), session_participant_t *);

  // Copy configuration
  if (config->address) {
    SAFE_STRNCPY(p->address, config->address, sizeof(p->address));
  } else {
    SAFE_STRNCPY(p->address, "127.0.0.1", sizeof(p->address));
  }

  p->port = config->port > 0 ? config->port : 27224;
  p->encryption_enabled = config->encryption_enabled;

  if (config->password) {
    SAFE_STRNCPY(p->password, config->password, sizeof(p->password));
  }

  if (config->server_key) {
    SAFE_STRNCPY(p->server_key, config->server_key, sizeof(p->server_key));
  }

  p->enable_audio = config->enable_audio;
  p->enable_video = config->enable_video;
  p->callbacks = config->callbacks;
  p->user_data = config->user_data;

  // Initialize socket to invalid
  p->socket = INVALID_SOCKET_VALUE;
  p->connected = false;
  p->client_id = 0;
  p->video_active = false;
  p->audio_active = false;

  // Initialize settings
  session_settings_init(&p->settings);

  p->initialized = true;
  return p;
}

void session_participant_destroy(session_participant_t *p) {
  if (!p) {
    return;
  }

  // Disconnect if connected
  if (p->connected) {
    session_participant_disconnect(p);
  }

  // Close socket if open
  if (p->socket != INVALID_SOCKET_VALUE) {
    socket_close(p->socket);
    p->socket = INVALID_SOCKET_VALUE;
  }

  // Clear sensitive data
  memset(p->password, 0, sizeof(p->password));
  memset(p->server_key, 0, sizeof(p->server_key));

  p->initialized = false;
  SAFE_FREE(p);
}

/* ============================================================================
 * Session Participant Connection Functions
 * ============================================================================ */

asciichat_error_t session_participant_connect(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_connect: invalid participant");
  }

  if (p->connected) {
    return ASCIICHAT_OK; // Already connected
  }

  // TODO: Implement actual connection logic using existing client code
  // This would involve:
  // 1. Creating a TCP socket
  // 2. Connecting to server
  // 3. Performing crypto handshake
  // 4. Starting receive thread
  // 5. Sending capabilities
  //
  // For now, this is a stub that sets up the structure

  log_info("session_participant_connect: stub implementation - would connect to %s:%d", p->address, p->port);

  // Invoke error callback since this is not implemented yet
  if (p->callbacks.on_error) {
    p->callbacks.on_error(p, ERROR_NOT_SUPPORTED, "Connection not implemented yet (stub)", p->user_data);
  }

  return SET_ERRNO(ERROR_NOT_SUPPORTED, "session_participant_connect: not implemented yet");
}

void session_participant_disconnect(session_participant_t *p) {
  if (!p || !p->initialized) {
    return;
  }

  if (!p->connected) {
    return;
  }

  // Stop media streams
  if (p->video_active) {
    session_participant_stop_video(p);
  }
  if (p->audio_active) {
    session_participant_stop_audio(p);
  }

  // Close socket
  if (p->socket != INVALID_SOCKET_VALUE) {
    socket_close(p->socket);
    p->socket = INVALID_SOCKET_VALUE;
  }

  p->connected = false;
  p->client_id = 0;

  // Invoke callback
  if (p->callbacks.on_disconnected) {
    p->callbacks.on_disconnected(p, p->user_data);
  }
}

bool session_participant_is_connected(session_participant_t *p) {
  if (!p || !p->initialized) {
    return false;
  }
  return p->connected;
}

uint32_t session_participant_get_client_id(session_participant_t *p) {
  if (!p || !p->initialized || !p->connected) {
    return 0;
  }
  return p->client_id;
}

/* ============================================================================
 * Session Participant Media Control Functions
 * ============================================================================ */

asciichat_error_t session_participant_start_video(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_start_video: invalid participant");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_video: not connected");
  }

  if (p->video_active) {
    return ASCIICHAT_OK; // Already active
  }

  if (!p->enable_video) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_video: video not enabled");
  }

  // TODO: Start webcam capture thread using session_capture_ctx_t

  p->video_active = true;
  return ASCIICHAT_OK;
}

void session_participant_stop_video(session_participant_t *p) {
  if (!p || !p->initialized) {
    return;
  }

  if (!p->video_active) {
    return;
  }

  // TODO: Stop webcam capture thread

  p->video_active = false;
}

bool session_participant_is_video_active(session_participant_t *p) {
  if (!p || !p->initialized) {
    return false;
  }
  return p->video_active;
}

asciichat_error_t session_participant_start_audio(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_start_audio: invalid participant");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_audio: not connected");
  }

  if (p->audio_active) {
    return ASCIICHAT_OK; // Already active
  }

  if (!p->enable_audio) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_audio: audio not enabled");
  }

  // TODO: Start audio using session_audio_ctx_t

  p->audio_active = true;
  return ASCIICHAT_OK;
}

void session_participant_stop_audio(session_participant_t *p) {
  if (!p || !p->initialized) {
    return;
  }

  if (!p->audio_active) {
    return;
  }

  // TODO: Stop audio

  p->audio_active = false;
}

bool session_participant_is_audio_active(session_participant_t *p) {
  if (!p || !p->initialized) {
    return false;
  }
  return p->audio_active;
}

/* ============================================================================
 * Session Participant Settings Functions
 * ============================================================================ */

asciichat_error_t session_participant_get_settings(session_participant_t *p, session_settings_t *settings) {
  if (!p || !p->initialized || !settings) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_get_settings: invalid parameter");
  }

  memcpy(settings, &p->settings, sizeof(session_settings_t));
  return ASCIICHAT_OK;
}

asciichat_error_t session_participant_request_settings(session_participant_t *p, const session_settings_t *settings) {
  if (!p || !p->initialized || !settings) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_request_settings: invalid parameter");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_request_settings: not connected");
  }

  // TODO: Send settings update request to server

  return SET_ERRNO(ERROR_NOT_SUPPORTED, "session_participant_request_settings: not implemented yet");
}

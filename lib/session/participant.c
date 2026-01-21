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
#include "options/options.h"
#include "asciichat_errno.h"
#include "platform/socket.h"
#include "platform/thread.h"
#include "log/logging.h"
#include "session/capture.h"
#include "session/audio.h"
#include "network/packet.h"
#include "util/time.h"
#include "audio/opus_codec.h"

#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#endif

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

  /** @brief Video capture context (for webcam/file media) */
  session_capture_ctx_t *video_capture;

  /** @brief Audio capture context (for microphone) */
  session_audio_ctx_t *audio_capture;

  /** @brief Video capture thread handle */
  asciichat_thread_t video_capture_thread;

  /** @brief Audio capture thread handle */
  asciichat_thread_t audio_capture_thread;

  /** @brief Video capture thread running flag */
  bool video_capture_running;

  /** @brief Audio capture thread running flag */
  bool audio_capture_running;

  /** @brief Opus encoder for audio compression */
  opus_codec_t *opus_encoder;

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

  p->port = config->port > 0 ? config->port : OPT_PORT_INT_DEFAULT;
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

  // Stop capture threads if running
  if (p->video_capture_running) {
    session_participant_stop_video_capture(p);
  }
  if (p->audio_capture_running) {
    session_participant_stop_audio_capture(p);
  }

  // Clean up media capture contexts
  if (p->video_capture) {
    session_capture_destroy(p->video_capture);
    p->video_capture = NULL;
  }
  if (p->audio_capture) {
    session_audio_destroy(p->audio_capture);
    p->audio_capture = NULL;
  }
  if (p->opus_encoder) {
    opus_codec_destroy(p->opus_encoder);
    p->opus_encoder = NULL;
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

/**
 * @brief Establish TCP connection to server
 * @return Socket on success, INVALID_SOCKET_VALUE on failure
 */
static socket_t connect_to_server(const char *address, int port) {
  struct addrinfo hints, *result = NULL, *rp = NULL;
  char port_str[16];

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  snprintf(port_str, sizeof(port_str), "%d", port);

  int s = getaddrinfo(address, port_str, &hints, &result);
  if (s != 0) {
    SET_ERRNO(ERROR_NETWORK, "getaddrinfo failed: %s", gai_strerror(s));
    return INVALID_SOCKET_VALUE;
  }

  socket_t sock = INVALID_SOCKET_VALUE;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock == INVALID_SOCKET_VALUE) {
      continue;
    }

    if (connect(sock, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
      break; // Success
    }

    socket_close(sock);
    sock = INVALID_SOCKET_VALUE;
  }

  freeaddrinfo(result);

  if (sock == INVALID_SOCKET_VALUE) {
    SET_ERRNO_SYS(ERROR_NETWORK_CONNECT, "Failed to connect to %s:%d", address, port);
    return INVALID_SOCKET_VALUE;
  }

  return sock;
}

asciichat_error_t session_participant_connect(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_connect: invalid participant");
  }

  if (p->connected) {
    return ASCIICHAT_OK; // Already connected
  }

  // Create TCP connection to server
  p->socket = connect_to_server(p->address, p->port);
  if (p->socket == INVALID_SOCKET_VALUE) {
    log_error("Failed to connect to server at %s:%d", p->address, p->port);
    if (p->callbacks.on_error) {
      p->callbacks.on_error(p, ERROR_NETWORK_CONNECT, "Failed to connect to server", p->user_data);
    }
    return GET_ERRNO();
  }

  p->connected = true;
  p->client_id = 0; // Will be assigned by server

  log_info("Connected to server at %s:%d", p->address, p->port);

  // Invoke callback
  if (p->callbacks.on_connected) {
    p->callbacks.on_connected(p, p->client_id, p->user_data);
  }

  return ASCIICHAT_OK;
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

/* ============================================================================
 * Session Participant Media Capture Threads
 * ============================================================================ */

/**
 * @brief Video capture thread - captures and transmits frames to host
 *
 * DESIGN: Reuses session_capture_ctx_t for unified media source handling.
 * Captures frames at target FPS, resizes for network efficiency, and sends
 * via IMAGE_FRAME packets.
 */
static void *participant_video_capture_thread(void *arg) {
  session_participant_t *p = (session_participant_t *)arg;
  if (!p || !p->video_capture) {
    SET_ERRNO(ERROR_INVALID_PARAM, "participant_video_capture_thread: invalid participant or video_capture context");
    return NULL;
  }

  log_info("Video capture thread started");

  while (p->video_capture_running && p->connected) {
    // Capture frame from media source (webcam, file, test pattern)
    image_t *raw_frame = session_capture_read_frame(p->video_capture);
    if (!raw_frame) {
      platform_sleep_ms(1);
      continue;
    }

    // Process for network transmission (resize to bandwidth-optimal dimensions)
    image_t *processed = session_capture_process_for_transmission(p->video_capture, raw_frame);
    if (processed) {
      // Send to host via IMAGE_FRAME packet
      asciichat_error_t err = send_image_frame_packet(p->socket, (const void *)processed->pixels,
                                                      (uint16_t)processed->w, (uint16_t)processed->h, 0);
      if (err != ASCIICHAT_OK) {
        log_warn_every(5000000, "Failed to send video frame: %d", err);
      }
      image_destroy(processed);
    }

    // Sleep to maintain target frame rate (adaptive based on source)
    session_capture_sleep_for_fps(p->video_capture);
  }

  log_info("Video capture thread stopped");
  return NULL;
}

/**
 * @brief Audio capture thread - captures and transmits audio to host
 *
 * DESIGN: Captures microphone samples, encodes to Opus (lossy compression),
 * and sends via AUDIO packets. Async queueing would decouple capture from
 * network I/O (future optimization).
 */
static void *participant_audio_capture_thread(void *arg) {
  session_participant_t *p = (session_participant_t *)arg;
  if (!p || !p->audio_capture || !p->opus_encoder) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "participant_audio_capture_thread: invalid participant, audio_capture, or opus_encoder");
    return NULL;
  }

  log_info("Audio capture thread started");

  float sample_buffer[960]; // 20ms @ 48kHz
  uint8_t opus_buffer[1000];

  while (p->audio_capture_running && p->connected) {
    // Read microphone samples (20ms chunk)
    size_t samples_read = session_audio_read_captured(p->audio_capture, sample_buffer, 960);
    if (samples_read <= 0) {
      platform_sleep_ms(1);
      continue;
    }

    // Encode to Opus (lossy compression for bandwidth efficiency)
    size_t opus_len =
        opus_codec_encode(p->opus_encoder, sample_buffer, (int)samples_read, opus_buffer, sizeof(opus_buffer));
    if (opus_len > 0) {
      uint16_t frame_sizes[1] = {(uint16_t)opus_len};
      asciichat_error_t err =
          av_send_audio_opus_batch(p->socket, opus_buffer, opus_len, frame_sizes, 48000, 20, 1, NULL);
      if (err != ASCIICHAT_OK) {
        log_warn_every(5000000, "Failed to send audio packet: %d", err);
      }
    }
  }

  log_info("Audio capture thread stopped");
  return NULL;
}

/* ============================================================================
 * Session Participant Media Capture Public API
 * ============================================================================ */

asciichat_error_t session_participant_start_video_capture(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_start_video_capture: invalid participant");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_video_capture: not connected");
  }

  if (!p->enable_video) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_video_capture: video not enabled");
  }

  if (p->video_capture_running) {
    return ASCIICHAT_OK; // Already running
  }

  // Create video capture context if not already created
  if (!p->video_capture) {
    session_capture_config_t config = {
        .type = MEDIA_SOURCE_WEBCAM,
        .path = "0", // Default device
        .target_fps = 60,
        .resize_for_network = true, // Optimize for bandwidth
    };
    p->video_capture = session_capture_create(&config);
    if (!p->video_capture) {
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to create video capture context");
    }
  }

  // Spawn video capture thread (media source is ready in ctx)
  p->video_capture_running = true;
  if (asciichat_thread_create(&p->video_capture_thread, participant_video_capture_thread, p) != 0) {
    log_error("Failed to spawn video capture thread");
    p->video_capture_running = false;
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn video capture thread");
  }

  log_info("Video capture started");
  return ASCIICHAT_OK;
}

void session_participant_stop_video_capture(session_participant_t *p) {
  if (!p || !p->initialized) {
    return;
  }

  if (!p->video_capture_running) {
    return;
  }

  // Signal thread to stop
  p->video_capture_running = false;

  // Wait for thread to complete
  asciichat_thread_join(&p->video_capture_thread, NULL);

  log_info("Video capture stopped");
}

asciichat_error_t session_participant_start_audio_capture(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_start_audio_capture: invalid participant");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_audio_capture: not connected");
  }

  if (!p->enable_audio) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_audio_capture: audio not enabled");
  }

  if (p->audio_capture_running) {
    return ASCIICHAT_OK; // Already running
  }

  // Create audio capture context if not already created
  if (!p->audio_capture) {
    p->audio_capture = session_audio_create(false); // false = participant mode (no mixing)
    if (!p->audio_capture) {
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to create audio capture context");
    }
  }

  // Start audio capture and playback
  asciichat_error_t err = session_audio_start_duplex(p->audio_capture);
  if (err != ASCIICHAT_OK) {
    return SET_ERRNO(err, "Failed to start audio duplex");
  }

  // Create Opus encoder for audio compression (48kHz, VOIP mode, 24 kbps)
  if (!p->opus_encoder) {
    p->opus_encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
    if (!p->opus_encoder) {
      session_audio_stop(p->audio_capture);
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to create Opus encoder");
    }
  }

  // Spawn audio capture thread
  p->audio_capture_running = true;
  if (asciichat_thread_create(&p->audio_capture_thread, participant_audio_capture_thread, p) != 0) {
    log_error("Failed to spawn audio capture thread");
    p->audio_capture_running = false;
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn audio capture thread");
  }

  log_info("Audio capture started");
  return ASCIICHAT_OK;
}

void session_participant_stop_audio_capture(session_participant_t *p) {
  if (!p || !p->initialized) {
    return;
  }

  if (!p->audio_capture_running) {
    return;
  }

  // Signal thread to stop
  p->audio_capture_running = false;

  // Wait for thread to complete
  asciichat_thread_join(&p->audio_capture_thread, NULL);

  // Stop audio streams
  if (p->audio_capture) {
    session_audio_stop(p->audio_capture);
  }

  log_info("Audio capture stopped");
}

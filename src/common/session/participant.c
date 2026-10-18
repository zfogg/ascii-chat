/**
 * @file session/participant.c
 * @brief 👤 Client-side session participation implementation
 * @ingroup session
 *
 * Implements the session participant abstraction for client-side connection
 * management and media streaming.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "session/participant.h"
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/network.h>
#include <ascii-chat/log/log.h>
#include "session/capture.h"
#include "session/audio.h"
#include "session/media.h"
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/audio/opus.h>

#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Session Participant Context Structure
 * ============================================================================ */

/**
 * @brief Internal session participant structure
 *
 * Contains connection state, media stream state, and callback configuration.
 */
typedef struct session_participant {
  /** @brief Server address */
  char address[BUFFER_SIZE_SMALL];

  /** @brief Server port */
  int port;

  /** @brief Encryption enabled */
  bool encryption_enabled;

  /** @brief Password (if any) */
  char password[BUFFER_SIZE_SMALL];

  /** @brief Server key for verification (if any) */
  char server_key[BUFFER_SIZE_MEDIUM];

  /** @brief Audio enabled */
  bool enable_audio;

  /** @brief Video enabled */
  bool enable_video;

  /** @brief Event callbacks */
  session_participant_callbacks_t callbacks;

  /** @brief User data for callbacks */
  void *user_data;

  /** @brief Selected transport used by all media workers. */
  struct acip_transport *transport;

  /** @brief Responsibility for releasing the selected transport. */
  session_transport_ownership_t transport_ownership;

  /** @brief Currently connected */
  bool connected;

  /** @brief Assigned client ID */
  uint32_t client_id;

  /** @brief Video streaming active */
  session_media_t video;

  /** @brief Audio streaming active */
  session_media_t audio;

  /** @brief Current session settings */
  session_settings_t settings;

  /** @brief Video capture context (for webcam/file media) */
  session_capture_ctx_t *video_capture;

  /** @brief Audio capture context (for microphone) */
  session_audio_ctx_t *audio_capture;

  /** Audio callbacks borrow the video media source until audio is stopped. */
  bool audio_uses_video;

  /** @brief Video capture thread handle */
  asciichat_thread_t video_capture_thread;

  /** @brief Audio capture thread handle */
  asciichat_thread_t audio_capture_thread;

  /** @brief Video worker handle requires joining */
  bool video_thread_created;

  /** @brief Audio worker handle requires joining */
  bool audio_thread_created;

  /** @brief Opus encoder for audio compression */
  opus_codec_t *opus_encoder;

  /** @brief Context is initialized */
  bool initialized;
} session_participant_t;

/* ============================================================================
 * Session Participant Lifecycle Functions
 * ============================================================================ */

session_participant_t *session_participant_create(const session_participant_config_t *config) {
  log_info("session_participant_create: START - config=%p", config);

  if (!config) {
    log_error("session_participant_create: config is NULL");
    SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_create: NULL config");
    return NULL;
  }

  // Allocate participant
  session_participant_t *p = SAFE_CALLOC(1, sizeof(session_participant_t), session_participant_t *);
  log_info("session_participant_create: allocated p=%p", p);

  // Copy configuration
  if (config->address) {
    SAFE_STRNCPY(p->address, config->address, sizeof(p->address));
  } else {
    SAFE_STRNCPY(p->address, "127.0.0.1", sizeof(p->address));
  }

  p->port = config->port > 0 ? config->port : OPT_PORT_INT_DEFAULT;
  p->encryption_enabled = config->encryption_enabled;

  log_info("session_participant_create: address=%s, port=%d", p->address, p->port);

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

  p->transport = NULL;
  p->connected = false;
  p->client_id = 0;

  // Initialize settings
  session_settings_init(&p->settings);

  p->initialized = true;
  log_info("session_participant_create: DONE - returning p=%p (initialized=%d, connected=%d)", p, p->initialized,
           p->connected);
  return p;
}

void session_participant_destroy(session_participant_t *p) {
  if (!p) {
    return;
  }

  session_participant_disconnect(p);
  // A failed join retains the participant and its resources for a safe retry.
  if (p->video_thread_created || p->audio_thread_created)
    return;

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

  safe_snprintf(port_str, sizeof(port_str), "%d", port);

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
  log_debug("session_participant_connect: enter - p=%p", p);

  if (!p || !p->initialized) {
    log_debug("session_participant_connect: invalid param or not initialized");
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_connect: invalid participant");
  }

  log_debug("session_participant_connect: p->initialized=%d, p->connected=%d", p->initialized, p->connected);

  if (p->connected) {
    log_debug("session_participant_connect: already connected, returning OK");
    return ASCIICHAT_OK; // Already connected
  }

  session_participant_disconnect(p);
  if (p->video_thread_created || p->audio_thread_created)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stop media before reconnecting TCP");

  // Create TCP connection to server
  log_debug("session_participant_connect: calling connect_to_server(%s, %d)", p->address, p->port);
  socket_t socket = connect_to_server(p->address, p->port);
  log_debug("session_participant_connect: connect_to_server returned socket=%d (INVALID=%d)", socket,
            INVALID_SOCKET_VALUE);

  if (socket == INVALID_SOCKET_VALUE) {
    log_error("Failed to connect to server at %s:%d", p->address, p->port);
    if (p->callbacks.on_error) {
      p->callbacks.on_error(p, ERROR_NETWORK_CONNECT, "Failed to connect to server", p->user_data);
    }
    return SET_ERRNO(ERROR_NETWORK_CONNECT, "Session participant could not connect to server");
  }

  asciichat_errno_scope_t checkpoint = asciichat_errno_checkpoint();
  acip_transport_t *transport = acip_tcp_transport_create_owned("session_participant_tcp", socket, NULL);
  if (!transport) {
    asciichat_error_context_t cause;
    asciichat_error_t error = asciichat_errno_peek_since(checkpoint, &cause) ? cause.code : ERROR_NETWORK_CONNECT;
    socket_close(socket);
    return SET_ERRNO(error, "Failed to create participant TCP transport");
  }
  p->transport = transport;
  p->transport_ownership = SESSION_TRANSPORT_OWNED;

  log_debug("session_participant_connect: setting p->connected=true");
  p->connected = true;
  p->client_id = 0; // Will be assigned by server

  log_info("Connected to server at %s:%d (socket=%d)", p->address, p->port, socket);
  log_debug("session_participant_connect: p->connected=%d after setting", p->connected);

  // Invoke callback
  if (p->callbacks.on_connected) {
    p->callbacks.on_connected(p, p->client_id, p->user_data);
  }

  log_debug("session_participant_connect: returning ASCIICHAT_OK");
  return ASCIICHAT_OK;
}

void session_participant_disconnect(session_participant_t *p) {
  if (!p || !p->initialized) {
    return;
  }

  bool was_connected = p->connected;
  session_participant_stop_audio(p);
  session_participant_stop_video(p);
  if (p->video_thread_created || p->audio_thread_created)
    return;

  if (p->transport && p->transport_ownership == SESSION_TRANSPORT_OWNED)
    acip_transport_destroy(p->transport);
  p->transport = NULL;
  p->transport_ownership = SESSION_TRANSPORT_BORROWED;

  p->connected = false;
  p->client_id = 0;

  // Invoke callback
  if (was_connected && p->callbacks.on_disconnected) {
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

socket_t session_participant_get_socket(session_participant_t *p) {
  if (!p || !p->initialized) {
    return INVALID_SOCKET_VALUE;
  }
  return acip_transport_get_socket(p->transport);
}

/* ============================================================================
 * Session Participant Media Control Functions
 * ============================================================================ */

bool session_participant_is_video_active(session_participant_t *p) {
  return p && p->initialized && session_media_is_active(&p->video);
}

bool session_participant_is_audio_active(session_participant_t *p) {
  return p && p->initialized && session_media_is_active(&p->audio);
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

  while (session_media_should_run(&p->video)) {
    // Capture frame from media source (webcam, file, test pattern)
    image_t *raw_frame = session_capture_read_frame(p->video_capture);
    if (!raw_frame) {
      APP_CALLBACK_VOID(platform_pump_events);
      platform_sleep_ms(1);
      continue;
    }

    // Process for network transmission (resize to bandwidth-optimal dimensions)
    image_t *processed = session_capture_process_for_transmission(p->video_capture, raw_frame);
    if (processed) {
      // Send to host via IMAGE_FRAME packet
      asciichat_error_t err = acip_send_image_frame(p->transport, processed->pixels, processed->w, processed->h, 0);
      if (err != ASCIICHAT_OK) {
        log_warn_every(5 * US_PER_SEC_INT, "Failed to send video frame: %d", err);
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

  while (session_media_should_run(&p->audio)) {
    // Read microphone samples (20ms chunk)
    size_t samples_read = session_audio_read_captured(p->audio_capture, sample_buffer, 960);
    if (samples_read <= 0) {
      APP_CALLBACK_VOID(platform_pump_events);
      platform_sleep_ms(1);
      continue;
    }

    // Encode to Opus (lossy compression for bandwidth efficiency)
    size_t opus_len =
        opus_codec_encode(p->opus_encoder, sample_buffer, (int)samples_read, opus_buffer, sizeof(opus_buffer));
    if (opus_len > 0) {
      uint16_t frame_sizes[1] = {(uint16_t)opus_len};
      asciichat_error_t err =
          acip_send_audio_opus_batch(p->transport, opus_buffer, opus_len, frame_sizes, 1, 48000, 20);
      if (err != ASCIICHAT_OK) {
        log_warn_every(5 * US_PER_SEC_INT, "Failed to send audio packet: %d", err);
      }
    }
  }

  log_info("Audio capture thread stopped");
  return NULL;
}

/* ============================================================================
 * Session Participant Media Capture Public API
 * ============================================================================ */

static asciichat_error_t participant_startup_error(asciichat_errno_scope_t checkpoint, const char *message) {
  asciichat_error_context_t cause;
  asciichat_error_t code = asciichat_errno_peek_since(checkpoint, &cause) ? cause.code : ERROR_INVALID_STATE;
  return SET_ERRNO(code, "%s", message);
}

asciichat_error_t session_participant_start_video(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_start_video: invalid participant");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_video: not connected");
  }

  if (!p->enable_video) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_video: video not enabled");
  }

  if (session_media_is_active(&p->video))
    return ASCIICHAT_OK;
  uint32_t generation = session_media_begin(&p->video);
  if (!generation)
    return SET_ERRNO(ERROR_INVALID_STATE, "video is starting or stopping");

  // Create video capture context if not already created
  if (!p->video_capture) {
    session_capture_config_t config = {
        .type = MEDIA_SOURCE_WEBCAM,
        .path = "0", // Default device
        .target_fps = 60,
        .resize_for_network = true, // Optimize for bandwidth
    };
    const char *file = GET_OPTION(media_file);
    const char *url = GET_OPTION(media_url);
    char webcam_index[32];
    safe_snprintf(webcam_index, sizeof(webcam_index), "%u", GET_OPTION(webcam_index));
    config.path = webcam_index;
    if (url && url[0]) {
      config.type = MEDIA_SOURCE_FILE;
      config.path = url;
    } else if (file && file[0]) {
      config.type = strcmp(file, "-") == 0 ? MEDIA_SOURCE_STDIN : MEDIA_SOURCE_FILE;
      config.path = config.type == MEDIA_SOURCE_STDIN ? NULL : file;
    }
    config.loop = GET_OPTION(media_loop);
    asciichat_errno_scope_t checkpoint = asciichat_errno_checkpoint();
    p->video_capture = session_capture_create(&config);
    if (!p->video_capture) {
      asciichat_error_t error = participant_startup_error(checkpoint, "Failed to create video capture context");
      session_participant_stop_video(p);
      return error;
    }
  }

  // Spawn video capture thread (media source is ready in ctx)
  if (asciichat_thread_create(&p->video_capture_thread, "video_capture", participant_video_capture_thread, p) != 0) {
    log_error("Failed to spawn video capture thread");
    session_participant_stop_video(p);
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn video capture thread");
  }

  p->video_thread_created = true;
  session_media_complete(&p->video, generation);
  log_info("Video capture started");
  return ASCIICHAT_OK;
}

void session_participant_stop_video(session_participant_t *p) {
  if (!p || !p->initialized)
    return;
  if (p->audio_uses_video)
    session_participant_stop_audio(p);
  if (p->audio_uses_video && p->audio_thread_created)
    return;
  session_media_begin_stop(&p->video);
  if (p->video_thread_created) {
    if (asciichat_thread_join(&p->video_capture_thread, NULL) != 0) {
      log_error("Failed to join video capture worker; retaining its resources");
      return;
    }
    p->video_thread_created = false;
  }
  if (p->video_capture) {
    session_capture_destroy(p->video_capture);
    p->video_capture = NULL;
  }
  session_media_finish_stop(&p->video);
}

asciichat_error_t session_participant_start_audio(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_participant_start_audio: invalid participant");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_audio: not connected");
  }

  if (!p->enable_audio) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_participant_start_audio: audio not enabled");
  }

  if (session_media_is_active(&p->audio))
    return ASCIICHAT_OK;
  uint32_t generation = session_media_begin(&p->audio);
  if (!generation)
    return SET_ERRNO(ERROR_INVALID_STATE, "audio is starting or stopping");

  // Create audio capture context if not already created
  if (!p->audio_capture) {
    asciichat_errno_scope_t checkpoint = asciichat_errno_checkpoint();
    p->audio_capture = session_audio_create(false); // false = participant mode (no mixing)
    if (!p->audio_capture) {
      asciichat_error_t error = participant_startup_error(checkpoint, "Failed to create audio capture context");
      session_participant_stop_audio(p);
      return error;
    }
  }

  void *source = p->video_capture ? session_capture_get_media_source(p->video_capture) : NULL;
  p->audio_uses_video = source != NULL;
  session_audio_set_capture_source(p->audio_capture, source);

  // Start audio capture and playback
  asciichat_error_t err = session_audio_start_duplex(p->audio_capture);
  if (err != ASCIICHAT_OK) {
    session_participant_stop_audio(p);
    return SET_ERRNO(err, "Failed to start audio duplex");
  }

  // Create Opus encoder for audio compression (48kHz, VOIP mode, 24 kbps)
  if (!p->opus_encoder) {
    asciichat_errno_scope_t checkpoint = asciichat_errno_checkpoint();
    p->opus_encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
    if (!p->opus_encoder) {
      asciichat_error_t error = participant_startup_error(checkpoint, "Failed to create Opus encoder");
      session_participant_stop_audio(p);
      return error;
    }
  }

  // Spawn audio capture thread
  if (asciichat_thread_create(&p->audio_capture_thread, "audio_capture", participant_audio_capture_thread, p) != 0) {
    log_error("Failed to spawn audio capture thread");
    session_participant_stop_audio(p);
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn audio capture thread");
  }

  p->audio_thread_created = true;
  session_media_complete(&p->audio, generation);
  log_info("Audio capture started");
  return ASCIICHAT_OK;
}

void session_participant_stop_audio(session_participant_t *p) {
  if (!p || !p->initialized)
    return;
  session_media_begin_stop(&p->audio);
  if (p->audio_thread_created) {
    if (asciichat_thread_join(&p->audio_capture_thread, NULL) != 0) {
      log_error("Failed to join audio capture worker; retaining its resources");
      return;
    }
    p->audio_thread_created = false;
  }
  if (p->audio_capture) {
    session_audio_destroy(p->audio_capture);
    p->audio_capture = NULL;
  }
  if (p->opus_encoder) {
    opus_codec_destroy(p->opus_encoder);
    p->opus_encoder = NULL;
  }
  p->audio_uses_video = false;
  session_media_finish_stop(&p->audio);
}

/* ============================================================================
 * Session Participant Transport Functions (WebRTC Integration)
 * ============================================================================ */

asciichat_error_t session_participant_set_transport(session_participant_t *p, acip_transport_t *transport,
                                                    session_transport_ownership_t ownership) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Participant is NULL or not initialized");
  }

  if (session_media_should_run(&p->video) || session_media_should_run(&p->audio) || p->video_thread_created ||
      p->audio_thread_created)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stop media before replacing its transport");

  log_info("session_participant_set_transport: Setting transport=%p (was=%p)", transport, p->transport);

  if (ownership != SESSION_TRANSPORT_BORROWED && ownership != SESSION_TRANSPORT_OWNED)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport ownership");
  if (transport == p->transport) {
    if (transport && ownership != p->transport_ownership)
      return SET_ERRNO(ERROR_INVALID_STATE, "Detach before transferring transport ownership");
    return ASCIICHAT_OK;
  }
  if (p->transport && p->transport_ownership == SESSION_TRANSPORT_OWNED)
    acip_transport_destroy(p->transport);
  p->transport = transport;
  p->transport_ownership = transport ? ownership : SESSION_TRANSPORT_BORROWED;
  p->connected = p->transport && acip_transport_is_connected(p->transport);

  return ASCIICHAT_OK;
}

asciichat_error_t session_participant_take_transport(session_participant_t *p, acip_transport_t **transport,
                                                     session_transport_ownership_t *ownership) {
  if (!p || !p->initialized || !transport || !ownership)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport transfer parameters");
  if (session_media_should_run(&p->video) || session_media_should_run(&p->audio) || p->video_thread_created ||
      p->audio_thread_created)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stop media before transferring its transport");
  *transport = p->transport;
  *ownership = p->transport_ownership;
  p->transport = NULL;
  p->transport_ownership = SESSION_TRANSPORT_BORROWED;
  p->connected = false;
  return ASCIICHAT_OK;
}

acip_transport_t *session_participant_get_transport(session_participant_t *p) {
  if (!p || !p->initialized) {
    return NULL;
  }

  return p->transport;
}

bool session_participant_has_transport(session_participant_t *p) {
  if (!p) {
    return false;
  }

  return p->transport != NULL;
}

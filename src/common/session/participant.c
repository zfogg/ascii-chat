#include <ascii-chat/network/acip/settings.h>
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
#include "session/connection.h"
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/log/log.h>
#include "session/capture.h"
#include "session/audio.h"
#include "session/av_state.h"
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/negotiation.h>
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
  session_connection_t connection;
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
  network_connection_t *network;

  /** @brief Responsibility for releasing the selected transport. */
  acip_transport_ownership_t transport_ownership;

  /** @brief Currently connected */
  bool connected;

  /** @brief Assigned client ID */
  uint32_t client_id;

  /** @brief Video streaming active */
  session_av_state_t video;

  /** @brief Audio streaming active */
  session_av_state_t audio;

  /** @brief Current session settings */
  session_settings_t settings;
  mutex_t settings_mutex;
  uint32_t settings_request;

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

static void participant_interrupt(void *context);
static asciichat_error_t participant_join(void *context);
static void participant_release(void *context);

static void participant_connection_init(session_participant_t *p) {
  session_connection_init(&p->connection,
                          (session_connection_ops_t){participant_interrupt, participant_join, participant_release}, p);
}

session_participant_t *session_participant_create(const session_participant_config_t *config) {
  log_info("config=%p", config);

  if (!config) {
    log_error("config is NULL");
    SET_ERRNO(ERROR_INVALID_PARAM, "NULL config");
    return NULL;
  }

  // Allocate participant
  session_participant_t *p = SAFE_CALLOC(1, sizeof(session_participant_t), session_participant_t *);
  log_info("allocated p=%p", p);

  // Copy configuration
  if (config->address) {
    SAFE_STRNCPY(p->address, config->address, sizeof(p->address));
  } else {
    SAFE_STRNCPY(p->address, "127.0.0.1", sizeof(p->address));
  }

  p->port = config->port > 0 ? config->port : OPT_PORT_INT_DEFAULT;
  p->encryption_enabled = config->encryption_enabled;

  log_info("address=%s, port=%d", p->address, p->port);

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
  if (mutex_init(&p->settings_mutex, "participant_settings") != 0) {
    SAFE_FREE(p);
    return NULL;
  }

  participant_connection_init(p);
  p->initialized = true;
  log_info("returning p=%p (initialized=%d, connected=%d)", p, p->initialized, p->connected);
  return p;
}

void session_participant_destroy(session_participant_t *p) {
  if (!p) {
    return;
  }

  session_participant_disconnect(p);
  // A failed join retains the participant and its resources for a safe retry.
  if (atomic_load_int(&p->connection.state) != SESSION_CONNECTION_STOPPED || p->video_thread_created ||
      p->audio_thread_created)
    return;

  // Clear sensitive data
  memset(p->password, 0, sizeof(p->password));
  memset(p->server_key, 0, sizeof(p->server_key));

  p->initialized = false;
  mutex_destroy(&p->settings_mutex);
  SAFE_FREE(p);
}

/* ============================================================================
 * Session Participant Connection Functions
 * ============================================================================ */

asciichat_error_t session_participant_connect(session_participant_t *p) {
  log_debug("p=%p", p);

  if (!p || !p->initialized) {
    log_debug("invalid param or not initialized");
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid participant");
  }

  log_debug("p->initialized=%d, p->connected=%d", p->initialized, p->connected);

  if (p->connected) {
    log_debug("already connected, returning OK");
    return ASCIICHAT_OK; // Already connected
  }
  if (p->port > UINT16_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Participant port is outside the transport endpoint range");

  session_participant_disconnect(p);
  if (p->video_thread_created || p->audio_thread_created)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stop media before reconnecting");

  participant_connection_init(p);

  asciichat_error_t result = network_connection_open("participant", p->address, p->port, NULL, &p->network, NULL);
  if (result != ASCIICHAT_OK)
    return result;
  p->transport = network_connection_transport(p->network);
  p->transport_ownership = ACIP_TRANSPORT_OWNED;
  result = network_connection_prepare_security(p->network, false, p->password[0] ? p->password : NULL);
  if (result == ASCIICHAT_OK) {
    crypto_handshake_context_t *ctx = network_connection_handshake(p->network);
    if (p->server_key[0]) {
      ctx->verify_server_key = true;
      SAFE_STRNCPY(ctx->expected_server_key, p->server_key, sizeof(ctx->expected_server_key));
    }
    result =
        network_client_negotiate(p->transport, ctx, network_connection_crypto_mode(p->network), NETWORK_PROTOCOL_MEDIA);
  }
  if (result != ASCIICHAT_OK) {
    network_connection_destroy(&p->network);
    p->transport = NULL;
    return result;
  }
  network_connection_ready(p->network);
  log_debug("session_participant_connect: setting p->connected=true");

  mutex_lock(&p->settings_mutex);
  p->settings_request = 0;
  p->settings.version = 0;
  mutex_unlock(&p->settings_mutex);
  session_connection_start(&p->connection);
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

  bool was_connected = p->connected;
  if (session_connection_stop(&p->connection) != ASCIICHAT_OK)
    return;

  // Invoke callback
  if (was_connected && p->callbacks.on_disconnected) {
    p->callbacks.on_disconnected(p, p->user_data);
  }
}

static void participant_interrupt(void *context) {
  session_participant_t *p = context;
  session_av_state_begin_stop(&p->audio);
  session_av_state_begin_stop(&p->video);
  session_connection_interrupt_transport(p->transport);
}

static asciichat_error_t participant_join(void *context) {
  session_participant_t *p = context;
  session_participant_stop_audio(p);
  session_participant_stop_video(p);
  if (p->video_thread_created || p->audio_thread_created)
    return SET_ERRNO(ERROR_THREAD, "Participant workers still own connection resources");
  return ASCIICHAT_OK;
}

static void participant_release(void *context) {
  session_participant_t *p = context;
  network_connection_destroy(&p->network);
  p->transport = NULL;
  p->transport_ownership = ACIP_TRANSPORT_BORROWED;
  p->connected = false;
  p->client_id = 0;
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

bool session_participant_is_video_active(session_participant_t *p) {
  return p && p->initialized && session_av_state_is_active(&p->video);
}

bool session_participant_is_audio_active(session_participant_t *p) {
  return p && p->initialized && session_av_state_is_active(&p->audio);
}

/* ============================================================================
 * Session Participant Settings Functions
 * ============================================================================ */

asciichat_error_t session_participant_get_settings(session_participant_t *p, session_settings_t *settings) {
  if (!p || !p->initialized || !settings) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid parameter");
  }

  mutex_lock(&p->settings_mutex);
  memcpy(settings, &p->settings, sizeof(session_settings_t));
  mutex_unlock(&p->settings_mutex);
  return ASCIICHAT_OK;
}

asciichat_error_t session_participant_request_settings(session_participant_t *p, const session_settings_t *settings) {
  if (!p || !p->initialized || !settings) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid parameter");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "not connected");
  }

  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  session_settings_to_options(settings, values);
  if (values->color_mode == TERM_COLOR_AUTO) {
    terminal_capabilities_t caps = detect_terminal_capabilities();
    values->color_mode = caps.detection_reliable ? caps.color_level : TERM_COLOR_NONE;
  }
  acip_render_settings_t prepared = {0};
  asciichat_error_t result = acip_render_settings_prepare(&prepared, values);
  if (result == ASCIICHAT_OK) {
    mutex_lock(&p->settings_mutex);
    uint32_t request = ++p->settings_request;
    mutex_unlock(&p->settings_mutex);
    result = acip_settings_send(p->transport, values, request, UINT32_MAX);
  }
  SAFE_FREE(values);
  return result;
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
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid participant or video_capture context");
    return NULL;
  }

  log_info("Video capture thread started");

  while (session_av_state_should_run(&p->video)) {
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
      if (err == ASCIICHAT_OK)
        session_capture_complete_open(p->video_capture, session_capture_source_epoch(p->video_capture));
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
    SET_ERRNO(ERROR_INVALID_PARAM, "invalid participant, audio_capture, or opus_encoder");
    return NULL;
  }

  log_info("Audio capture thread started");

  float sample_buffer[960]; // 20ms @ 48kHz
  uint8_t opus_buffer[1000];

  while (session_av_state_should_run(&p->audio)) {
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
  asciichat_error_t code = errno_peek_since(checkpoint, &cause) ? cause.code : ERROR_INVALID_STATE;
  return SET_ERRNO(code, "%s", message);
}

asciichat_error_t session_participant_start_video(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid participant");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "not connected");
  }

  if (!p->enable_video) {
    return SET_ERRNO(ERROR_INVALID_STATE, "video not enabled");
  }

  if (session_av_state_is_active(&p->video))
    return ASCIICHAT_OK;
  uint32_t generation = session_av_state_begin(&p->video);
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
    asciichat_errno_scope_t checkpoint = errno_checkpoint();
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
  session_av_state_complete(&p->video, generation);
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
  session_av_state_begin_stop(&p->video);
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
  session_av_state_finish_stop(&p->video);
}

asciichat_error_t session_participant_start_audio(session_participant_t *p) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "invalid participant");
  }

  if (!p->connected) {
    return SET_ERRNO(ERROR_INVALID_STATE, "not connected");
  }

  if (!p->enable_audio) {
    return SET_ERRNO(ERROR_INVALID_STATE, "audio not enabled");
  }

  if (session_av_state_is_active(&p->audio))
    return ASCIICHAT_OK;
  uint32_t generation = session_av_state_begin(&p->audio);
  if (!generation)
    return SET_ERRNO(ERROR_INVALID_STATE, "audio is starting or stopping");

  // Create audio capture context if not already created
  if (!p->audio_capture) {
    asciichat_errno_scope_t checkpoint = errno_checkpoint();
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
  session_capture_set_audio_context(p->video_capture, session_audio_get_context(p->audio_capture));

  // Start audio capture and playback
  asciichat_error_t err = session_audio_start_duplex(p->audio_capture);
  if (err != ASCIICHAT_OK) {
    session_participant_stop_audio(p);
    return SET_ERRNO(err, "Failed to start audio duplex");
  }

  // Create Opus encoder for audio compression (48kHz, VOIP mode, 24 kbps)
  if (!p->opus_encoder) {
    asciichat_errno_scope_t checkpoint = errno_checkpoint();
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
  session_av_state_complete(&p->audio, generation);
  log_info("Audio capture started");
  return ASCIICHAT_OK;
}

void session_participant_stop_audio(session_participant_t *p) {
  if (!p || !p->initialized)
    return;
  session_av_state_begin_stop(&p->audio);
  if (p->audio_thread_created) {
    if (asciichat_thread_join(&p->audio_capture_thread, NULL) != 0) {
      log_error("Failed to join audio capture worker; retaining its resources");
      return;
    }
    p->audio_thread_created = false;
  }
  if (p->audio_capture) {
    session_capture_set_audio_context(p->video_capture, NULL);
    session_audio_destroy(p->audio_capture);
    p->audio_capture = NULL;
  }
  if (p->opus_encoder) {
    opus_codec_destroy(p->opus_encoder);
    p->opus_encoder = NULL;
  }
  p->audio_uses_video = false;
  session_av_state_finish_stop(&p->audio);
}

/* ============================================================================
 * Session Participant Transport Functions (WebRTC Integration)
 * ============================================================================ */

asciichat_error_t session_participant_set_transport(session_participant_t *p, acip_transport_t *transport,
                                                    acip_transport_ownership_t ownership) {
  if (!p || !p->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Participant is NULL or not initialized");
  }

  if (session_av_state_should_run(&p->video) || session_av_state_should_run(&p->audio) || p->video_thread_created ||
      p->audio_thread_created)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stop media before replacing its transport");

  log_info("Setting transport=%p (was=%p)", transport, p->transport);

  if (ownership != ACIP_TRANSPORT_BORROWED && ownership != ACIP_TRANSPORT_OWNED)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport ownership");
  if (transport == p->transport) {
    if (transport && ownership != p->transport_ownership)
      return SET_ERRNO(ERROR_INVALID_STATE, "Detach before transferring transport ownership");
    return ASCIICHAT_OK;
  }
  asciichat_error_t result = session_connection_stop(&p->connection);
  if (result != ASCIICHAT_OK)
    return result;
  participant_connection_init(p);
  if (transport && ownership == ACIP_TRANSPORT_OWNED) {
    result = network_connection_adopt(transport, &p->network);
    if (result != ASCIICHAT_OK)
      return result;
  }
  p->transport = transport;
  mutex_lock(&p->settings_mutex);
  p->settings_request = 0;
  p->settings.version = 0;
  mutex_unlock(&p->settings_mutex);
  p->transport_ownership = transport ? ownership : ACIP_TRANSPORT_BORROWED;
  if (transport) {
    result = session_connection_start(&p->connection);
    if (result != ASCIICHAT_OK)
      return result;
  }
  p->connected = p->transport && acip_transport_is_connected(p->transport);

  return ASCIICHAT_OK;
}

asciichat_error_t session_participant_take_connection(session_participant_t *p, network_connection_t **connection) {
  if (!p || !p->initialized || !connection)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing participant connection output");
  if (session_av_state_should_run(&p->video) || session_av_state_should_run(&p->audio) || p->video_thread_created ||
      p->audio_thread_created)
    return SET_ERRNO(ERROR_INVALID_STATE, "Stop media before transferring its connection");
  if (!p->network)
    return SET_ERRNO(ERROR_INVALID_STATE, "Participant does not own a connection");
  *connection = p->network;
  p->network = NULL;
  p->transport = NULL;
  p->transport_ownership = ACIP_TRANSPORT_BORROWED;
  p->connected = false;
  return ASCIICHAT_OK;
}

asciichat_error_t session_participant_set_connection(session_participant_t *p, network_connection_t **owner) {
  if (!owner || !*owner)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing participant connection");
  asciichat_error_t result = session_participant_set_transport(p, NULL, ACIP_TRANSPORT_BORROWED);
  if (result != ASCIICHAT_OK)
    return result;
  p->network = *owner;
  *owner = NULL;
  p->transport = network_connection_transport(p->network);
  p->transport_ownership = ACIP_TRANSPORT_OWNED;
  result = session_connection_start(&p->connection);
  p->connected = result == ASCIICHAT_OK && acip_transport_is_connected(p->transport);
  return result;
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

network_connection_t *session_participant_connection(session_participant_t *p) {
  return p ? p->network : NULL;
}

asciichat_error_t session_participant_receive_settings(session_participant_t *p, const void *payload, size_t length) {
  if (!p || !p->initialized)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid participant");
  acip_settings_header_t header;
  const char *json;
  size_t json_length;
  asciichat_error_t result = acip_settings_decode(payload, length, &header, &json, &json_length);
  if (result != ASCIICHAT_OK)
    return result;
  mutex_lock(&p->settings_mutex);
  if ((header.request && header.request != p->settings_request) || header.revision < p->settings.version) {
    mutex_unlock(&p->settings_mutex);
    return ASCIICHAT_OK;
  }
  options_t *current = SAFE_MALLOC(sizeof(*current), options_t *),
            *candidate = SAFE_MALLOC(sizeof(*candidate), options_t *);
  session_settings_to_options(&p->settings, current);
  result = options_prepare_json(current, json, json_length, candidate, true);
  if (result == ASCIICHAT_OK) {
    session_settings_from_values(candidate, &p->settings);
    p->settings.version = header.revision;
  }
  session_settings_t effective = p->settings;
  SAFE_FREE(current);
  SAFE_FREE(candidate);
  mutex_unlock(&p->settings_mutex);
  if (result != ASCIICHAT_OK)
    return result;
  if (header.status && p->callbacks.on_error)
    p->callbacks.on_error(p, (asciichat_error_t)header.status, "Remote rendering update rejected", p->user_data);
  if (p->callbacks.on_settings_changed)
    p->callbacks.on_settings_changed(p, &effective, p->user_data);
  return ASCIICHAT_OK;
}

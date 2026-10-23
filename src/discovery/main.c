#include <ascii-chat/ui/input.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/stats/runtime.h>
/**
 * @file discovery/main.c
 * @ingroup discovery_main
 * @brief ascii-chat Discovery Mode Entry Point
 *
 * Discovery mode enables zero-configuration video chat where participants
 * can dynamically become hosts based on NAT quality. This implements the
 * "one command to start, one command to join" philosophy.
 *
 * ## Discovery Flow
 *
 * 1. Parse session string (word-word-word format)
 * 2. Query ACDS for session information
 * 3. Assess NAT quality (STUN test)
 * 4. Either:
 *    a. Connect to existing host as participant
 *    b. Become host if no host exists or better NAT quality
 * 5. Handle dynamic role changes during session
 *
 * ## Implementation Architecture
 *
 * Discovery mode uses lib/session APIs for media handling:
 * - session_host_t: For rendering and broadcasting when elected host
 * - session_participant_t: For capture and display when not host
 * - discovery_session_t: For ACDS connection and host negotiation
 *
 * No network protocol code duplication - all media handling via lib/session.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 * @version 1.0
 */

#include <ascii-chat/ui/notice.h>

#include "main.h"
#include "../main.h" // Global exit API
#include "session.h"
#include "session/capture.h"
#include "session/display.h"
#include "session/host.h"
#include "session/audio.h"
#include "session/render.h"
#include "session/keyboard_handler.h"
#include "session/client_like.h"
#include "session/participant.h"
#include "session/connection.h"
#include "session/protocol.h"
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/platform/terminal.h>

#include <stdio.h>

#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/common.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/client_pipeline.h>

/* ============================================================================
 * Global Discovery Session
 * ============================================================================ */

typedef struct {
  session_connection_t connection;
  session_protocol_t protocol;
  network_connection_t *network;
  asciichat_thread_t receiver_thread;
  asciichat_thread_t audio_thread;
  bool receiver_started;
  bool audio_started;
  acip_transport_t *transport;
  session_display_ctx_t *display;
  atomic_t running;
  asciichat_error_t result;
  audio_context_t *audio;
  client_audio_pipeline_t *audio_pipeline;
  uint64_t observed_audio_blocks, observed_audio_samples, observed_audio_ns, observed_audio_gap_ns;
  uint64_t observed_video_frames;
} discovery_video_receiver_t;

static void discovery_media_interrupt(void *context) {
  discovery_video_receiver_t *receiver = context;
  atomic_store_bool(&receiver->running, false);
  session_connection_interrupt_transport(receiver->transport);
}

static asciichat_error_t discovery_media_join(void *context) {
  discovery_video_receiver_t *receiver = context;
  if (receiver->receiver_started) {
    asciichat_error_t result = thread_join(&receiver->receiver_thread, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    receiver->receiver_started = false;
  }
  if (receiver->audio_started) {
    asciichat_error_t result = thread_join(&receiver->audio_thread, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    receiver->audio_started = false;
  }
  return ASCIICHAT_OK;
}

static void discovery_media_release(void *context) {
  discovery_video_receiver_t *receiver = context;
  if (receiver->audio) {
    audio_destroy(receiver->audio);
    receiver->audio = NULL;
  }
  client_audio_pipeline_destroy(receiver->audio_pipeline);
  receiver->audio_pipeline = NULL;
  log_info("DISCOVERY_MEDIA_RELEASED: workers=0");
}

typedef struct {
  session_host_t *host;
  session_audio_ctx_t *audio;
  uint32_t participant_id;
  atomic_t running;
  asciichat_thread_t thread;
  bool started;
  asciichat_error_t result;
} discovery_host_audio_capture_t;

static void *discovery_host_audio_capture(void *user_data) {
  discovery_host_audio_capture_t *capture = user_data;
  float samples[960];
  while (atomic_load_bool(&capture->running) && !should_exit()) {
    size_t count = session_audio_read_captured(capture->audio, samples, 960);
    if (!count) {
      platform_sleep_ns(NS_PER_MS_INT);
      continue;
    }
    capture->result = session_host_inject_audio(capture->host, capture->participant_id, samples, count);
    if (capture->result != ASCIICHAT_OK)
      break;
  }
  atomic_store_bool(&capture->running, false);
  return NULL;
}

static void discovery_host_audio_join(discovery_host_audio_capture_t *capture) {
  atomic_store_bool(&capture->running, false);
  if (!capture->started)
    return;
  // The worker borrows this role's stack and audio context. Retain both until
  // the join succeeds, including error/shutdown paths.
  while (thread_join(&capture->thread, NULL) != ASCIICHAT_OK) {
    log_warn_every(NS_PER_SEC_INT, "Waiting for native host audio capture worker to stop");
    platform_sleep_ns(NS_PER_MS_INT);
  }
  capture->started = false;
}

static void *discovery_audio_send_thread(void *user_data) {
  discovery_video_receiver_t *ctx = user_data;
  float samples[960];
  uint8_t encoded[CLIENT_AUDIO_PIPELINE_MAX_OPUS_PACKET];
  while (atomic_load_bool(&ctx->running) && !should_exit()) {
    if (audio_ring_buffer_available_read(ctx->audio->capture_buffer) < 960) {
      platform_sleep_ns(NS_PER_MS_INT);
      continue;
    }
    if (audio_ring_buffer_read(ctx->audio->capture_buffer, samples, 960) != 960)
      continue;
    ASCII_OTEL_SCOPE(CLIENT_AUDIO_CAPTURE);
    int size = client_audio_pipeline_capture(ctx->audio_pipeline, samples, 960, encoded, sizeof(encoded));
    if (size <= 0)
      continue;
    uint16_t frame_size = (uint16_t)size;
    asciichat_error_t result = acip_send_audio_opus_batch(ctx->transport, encoded, size, &frame_size, 1, 48000, 20);
    if (result != ASCIICHAT_OK) {
      log_warn("Discovery audio send failed %s", asciichat_error_format(NULL, result));
      atomic_store_bool(&ctx->running, false);
      break;
    }
  }
  return NULL;
}

static asciichat_error_t discovery_present_frame(const ascii_frame_packet_t *header, const char *text, void *context) {
  discovery_video_receiver_t *receiver = context;
  (void)header;
  session_display_render_frame(receiver->display, text);
  if (GET_OPTION(test_audio) && text && text[0]) {
    receiver->observed_video_frames++;
    if (receiver->observed_video_frames == 1 || receiver->observed_video_frames % 25 == 0)
      log_info("Discovery decoded video: frames=%llu", (unsigned long long)receiver->observed_video_frames);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t discovery_play_samples(const float *samples, size_t count, void *context) {
  discovery_video_receiver_t *receiver = context;
  if (!receiver->audio)
    return ASCIICHAT_OK;
  size_t observed_count = count;
  double energy = 0;
  if (GET_OPTION(test_audio))
    for (size_t i = 0; i < count; ++i)
      energy += (double)samples[i] * samples[i];
  while (count) {
    size_t chunk = count > 5760 ? 5760 : count;
    asciichat_error_t result = audio_write_samples(receiver->audio, samples, (int)chunk);
    if (result != ASCIICHAT_OK)
      return result;
    samples += chunk;
    count -= chunk;
  }
  // Test-source diagnostics observe remote decoded PCM only, after successful
  // playback submission. Local test monitoring never reaches this callback.
  if (GET_OPTION(test_audio) && observed_count && energy / observed_count >= 0.000025) {
    uint64_t now = time_get_ns();
    if (receiver->observed_audio_ns && now - receiver->observed_audio_ns > receiver->observed_audio_gap_ns)
      receiver->observed_audio_gap_ns = now - receiver->observed_audio_ns;
    receiver->observed_audio_ns = now;
    receiver->observed_audio_samples += observed_count;
    receiver->observed_audio_blocks++;
    if (receiver->observed_audio_blocks == 1 || receiver->observed_audio_blocks % 25 == 0)
      log_info("Discovery decoded remote audio: blocks=%llu samples=%llu at_ns=%llu max_gap_ns=%llu",
               (unsigned long long)receiver->observed_audio_blocks,
               (unsigned long long)receiver->observed_audio_samples, (unsigned long long)now,
               (unsigned long long)receiver->observed_audio_gap_ns);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t discovery_decode_opus(const void *data, size_t length, float *samples, size_t capacity,
                                               size_t *decoded, void *context) {
  discovery_video_receiver_t *receiver = context;
  *decoded = 0;
  if (!receiver->audio)
    return ASCIICHAT_OK;
  int count = client_audio_pipeline_playback(receiver->audio_pipeline, data, (int)length, samples, (int)capacity);
  if (count < 0)
    return SET_ERRNO(ERROR_AUDIO, "Could not decode participant Opus audio");
  *decoded = (size_t)count;
  return ASCIICHAT_OK;
}

static void discovery_host_pong(void *arg) {
  discovery_session_host_pong(arg);
}

static asciichat_error_t discovery_media_packet(network_connection_t *network, packet_type_t type, const void *payload,
                                                size_t length, void *arg) {
  (void)network;
  discovery_video_receiver_t *receiver = arg;
  return session_protocol_dispatch(&receiver->protocol, type, payload, length);
}
static void *discovery_video_receive_thread(void *arg) {
  discovery_video_receiver_t *receiver = arg;
  network_connection_callbacks_t callbacks = {.packet = discovery_media_packet};
  receiver->result = network_connection_run(receiver->network, &callbacks, receiver);
  if (!atomic_load_bool(&receiver->running) || should_exit())
    receiver->result = ASCIICHAT_OK;
  atomic_store_bool(&receiver->running, false);
  return NULL;
}

/** Global discovery session created in discovery_main() and used by discovery_run() */
static discovery_session_t *g_discovery = NULL;

/* ============================================================================
 * State Change Callback
 * ============================================================================ */

/**
 * @brief Handle discovery session state changes
 *
 * @param new_state The new discovery state
 * @param user_data Pointer to discovery_session_t (unused, logged via session APIs)
 */
static void on_discovery_state_change(discovery_state_t new_state, void *user_data) {
  (void)user_data; // Unused
  static uint64_t setup_started;
  if (new_state == DISCOVERY_STATE_CONNECTING_ACDS)
    setup_started = time_get_ns();
  if (setup_started && (new_state == DISCOVERY_STATE_ACTIVE || new_state == DISCOVERY_STATE_FAILED)) {
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_CONNECTION_SETUP, time_get_ns() - setup_started);
    setup_started = 0;
  }

  const char *state_names[] = {"INIT",         "CONNECTING_ACDS", "CREATING_SESSION", "JOINING_SESSION",
                               "WAITING_PEER", "NEGOTIATING",     "STARTING_HOST",    "CONNECTING_HOST",
                               "ACTIVE",       "MIGRATING",       "FAILED",           "ENDED"};

  if (new_state >= 0 && new_state < (int)(sizeof(state_names) / sizeof(state_names[0]))) {
    if (new_state == DISCOVERY_STATE_ACTIVE && g_discovery)
      stats_runtime_connection_state(discovery_session_is_host(g_discovery) ? "ACTIVE / host" : "ACTIVE / participant");
    else
      stats_runtime_connection_state(state_names[new_state]);
    log_info("Discovery state: %s", state_names[new_state]);
  }
}

/**
 * @brief Handle session ready (session string available for sharing)
 *
 * @param session_string The session string to share with peer
 * @param user_data Unused
 */
static void on_session_ready(const char *session_string, void *user_data) {
  (void)user_data; // Unused

  if (session_string && session_string[0]) {
    const char *requested = GET_OPTION(session_string);
    splash_set_discovery_session(session_string, requested && requested[0]);
    log_info("Share this string to connect: %s", session_string);
    log_info("Run: ascii-chat %s", session_string);
  }
}

typedef struct {
  mutex_t mutex;
  char error[512];
  asciichat_error_t error_code;
} discovery_ui_state_t;

/** Save callback errors for reporting after terminal cleanup. */
static void on_discovery_error(asciichat_error_t error, const char *message, void *user_data) {
  discovery_ui_state_t *ui = user_data;
  mutex_lock(&ui->mutex);
  SAFE_STRNCPY(ui->error, message ? message : "Unknown discovery error", sizeof(ui->error));
  ui->error_code = error;
  mutex_unlock(&ui->mutex);
  log_error("%s", asciichat_error_format(message ? message : "Unknown discovery error", error));
  signal_exit();
}

/**
 * Adapter function for session capture exit callback
 * Converts from void*->bool signature to match session_capture_should_exit_fn
 *
 * @param user_data Unused (NULL)
 * @return true if capture should exit
 */
static bool discovery_capture_should_exit_adapter(void *user_data) {
  (void)user_data; // Unused parameter
  return should_exit();
}

/**
 * Discovery mode keyboard handler callback
 *
 * Enables interactive media controls during participant role.
 * Passes keyboard input to the session handler with capture context.
 *
 * @param capture Capture context for media source control
 * @param key Keyboard key code
 * @param user_data Unused (NULL)
 */
static void discovery_keyboard_handler(session_capture_ctx_t *capture, int key, void *user_data) {
  (void)user_data; // Unused parameter
  // Discovery mode doesn't have a display context yet, pass NULL for display
  // Help screen will be silently ignored (no-op)
  session_handle_keyboard_input(capture, NULL, (keyboard_key_t)key);
}

/* ============================================================================
 * Discovery Run Callback (for session_client_like)
 * ============================================================================ */

/**
 * Discovery mode main loop callback for session_client_like framework
 *
 * Handles role negotiation (host/participant) and media flow based on role.
 * The framework calls this after shared initialization (capture/display/audio).
 *
 * @param capture   Initialized capture context (provided by session_client_like)
 * @param display   Initialized display context (provided by session_client_like)
 * @param user_data Unused (NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t discovery_send_settings(const options_t *values, uint32_t request, void *context) {
  return acip_settings_send(context, values, request, UINT32_MAX);
}

static asciichat_error_t discovery_run(session_capture_ctx_t *capture, session_display_ctx_t *display,
                                       void *user_data) {
  (void)user_data; // Unused

  asciichat_error_t result = ASCIICHAT_OK;
  bool snapshot_mode = GET_OPTION(snapshot_mode);

role_dispatch:
  result = discovery_session_apply_handoff(g_discovery);
  if (result != ASCIICHAT_OK)
    return result;

  // Wait for session to become active (host negotiation complete)
  // This processes ACDS events until we have a determined role
  // Snapshot capture requires negotiation to finish before its exit condition is honored.
  // The snapshot delay will be honored once we transition to active state and start capturing
  while (true) {
    if (should_exit() && !snapshot_mode) {
      return ASCIICHAT_OK;
    }
    result = discovery_session_process(g_discovery, 50 * NS_PER_MS_INT);
    if (result != ASCIICHAT_OK && result != ERROR_NETWORK_TIMEOUT) {
      log_error("Discovery session process failed %s", asciichat_error_format(NULL, result));
      return result;
    }

    if (g_discovery->handoff_pending)
      goto role_dispatch;
    if (discovery_session_is_active(g_discovery)) {
      break; // Session is active, role is determined
    }
    platform_sleep_ns(NS_PER_MS_INT);
  }

  // In snapshot mode, proceed to role handling even if should_exit() is true
  // We'll check for exit conditions during the actual frame rendering phase
  if (should_exit() && !snapshot_mode) {
    log_info("Shutdown requested during discovery negotiation");
    return ASCIICHAT_OK;
  }

  // Session is active - run based on our role
  bool we_are_host = discovery_session_is_host(g_discovery);

  if (we_are_host) {
    // HOST ROLE: Capture own media, manage participants, mix and broadcast
    log_info("Hosting session - capturing and broadcasting");

    // Keep terminal logging enabled until a peer connects and we have video to render.
    // This keeps the splash/log UI visible while waiting for peers instead of a blank screen.

    // Get the host context already created and started during discovery negotiation
    session_host_t *host = discovery_session_get_host(g_discovery);
    if (!host) {
      log_fatal("Failed to get session host from discovery");
      return ERROR_INVALID_STATE;
    }

    // Set display context so host renders frames locally
    asciichat_error_t display_result = session_host_set_display(host, display);
    if (display_result != ASCIICHAT_OK) {
      log_error("Failed to set display context on host: %d", display_result);
      // Non-fatal - host can still render frames for broadcasting, just not locally
    } else {
      log_info("Display context set on host for local frame rendering");
    }

    // The passed capture context is set up for network mode (participant receiving frames).
    // For host role, we need local media capture. Reuse the existing capture context
    // which should have test pattern, webcam, or file configured.
    // Note: session_client_like should have initialized this with the media source.

    // Add memory participant for host's own media
    uint32_t host_participant_id = session_host_add_memory_participant(host);
    if (host_participant_id == 0) {
      log_error("Failed to add memory participant for host");
      // Host context is owned by discovery session - don't destroy
      return ERROR_INVALID_STATE;
    }

    log_info("Host participating with ID %u", host_participant_id);

    // The native host is also a media source. Own its capture worker for this
    // role and feed the same memory participant used for local video.
    session_audio_ctx_t *host_audio = NULL;
    discovery_host_audio_capture_t host_capture = {.host = host, .participant_id = host_participant_id};
    if (GET_OPTION(audio_enabled)) {
      host_audio = session_audio_create(true);
      if (!host_audio)
        return SET_ERRNO(ERROR_AUDIO, "Failed to create host audio capture");
      session_audio_set_capture_source(host_audio, session_capture_get_media_source(capture));
      result = session_audio_start_capture(host_audio);
      if (result != ASCIICHAT_OK) {
        session_audio_destroy(host_audio);
        return result;
      }
      host_capture.audio = host_audio;
      atomic_store_bool(&host_capture.running, true);
      result = acthread_create(&host_capture.thread, "host_audio_capture", discovery_host_audio_capture, &host_capture);
      if (result != ASCIICHAT_OK) {
        session_audio_destroy(host_audio);
        return result;
      }
      host_capture.started = true;
      session_capture_set_audio_context(capture, session_audio_get_context(host_audio));
    }

    // Start the render thread (handles video mixing and ASCII rendering for display)
    result = session_host_start_render(host);
    if (result != ASCIICHAT_OK) {
      discovery_host_audio_join(&host_capture);
      session_capture_set_audio_context(capture, NULL);
      session_audio_destroy(host_audio);
      log_error("Failed to start render thread: %d", result);
      // Host context is owned by discovery session - don't destroy
      return result;
    }

    // Main loop: capture own media and keep discovery responsive
    while (discovery_session_is_active(g_discovery)) {
      ASCII_OTEL_SCOPE(CLIENT_VIDEO_FRAME);
      // Exit conditions:
      // 1. Non-snapshot mode: check should_exit() (Ctrl+C, SIGTERM, any exit signal)
      // 2. Snapshot mode: exit when should_exit() is true AND we've rendered at least one frame
      //    (to allow the full snapshot_delay to elapse while ignoring the snapshot timeout in should_exit)
      // 3. Discovery session ended: always exit

      if (should_exit()) {
        // In snapshot mode, if we haven't rendered any frames yet, don't exit yet
        // Wait until at least one frame has been displayed
        if (!snapshot_mode || g_snapshot_first_frame_rendered) {
          log_debug("Exit condition met (snapshot_mode=%d, first_frame_rendered=%d)", snapshot_mode,
                    g_snapshot_first_frame_rendered ? 1 : 0);
          break;
        } else {
          log_debug_every(5 * NS_PER_SEC_INT,
                          "Waiting for first frame (snapshot_mode=true, first_frame_rendered=false)");
        }
      }

      keyboard_key_t key = ui_input_read_key(UI_SCREEN_MEDIA);
      if (key != KEY_NONE)
        session_handle_keyboard_input(capture, display, key);
      // Capture frame from local media (webcam, test pattern, file, etc.)
      // The capture context is set up during session_client_like_run() and handles all media types
      asciichat_errno_scope_t frame_scope = errno_checkpoint();
      image_t *frame = session_capture_read_frame(capture);
      asciichat_error_context_t capture_failure;
      if (!frame && errno_peek_since(frame_scope, &capture_failure)) {
        result = capture_failure.code;
        break;
      }
      if (frame) {
        log_debug_every(5 * NS_PER_SEC_INT, "Captured frame, injecting into host");
        // Inject host's frame into mixer for broadcasting
        result = session_host_inject_frame(host, host_participant_id, frame);
        if (result == ASCIICHAT_OK)
          session_capture_complete_open(capture, session_capture_source_epoch(capture));
        if (result != ASCIICHAT_OK) {
          LOG_ERRNO_IF_SET("Failed to inject host frame");
          break;
        }
      } else {
        log_debug_every(5 * NS_PER_SEC_INT, "No frame captured");
      }

      if (host_capture.started && !atomic_load_bool(&host_capture.running))
        break;

      // Keep discovery session responsive (NAT negotiations, migrations)
      asciichat_errno_scope_t process_scope = errno_scope_begin();
      result = discovery_session_process(g_discovery, 10 * NS_PER_MS_INT);
      if (result != ASCIICHAT_OK && result != ERROR_NETWORK_TIMEOUT) {
        log_error("Discovery session process failed %s", asciichat_error_format(NULL, result));
        break;
      }

      if (result == ASCIICHAT_OK)
        ASSERT_NO_ERRNO_SINCE(process_scope);
      errno_scope_end(process_scope, ASCIICHAT_ERRNO_HANDLED);
      result = ASCIICHAT_OK;

      // Frame rate limiting (60 FPS)
      session_capture_sleep_for_fps(capture);
    }

    log_debug("Host role main loop exited (snapshot_mode=%d, first_frame_rendered=%d, session_active=%d)",
              snapshot_mode, g_snapshot_first_frame_rendered ? 1 : 0, discovery_session_is_active(g_discovery) ? 1 : 0);

    discovery_host_audio_join(&host_capture);
    if (result == ASCIICHAT_OK && host_capture.result != ASCIICHAT_OK)
      result = SET_ERRNO(host_capture.result, "Native host audio capture worker failed");
    session_capture_set_audio_context(capture, NULL);
    session_audio_destroy(host_audio);

    // Stop render thread before returning (display will be destroyed by caller)
    session_host_stop_render(host);

    if (result != ASCIICHAT_OK)
      return result;

    if (should_exit()) {
      return ASCIICHAT_OK;
    }

    if (g_discovery->handoff_pending) {
      // A retained host re-enters this branch and registers fresh local media.
      result = session_host_remove_client(host, host_participant_id);
      if (result != ASCIICHAT_OK)
        return result;
      goto role_dispatch;
    }

    // If we reach here, discovery session is no longer active (role change)
    if (!discovery_session_is_active(g_discovery)) {
      log_info("Session ended or role changed");
      return ASCIICHAT_OK;
    }
  } else {
    session_participant_t *participant = discovery_session_get_participant(g_discovery);
    acip_transport_t *transport = session_participant_get_transport(participant);
    if (transport) {
      bool enable_audio = GET_OPTION(audio_enabled);
      log_info("Discovery participant audio: %s", enable_audio ? "enabled" : "disabled");
      uint64_t advertised_revision = UINT64_MAX;
      terminal_size_t advertised = {.cols = GET_OPTION(width) > 0 ? GET_OPTION(width) : 80,
                                    .rows = GET_OPTION(height) > 0 ? GET_OPTION(height) : 24};
      terminal_capabilities_packet_t packet;
      session_protocol_build_capabilities(advertised.cols, advertised.rows, &packet);
      result = acip_send_capabilities(transport, &packet, sizeof(packet));
      if (result != ASCIICHAT_OK)
        return result;
      result = acip_send_stream_start(transport, STREAM_TYPE_VIDEO | (enable_audio ? STREAM_TYPE_AUDIO : 0));
      if (result != ASCIICHAT_OK)
        return result;
      discovery_video_receiver_t receiver = {.network = session_participant_connection(g_discovery->participant_ctx),
                                             .transport = transport,
                                             .display = display,
                                             .running = {0},
                                             .result = ASCIICHAT_OK};
      session_protocol_init(&receiver.protocol, transport,
                            (session_protocol_media_t){.terminal_output = true,
                                                       .on_frame = discovery_present_frame,
                                                       .on_samples = discovery_play_samples,
                                                       .decode_opus = discovery_decode_opus,
                                                       .context = &receiver},
                            (acip_client_callbacks_t){.on_pong = discovery_host_pong, .app_ctx = g_discovery});
      audio_context_t audio = {0};
      session_connection_init(
          &receiver.connection,
          (session_connection_ops_t){discovery_media_interrupt, discovery_media_join, discovery_media_release},
          &receiver);
      NAMED_REGISTER_ATOMIC(&receiver.running, "discovery.media_running", NULL);
      NAMED_REGISTER_ATOMIC(&receiver.connection.state, "discovery.media_state", NULL);
      if (enable_audio) {
        result = audio_init(&audio);
        if (result != ASCIICHAT_OK)
          goto media_cleanup;
        receiver.audio = &audio;
        session_capture_set_audio_context(capture, &audio);
        media_source_t *local_source = (media_source_t *)session_capture_get_media_source(capture);
        bool has_media_audio = media_source_has_audio(local_source);
        audio.capture_media_source = local_source;
        audio.playback_only = !audio_should_enable_microphone(GET_OPTION(audio_capture_source), has_media_audio);
        log_info("Discovery audio input: media=%d, microphone=%d", has_media_audio,
                 audio_should_enable_microphone(GET_OPTION(audio_capture_source), has_media_audio));
        client_audio_pipeline_config_t config = client_audio_pipeline_config_for_source(has_media_audio);
        config.flags.jitter_buffer = false;
        receiver.audio_pipeline = client_audio_pipeline_create(&config);
        if (!receiver.audio_pipeline) {
          result = SET_ERRNO(ERROR_AUDIO, "Failed to create discovery audio pipeline");
          goto media_cleanup;
        }
        audio_set_pipeline(&audio, receiver.audio_pipeline);
        receiver.audio = &audio;
        result = audio_start_duplex(&audio);
        if (result != ASCIICHAT_OK) {
          goto media_cleanup;
        }
      }
      atomic_store_bool(&receiver.running, true);
      result = acthread_create(&receiver.receiver_thread, "discovery_video_receive", discovery_video_receive_thread,
                               &receiver);
      if (result != ASCIICHAT_OK)
        goto media_cleanup;
      receiver.receiver_started = true;
      if (enable_audio) {
        result =
            acthread_create(&receiver.audio_thread, "discovery_audio_send", discovery_audio_send_thread, &receiver);
        receiver.audio_started = result == ASCIICHAT_OK;
        if (!receiver.audio_started)
          atomic_store_bool(&receiver.running, false);
      }
      if (result != ASCIICHAT_OK)
        goto media_cleanup;
      result = session_connection_start(&receiver.connection);
      if (result != ASCIICHAT_OK)
        goto media_cleanup;
      while (!should_exit() && discovery_session_is_active(g_discovery) && acip_transport_is_connected(transport) &&
             atomic_load_bool(&receiver.running)) {
        ASCII_OTEL_SCOPE(CLIENT_VIDEO_FRAME);
        result =
            session_protocol_update_settings(&advertised, &advertised_revision, discovery_send_settings, transport);
        if (result != ASCIICHAT_OK)
          break;
        keyboard_key_t key = ui_input_read_key(UI_SCREEN_MEDIA);
        if (key != KEY_NONE)
          session_handle_keyboard_input(capture, display, key);
        uint64_t iteration_start = time_get_ns();
        if (snapshot_mode && g_snapshot_first_frame_rendered_ns > 0 &&
            time_ns_to_s(time_elapsed_ns(g_snapshot_first_frame_rendered_ns, iteration_start)) >=
                GET_OPTION(snapshot_delay)) {
          signal_exit();
          break;
        }
        uint64_t capture_start = time_get_ns();
        asciichat_errno_scope_t frame_scope = errno_checkpoint();
        image_t *frame = session_capture_read_frame(capture);
        asciichat_error_context_t capture_failure;
        if (!frame && errno_peek_since(frame_scope, &capture_failure)) {
          result = capture_failure.code;
          break;
        }
        uint64_t capture_end = time_get_ns();
        uint64_t processing_start = capture_end;
        uint64_t processing_end = processing_start;
        uint64_t send_start = processing_start;
        uint64_t send_end = send_start;
        if (frame) {
          image_t *processed = session_capture_process_for_transmission(capture, frame);
          if (processed) {
            processing_end = time_get_ns();
            send_start = processing_end;
            result = acip_send_image_frame(transport, processed->pixels, processed->w, processed->h, PIXEL_FORMAT_RGB);
            send_end = time_get_ns();
            image_destroy(processed);
            if (result != ASCIICHAT_OK)
              break;
          }
        }
        if (processing_end == processing_start)
          processing_end = time_get_ns();
        if (frame && result == ASCIICHAT_OK)
          session_capture_complete_open(capture, session_capture_source_epoch(capture));
        uint64_t signaling_start = send_end;
        // The capture scheduler owns the wait; signaling must not add another
        // sleep to the media frame deadline.
        result = discovery_session_process(g_discovery, 0);
        uint64_t signaling_end = time_get_ns();
        if (result != ASCIICHAT_OK && result != ERROR_NETWORK_TIMEOUT)
          break;
        uint64_t event_start = time_get_ns();
        APP_CALLBACK_VOID(platform_pump_events);
        uint64_t event_end = time_get_ns();
        uint64_t elapsed = time_get_ns() - iteration_start;
        if (elapsed > 50 * NS_PER_MS_INT) {
          log_warn_every(US_PER_SEC_INT,
                         "Discovery media loop took %.1fms (capture=%.1fms process=%.1fms send=%.1fms signaling=%.1fms "
                         "events=%.1fms)",
                         (double)elapsed / 1e6, (double)(capture_end - capture_start) / 1e6,
                         (double)(processing_end - processing_start) / 1e6, (double)(send_end - send_start) / 1e6,
                         (double)(signaling_end - signaling_start) / 1e6, (double)(event_end - event_start) / 1e6);
        }
        session_capture_sleep_for_fps(capture);
      }
    media_cleanup:
      session_capture_set_audio_context(capture, NULL);
      /* These workers borrow stack audio/display state. Retain it and retry a
       * failed join before returning to the application/media cleanup. */
      while (session_connection_stop(&receiver.connection) != ASCIICHAT_OK) {
        LOG_ERRNO_IF_SET("Waiting for discovery media workers");
        platform_sleep_ms(10);
      }
      NAMED_UNREGISTER(&receiver.running);
      NAMED_UNREGISTER(&receiver.connection.state);
      if (!should_exit() && g_discovery->session_type == SESSION_TYPE_WEBRTC) {
        if (!g_discovery->handoff_pending) {
          g_discovery->state = DISCOVERY_STATE_MIGRATING;
          g_discovery->migration.detection_time_ms = time_get_ns() / NS_PER_MS_INT;
        }
        goto role_dispatch;
      }
      if (receiver.result != ASCIICHAT_OK)
        return receiver.result;
      if (result != ASCIICHAT_OK && result != ERROR_NETWORK_TIMEOUT)
        return result;
      return ASCIICHAT_OK;
    }
    return SET_ERRNO(ERROR_INVALID_STATE, "Active discovery participant has no connection");
  }

  return result;
}

/* ============================================================================
 * Main Discovery Mode Loop
 * ============================================================================ */

static asciichat_error_t discovery_prepare(void *user_data) {
  (void)user_data;
  return discovery_session_start(g_discovery);
}

/** Run discovery mode using the shared splash, media, and terminal lifecycle. */
int discovery_main(void) {

  // Discovery-specific setup: initialize ACDS connection and session negotiation
  // Note: Shared setup (keepawake, splash, terminal, capture, display, audio)
  // is handled by session_client_like_run()

  // Get session string from options (command-line argument)
  const char *session_string = GET_OPTION(session_string);
  bool is_initiator = (session_string == NULL || session_string[0] == '\0');

  int port_int = GET_OPTION(port);

  log_debug("Discovery: is_initiator=%d, port=%d", is_initiator, port_int);

  // Validate mutually exclusive options: discovery_service_url vs discovery_port
  const char *service_url = GET_OPTION(discovery_service_url);
  if (service_url && service_url[0] != '\0') {
    int discovery_port = GET_OPTION(discovery_port);
    if (discovery_port != ACIP_DISCOVERY_DEFAULT_PORT) {
      log_fatal("--discovery-service-url and --discovery-service-port are mutually exclusive");
      return ERROR_USAGE;
    }
  }

  discovery_ui_state_t ui = {0};
  asciichat_error_t ui_result = mutex_init(&ui.mutex, "discovery_ui");
  if (ui_result != ASCIICHAT_OK)
    return ui_result;

  // Create discovery session configuration
  discovery_config_t discovery_config = {
      .acds_address = GET_OPTION(discovery_server),
      .acds_port = (uint16_t)GET_OPTION(discovery_port),
      .acds_url = service_url,
      .session_string = is_initiator ? NULL : session_string,
      .password = GET_OPTION(password),
      .local_port = (uint16_t)port_int,
      .on_state_change = on_discovery_state_change,
      .on_session_ready = on_session_ready,
      .on_error = on_discovery_error,
      .callback_user_data = &ui,
      .should_exit_callback = discovery_capture_should_exit_adapter,
      .exit_callback_data = NULL,
  };

  acip_websocket_runtime_t *websocket_runtime = NULL;
  if (service_url && (strncmp(service_url, "ws://", 5) == 0 || strncmp(service_url, "wss://", 6) == 0)) {
    asciichat_error_t runtime_result = acip_websocket_runtime_create(&websocket_runtime);
    if (runtime_result != ASCIICHAT_OK) {
      mutex_destroy(&ui.mutex);
      return runtime_result;
    }
  }

  // Create discovery session (stored in global for discovery_run() to access)
  g_discovery = discovery_session_create(&discovery_config);
  if (!g_discovery) {
    mutex_destroy(&ui.mutex);
    acip_websocket_runtime_destroy(&websocket_runtime);
    log_fatal("Failed to create discovery session");
    return ERROR_MEMORY;
  }

  splash_set_discovery_session(session_string, !is_initiator);

  // No network interrupt callback needed - discovery session handles its own shutdown
  set_interrupt_callback(NULL);

  /* ========================================================================
   * Configure and Run Shared Client-Like Session Framework
   * ========================================================================
   * session_client_like_run() handles all shared initialization:
   * - Terminal output management
   * - Keepawake system
   * - Splash screen lifecycle
   * - Media source selection
   * - FPS probing
   * - Audio initialization
   * - Display context creation
   * - Proper cleanup ordering
   *
   * Discovery mode provides:
   * - discovery_run() callback: NAT negotiation, role determination, media handling
   * - discovery_keyboard_handler: interactive controls for participant role
   */

  session_client_like_config_t config = {
      .run_fn = discovery_run,
      .prepare_fn = discovery_prepare,
      .run_user_data = NULL,
      .kind = SESSION_CLIENT_LIKE_KIND_DISCOVERY,
      .discovery = (void *)g_discovery, // Opaque pointer to discovery session
      .custom_should_exit = NULL,
      .exit_user_data = NULL,
      .keyboard_handler = discovery_keyboard_handler,
      .max_reconnect_attempts = 0, // Discovery doesn't retry - role is determined once
      .should_reconnect_callback = NULL,
      .reconnect_user_data = NULL,
      .reconnect_delay_ms = 0,
      .print_newline_on_tty_exit = false, // Server/participant manages cursor
  };

  log_debug("Discovery: calling session_client_like_run()");
  asciichat_error_t session_result = session_client_like_run(&config);
  log_debug("Discovery: session_client_like_run() returned %d", session_result);

  // Cleanup discovery session
  log_debug("Discovery: cleaning up");

  if (g_discovery) {
    discovery_session_stop(g_discovery);
    while (discovery_session_destroy(g_discovery) != ASCIICHAT_OK) {
      LOG_ERRNO_IF_SET("Waiting for discovery connection resources");
      platform_sleep_ms(10);
    }
    g_discovery = NULL;
  }

  acip_websocket_runtime_destroy(&websocket_runtime);
  splash_clear_discovery_session();
  char discovery_error[sizeof(ui.error)];
  mutex_lock(&ui.mutex);
  SAFE_STRNCPY(discovery_error, ui.error, sizeof(discovery_error));
  asciichat_error_t discovery_error_code = ui.error_code;
  mutex_unlock(&ui.mutex);

  if (session_result != ASCIICHAT_OK || discovery_error[0]) {
    log_set_terminal_output(true);
    NOTICE(WARNING, "DISCOVERY FAILED", "Discovery failed: %s",
           discovery_error[0] ? discovery_error : asciichat_error_format(NULL, session_result));
    if (session_result == ASCIICHAT_OK)
      session_result = discovery_error_code;
  }
  mutex_destroy(&ui.mutex);
  return (session_result == ASCIICHAT_OK) ? 0 : (int)session_result;
}

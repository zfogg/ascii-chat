#include "connection_internal.h"

/* Client mode adapter: worker startup, display/snapshot effects and crypto/control
 * callbacks. Media parsing and participant state live in session/protocol. */
#include <ascii-chat/network/errors.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/ui/notice.h>

#include "protocol_adapter.h"
#include "main.h"
#include "../main.h" // Global exit API
#include "server.h"
#include "display.h"
#include "capture.h"
#include "audio.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/analysis.h>
#include "keepalive.h"
#include <ascii-chat/thread_pool.h>

#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/validation.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/util/fps.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/handshake/client.h>

#include "crypto.h"

#include <ascii-chat/atomic.h>
#include <ascii-chat/debug/named.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#ifdef _WIN32
#include <ascii-chat/platform/windows_compat.h>
#endif

#include <errno.h>

static void handle_ascii_frame_packet(client_connection_t *connection, const ascii_frame_packet_t *frame_header,
                                      const char *frame_data) {
  ascii_frame_packet_t header = *frame_header;
  if (should_exit())
    return;

  // FPS tracking for received ASCII frames using reusable tracker utility

  // Initialize FPS tracker on first frame
  if (!connection->frame_fps_tracker_initialized) {
    int fps = GET_OPTION(fps);
    int expected_fps = fps > 0 ? ((fps > 144) ? 144 : fps) : DEFAULT_MAX_FPS;
    fps_init(&connection->frame_fps_tracker, expected_fps, "ASCII_RX");
    connection->frame_fps_tracker_initialized = true;
  }

  // Track this frame and detect lag
  fps_frame_ns(&connection->frame_fps_tracker, time_get_ns(), "ASCII frame");

  // Track frame dimension changes

  if (header.width > 0 && header.height > 0) {
    if (header.width != connection->frame_last_width || header.height != connection->frame_last_height) {
      connection->frame_last_width = header.width;
      connection->frame_last_height = header.height;
    }
  }

  // Handle snapshot mode timing
  if (GET_OPTION(snapshot_mode)) {

    connection->frame_snapshot_frame_count++;
    log_debug("Snapshot frame %d received", connection->frame_snapshot_frame_count);

    if (!connection->frame_first_frame_recorded) {
      connection->frame_first_frame_time_ns = time_get_ns();
      atomic_store_u64(
          &connection->snapshot_first_frame_time_ns,
          connection->frame_first_frame_time_ns); // Also store globally for access from data_reception_thread
      log_info("[SNAPSHOT_INIT] SET g_snapshot_first_frame_time_ns=%llu first_frame_time_ns=%llu",
               (unsigned long long)atomic_load_u64(&connection->snapshot_first_frame_time_ns),
               (unsigned long long)connection->frame_first_frame_time_ns);
      connection->frame_first_frame_recorded = true;

      if (GET_OPTION(snapshot_delay) == 0) {
        log_debug("Snapshot captured immediately (delay=0)!");
        signal_exit();
      } else {
        log_debug("Snapshot mode: first frame received, waiting %.2f seconds for webcam warmup...",
                  GET_OPTION(snapshot_delay));
      }
    } else {
      uint64_t current_time_ns = time_get_ns();
      double elapsed = time_ns_to_s(time_elapsed_ns(connection->frame_first_frame_time_ns, current_time_ns));
      double snapshot_delay = GET_OPTION(snapshot_delay);

      // Log every 0.5 seconds to debug timing

      if (current_time_ns - connection->frame_last_debug_log_ns > 500 * 1000 * 1000) { // 500ms
        log_info("SNAPSHOT_DELAY_CHECK: elapsed=%.3f target=%.3f first_frame_ns=%llu current_ns=%llu", elapsed,
                 snapshot_delay, (unsigned long long)connection->frame_first_frame_time_ns,
                 (unsigned long long)current_time_ns);
        connection->frame_last_debug_log_ns = current_time_ns;
      }

      if (elapsed >= snapshot_delay) {
        char duration_str[32];
        time_pretty((uint64_t)(elapsed * 1e9), -1, duration_str, sizeof(duration_str));
        log_info("Snapshot captured after %s (target=%.3f)", duration_str, snapshot_delay);
        signal_exit();
      }
    }
  }

  // Check if we need to clear console before rendering this frame
  // IMPORTANT: We track if this is the first frame to ensure proper initialization

  if (!connection->frame_first_frame_rendered) {
    // Always clear display and disable logging before rendering the first frame
    // This ensures clean ASCII display regardless of packet arrival order
    log_debug("First frame - clearing display and disabling terminal logging");
    log_set_terminal_output(false);
    display_full_reset();
    connection->frame_first_frame_rendered = true;
    connection->protocol.server_state_initialized = true; // Mark as initialized
    connection->protocol.layout_changed = false;          // Clear any pending clear request
    log_debug("CLIENT_DISPLAY: Display cleared, ready for ASCII frames");
  } else if (connection->protocol.layout_changed) {
    // Subsequent clear request from server (e.g., after client list changes)
    log_debug("CLIENT_DISPLAY: Clearing display for layout change");
    log_set_terminal_output(false);
    display_full_reset();
    connection->protocol.layout_changed = false;
  }

  // Safety check before rendering
  if (!frame_data || header.original_size == 0) {
    log_error("Invalid frame data for rendering: frame_data=%p, size=%u", frame_data, header.original_size);
    return;
  }

  // The server paces frames using our negotiated desired_fps. Present received
  // frames without a second arrival-time limiter: network jitter can deliver a
  // correctly paced frame just before its nominal interval. The UI controller
  // coalesces pending frames if the terminal cannot keep up.

  // Frame counting now handled in session_display_write_ascii() which is called
  // by both client and host/discovery modes for consistent metrics

  // Render ASCII art frame (display_render_frame will apply effects like --matrix)
  log_debug("🎬 CALLING_DISPLAY_RENDER: frame_data=%p len=%zu, calling display_render_frame()", frame_data,
            strlen(frame_data));
  display_render_frame(connection, frame_data);
  log_debug("🎬 DISPLAY_RENDER_RETURNED: frame was rendered");
}

static bool handle_error_message_packet(client_connection_t *connection, const void *data, size_t len) {
  asciichat_error_t remote_error = ASCIICHAT_OK;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1] = {0};

  asciichat_error_t parse_result = packet_parse_error_message(data, len, &remote_error, message, sizeof(message), NULL);
  if (parse_result != ASCIICHAT_OK) {
    log_error("Failed to parse error packet from server: %s", asciichat_error_string(parse_result));
    return false;
  }

  log_error("Server reported error %d (%s): %s", remote_error, asciichat_error_string(remote_error), message);
  remote_error = network_error_public_code(remote_error);
  asciichat_errno_scope_t notification = asciichat_errno_scope_begin();
  SET_ERRNO(remote_error, "Server notification: %s", asciichat_error_string(remote_error));
  if (network_error_action(remote_error) == NETWORK_ERROR_CONTINUE) {
    log_warn("Server audio unavailable; continuing video");
    asciichat_errno_scope_end(notification, ASCIICHAT_ERRNO_HANDLED);
    return true;
  }
  server_connection_set_remote_error(connection, remote_error);
  if (network_error_action(remote_error) == NETWORK_ERROR_STOP) {
    asciichat_errno_request_exit(remote_error);
    signal_exit();
  }
  log_warn("Server rejected this connection; closing it");
  asciichat_errno_scope_end(notification, ASCIICHAT_ERRNO_HANDLED);
  server_connection_shutdown(connection);
  server_connection_lost(connection);
  return true;
}

static void handle_remote_log_packet(const void *data, size_t len) {
  log_level_t remote_level = LOG_INFO;
  remote_log_direction_t direction = REMOTE_LOG_DIRECTION_UNKNOWN;
  uint16_t flags = 0;
  char message[MAX_REMOTE_LOG_MESSAGE_LENGTH + 1] = {0};

  asciichat_error_t parse_result =
      packet_parse_remote_log(data, len, &remote_level, &direction, &flags, message, sizeof(message), NULL);
  if (parse_result != ASCIICHAT_OK) {
    log_error("Failed to parse remote log packet from server: %s", asciichat_error_string(parse_result));
    return;
  }

  if (direction != REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT) {
    log_error("Remote log packet direction mismatch (direction=%u)", direction);
    return;
  }

  bool truncated = (flags & REMOTE_LOG_FLAG_TRUNCATED) != 0;

  if (truncated) {
    log_msg(remote_level, __FILE__, __LINE__, __func__, "[REMOTE SERVER] %s [message truncated]", message);
  } else {
    log_msg(remote_level, __FILE__, __LINE__, __func__, "[REMOTE SERVER] %s", message);
  }
}

static asciichat_error_t client_present_frame(const ascii_frame_packet_t *header, const char *text, void *context) {
  handle_ascii_frame_packet(context, header, text);
  return ASCIICHAT_OK;
}

static asciichat_error_t client_play_samples(const float *samples, size_t count, void *context) {
  if (GET_OPTION(audio_enabled))
    audio_process_received_samples(context, samples, (int)count);
  return ASCIICHAT_OK;
}

static asciichat_error_t client_decode_opus(const void *data, size_t length, float *samples, size_t capacity,
                                            size_t *decoded, void *context) {
  *decoded = 0;
  if (!GET_OPTION(audio_enabled))
    return ASCIICHAT_OK;
  int count = audio_decode_opus(context, data, length, samples, (int)capacity);
  // The audio pipeline provides packet-loss concealment for corrupt frames.
  if (count <= 0)
    count = audio_decode_opus(context, NULL, 0, samples, (int)capacity);
  if (count <= 0)
    return SET_ERRNO(ERROR_AUDIO, "Failed to decode participant Opus audio");
  *decoded = (size_t)count;
  return ASCIICHAT_OK;
}

static void client_track_audio(size_t length, void *context) {
  (void)context;
  if (GET_OPTION(audio_enabled) && GET_OPTION(audio_analysis_enabled))
    audio_analysis_track_received_packet(length);
}

/* ============================================================================
 * ACIP Callback Forward Declarations and Structure
 * ============================================================================ */

// Forward declarations for ACIP callbacks (implemented after this section)
static void acip_on_error(const error_packet_t *header, const char *message, void *ctx);
static void acip_on_remote_log(const remote_log_packet_t *header, const char *message, void *ctx);
static void acip_on_ping(void *ctx);
static void acip_on_pong(void *ctx);
static void acip_on_clear_console(void *ctx);
static void acip_on_crypto_rekey_request(const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_rekey_response(const void *payload, size_t payload_len, void *ctx);
static void acip_on_webrtc_sdp(const acip_webrtc_sdp_t *sdp, size_t total_len, void *ctx);
static void acip_on_webrtc_ice(const acip_webrtc_ice_t *ice, size_t total_len, void *ctx);
static void acip_on_session_joined(const acip_session_joined_t *joined, void *ctx);
static void acip_on_crypto_key_exchange_init(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_auth_challenge(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_server_auth_resp(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_auth_failed(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_handshake_complete(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_parameters(packet_type_t type, const void *payload, size_t payload_len, void *ctx);

static const acip_client_callbacks_t g_acip_client_callbacks = {
    .on_error = acip_on_error,
    .on_remote_log = acip_on_remote_log,
    .on_ping = acip_on_ping,
    .on_pong = acip_on_pong,
    .on_clear_console = acip_on_clear_console,
    .on_crypto_rekey_request = acip_on_crypto_rekey_request,
    .on_crypto_rekey_response = acip_on_crypto_rekey_response,
    .on_webrtc_sdp = acip_on_webrtc_sdp,
    .on_webrtc_ice = acip_on_webrtc_ice,
    .on_session_joined = acip_on_session_joined,
    .on_crypto_key_exchange_init = acip_on_crypto_key_exchange_init,
    .on_crypto_auth_challenge = acip_on_crypto_auth_challenge,
    .on_crypto_server_auth_resp = acip_on_crypto_server_auth_resp,
    .on_crypto_auth_failed = acip_on_crypto_auth_failed,
    .on_crypto_handshake_complete = acip_on_crypto_handshake_complete,
    .on_crypto_parameters = acip_on_crypto_parameters,
    .app_ctx = NULL};

/* ============================================================================
 * Data Reception Thread
 * ============================================================================ */

static void *data_reception_thread_func(void *arg) {
  client_connection_t *connection = arg;

  log_warn("[FRAME_RECV_LOOP] 🔄 THREAD_STARTED: Data reception thread active, callbacks initialized");

#ifdef DEBUG_THREADS
  log_debug("[FRAME_RECV_LOOP] Thread lifecycle tracking enabled");
#endif

  int packet_count = 0;
  while (!should_exit() && server_connection_is_active(connection)) {
    // Main loop: receive and process packets while connection is active
    // When connection becomes inactive or shutdown is requested, thread exits cleanly

    // Debug: periodically check loop condition
    if (packet_count % 50 == 0) {
      bool exit_flag = should_exit();
      bool conn_active = server_connection_is_active(connection);
      if (packet_count > 0) {
        log_debug("[FRAME_RECV_LOOP] DEBUG: packet_count=%d, should_exit=%d, server_connection_is_active=%d",
                  packet_count, exit_flag, conn_active);
      }
    }

    // Receive and dispatch packet using ACIP transport API
    // This combines packet reception, decryption, parsing, handler dispatch, and cleanup
    acip_transport_t *transport = server_connection_get_transport(connection);
    if (packet_count < 3) {
    }
    if (!transport) {
      log_error("[FRAME_RECV_LOOP] ❌ NO_TRANSPORT: connection lost, transport not available");
      server_connection_lost(connection);
      break;
    }

    log_debug("[FRAME_RECV_LOOP] 📥 RECV_WAITING: awaiting packet #%d from server (transport ready)", packet_count + 1);

    if (packet_count == 0) {
    }
    // Use short 16ms timeout to allow rendering at 60 FPS
    // Instead of blocking indefinitely waiting for a packet,
    // we timeout quickly so main thread can continue rendering
    asciichat_errno_scope_t receive_scope = asciichat_errno_scope_begin();
    asciichat_error_t acip_result = session_protocol_receive(&connection->protocol);
    if (packet_count == 0) {
    }

    if (acip_result == ASCIICHAT_OK) {
      ASSERT_NO_ERRNO_SINCE(receive_scope);
      asciichat_errno_scope_end(receive_scope, ASCIICHAT_ERRNO_HANDLED);
      packet_count++;
      log_debug("[FRAME_RECV_LOOP] ✅ PACKET_%d_DISPATCHED: callbacks processed successfully", packet_count);
    } else if (acip_result == ERROR_NETWORK_TIMEOUT) {
      asciichat_errno_scope_end(receive_scope, ASCIICHAT_ERRNO_DISMISSED);
      // Network timeouts are expected - socket may have no data available
      // Continue looping to allow snapshot timer to expire
      log_debug("[FRAME_RECV_LOOP] ⏱️  TIMEOUT: No data available, retrying (packets received: %d)", packet_count);
      continue;
    } else if (acip_result == ERROR_NETWORK_INCOMPLETE &&
               HAS_ERRNO_CODE_SINCE(receive_scope, ERROR_NETWORK_INCOMPLETE)) {
      asciichat_errno_scope_end(receive_scope, ASCIICHAT_ERRNO_HANDLED);
      continue;
    } else {
      // Handle receive/dispatch errors - ALWAYS exit on network errors
      // NOTE: Log to file even if terminal logging is disabled (important for debugging)
      log_warn("[FRAME_RECV_LOOP] ⚠️  RECV_ERROR_BEFORE_SNAPSHOT_CHECK: acip_result=%d: %s (after %d packets received)",
               acip_result, asciichat_error_string(acip_result), packet_count);

      // Before breaking due to error, check if snapshot_delay timer has fired in snapshot mode
      bool should_break_now = true;
      if (GET_OPTION(snapshot_mode) && atomic_load_u64(&connection->snapshot_first_frame_time_ns) > 0) {
        uint64_t current_time_ns = time_get_ns();
        double elapsed =
            time_ns_to_s(time_elapsed_ns(atomic_load_u64(&connection->snapshot_first_frame_time_ns), current_time_ns));
        double snapshot_delay = GET_OPTION(snapshot_delay);

        if (elapsed >= snapshot_delay) {
          char duration_str[32];
          time_pretty((uint64_t)(elapsed * 1e9), -1, duration_str, sizeof(duration_str));
          log_info("🎬 SNAPSHOT TIMEOUT FIRED (ON_ERROR): Snapshot captured after %s! (target=%.3f)", duration_str,
                   snapshot_delay);
          signal_exit();
          should_break_now = false; // Don't disconnect, let normal shutdown handle it
        }
      }

      if (should_break_now) {
        // Network errors (ERROR_NETWORK, ERROR_NETWORK_PROTOCOL, etc) always disconnect
        if (acip_result == ERROR_NETWORK || acip_result == ERROR_NETWORK_PROTOCOL) {
          log_warn("[FRAME_RECV_LOOP] ⚠️  NETWORK_ERROR: Server disconnected after %d packets, exiting loop",
                   packet_count);
          server_connection_lost(connection);
          break;
        }

        // Check errno context for additional error details
        if (HAS_ERRNO_CODE_SINCE(receive_scope, ERROR_CRYPTO)) {
          NOTICE(DANGER, "ENCRYPTION POLICY VIOLATION", "Server violated the encryption policy; closing connection.");
          LOG_ERRNO_IF_SET("Server violated the encryption policy");
          asciichat_errno_request_exit(ERROR_CRYPTO);
          signal_exit();
          break;
        }

        // Other errors - still disconnect to prevent infinite loop
        log_error("[FRAME_RECV_LOOP] ❌ RECV_FAILED: packet #%d failed after %d packets, disconnecting",
                  packet_count + 1, packet_count);
        server_connection_lost(connection);
        break;
      }
    }
  }

  // Log why the loop exited (DEBUG to avoid affecting timing)
  bool final_exit_flag = should_exit();
  bool final_conn_active = server_connection_is_active(connection);
  log_debug("[FRAME_RECV_LOOP] ⏹️  LOOP_EXIT: should_exit=%d server_connection_is_active=%d (packets_received=%d)",
            final_exit_flag, final_conn_active, packet_count);

  // Before exiting due to connection loss, check if snapshot_delay timer has fired
  // If it has, take the snapshot and exit cleanly instead of abandoning the snapshot
  if (GET_OPTION(snapshot_mode)) {
    log_debug("[FRAME_RECV_LOOP] ⏰ SNAPSHOT_CHECK_ON_EXIT: snapshot_mode enabled, g_snapshot_first_frame_time_ns=%llu",
              (unsigned long long)atomic_load_u64(&connection->snapshot_first_frame_time_ns));

    if (atomic_load_u64(&connection->snapshot_first_frame_time_ns) > 0) {
      uint64_t current_time_ns = time_get_ns();
      double elapsed =
          time_ns_to_s(time_elapsed_ns(atomic_load_u64(&connection->snapshot_first_frame_time_ns), current_time_ns));
      double snapshot_delay = GET_OPTION(snapshot_delay);

      log_debug("[FRAME_RECV_LOOP] ⏰ SNAPSHOT_CHECK_DETAILS: elapsed=%.3f target=%.3f", elapsed, snapshot_delay);

      if (elapsed >= snapshot_delay) {
        char duration_str[32];
        time_pretty((uint64_t)(elapsed * 1e9), -1, duration_str, sizeof(duration_str));
        log_info("🎬 SNAPSHOT TIMEOUT FIRED (ON_CONNECTION_LOSS): Snapshot captured after %s! (target=%.3f)",
                 duration_str, snapshot_delay);
        signal_exit();
      }
    }
  }

  log_warn("[FRAME_RECV_LOOP] 🔴 THREAD_EXITING: received %d packets total, connection inactive or shutdown requested",
           packet_count);

#ifdef DEBUG_THREADS
  log_debug("[FRAME_RECV_LOOP] Thread lifecycle tracking - exit");
#endif

  atomic_store_bool(&connection->data_thread_exited, true);

  // The platform thread wrapper unregisters pending errors on return.
  log_debug("[FRAME_RECV_LOOP] Thread terminating");
  return NULL;
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

asciichat_error_t client_protocol_start(client_connection_t *connection) {
  log_warn("[FRAME_RECV_INIT] 🟢 PROTOCOL_START: Starting client protocol initialization");

  // The server can send Opus audio as soon as streaming starts. Initialize the
  // decoder before either transport's receive worker can dispatch those packets.
  asciichat_error_t audio_result = audio_connection_init(connection);
  if (audio_result != ASCIICHAT_OK)
    return audio_result;
  if (client_connection_checkpoint("audio-pipeline") != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");

  acip_client_callbacks_t control = g_acip_client_callbacks;
  control.app_ctx = connection;
  session_protocol_init(&connection->protocol, server_connection_get_transport(connection),
                        (session_protocol_media_t){.on_frame = client_present_frame,
                                                   .on_samples = client_play_samples,
                                                   .decode_opus = client_decode_opus,
                                                   .on_audio_packet = client_track_audio,
                                                   .context = connection},
                        control);

  // Reset display state for new connection
  display_reset_for_new_connection(connection);

  // Check if we're on a WebSocket transport (crypto handshake is async via ACIP callbacks)
  // For WS/WSS, we must start data_reception FIRST so it can handle the crypto handshake,
  // then wait for crypto to complete before sending CAPABILITIES/STREAM_START.
  // For TCP, crypto is already done synchronously before this function is called.
  acip_transport_t *init_transport = server_connection_get_transport(connection);
  bool is_websocket = init_transport && acip_transport_get_type(init_transport) == ACIP_TRANSPORT_WEBSOCKET;

  if (is_websocket) {
    // WebSocket: start data reception thread FIRST to handle async crypto handshake.
    // TCP does crypto synchronously in connection_attempt.c, but WS crypto flows
    // asynchronously via ACIP callbacks in the data_reception thread.
    log_debug("[FRAME_RECV_INIT] WS: Starting data_reception for async crypto handshake");
    atomic_store_bool(&connection->data_thread_exited, false);
    if (thread_pool_spawn(connection->client_worker_pool, data_reception_thread_func, connection, 1,
                          "data_reception") != ASCIICHAT_OK) {
      log_error("[FRAME_RECV_INIT] Failed to start data reception thread");
      LOG_ERRNO_IF_SET("Data reception thread creation failed");
      return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
    }
    if (client_connection_checkpoint("receive") != ASCIICHAT_OK)
      return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
    log_debug("[FRAME_RECV_INIT] WS: Data thread spawned, waiting for crypto handshake");
  }

  // Send CLIENT_CAPABILITIES packet (after crypto is ready for both TCP and WS)
  // Server expects this as the first packet after crypto handshake
  log_debug("[FRAME_RECV_INIT] Sending CLIENT_CAPABILITIES (terminal size)");
  asciichat_error_t cap_result = threaded_send_terminal_size_with_auto_detect(
      connection, (int)terminal_get_effective_width(), (int)terminal_get_effective_height());
  if (cap_result != ASCIICHAT_OK) {
    log_error("[FRAME_RECV_INIT] Failed to send terminal size capabilities");
    return cap_result;
  }

  // Send STREAM_START packet with combined stream types
  uint32_t stream_types = STREAM_TYPE_VIDEO; // Always have video
  if (GET_OPTION(audio_enabled)) {
    stream_types |= STREAM_TYPE_AUDIO; // Add audio if enabled
  }
  log_debug("[FRAME_RECV_INIT] Sending STREAM_START: types=0x%x (video%s)", stream_types,
            (stream_types & STREAM_TYPE_AUDIO) ? "+audio" : "");
  asciichat_error_t stream_result = threaded_send_stream_start_packet(connection, stream_types);
  if (stream_result != ASCIICHAT_OK) {
    log_error("[FRAME_RECV_INIT] Failed to send stream start");
    return stream_result;
  }

  // Start data reception thread (TCP only - WS already started it above for crypto)
  if (!is_websocket) {
    log_debug("[FRAME_RECV_INIT] TCP: Starting data reception thread");
    atomic_store_bool(&connection->data_thread_exited, false);
    if (thread_pool_spawn(connection->client_worker_pool, data_reception_thread_func, connection, 1,
                          "data_reception") != ASCIICHAT_OK) {
      log_error("[FRAME_RECV_INIT] Failed to start data reception thread");
      LOG_ERRNO_IF_SET("Data reception thread creation failed");
      return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
    }
  }

  if (client_connection_checkpoint("receive") != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");

  // Start webcam capture thread
  log_debug("Starting webcam capture thread...");
  if (capture_start_thread(connection) != 0) {
    log_error("Failed to start webcam capture thread");
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
  }
  log_debug("Webcam capture thread started successfully");
  if (client_connection_checkpoint("capture") != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");

  // Start local media audio only after the connection succeeds. Starting the
  // worker during client initialization would consume file audio while dialing.
  audio_context_t *audio_context = connection->audio_context;
  if (audio_context && audio_context->initialized) {
    audio_context->capture_media_source = session_capture_get_media_source(connection->capture_context);
    if (audio_start_duplex(audio_context) != ASCIICHAT_OK) {
      log_error("Failed to start full-duplex audio");
      return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
    }
  }

  if (client_connection_checkpoint("audio-duplex") != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");

  // Initialize audio sender thread BEFORE starting audio capture
  // This ensures sender is ready when capture thread starts queueing packets
  // Must happen after connection succeeds to prevent deadlock if connection fails
  if (audio_sender_init(connection) != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");

  // Start audio capture thread if audio is enabled
  log_debug("Starting audio capture thread...");
  if (audio_start_thread(connection) != 0) {
    log_error("Failed to start audio capture thread");
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
  }
  log_debug("Audio capture thread started successfully (or skipped if audio disabled)");

  if (client_connection_checkpoint("audio-capture") != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");

  // Start keepalive/ping thread to prevent server timeout
  log_debug("Starting keepalive/ping thread...");
  if (keepalive_start_thread(connection) != 0) {
    log_error("Failed to start keepalive/ping thread");
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
  }
  log_debug("Keepalive/ping thread started successfully");

  if (client_connection_checkpoint("keepalive") != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
  return ASCIICHAT_OK;
}

bool client_protocol_connection_lost(client_connection_t *connection) {
  return atomic_load_bool(&connection->data_thread_exited) || server_connection_is_lost(connection);
}

/* ============================================================================
 * ACIP Callback Implementations
 * ============================================================================ */

static void acip_on_error(const error_packet_t *header, const char *message, void *ctx) {
  client_connection_t *connection = ctx;
  (void)ctx;

  // Reconstruct packet for existing handler
  size_t msg_len = message ? strlen(message) : 0;
  size_t total_len = sizeof(*header) + msg_len;

  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    log_error("Failed to allocate buffer for error packet callback");
    return;
  }

  memcpy(packet, header, sizeof(*header));
  if (msg_len > 0) {
    // NOLINTNEXTLINE(bugprone-not-null-terminated-result) - binary packet data
    memcpy(packet + sizeof(*header), message, msg_len);
  }

  handle_error_message_packet(connection, packet, total_len);
  SAFE_FREE(packet);
}

static void acip_on_ping(void *ctx) {
  client_connection_t *connection = ctx;
  (void)ctx;

  // Respond with PONG
  if (threaded_send_pong_packet(connection) != ASCIICHAT_OK) {
    log_error("Failed to send PONG response");
  }
}

static void acip_on_remote_log(const remote_log_packet_t *header, const char *message, void *ctx) {
  (void)ctx;

  // Reconstruct packet for existing handler
  size_t msg_len = strlen(message);
  size_t total_len = sizeof(*header) + msg_len;

  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    log_error("Failed to allocate buffer for remote log callback");
    return;
  }

  memcpy(packet, header, sizeof(*header));
  // NOLINTNEXTLINE(bugprone-not-null-terminated-result) - binary packet data, not string
  memcpy(packet + sizeof(*header), message, msg_len);

  handle_remote_log_packet(packet, total_len);
  SAFE_FREE(packet);
}

static void acip_on_pong(void *ctx) {
  (void)ctx;
  // Pong received - no action needed (server acknowledged our ping)
}

static void acip_on_clear_console(void *ctx) {
  (void)ctx;

  // Server requested console clear
  display_full_reset();
  log_debug("Console cleared by server");
}

static void acip_on_crypto_rekey_request(const void *payload, size_t payload_len, void *ctx) {
  client_connection_t *connection = ctx;
  (void)ctx;

  // Process the server's rekey request
  asciichat_error_t crypto_result = crypto_client_process_rekey_request(connection, payload, payload_len);
  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_REQUEST: %d", crypto_result);
    return;
  }

  // Send REKEY_RESPONSE
  crypto_result = crypto_client_send_rekey_response(connection);
  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_RESPONSE: %d", crypto_result);
  }
}

static void acip_on_crypto_rekey_response(const void *payload, size_t payload_len, void *ctx) {
  client_connection_t *connection = ctx;
  (void)ctx;

  // Process server's response
  asciichat_error_t crypto_result = crypto_client_process_rekey_response(connection, payload, payload_len);
  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_RESPONSE: %d", crypto_result);
    return;
  }

  // Send REKEY_COMPLETE
  crypto_result = crypto_client_send_rekey_complete(connection);
  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_COMPLETE: %d", crypto_result);
  }
}

static void acip_on_webrtc_sdp(const acip_webrtc_sdp_t *sdp, size_t total_len, void *ctx) {
  (void)ctx;
  (void)total_len; // Peer manager reads variable data via pointer arithmetic

  // Check if WebRTC is initialized
  if (!g_peer_manager) {
    log_warn("Received WebRTC SDP but peer manager not initialized - ignoring");
    return;
  }

  // Log SDP type for debugging
  const char *sdp_type_str = (sdp->sdp_type == 0) ? "offer" : "answer";
  log_debug("Received WebRTC SDP %s from participant (session_id=%.8s...)", sdp_type_str,
            (const char *)sdp->session_id);

  // Handle SDP through peer manager (extracts variable data internally)
  asciichat_error_t result = webrtc_peer_manager_handle_sdp(g_peer_manager, sdp);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to handle WebRTC SDP: %s", asciichat_error_string(result));
  }
}

static void acip_on_webrtc_ice(const acip_webrtc_ice_t *ice, size_t total_len, void *ctx) {
  (void)ctx;
  (void)total_len; // Peer manager reads variable data via pointer arithmetic

  // Check if WebRTC is initialized
  if (!g_peer_manager) {
    log_warn("Received WebRTC ICE but peer manager not initialized - ignoring");
    return;
  }

  log_debug("Received WebRTC ICE candidate from participant (session_id=%.8s...)", (const char *)ice->session_id);

  // Handle ICE through peer manager (extracts variable data internally)
  asciichat_error_t result = webrtc_peer_manager_handle_ice(g_peer_manager, ice);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to handle WebRTC ICE: %s", asciichat_error_string(result));
  }
}

static void acip_on_session_joined(const acip_session_joined_t *joined, void *ctx) {
  (void)ctx; // Unused for now, may be used in future for context passing

  if (!joined) {
    log_error("SESSION_JOINED callback received NULL response");
    return;
  }

  // Check if join was successful
  if (!joined->success) {
    log_error("ACDS session join failed: error %d: %s", joined->error_code, joined->error_message);
    // Connection will timeout waiting for SDP/WebRTC completion and fallback to next stage
    return;
  }

  // Join succeeded - we have session context now
  log_debug("ACDS session join succeeded (participant_id=%.8s..., session_type=%s, server=%s:%u)",
            (const char *)joined->participant_id, joined->session_type == 1 ? "WebRTC" : "DirectTCP",
            joined->server_address, joined->server_port);

  // Check if this is a WebRTC session
  if (joined->session_type == SESSION_TYPE_WEBRTC) {
    // TODO: Phase 3 - Initialize WebRTC connection with TURN credentials
    // webrtc_initialize_session(joined->session_id, joined->participant_id,
    //                           joined->turn_username, joined->turn_password);
    log_debug("WebRTC session detected - TODO: initialize WebRTC with TURN credentials");
  } else {
    // Direct TCP - connection is already established or will be established
    log_debug("Direct TCP session - using existing connection");
  }
}

static void acip_on_crypto_key_exchange_init(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  client_connection_t *connection = ctx;

  log_debug("Received CRYPTO_KEY_EXCHANGE_INIT from server");

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot handle key exchange - no transport available");
    return;
  }

  asciichat_error_t result = crypto_handshake_client_key_exchange(&connection->crypto_ctx, transport, type,
                                                                  (const uint8_t *)payload, payload_len);
  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake key exchange failed");
    server_connection_lost(connection);
  } else {
    log_debug("Sent CRYPTO_KEY_EXCHANGE_RESP to server");
  }
}

static void acip_on_crypto_auth_challenge(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  client_connection_t *connection = ctx;

  log_debug("Received CRYPTO_AUTH_CHALLENGE from server");

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot handle auth challenge - no transport available");
    return;
  }

  asciichat_error_t result = crypto_handshake_client_auth_response(&connection->crypto_ctx, transport, type,
                                                                   (const uint8_t *)payload, payload_len);
  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake auth response failed");
    server_connection_lost(connection);
  } else {
    log_debug("Sent CRYPTO_AUTH_RESPONSE to server");
  }
}

static void acip_on_crypto_server_auth_resp(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  client_connection_t *connection = ctx;

  log_debug("Received CRYPTO_SERVER_AUTH_RESP from server");

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot handle server auth response - no transport available");
    return;
  }

  asciichat_error_t result =
      crypto_handshake_client_complete(&connection->crypto_ctx, transport, type, (const uint8_t *)payload, payload_len);
  if (result != ASCIICHAT_OK) {
    NOTICE(DANGER, "HANDSHAKE VERIFICATION FAILED", "Crypto handshake verification failed");
    server_connection_lost(connection);
  } else {
    log_info("Crypto handshake completed successfully (mutual auth)");

    // Link crypto context to transport for automatic encryption
    transport->crypto_ctx = &connection->crypto_ctx.crypto_ctx;
  }
}

static void acip_on_crypto_auth_failed(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  client_connection_t *connection = ctx;
  (void)ctx;
  (void)type;

  // Extract error message if present
  char error_msg[256] = "Unknown error";
  if (payload && payload_len > 0) {
    size_t msg_len = payload_len < sizeof(error_msg) - 1 ? payload_len : sizeof(error_msg) - 1;
    memcpy(error_msg, payload, msg_len);
    error_msg[msg_len] = '\0';
  }

  NOTICE(DANGER, "AUTHENTICATION REJECTED", "Server rejected authentication: %s", error_msg);
  log_error("Disconnecting - crypto handshake failed");

  server_connection_lost(connection);
}

static void acip_on_crypto_handshake_complete(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  client_connection_t *connection = ctx;

  log_debug("Received CRYPTO_HANDSHAKE_COMPLETE from server");

  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport) {
    log_error("Cannot complete handshake - no transport available");
    return;
  }

  asciichat_error_t result =
      crypto_handshake_client_complete(&connection->crypto_ctx, transport, type, (const uint8_t *)payload, payload_len);
  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake completion failed");
    server_connection_lost(connection);
  } else {
    log_info("Crypto handshake completed successfully");

    // Link crypto context to transport for automatic encryption
    transport->crypto_ctx = &connection->crypto_ctx.crypto_ctx;
  }
}

static void acip_on_crypto_parameters(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  client_connection_t *connection = ctx;
  (void)type;

  log_debug("Received CRYPTO_PARAMETERS from server (payload_len=%zu)", payload_len);

  if (payload_len != sizeof(crypto_parameters_packet_t)) {
    log_error("Invalid CRYPTO_PARAMETERS size: %zu (expected %zu)", payload_len, sizeof(crypto_parameters_packet_t));
    server_connection_lost(connection);
    return;
  }

  crypto_parameters_packet_t server_params;
  memcpy(&server_params, payload, sizeof(crypto_parameters_packet_t));

  // Log parameters (fields are still in network byte order for set_parameters)
  uint16_t kex_pubkey_size = NET_TO_HOST_U16(server_params.kex_public_key_size);
  uint16_t auth_pubkey_size = NET_TO_HOST_U16(server_params.auth_public_key_size);
  uint16_t signature_size = NET_TO_HOST_U16(server_params.signature_size);
  uint16_t shared_secret_size = NET_TO_HOST_U16(server_params.shared_secret_size);

  log_debug("Server crypto parameters: KEX=%u, Auth=%u, Cipher=%u (key_size=%u, auth_size=%u, sig_size=%u, "
            "secret_size=%u, verification=%u)",
            server_params.selected_kex, server_params.selected_auth, server_params.selected_cipher, kex_pubkey_size,
            auth_pubkey_size, signature_size, shared_secret_size, server_params.verification_enabled);

  // Set the crypto parameters in the handshake context so KEY_EXCHANGE_INIT knows sizes
  asciichat_error_t result = crypto_handshake_set_parameters(&connection->crypto_ctx, &server_params);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to set crypto parameters: %d", result);
    server_connection_lost(connection);
    return;
  }

  // Store verification flag
  if (server_params.verification_enabled) {
    connection->crypto_ctx.server_uses_client_auth = true;
    connection->crypto_ctx.require_client_auth = true;
    log_info("Server will verify client identity (whitelist enabled)");
  }

  log_info("CRYPTO_PARAMETERS processed - ready for KEY_EXCHANGE_INIT");
}

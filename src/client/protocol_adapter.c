#include "connection_internal.h"

/* Client mode adapter: worker startup, display/snapshot effects and crypto/control
 * callbacks. Media parsing and participant state live in session/protocol. */
#include <ascii-chat/network/errors.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/tui/notice.h>

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
#include <ascii-chat/network/acip/send.h>
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
        log_debug("SNAPSHOT_DELAY_CHECK: elapsed=%.3f target=%.3f first_frame_ns=%llu current_ns=%llu", elapsed,
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
  log_debug_every(US_PER_SEC_INT, "CALLING_DISPLAY_RENDER: frame_data=%p len=%zu, calling display_render_frame()",
                  frame_data, strlen(frame_data));
  display_render_frame(connection, frame_data);
  log_debug_every(US_PER_SEC_INT, "DISPLAY_RENDER_RETURNED: frame was rendered");
}

static bool handle_error_message_packet(client_connection_t *connection, const void *data, size_t len) {
  asciichat_error_t remote_error = ASCIICHAT_OK;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1] = {0};

  asciichat_error_t parse_result = packet_parse_error_message(data, len, &remote_error, message, sizeof(message), NULL);
  if (parse_result != ASCIICHAT_OK) {
    log_error("Failed to parse error packet from server: %s", asciichat_error_format(NULL, parse_result));
    return false;
  }

  log_error("Server reported %s", asciichat_error_format(message, remote_error));
  remote_error = network_error_public_code(remote_error);
  asciichat_errno_scope_t notification = errno_scope_begin();
  SET_ERRNO(remote_error, "Server notification: %s", asciichat_error_string(remote_error));
  if (network_error_action(remote_error) == NETWORK_ERROR_CONTINUE) {
    log_warn("Server audio unavailable; continuing video");
    errno_scope_end(notification, ASCIICHAT_ERRNO_HANDLED);
    return true;
  }
  server_connection_set_remote_error(connection, remote_error);
  if (network_error_action(remote_error) == NETWORK_ERROR_STOP) {
    errno_request_exit(remote_error);
    signal_exit();
  }
  log_warn("Server rejected this connection; closing it");
  errno_scope_end(notification, ASCIICHAT_ERRNO_HANDLED);
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
    log_error("Failed to parse remote log packet from server: %s", asciichat_error_format(NULL, parse_result));
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

static const acip_client_callbacks_t g_acip_client_callbacks = {.on_error = acip_on_error,
                                                                .on_remote_log = acip_on_remote_log,
                                                                .on_ping = acip_on_ping,
                                                                .on_pong = acip_on_pong,
                                                                .on_clear_console = acip_on_clear_console,
                                                                .on_crypto_rekey_request = acip_on_crypto_rekey_request,
                                                                .on_crypto_rekey_response =
                                                                    acip_on_crypto_rekey_response,
                                                                .on_webrtc_sdp = acip_on_webrtc_sdp,
                                                                .on_webrtc_ice = acip_on_webrtc_ice,
                                                                .on_session_joined = acip_on_session_joined,
                                                                .app_ctx = NULL};

/* ============================================================================
 * Data Reception Thread
 * ============================================================================ */

static asciichat_error_t client_connection_packet(network_connection_t *network, packet_type_t type,
                                                  const void *payload, size_t length, void *arg) {
  (void)network;
  client_connection_t *connection = arg;
  return session_protocol_dispatch(&connection->protocol, type, payload, length);
}
static void *data_reception_thread_func(void *arg) {
  client_connection_t *connection = arg;
  network_connection_callbacks_t callbacks = {.packet = client_connection_packet};
  asciichat_error_t result = network_connection_run(connection->network, &callbacks, connection);
  uint64_t first_frame = atomic_load_u64(&connection->snapshot_first_frame_time_ns);
  if (GET_OPTION(snapshot_mode) && first_frame &&
      time_ns_to_s(time_elapsed_ns(first_frame, time_get_ns())) >= GET_OPTION(snapshot_delay)) {
    signal_exit();
  } else if (!should_exit()) {
    if (result == ERROR_CRYPTO) {
      NOTICE(DANGER, "ENCRYPTION POLICY VIOLATION", "Server violated the encryption policy; closing connection.");
      errno_request_exit(ERROR_CRYPTO);
      signal_exit();
    }
    server_connection_lost(connection);
  }
  atomic_store_bool(&connection->data_thread_exited, true);
  return NULL;
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

// Before the receive worker starts, an admission rejection can race STREAM_START.
// Preserve the peer's public error instead of reporting only the failed write.
static asciichat_error_t client_startup_peer_error(client_connection_t *connection, asciichat_error_t send_error) {
  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport || (send_error != ERROR_NETWORK && send_error != ERROR_NETWORK_CONNECT))
    return send_error;
  uint64_t previous_timeout = transport->receive_timeout_ns;
  uint64_t deadline = time_get_ns() + 500 * NS_PER_MS_INT;
  asciichat_error_t result = send_error;
  while (time_get_ns() < deadline && !should_exit()) {
    transport->receive_timeout_ns = 100 * NS_PER_MS_INT;
    packet_type_t type = 0;
    void *payload = NULL, *allocation = NULL;
    size_t length = 0;
    bool notifications = errno_notifications_suppress(true);
    asciichat_errno_scope_t scope = errno_scope_begin();
    asciichat_error_t received = packet_receive_via_transport(transport, &type, &payload, &length, &allocation);
    errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
    errno_notifications_suppress(notifications);
    if (received == ASCIICHAT_OK && type == PACKET_TYPE_ERROR_MESSAGE)
      result = network_error_decode(type, payload, length);
    buffer_release(allocation);
    if (result != send_error ||
        (received != ASCIICHAT_OK && received != ERROR_NETWORK_TIMEOUT && received != ERROR_NETWORK_INCOMPLETE))
      break;
  }
  transport->receive_timeout_ns = previous_timeout;
  return result;
}

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
                        (session_protocol_media_t){.terminal_output = true,
                                                   .on_frame = client_present_frame,
                                                   .on_samples = client_play_samples,
                                                   .decode_opus = client_decode_opus,
                                                   .on_audio_packet = client_track_audio,
                                                   .context = connection,
                                                   .frame_workspace = &connection->decoded_ascii_workspace},
                        control);

  // Reset display state for new connection
  display_reset_for_new_connection(connection);

  // Send CLIENT_CAPABILITIES packet (after crypto is ready for both TCP and WS)
  // Server expects this as the first packet after crypto handshake
  log_debug("[FRAME_RECV_INIT] Sending CLIENT_CAPABILITIES (terminal size)");
  asciichat_error_t cap_result = threaded_send_terminal_size_with_auto_detect(
      connection, (int)terminal_get_effective_width(), (int)terminal_get_effective_height());
  if (cap_result != ASCIICHAT_OK) {
    log_error("[FRAME_RECV_INIT] Failed to send terminal size capabilities");
    return client_startup_peer_error(connection, cap_result);
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
    return client_startup_peer_error(connection, stream_result);
  }

  // Every transport has completed security negotiation before protocol startup.
  atomic_store_bool(&connection->data_thread_exited, false);
  if (thread_pool_spawn(connection->client_worker_pool, data_reception_thread_func, connection, 1, "data_reception") !=
      ASCIICHAT_OK) {
    log_error("[FRAME_RECV_INIT] Failed to start data reception thread");
    LOG_ERRNO_IF_SET("Data reception thread creation failed");
    return SET_ERRNO(ERROR_THREAD, "Could not start client protocol workers");
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
    session_capture_set_audio_context(connection->capture_context, audio_context);
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
  server_connection_pong_received(ctx);
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
    log_error("Failed to handle WebRTC SDP: %s", asciichat_error_format(NULL, result));
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
    log_error("Failed to handle WebRTC ICE: %s", asciichat_error_format(NULL, result));
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

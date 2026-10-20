#include "connection_internal.h"
#include <ascii-chat/network/errors.h>
/**
 * @file client/capture.c
 * @ingroup client_capture
 * @brief 📹 Client webcam capture: dedicated capture thread with frame rate limiting and network transmission
 *
 * The capture system follows a producer-consumer pattern:
 * - **Producer**: Webcam capture thread reads frames from camera
 * - **Processing**: Frame resizing and format conversion pipeline
 * - **Consumer**: Network transmission thread sends processed frames
 * - **Rate Limiting**: Frame rate control to prevent bandwidth overload
 *
 * ## Threading Model
 *
 * Capture operations run in a dedicated thread:
 * - **Main Thread**: Manages thread lifecycle and coordination
 * - **Capture Thread**: Continuous frame capture and processing loop
 * - **Synchronization**: Atomic flags coordinate thread shutdown
 * - **Resource Management**: Clean webcam and memory resource cleanup
 *
 * ## Frame Processing Pipeline
 *
 * Raw webcam frames undergo comprehensive processing:
 * 1. **Capture**: Read raw frame from webcam device
 * 2. **Validation**: Check frame validity and dimensions
 * 3. **Aspect Ratio**: Calculate optimal resize dimensions
 * 4. **Resizing**: Scale frame to network-optimal size (800x600 max)
 * 5. **Serialization**: Pack frame data into network packet format
 * 6. **Transmission**: Send via IMAGE_FRAME packet to server
 *
 * ## Frame Rate Management
 *
 * Implements intelligent frame rate limiting:
 * - **Capture Rate**: 144 FPS to support high-refresh displays (~6.9ms intervals)
 * - **Timing Control**: Monotonic clock for accurate frame intervals
 * - **Adaptive Delays**: Dynamic sleep adjustment for consistent timing
 * - **Display Support**: High capture rate enables smooth playback on promotion displays
 *
 * ## Image Resizing Strategy
 *
 * Optimizes frame size for network efficiency:
 * - **Maximum Dimensions**: 800x600 pixels for reasonable bandwidth
 * - **Aspect Ratio Preservation**: Maintain original camera aspect ratio
 * - **Intelligent Scaling**: Fit-to-bounds algorithm for optimal sizing
 * - **Quality Balance**: Large enough for server flexibility, small enough for efficiency
 *
 * ## Packet Format and Transmission
 *
 * Frame data serialized for network transmission:
 * - **Header**: Width and height as 32-bit network byte order integers
 * - **Payload**: RGB pixel data as continuous byte array
 * - **Size Limits**: Enforce maximum packet size for protocol compliance
 * - **Error Handling**: Graceful handling of oversized frames
 *
 * ## Platform Compatibility
 *
 * Uses webcam abstraction layer for cross-platform support:
 * - **Linux**: Video4Linux2 (V4L2) webcam interface
 * - **macOS**: AVFoundation framework integration
 * - **Windows**: DirectShow API wrapper (stub implementation)
 * - **Fallback**: Test pattern generation when webcam unavailable
 *
 * ## Integration Points
 *
 * - **main.c**: Capture subsystem lifecycle management
 * - **server.c**: Network packet transmission and connection monitoring
 * - **webcam.c**: Low-level webcam device interface and frame reading
 * - **image.c**: Image processing and memory management functions
 *
 * ## Error Handling
 *
 * Capture errors handled with appropriate recovery:
 * - **Device Errors**: Webcam unavailable or device busy (continue with warnings)
 * - **Memory Errors**: Frame allocation failures (skip frame, continue)
 * - **Network Errors**: Transmission failures (trigger connection loss detection)
 * - **Processing Errors**: Invalid frames or resize failures (skip and retry)
 *
 * ## Resource Management
 *
 * Careful resource lifecycle management:
 * - **Webcam Device**: Proper initialization and cleanup
 * - **Frame Buffers**: Automatic memory management with leak prevention
 * - **Thread Resources**: Clean thread termination and resource release
 * - **Network Buffers**: Efficient packet buffer allocation and cleanup
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */
#include "session/capture.h"
#include "main.h"
#include "../main.h" // Global exit API
#include "server.h"
#include "audio.h"
#include "session/capture.h"
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/util/fps.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/thread.h> // For THREAD_IS_CREATED macro
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/debug/named.h>
#include <time.h>
#include <string.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/thread_pool.h>

/* ============================================================================
 * Session Capture Context
 * ============================================================================ */

/**
 * @brief Session capture context (webcam, file, or stdin)
 *
 * Unified capture context using the session library that abstracts over
 * webcam, media files, stdin, and test pattern sources.
 * Created during initialization, destroyed during cleanup.
 *
 * @ingroup client_capture
 */
static session_capture_ctx_t *g_capture_capture_ctx = NULL;

/* ============================================================================
 * Capture Thread Management
 * ============================================================================ */

/* ============================================================================
 * Frame Processing Constants
 * ============================================================================ */
/** Fallback capture FPS when no target was configured */
#define CAPTURE_TARGET_FPS 144

/* Frame processing now handled by session library via session_capture_process_for_transmission() */
/* ============================================================================
 * Capture Thread Implementation
 * ============================================================================ */

/**
 * @brief Webcam capture thread function
 *
 * Implements the continuous frame capture loop with rate limiting,
 * processing pipeline, and network transmission. Handles connection
 * monitoring and graceful thread termination.
 *
 * Capture Loop Operation:
 * 1. Check global shutdown flags and connection status
 * 2. Implement frame rate limiting with monotonic timing
 * 3. Read frame from webcam device
 * 4. Process frame through resizing pipeline
 * 5. Serialize frame data into network packet format
 * 6. Transmit packet to server via connection
 * 7. Clean up resources and repeat until shutdown
 *
 * Error Handling:
 * - Webcam read failures: Log and continue (device may be warming up)
 * - Processing failures: Skip frame and continue
 * - Network failures: Signal connection loss for reconnection
 * - Resource failures: Clean up and continue with next frame
 *
 * @param arg Unused thread argument
 * @return NULL on thread exit
 *
 * @ingroup client_capture
 */
static void *webcam_capture_thread_func(void *arg) {
  client_connection_t *connection = arg;

  // FPS tracking for webcam capture thread
  fps_t fps_tracker = {0};
  bool fps_tracker_initialized = false;
  uint64_t capture_frame_count = 0;
  uint64_t last_capture_frame_time_ns = 0;
  image_t *transmission_frame = NULL; // Capture worker owns reusable network pixels.
  image_t *last_frame = NULL;         // Borrows transmission_frame for paused playback.
  const char *video_codec = GET_OPTION(video_codec);
  bool automatic_codec = strcmp(video_codec, "auto") == 0;
  video_codec_t selected_codec = strcmp(video_codec, "raw") == 0    ? VIDEO_CODEC_RGBA
                                 : strcmp(video_codec, "h264") == 0 ? VIDEO_CODEC_H264
                                                                    : VIDEO_CODEC_H265;
  // Each connection starts at the requested codec with a fresh bitstream.
  h265_encoder_destroy(connection->video_encoder);
  connection->video_encoder = NULL;
  if (!fps_tracker_initialized) {
    int target_fps = (int)session_capture_get_target_fps(connection->capture_context);
    fps_init(&fps_tracker, target_fps, "WEBCAM_TX");
    fps_tracker_initialized = true;
  }

  while (!should_exit() && !server_connection_is_lost(connection)) {
    // Check connection status
    if (!server_connection_is_active(connection)) {
      log_debug_every(LOG_RATE_NORMAL, "Capture thread: waiting for connection to become active");
      platform_sleep_us(100 * US_PER_MS_INT); // Wait for connection
      continue;
    }

    // Frame rate limiting using session capture adaptive sleep
    session_capture_sleep_for_fps(connection->capture_context);

    // Re-check connection after FPS sleep in case it was lost during sleep (reconnection)
    if (!server_connection_is_active(connection) || server_connection_is_lost(connection)) {
      log_debug("Capture thread: connection lost after FPS sleep, exiting");
      break;
    }

    // Read frame using session capture library
    asciichat_errno_scope_t frame_scope = asciichat_errno_checkpoint();
    image_t *image = session_capture_read_frame(connection->capture_context);
    asciichat_error_context_t capture_failure;
    if (!image && asciichat_errno_peek_since(frame_scope, &capture_failure)) {
      asciichat_errno_request_exit(capture_failure.code);
      signal_exit();
      break;
    }

    // Check if media is paused and we have a last frame - render last frame to keep keyboard polling active
    if (!image) {
      media_source_t *source = (media_source_t *)session_capture_get_media_source(connection->capture_context);
      if (source && media_source_get_type(source) == MEDIA_SOURCE_FILE && media_source_is_paused(source) &&
          last_frame) {
        // Use last frame when paused (keeps display thread rendering and keyboard polling active)
        image = last_frame;
        log_debug_every(LOG_RATE_SLOW, "Using cached frame while paused");
      } else if (session_capture_at_end(connection->capture_context)) {
        // Check if we've reached end of file for media sources
        log_debug("Media source reached end of file");
        last_frame = NULL;
        break; // Exit capture loop - end of media
      } else {
        log_debug_every(LOG_RATE_SLOW, "No frame available from media source yet (returned NULL)");
        platform_sleep_us(10 * US_PER_MS_INT); // 10ms delay before retry
        continue;
      }
    }

    // Track frame for FPS reporting
    fps_frame_ns(&fps_tracker, time_get_ns(), "webcam frame captured");

    // Copy/resize into capture-owned storage; the media source retains its input.
    if (session_capture_process_for_transmission_into(connection->capture_context, image, &transmission_frame) !=
        ASCIICHAT_OK)
      continue;
    image_t *processed_image = transmission_frame;

    // Check connection before sending
    if (!server_connection_is_active(connection)) {
      log_warn("Connection lost before sending, stopping video transmission");
      break;
    }

    // Send frame packet to server using proper packet format
    acip_transport_t *transport = server_connection_get_transport(connection);
    if (!transport || !server_connection_is_active(connection)) {
      log_warn("Transport became unavailable during capture, stopping transmission");
      break;
    }

    uint64_t send_start_ns = time_get_ns();

    asciichat_errno_scope_t send_scope = asciichat_errno_scope_begin();
    asciichat_error_t send_result;
    bool encoding_failed = false;
    for (;;) {
      if (selected_codec == VIDEO_CODEC_RGBA) {
        send_result = threaded_send_image_frame(connection, processed_image->pixels, (uint32_t)processed_image->w,
                                                (uint32_t)processed_image->h, 1);
        break;
      }
      asciichat_errno_scope_t encode_scope = asciichat_errno_scope_begin();
      send_result = threaded_send_image_frame_encoded(connection, processed_image->pixels, (uint32_t)processed_image->w,
                                                      (uint32_t)processed_image->h, selected_codec, &encoding_failed);
      if (!encoding_failed || !automatic_codec || send_result == ERROR_MEMORY) {
        if (send_result == ASCIICHAT_OK)
          asciichat_errno_scope_end(encode_scope, ASCIICHAT_ERRNO_HANDLED);
        break;
      }
      log_warn("%s encoding failed; automatic video is switching to %s",
               selected_codec == VIDEO_CODEC_H265 ? "HEVC" : "H.264",
               selected_codec == VIDEO_CODEC_H265 ? "H.264" : "raw");
      asciichat_errno_scope_end(encode_scope, ASCIICHAT_ERRNO_DISMISSED);
      h265_encoder_destroy(connection->video_encoder);
      connection->video_encoder = NULL;
      selected_codec = selected_codec == VIDEO_CODEC_H265 ? VIDEO_CODEC_H264 : VIDEO_CODEC_RGBA;
      encoding_failed = false;
    }
    if (send_result == ASCIICHAT_OK)
      session_capture_complete_open(connection->capture_context,
                                    session_capture_source_epoch(connection->capture_context));
    const char *codec_name = selected_codec == VIDEO_CODEC_H265   ? "H265"
                             : selected_codec == VIDEO_CODEC_H264 ? "H264"
                                                                  : "RAW";
    if (encoding_failed) {
      log_error("%s video encoding failed (%s). Use --video-codec auto for fallback or --video-codec raw.", codec_name,
                asciichat_error_format(NULL, send_result));
      asciichat_errno_request_exit(send_result);
      signal_exit();
      break;
    }

    // If send failed due to connection loss, break out of loop
    if (send_result != ASCIICHAT_OK && !server_connection_is_active(connection)) {
      log_debug("Connection lost during send, stopping transmission");
      break;
    }
    uint64_t send_duration_ns = time_elapsed_ns(send_start_ns, time_get_ns());

    if (send_result != ASCIICHAT_OK && network_error_is_local_rejection(send_result)) {
      LOG_ERRNO_IF_SET("Dropping locally rejected video frame");
      asciichat_errno_scope_end(send_scope, ASCIICHAT_ERRNO_HANDLED);
      continue;
    }
    if (send_result != ASCIICHAT_OK) {
      log_error("🔴 CAPTURE_SEND_FAILED: IMAGE_FRAME_%s send failed %s after %.1fms, closing connection", codec_name,
                asciichat_error_format(NULL, send_result), (double)send_duration_ns / 1e6);
      server_connection_lost(connection);
      break;
    }

    ASSERT_NO_ERRNO_SINCE(send_scope);
    asciichat_errno_scope_end(send_scope, ASCIICHAT_ERRNO_HANDLED);
    if (send_duration_ns > 500 * NS_PER_MS_INT) {
      log_warn("⚠️  SLOW_FRAME_SEND: %.1fms to send %ux%u %s frame (may indicate full send buffer)",
               (double)send_duration_ns / 1e6, processed_image->w, processed_image->h, codec_name);
    }

    log_debug_every(LOG_RATE_NORMAL, "CAPTURE_FRAME_SENT: IMAGE_FRAME_%s delivered to server in %.1fms", codec_name,
                    (double)send_duration_ns / 1e6);

    // Keep the processed workspace itself for pause; no second image copy.
    last_frame = processed_image;

    // FPS tracking - frame successfully captured and sent
    capture_frame_count++;

    // Calculate time since last frame for lag detection (using nanosecond precision internally)
    uint64_t frame_capture_time_ns = time_get_ns();

    uint64_t frame_interval_ns = time_elapsed_ns(last_capture_frame_time_ns, frame_capture_time_ns);
    last_capture_frame_time_ns = frame_capture_time_ns;

    // Expected frame interval in nanoseconds
    uint64_t expected_interval_ns =
        NS_PER_SEC_INT / (uint64_t)session_capture_get_target_fps(connection->capture_context);
    uint64_t lag_threshold_ns = expected_interval_ns + (expected_interval_ns / 2); // 50% over expected

    // Log warning if frame took too long to capture (display in milliseconds for readability)
    if (capture_frame_count > 1 && frame_interval_ns > lag_threshold_ns) {
      double late_ms = (double)(frame_interval_ns - expected_interval_ns) / 1e6;
      double expected_ms = (double)expected_interval_ns / 1e6;
      double actual_ms = (double)frame_interval_ns / 1e6;
      double actual_fps = 1e9 / (double)frame_interval_ns;
      log_warn_every(LOG_RATE_FAST,
                     "CLIENT CAPTURE LAG: Frame captured %.1fms late (expected %.1fms, got %.1fms, actual fps: %.1f)",
                     late_ms, expected_ms, actual_ms, actual_fps);
    }


    // Yield to reduce CPU usage
    platform_sleep_us(1 * US_PER_MS_INT); // 1ms
  }

#ifdef DEBUG_THREADS
  log_debug("Webcam capture thread stopped");
#endif

  image_destroy(transmission_frame);
  transmission_frame = NULL;
  last_frame = NULL;

  log_debug("CAPTURE_THREAD_EXIT: About to mark thread as exited");
  atomic_store_bool(&connection->capture_thread_exited, true);
  log_debug("CAPTURE_THREAD_EXIT: Thread marked as exited, cleaning up errno");

  // Clean up thread-local error context before exit

  log_debug("CAPTURE_THREAD_EXIT: Exiting capture thread");
  return NULL;
}
/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */
/**
 * Initialize capture subsystem
 *
 * Sets up media source (webcam, file, or stdin) and prepares capture system for operation.
 * Must be called once during client initialization.
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_capture
 */
int capture_init() {
  // Build capture configuration from options
  session_capture_config_t config = {0};
  const char *media_url = GET_OPTION(media_url);
  const char *media_file = GET_OPTION(media_file);
  bool media_from_stdin = GET_OPTION(media_from_stdin);

  if (media_url && media_url[0] != '\0') {
    // Network URL streaming (takes priority over --file)
    // Don't open webcam when streaming from URL
    config.type = MEDIA_SOURCE_FILE;
    config.path = media_url;
    config.loop = false; // Network URLs cannot be looped
    log_debug("Using network URL: %s (webcam disabled)", media_url);
  } else if (media_file && media_file[0] != '\0') {
    // File or stdin streaming - don't open webcam
    config.type = media_from_stdin ? MEDIA_SOURCE_STDIN : MEDIA_SOURCE_FILE;
    config.path = media_file;
    config.loop = GET_OPTION(media_loop) && !media_from_stdin;
    log_debug("Using media %s: %s (webcam disabled)", media_from_stdin ? "stdin" : "file", media_file);
  } else if (GET_OPTION(test_pattern)) {
    // Test pattern mode - don't open real webcam
    config.type = MEDIA_SOURCE_TEST;
    config.path = NULL;
    log_debug("Using test pattern mode");
  } else {
    // Webcam mode (default)
    static char webcam_index_str[32];
    safe_snprintf(webcam_index_str, sizeof(webcam_index_str), "%u", GET_OPTION(webcam_index));
    config.type = MEDIA_SOURCE_WEBCAM;
    config.path = webcam_index_str;
    log_debug("Using webcam device %u", GET_OPTION(webcam_index));
  }
  int configured_fps = GET_OPTION(fps);
  config.target_fps = configured_fps > 0 ? (uint32_t)configured_fps : CAPTURE_TARGET_FPS;
  config.resize_for_network = true; // Client always resizes for network transmission

  // Configure audio capture with fallback to microphone
  config.enable_audio = true;
  config.audio_fallback_to_mic = true;
  config.mic_audio_ctx = audio_get_context();

  // Add seek timestamp if specified
  config.initial_seek_timestamp = GET_OPTION(media_seek_timestamp);

  // Create capture context using session library
  asciichat_errno_scope_t create_scope = asciichat_errno_checkpoint();
  g_capture_capture_ctx = session_capture_create(&config);
  if (!g_capture_capture_ctx) {
    // Check if there's already an error set (e.g., ERROR_WEBCAM_IN_USE)
    asciichat_error_context_t failure;
    asciichat_error_t existing_error = asciichat_errno_peek_since(create_scope, &failure) ? failure.code : ASCIICHAT_OK;
    log_debug("session_capture_create failed %s", asciichat_error_format(NULL, existing_error));
    if (existing_error != ASCIICHAT_OK) {
      log_debug("Returning existing error %s", asciichat_error_format(NULL, existing_error));
      return existing_error;
    }
    SET_ERRNO(ERROR_MEDIA_INIT, "Failed to initialize capture source");
    return -1;
  }

  return 0;
}
/**
 * Start capture thread
 *
 * Creates and starts the webcam capture thread. Also sends stream
 * start notification to server.
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_capture
 */
int capture_start_thread(client_connection_t *connection) {
  if (!connection->capture_context)
    return 0;
  if (THREAD_IS_CREATED(connection->capture_thread_created)) {
    log_warn("Capture thread already created");
    return 0;
  }

  // Start webcam capture thread
  atomic_store_bool(&connection->capture_thread_exited, false);
  if (thread_pool_spawn(connection->client_worker_pool, webcam_capture_thread_func, connection, 2, "webcam_capture") !=
      ASCIICHAT_OK) {
    SET_ERRNO(ERROR_THREAD, "Webcam capture thread creation failed");
    LOG_ERRNO_IF_SET("Webcam capture thread creation failed");
    return -1;
  }

  connection->capture_thread_created = true;
  log_debug("Webcam capture thread created successfully");

  return 0;
}
/**
 * Stop capture thread
 *
 * Gracefully stops the capture thread and cleans up resources.
 * Safe to call multiple times.
 *
 * @ingroup client_capture
 */
void capture_stop_thread(client_connection_t *connection) {
  if (!connection)
    return;
  if (!THREAD_IS_CREATED(connection->capture_thread_created)) {
    return;
  }

  // Wait for thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load_bool(&connection->capture_thread_exited)) {
    platform_sleep_us(100 * US_PER_MS_INT); // 100ms
    wait_count++;
  }

  if (!atomic_load_bool(&connection->capture_thread_exited)) {
    log_warn("Capture thread not responding after 2 seconds - will be joined by thread pool");
  }

  // Thread will be joined by thread_pool_stop_all() in client_connection_stop()
  connection->capture_thread_created = false;
}
/**
 * Check if capture thread has exited
 *
 * @return true if thread has exited, false otherwise
 *
 * @ingroup client_capture
 */
bool capture_thread_exited(client_connection_t *connection) {
  return atomic_load_bool(&connection->capture_thread_exited);
}
/**
 * Cleanup capture subsystem
 *
 * Stops capture thread and cleans up media source resources.
 * Called during client shutdown.
 *
 * @ingroup client_capture
 */
void capture_cleanup() {

  // Destroy capture context
  if (g_capture_capture_ctx) {
    session_capture_destroy(g_capture_capture_ctx);
    g_capture_capture_ctx = NULL;
  }
}

void *capture_get_media_source(void) {
  return session_capture_get_media_source(g_capture_capture_ctx);
}

session_capture_ctx_t *capture_get_context(void) {
  return g_capture_capture_ctx;
}

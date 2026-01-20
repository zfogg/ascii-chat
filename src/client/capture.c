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
#include "capture.h"
#include "main.h"
#include "server.h"
#include "session/capture.h"
#include "video/image.h"
#include "common.h"
#include "asciichat_errno.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access
#include "util/fps.h"
#include "util/thread.h" // For THREAD_IS_CREATED macro
#include "network/acip/send.h"
#include "network/acip/client.h"
#include <stdatomic.h>
#include <time.h>
#include <string.h>
#include "platform/abstraction.h"
#include "thread_pool.h"

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
static session_capture_ctx_t *g_capture_ctx = NULL;

/* ============================================================================
 * Capture Thread Management
 * ============================================================================ */

/**
 * @brief Webcam capture thread handle
 *
 * Thread handle for the background thread that captures video frames from
 * the webcam device. Created during connection establishment, joined during shutdown.
 *
 * @ingroup client_capture
 */
__attribute__((unused)) static asciichat_thread_t g_capture_thread;

/**
 * @brief Flag indicating if capture thread was successfully created
 *
 * Used during shutdown to determine whether the thread handle is valid and
 * should be joined. Prevents attempting to join a thread that was never created.
 *
 * @ingroup client_capture
 */
static bool g_capture_thread_created = false;

/**
 * @brief Atomic flag indicating capture thread has exited
 *
 * Set by the capture thread when it exits. Used by other threads to detect
 * thread termination without blocking on thread join operations.
 *
 * @ingroup client_capture
 */
static atomic_bool g_capture_thread_exited = false;

/* ============================================================================
 * Frame Processing Constants
 * ============================================================================ */
/** Target capture FPS for network transmission (144 FPS for high-refresh displays) */
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
  (void)arg;

  // FPS tracking for webcam capture thread
  static fps_t fps_tracker = {0};
  static bool fps_tracker_initialized = false;
  static uint64_t capture_frame_count = 0;
  static struct timespec last_capture_frame_time = {0, 0};
  if (!fps_tracker_initialized) {
    fps_init(&fps_tracker, CAPTURE_TARGET_FPS, "WEBCAM_TX");
    fps_tracker_initialized = true;
  }

  while (!should_exit() && !server_connection_is_lost()) {
    // Check connection status
    if (!server_connection_is_active()) {
      log_debug_every(LOG_RATE_NORMAL, "Capture thread: waiting for connection to become active");
      platform_sleep_usec(100 * 1000); // Wait for connection
      continue;
    }

    // Frame rate limiting using session capture adaptive sleep
    session_capture_sleep_for_fps(g_capture_ctx);

    // Read frame using session capture library
    image_t *image = session_capture_read_frame(g_capture_ctx);

    if (!image) {
      // Check if we've reached end of file for media sources
      if (session_capture_at_end(g_capture_ctx)) {
        log_debug("Media source reached end of file");
        break; // Exit capture loop - end of media
      }
      log_debug("No frame available from media source yet (returned NULL)");
      platform_sleep_usec(10000); // 10ms delay before retry
      continue;
    }

    // Track frame for FPS reporting
    struct timespec frame_capture_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &frame_capture_time);
    fps_frame(&fps_tracker, &frame_capture_time, "webcam frame captured");

    // Process frame for network transmission using session library
    // session_capture_process_for_transmission() returns a new image that we own
    // NOTE: The original 'image' is owned by media_source - do NOT free it!
    image_t *processed_image = session_capture_process_for_transmission(g_capture_ctx, image);
    if (!processed_image) {
      SET_ERRNO(ERROR_INVALID_STATE, "Failed to process frame for transmission");
      // NOTE: Do NOT free 'image' - it's owned by capture context
      continue;
    }

    // Check connection before sending
    if (!server_connection_is_active()) {
      log_warn("Connection lost before sending, stopping video transmission");
      image_destroy(processed_image);
      break;
    }

    // Send frame packet to server using proper packet format
    acip_transport_t *transport = server_connection_get_transport();
    log_debug_every(LOG_RATE_SLOW, "Capture thread: sending IMAGE_FRAME %ux%u via transport %p", processed_image->w,
                    processed_image->h, (void *)transport);
    asciichat_error_t send_result = acip_send_image_frame(transport, (const void *)processed_image->pixels,
                                                          (uint32_t)processed_image->w, (uint32_t)processed_image->h,
                                                          1); // pixel_format = 1 (RGB24)

    if (send_result != ASCIICHAT_OK) {
      log_error("Failed to send image frame: %d", send_result);
      server_connection_lost();
      image_destroy(processed_image);
      break;
    }
    log_debug_every(LOG_RATE_SLOW, "Capture thread: IMAGE_FRAME sent successfully");

    // FPS tracking - frame successfully captured and sent
    capture_frame_count++;

    // Calculate time since last frame for lag detection
    uint64_t frame_interval_us =
        ((uint64_t)frame_capture_time.tv_sec * 1000000 + (uint64_t)frame_capture_time.tv_nsec / 1000) -
        ((uint64_t)last_capture_frame_time.tv_sec * 1000000 + (uint64_t)last_capture_frame_time.tv_nsec / 1000);
    last_capture_frame_time = frame_capture_time;

    // Expected frame interval in microseconds
    uint64_t expected_interval_us = 1000000 / session_capture_get_target_fps(g_capture_ctx);
    uint64_t lag_threshold_us = expected_interval_us + (expected_interval_us / 2); // 50% over expected

    // Log warning if frame took too long to capture
    if (capture_frame_count > 1 && frame_interval_us > lag_threshold_us) {
      log_warn_every(LOG_RATE_FAST,
                     "CLIENT CAPTURE LAG: Frame captured %.1fms late (expected %.1fms, got %.1fms, actual fps: %.1f)",
                     (double)(frame_interval_us - expected_interval_us) / 1000.0, (double)expected_interval_us / 1000.0,
                     (double)frame_interval_us / 1000.0, 1000000.0 / (double)frame_interval_us);
    }

    // Clean up processed frame
    image_destroy(processed_image);
    processed_image = NULL;

    // Yield to reduce CPU usage
    platform_sleep_usec(1000); // 1ms
  }

#ifdef DEBUG_THREADS
  log_debug("Webcam capture thread stopped");
#endif

  atomic_store(&g_capture_thread_exited, true);

  // Clean up thread-local error context before exit
  asciichat_errno_cleanup();

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
  const char *media_file = GET_OPTION(media_file);
  bool media_from_stdin = GET_OPTION(media_from_stdin);

  if (media_file[0] != '\0') {
    // File or stdin streaming
    config.type = media_from_stdin ? MEDIA_SOURCE_STDIN : MEDIA_SOURCE_FILE;
    config.path = media_file;
    config.loop = GET_OPTION(media_loop) && !media_from_stdin;
    log_debug("Using media %s: %s", media_from_stdin ? "stdin" : "file", media_file);
  } else if (GET_OPTION(test_pattern)) {
    // Test pattern mode
    config.type = MEDIA_SOURCE_TEST;
    config.path = NULL;
    log_debug("Using test pattern mode");
  } else {
    // Webcam mode (default)
    static char webcam_index_str[32];
    snprintf(webcam_index_str, sizeof(webcam_index_str), "%u", GET_OPTION(webcam_index));
    config.type = MEDIA_SOURCE_WEBCAM;
    config.path = webcam_index_str;
    log_debug("Using webcam device %u", GET_OPTION(webcam_index));
  }
  config.target_fps = CAPTURE_TARGET_FPS;
  config.resize_for_network = true; // Client always resizes for network transmission

  // Create capture context using session library
  g_capture_ctx = session_capture_create(&config);
  if (!g_capture_ctx) {
    // Check if there's already an error set (e.g., ERROR_WEBCAM_IN_USE)
    asciichat_error_t existing_error = GET_ERRNO();
    log_debug("session_capture_create failed, GET_ERRNO() returned: %d", existing_error);
    if (existing_error != ASCIICHAT_OK) {
      log_debug("Returning existing error code %d", existing_error);
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
int capture_start_thread() {
  if (THREAD_IS_CREATED(g_capture_thread_created)) {
    log_warn("Capture thread already created");
    return 0;
  }

  // Start webcam capture thread
  atomic_store(&g_capture_thread_exited, false);
  if (thread_pool_spawn(g_client_worker_pool, webcam_capture_thread_func, NULL, 2, "webcam_capture") != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_THREAD, "Webcam capture thread creation failed");
    LOG_ERRNO_IF_SET("Webcam capture thread creation failed");
    return -1;
  }

  g_capture_thread_created = true;
  log_debug("Webcam capture thread created successfully");

  // Notify server we're starting to send video
  if (threaded_send_stream_start_packet(STREAM_TYPE_VIDEO) < 0) {
    LOG_ERRNO_IF_SET("Failed to send stream start packet");
  }

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
void capture_stop_thread() {
  if (!THREAD_IS_CREATED(g_capture_thread_created)) {
    return;
  }

  // Wait for thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_capture_thread_exited)) {
    platform_sleep_usec(100000); // 100ms
    wait_count++;
  }

  if (!atomic_load(&g_capture_thread_exited)) {
    log_warn("Capture thread not responding after 2 seconds - will be joined by thread pool");
  }

  // Thread will be joined by thread_pool_stop_all() in protocol_stop_connection()
  g_capture_thread_created = false;
}
/**
 * Check if capture thread has exited
 *
 * @return true if thread has exited, false otherwise
 *
 * @ingroup client_capture
 */
bool capture_thread_exited() {
  return atomic_load(&g_capture_thread_exited);
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
  capture_stop_thread();

  // Destroy capture context
  if (g_capture_ctx) {
    session_capture_destroy(g_capture_ctx);
    g_capture_ctx = NULL;
  }
}

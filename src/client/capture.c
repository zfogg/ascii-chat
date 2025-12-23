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
#include "os/webcam.h"
#include "image2ascii/image.h"
#include "common.h"
#include "asciichat_errno.h"
#include "options.h"
#include "util/time.h"
#include "util/time.h"
#include <stdatomic.h>
#include <time.h>
#include <string.h>
#include "platform/abstraction.h"

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
static asciithread_t g_capture_thread;

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
/** Target frame interval in milliseconds (30 FPS = 33ms) */
// Use FRAME_INTERVAL_MS from common.h
/** Maximum frame width for network transmission - reduced for bandwidth optimization */
#define MAX_FRAME_WIDTH 480
/** Maximum frame height for network transmission - reduced for bandwidth optimization */
#define MAX_FRAME_HEIGHT 270

/* ============================================================================
 * Frame Processing Functions
 * ============================================================================ */
/**
 * Calculate optimal frame dimensions for network transmission
 *
 * Implements fit-to-bounds scaling algorithm that maintains original
 * aspect ratio while ensuring frame fits within network size limits.
 * Uses floating-point arithmetic for precise aspect ratio calculations.
 *
 * Scaling Algorithm:
 * 1. Calculate original image aspect ratio (width/height)
 * 2. Compare with maximum bounds aspect ratio
 * 3. Scale by width if max bounds are wider than image
 * 4. Scale by height if max bounds are taller than image
 * 5. Return dimensions that fit within bounds
 *
 * @param original_width Original frame width from webcam
 * @param original_height Original frame height from webcam
 * @param max_width Maximum allowed width for transmission
 * @param max_height Maximum allowed height for transmission
 * @param result_width Output parameter for calculated width
 * @param result_height Output parameter for calculated height
 *
 * @ingroup client_capture
 */
static void calculate_optimal_dimensions(ssize_t original_width, ssize_t original_height, ssize_t max_width,
                                         ssize_t max_height, ssize_t *result_width, ssize_t *result_height) {
  // Calculate original aspect ratio
  float img_aspect = (float)original_width / (float)original_height;
  // Check if image needs resizing
  if (original_width <= max_width && original_height <= max_height) {
    // Image is already within bounds - use as-is
    *result_width = original_width;
    *result_height = original_height;
    return;
  }
  // Determine scaling factor based on which dimension is the limiting factor
  if ((float)max_width / (float)max_height > img_aspect) {
    // Max box is wider than image aspect - scale by height
    *result_height = max_height;
    *result_width = (ssize_t)(max_height * img_aspect);
  } else {
    // Max box is taller than image aspect - scale by width
    *result_width = max_width;
    *result_height = (ssize_t)(max_width / img_aspect);
  }
}
/**
 * Process and resize webcam frame for transmission
 *
 * Handles frame resizing with memory management and error handling.
 * Creates new image buffer only when resizing is required, otherwise
 * reuses original frame to minimize memory allocations.
 *
 * @param original_image Input frame from webcam
 * @param max_width Maximum allowed frame width
 * @param max_height Maximum allowed frame height
 * @return Processed image ready for transmission, or NULL on error
 *
 * @ingroup client_capture
 */
static image_t *process_frame_for_transmission(image_t *original_image, ssize_t max_width, ssize_t max_height) {
  if (!original_image) {
    return NULL;
  }
  // Calculate optimal dimensions
  ssize_t resized_width, resized_height;
  calculate_optimal_dimensions(original_image->w, original_image->h, max_width, max_height, &resized_width,
                               &resized_height);
  // Check if resizing is needed
  if (original_image->w == resized_width && original_image->h == resized_height) {
    // No resizing needed - return original image
    return original_image;
  }
  // Create new image for resized frame
  image_t *resized = image_new(resized_width, resized_height);
  if (!resized) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate resized image buffer");
    return NULL;
  }

  // Perform resizing operation
  image_resize(original_image, resized);

  // Destroy original image since we created a new one
  image_destroy(original_image);
  return resized;
}
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
  struct timespec last_capture_time = {0, 0};

  // FPS tracking for webcam capture thread
  uint64_t capture_frame_count = 0;
  struct timespec last_capture_frame_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &last_capture_frame_time);
  int expected_capture_fps = 144; // 144 fps target

  while (!should_exit() && !server_connection_is_lost()) {
    // Check connection status
    if (!server_connection_is_active()) {
      platform_sleep_usec(100 * 1000); // Wait for connection
      continue;
    }

    // Frame rate limiting using monotonic clock
    // Always capture at 144fps to support high-refresh displays, regardless of client's rendering FPS
    const long CAPTURE_INTERVAL_MS = 1000 / 144; // ~6.94ms for 144fps
    struct timespec current_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &current_time);
    long elapsed_ms = (current_time.tv_sec - last_capture_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - last_capture_time.tv_nsec) / 1000000;
    if (elapsed_ms < CAPTURE_INTERVAL_MS) {
      platform_sleep_usec((CAPTURE_INTERVAL_MS - elapsed_ms) * 1000);
      continue;
    }

    image_t *image = webcam_read();

    if (!image) {
      log_info("No frame available from webcam yet (webcam_read returned NULL)");
      platform_sleep_usec(10000); // 10ms delay before retry
      continue;
    }

    // Process frame for network transmission
    image_t *processed_image = process_frame_for_transmission(image, MAX_FRAME_WIDTH, MAX_FRAME_HEIGHT);
    if (!processed_image) {
      SET_ERRNO(ERROR_INVALID_STATE, "Failed to process frame for transmission");
      if (image) {
        image_destroy(image);
      }
      continue;
    }
    // Serialize image data for network transmission

    // Create image frame packet in new format: [width:4][height:4][compressed_flag:4][data_size:4][pixel_data]
    // This matches what the server expects in handle_image_frame_packet()
    if (processed_image->w <= 0 || processed_image->h <= 0) {
      SET_ERRNO(ERROR_INVALID_FRAME, "Processed image has invalid dimensions: %dx%d", processed_image->w,
                processed_image->h);
      image_destroy(processed_image);
      continue;
    }

    if (processed_image->w > IMAGE_MAX_WIDTH || processed_image->h > IMAGE_MAX_HEIGHT) {
      SET_ERRNO(ERROR_INVALID_FRAME, "Processed image dimensions exceed maximum: %dx%d (max %ux%u)", processed_image->w,
                processed_image->h, IMAGE_MAX_WIDTH, IMAGE_MAX_HEIGHT);
      image_destroy(processed_image);
      continue;
    }

    size_t pixel_count = 0;
    if (safe_size_mul((size_t)processed_image->w, (size_t)processed_image->h, &pixel_count)) {
      SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Pixel count overflow for processed image dimensions: %dx%d", processed_image->w,
                processed_image->h);
      image_destroy(processed_image);
      continue;
    }

    size_t pixel_size = 0;
    if (safe_size_mul(pixel_count, sizeof(rgb_t), &pixel_size)) {
      SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Pixel size overflow while preparing frame (%zu pixels)", pixel_count);
      image_destroy(processed_image);
      continue;
    }

    if (pixel_size > IMAGE_MAX_PIXELS_SIZE) {
      SET_ERRNO(ERROR_NETWORK_SIZE, "Pixel data exceeds maximum size: %zu bytes (max %zu)", pixel_size,
                (size_t)IMAGE_MAX_PIXELS_SIZE);
      image_destroy(processed_image);
      continue;
    }

    if (pixel_size > UINT32_MAX) {
      SET_ERRNO(ERROR_NETWORK_SIZE, "Pixel data exceeds protocol limit: %zu bytes (> UINT32_MAX)", pixel_size);
      image_destroy(processed_image);
      continue;
    }

    size_t header_size = sizeof(uint32_t) * 4; // width, height, compressed_flag, data_size
    if (pixel_size > SIZE_MAX - header_size) {
      SET_ERRNO(ERROR_NETWORK_SIZE, "Packet size overflow while preparing frame");
      image_destroy(processed_image);
      continue;
    }
    size_t packet_size = header_size + pixel_size;

    // Check packet size limits
    if (packet_size > MAX_PACKET_SIZE) {
      SET_ERRNO(ERROR_NETWORK_SIZE, "Packet too large: %zu bytes (max %d)", packet_size, MAX_PACKET_SIZE);
      image_destroy(processed_image);
      continue;
    }

    // Allocate packet buffer
    uint8_t *packet_data = SAFE_MALLOC(packet_size, void *);
    if (!packet_data) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate packet buffer of size %zu", packet_size);
      image_destroy(processed_image);
      continue;
    }

    // Build packet in new format
    uint32_t *header = (uint32_t *)packet_data;
    header[0] = htonl(processed_image->w);   // width
    header[1] = htonl(processed_image->h);   // height
    header[2] = htonl(0);                    // compressed_flag = 0 (uncompressed)
    header[3] = htonl((uint32_t)pixel_size); // data_size = pixel data length

    // Copy pixel data after header
    memcpy(packet_data + header_size, processed_image->pixels, pixel_size);
    // Check connection before sending
    if (!server_connection_is_active()) {
      log_warn("Connection lost before sending, stopping video transmission");
      SAFE_FREE(packet_data);
      image_destroy(processed_image);
      break;
    }

    // Send frame packet to server
    int send_result = threaded_send_packet(PACKET_TYPE_IMAGE_FRAME, packet_data, packet_size);

    if (send_result < 0) {
      // Signal connection loss for reconnection
      server_connection_lost();
      SAFE_FREE(packet_data);
      image_destroy(processed_image);
      break;
    }

    // FPS tracking - frame successfully captured and sent
    capture_frame_count++;

    // Calculate time since last frame
    uint64_t frame_interval_us =
        ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
        ((uint64_t)last_capture_frame_time.tv_sec * 1000000 + (uint64_t)last_capture_frame_time.tv_nsec / 1000);
    last_capture_frame_time = current_time;

    // Expected frame interval in microseconds (6944us for 144fps)
    uint64_t expected_interval_us = 1000000 / expected_capture_fps;
    uint64_t lag_threshold_us = expected_interval_us + (expected_interval_us / 2); // 50% over expected

    // Log warning if frame took too long to capture
    if (capture_frame_count > 1 && frame_interval_us > lag_threshold_us) {
      log_warn_every(1000000,
                     "CLIENT CAPTURE LAG: Frame captured %.1fms late (expected %.1fms, got %.1fms, actual fps: %.1f)",
                     (double)(frame_interval_us - expected_interval_us) / 1000.0, (double)expected_interval_us / 1000.0,
                     (double)frame_interval_us / 1000.0, 1000000.0 / (double)frame_interval_us);
    }

    // Update capture timing
    last_capture_time = current_time;
    // Clean up resources
    SAFE_FREE(packet_data);
    image_destroy(processed_image);
  }

#ifdef DEBUG_THREADS
  log_info("Webcam capture thread stopped");
#endif

  atomic_store(&g_capture_thread_exited, true);
  return NULL;
}
/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */
/**
 * Initialize capture subsystem
 *
 * Sets up webcam device and prepares capture system for operation.
 * Must be called once during client initialization.
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_capture
 */
int capture_init() {
  // Initialize webcam capture
  int webcam_index = opt_webcam_index;
  int result = webcam_init(webcam_index);
  if (result != 0) {
    SET_ERRNO(ERROR_WEBCAM, "Failed to initialize webcam (error code: %d)", result);
    // Preserve specific error code (e.g., WEBCAM vs WEBCAM_IN_USE)
    return result;
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
  if (g_capture_thread_created) {
    log_warn("Capture thread already created");
    return 0;
  }

  // Start webcam capture thread
  atomic_store(&g_capture_thread_exited, false);
  log_warn("THREAD_CREATE: About to create webcam capture thread");
  int result = ascii_thread_create(&g_capture_thread, webcam_capture_thread_func, NULL);
  log_warn("THREAD_CREATE: ascii_thread_create() returned %d, thread handle = %p", result, g_capture_thread);

  if (result != 0) {
    SET_ERRNO(ERROR_THREAD, "THREAD_CREATE: Webcam capture thread creation FAILED with result=%d", result);
    LOG_ERRNO_IF_SET("Webcam capture thread creation failed");
    return -1;
  }

  g_capture_thread_created = true;
  log_warn("THREAD_CREATE: Webcam capture thread created successfully, handle = %p", g_capture_thread);

  // Notify server we're starting to send video
  if (threaded_send_stream_start_packet(STREAM_TYPE_VIDEO) < 0) {
    LOG_ERRNO_IF_SET("Failed to send stream start packet");
  } else {
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
  if (!g_capture_thread_created) {
    return;
  }

  // Flush webcam to interrupt any blocking ReadSample operations
  // This allows the capture thread to notice should_exit() and exit cleanly
  webcam_flush();

  // Wait for thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_capture_thread_exited)) {
    platform_sleep_usec(100000); // 100ms
    // Keep flushing in case the thread went back into a blocking read
    if (wait_count % 5 == 0) {
      webcam_flush();
    }
    wait_count++;
  }

  if (!atomic_load(&g_capture_thread_exited)) {
    log_warn("Capture thread not responding after 2 seconds - forcing join with timeout");
  }

  // Join the thread with timeout to prevent hanging
  void *thread_retval = NULL;
  int join_result = ascii_thread_join_timeout(&g_capture_thread, &thread_retval, 5000); // 5 second timeout

  if (join_result == -2) {
    SET_ERRNO(ERROR_THREAD, "Capture thread join timed out - thread may be stuck, forcing termination");
    // Force close the thread handle to prevent resource leak
#ifdef _WIN32
    if (g_capture_thread) {
      CloseHandle(g_capture_thread);
      g_capture_thread = NULL;
    }
#else
    // On POSIX, threads clean up automatically after join
    g_capture_thread = 0;
#endif
  } else if (join_result != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to join capture thread, result=%d", join_result);
    // Still force close the handle to prevent leak
#ifdef _WIN32
    if (g_capture_thread) {
      CloseHandle(g_capture_thread);
      g_capture_thread = NULL;
    }
#else
    // On POSIX, threads clean up automatically after join
    g_capture_thread = 0;
#endif
  }

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
 * Stops capture thread and cleans up webcam resources.
 * Called during client shutdown.
 *
 * @ingroup client_capture
 */
void capture_cleanup() {
  capture_stop_thread();
  webcam_cleanup();
}

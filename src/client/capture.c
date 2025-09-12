/**
 * @file capture.c
 * @brief ASCII-Chat Client Media Capture Management
 *
 * This module manages webcam video capture and transmission for the ASCII-Chat
 * client. It implements a dedicated capture thread with frame rate limiting,
 * image processing pipeline, and network transmission with optimal sizing.
 *
 * ## Capture Architecture
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
 * - **Target Rate**: 30 FPS for smooth video (33ms intervals)
 * - **Timing Control**: Monotonic clock for accurate frame intervals
 * - **Adaptive Delays**: Dynamic sleep adjustment for consistent timing
 * - **Bandwidth Optimization**: Balance quality vs network utilization
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
#include "network.h"
#include "common.h"
#include "options.h"

#include <stdatomic.h>
#include <time.h>
#include <string.h>

#include "platform/abstraction.h"

/* ============================================================================
 * Capture Thread Management
 * ============================================================================ */

/** Webcam capture thread handle */
static asciithread_t g_capture_thread;

/** Flag indicating if capture thread was created */
static bool g_capture_thread_created = false;

/** Atomic flag indicating capture thread has exited */
static atomic_bool g_capture_thread_exited = false;

/* ============================================================================
 * Frame Processing Constants
 * ============================================================================ */

/** Target frame interval in milliseconds (30 FPS = 33ms) */
// Use FRAME_INTERVAL_MS from common.h

/** Maximum frame width for network transmission */
#define MAX_FRAME_WIDTH 800

/** Maximum frame height for network transmission */
#define MAX_FRAME_HEIGHT 600

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
 */
static void calculate_optimal_dimensions(ssize_t original_width, ssize_t original_height,
                                       ssize_t max_width, ssize_t max_height,
                                       ssize_t *result_width, ssize_t *result_height) {
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
 */
static image_t *process_frame_for_transmission(image_t *original_image, 
                                             ssize_t max_width, ssize_t max_height) {
  if (!original_image) {
    return NULL;
  }

  // Calculate optimal dimensions
  ssize_t resized_width, resized_height;
  calculate_optimal_dimensions(original_image->w, original_image->h,
                             max_width, max_height,
                             &resized_width, &resized_height);

  // Check if resizing is needed
  if (original_image->w == resized_width && original_image->h == resized_height) {
    // No resizing needed - return original image
    return original_image;
  }

  // Create new image for resized frame
  image_t *resized = image_new(resized_width, resized_height);
  if (!resized) {
    log_error("Failed to allocate resized image buffer");
    return NULL;
  }

  // Perform resizing operation
  image_resize(original_image, resized);
  
  // Destroy original image since we created a new one
  image_destroy(original_image);
  
  return resized;
}

/**
 * Serialize image data into network packet format
 *
 * Packs image data into format suitable for network transmission:
 * [width:4][height:4][rgb_data:width*height*3]
 * Uses network byte order for width and height fields.
 *
 * @param image Image to serialize
 * @param packet_size Output parameter for total packet size
 * @return Allocated packet buffer or NULL on error
 */
static uint8_t *serialize_image_packet(image_t *image, size_t *packet_size) {
  if (!image || !packet_size) {
    return NULL;
  }

  // Calculate packet components
  size_t rgb_size = (size_t)image->w * (size_t)image->h * sizeof(rgb_t);
  *packet_size = sizeof(uint32_t) * 2 + rgb_size; // width + height + pixels

  // Check packet size limits
  if (*packet_size > MAX_PACKET_SIZE) {
    log_error("Packet too large: %zu bytes (max %d)", *packet_size, MAX_PACKET_SIZE);
    return NULL;
  }

  // Allocate packet buffer
  uint8_t *packet_data = NULL;
  SAFE_MALLOC(packet_data, *packet_size, uint8_t *);
  if (!packet_data) {
    log_error("Failed to allocate packet buffer of size %zu", *packet_size);
    return NULL;
  }

  // Pack the image data
  uint32_t width_net = htonl(image->w);
  uint32_t height_net = htonl(image->h);
  memcpy(packet_data, &width_net, sizeof(uint32_t));
  memcpy(packet_data + sizeof(uint32_t), &height_net, sizeof(uint32_t));
  memcpy(packet_data + sizeof(uint32_t) * 2, image->pixels, rgb_size);

  return packet_data;
}

/* ============================================================================
 * Capture Thread Implementation
 * ============================================================================ */

/**
 * Main webcam capture thread function
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
 */
static void *webcam_capture_thread_func(void *arg) {
  (void)arg;

  struct timespec last_capture_time = {0, 0};

  log_info("Webcam capture thread started");

  while (!should_exit() && !server_connection_is_lost()) {
    // Check connection status
    if (!server_connection_is_active()) {
      usleep(100 * 1000); // Wait for connection
      continue;
    }

    // Frame rate limiting using monotonic clock
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - last_capture_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - last_capture_time.tv_nsec) / 1000000;

    if (elapsed_ms < FRAME_INTERVAL_MS) {
      usleep((FRAME_INTERVAL_MS - elapsed_ms) * 1000);
      continue;
    }

    // Capture frame from webcam
    image_t *image = webcam_read();
    if (!image) {
      log_info("No frame available from webcam yet (webcam_read returned NULL)");
      usleep(10000); // 10ms delay before retry
      continue;
    }

    // Process frame for network transmission
    image_t *processed_image = process_frame_for_transmission(image, 
                                                            MAX_FRAME_WIDTH, 
                                                            MAX_FRAME_HEIGHT);
    if (!processed_image) {
      log_error("Failed to process frame for transmission");
      if (image) {
        image_destroy(image);
      }
      continue;
    }

    // Serialize image data for network transmission
    size_t packet_size;
    uint8_t *packet_data = serialize_image_packet(processed_image, &packet_size);
    if (!packet_data) {
      image_destroy(processed_image);
      continue;
    }

    // Check connection before sending
    if (!server_connection_is_active()) {
      log_debug("Connection lost, stopping video transmission");
      free(packet_data);
      image_destroy(processed_image);
      break;
    }

    // Send frame packet to server
    if (server_send_packet(PACKET_TYPE_IMAGE_FRAME, packet_data, packet_size) < 0) {
      log_error("Failed to send video frame to server: %s", strerror(errno));
      // Signal connection loss for reconnection
      server_connection_lost();
      free(packet_data);
      image_destroy(processed_image);
      break;
    }

    // Update capture timing
    last_capture_time = current_time;

    // Clean up resources
    free(packet_data);
    image_destroy(processed_image);
  }

  log_info("Webcam capture thread stopped");
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
 */
int capture_init() {
  // Initialize webcam capture
  int webcam_index = opt_webcam_index;
  if (webcam_init(webcam_index) != 0) {
    log_error("Failed to initialize webcam");
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
 */
int capture_start_thread() {
  if (g_capture_thread_created) {
    log_warn("Capture thread already created");
    return 0;
  }

  // Start webcam capture thread
  atomic_store(&g_capture_thread_exited, false);
  if (ascii_thread_create(&g_capture_thread, webcam_capture_thread_func, NULL) != 0) {
    log_error("Failed to create webcam capture thread");
    return -1;
  }

  g_capture_thread_created = true;

  // Notify server we're starting to send video
  if (server_send_stream_start(STREAM_TYPE_VIDEO) < 0) {
    log_error("Failed to send stream start packet");
  }

  return 0;
}

/**
 * Stop capture thread
 *
 * Gracefully stops the capture thread and cleans up resources.
 * Safe to call multiple times.
 */
void capture_stop_thread() {
  if (!g_capture_thread_created) {
    return;
  }

  // Signal thread to stop
  // Signal main thread to exit via should_exit() mechanism

  // Wait for thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_capture_thread_exited)) {
    usleep(100000); // 100ms
    wait_count++;
  }

  if (!atomic_load(&g_capture_thread_exited)) {
    log_error("Capture thread not responding - forcing join");
  }

  // Join the thread
  ascii_thread_join(&g_capture_thread, NULL);
  g_capture_thread_created = false;

  log_info("Webcam capture thread stopped and joined");
}

/**
 * Check if capture thread has exited
 *
 * @return true if thread has exited, false otherwise
 */
bool capture_thread_exited() {
  return atomic_load(&g_capture_thread_exited);
}

/**
 * Cleanup capture subsystem
 *
 * Stops capture thread and cleans up webcam resources.
 * Called during client shutdown.
 */
void capture_cleanup() {
  capture_stop_thread();
  webcam_cleanup();
}
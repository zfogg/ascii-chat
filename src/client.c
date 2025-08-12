#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <zlib.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "ascii.h"
#include "common.h"
#include "mixer.h"
#include "network.h"
#include "options.h"
#include "audio.h"
#include "image.h"
#include "webcam.h"
#include "aspect_ratio.h"
#include "buffer_pool.h"
#include "frame_debug.h"

#define NETWORK_DEBUG
#define AUDIO_DEBUG
#define COMPRESSION_DEBUG

static int sockfd = 0;
static volatile bool g_should_exit = false;
static volatile bool g_first_connection = true;
static volatile bool g_should_reconnect = false;
static volatile bool g_connection_lost = false;

static audio_context_t g_audio_context = {0};

// Frame debugging tracker
static frame_debug_tracker_t g_client_frame_debug;

static pthread_t g_data_thread;
static bool g_data_thread_created = false;
static volatile bool g_data_thread_exited = false;

// Ping thread for keepalive
static pthread_t g_ping_thread;
static bool g_ping_thread_created = false;
static volatile bool g_ping_thread_exited = false;

// Webcam capture thread
static pthread_t g_capture_thread;
static bool g_capture_thread_created = false;
static volatile bool g_capture_thread_exited = false;

// Audio capture thread
static pthread_t g_audio_capture_thread;
static bool g_audio_capture_thread_created = false;
static volatile bool g_audio_capture_thread_exited = false;

// Mutex to protect socket sends (prevent interleaved packets)
static pthread_mutex_t g_send_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Multi-User Client State
 * ============================================================================
 */

// Multi-user client state
uint32_t g_my_client_id = 0;

// Remote client tracking (up to MAX_CLIENTS)
typedef struct {
  uint32_t client_id;
  char display_name[MAX_DISPLAY_NAME_LEN];
  bool is_active;
  time_t last_seen;
} remote_client_info_t;

// Server state tracking for console clear logic
static uint32_t g_last_active_count = 0;
static bool g_server_state_initialized = false;

/* ============================================================================
 * Function Declarations
 * ============================================================================
 */

// Multi-user packet handlers
static void handle_server_state_packet(const void *data, size_t len);

// Ping thread for connection keepalive
static void *ping_thread_func(void *arg);

// Webcam capture thread
static void *webcam_capture_thread_func(void *arg);

// Audio capture thread
static void *audio_capture_thread_func(void *arg);

/* ============================================================================
 * Thread-Safe Packet Sending Functions
 * ============================================================================
 */

// Define macros to use thread-safe versions
#define send_packet safe_send_packet
#define send_audio_packet safe_send_audio_packet
#define send_audio_batch_packet safe_send_audio_batch_packet
#define send_size_packet safe_send_size_packet
#define send_pong_packet safe_send_pong_packet
#define send_ping_packet safe_send_ping_packet
#define send_stream_start_packet safe_send_stream_start_packet
#define send_stream_stop_packet safe_send_stream_stop_packet
#define send_client_join_packet safe_send_client_join_packet

// Thread-safe wrapper for network.h send_packet
static int safe_send_packet(int sockfd, packet_type_t type, const void *data, size_t len) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_packet
  int result = send_packet(sockfd, type, data, len);
#define send_packet safe_send_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

// Thread-safe wrapper for network.h send_audio_packet
static int safe_send_audio_packet(int sockfd, const float *samples, int num_samples) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_audio_packet
  int result = send_audio_packet(sockfd, samples, num_samples);
#define send_audio_packet safe_send_audio_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

// Thread-safe wrapper for network.h send_audio_batch_packet
static int safe_send_audio_batch_packet(int sockfd, const float *samples, int num_samples, int batch_count) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_audio_batch_packet
  int result = send_audio_batch_packet(sockfd, samples, num_samples, batch_count);
#define send_audio_batch_packet safe_send_audio_batch_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

// Thread-safe wrapper for network.h send_size_packet
static int safe_send_size_packet(int sockfd, unsigned short width, unsigned short height) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_size_packet
  int result = send_size_packet(sockfd, width, height);
#define send_size_packet safe_send_size_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

// Thread-safe wrapper for network.h send_pong_packet
static int safe_send_pong_packet(int sockfd) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_pong_packet
  int result = send_pong_packet(sockfd);
#define send_pong_packet safe_send_pong_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

// Thread-safe wrapper for network.h send_ping_packet
static int safe_send_ping_packet(int sockfd) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_ping_packet
  int result = send_ping_packet(sockfd);
#define send_ping_packet safe_send_ping_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

// Thread-safe wrapper for network.h send_stream_start_packet
static int safe_send_stream_start_packet(int sockfd, uint32_t stream_type) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_stream_start_packet
  int result = send_stream_start_packet(sockfd, stream_type);
#define send_stream_start_packet safe_send_stream_start_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

// Thread-safe wrapper for network.h send_stream_stop_packet
static int safe_send_stream_stop_packet(int sockfd, uint32_t stream_type) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_stream_stop_packet
  int result = send_stream_stop_packet(sockfd, stream_type);
#define send_stream_stop_packet safe_send_stream_stop_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

// Thread-safe wrapper for network.h send_client_join_packet
static int safe_send_client_join_packet(int socketfd, const char *display_name, uint32_t capabilities) {
  pthread_mutex_lock(&g_send_mutex);
#undef send_client_join_packet
  int result = send_client_join_packet(socketfd, display_name, capabilities);
#define send_client_join_packet safe_send_client_join_packet
  pthread_mutex_unlock(&g_send_mutex);
  return result;
}

static int close_socket(int socketfd) {
  if (socketfd > 0) {
    log_info("Closing socket connection");
    if (0 > (socketfd = close(socketfd))) {
      log_error("Failed to close socket: %s", network_error_string(errno));
      return -1;
    }
    return socketfd;
  }
  return 0; // Socket connection not found. Just return 0 as if we closed a socket.
}

static void shutdown_client() {
  if (close_socket(sockfd) < 0) {
    exit(ASCIICHAT_ERR_NETWORK);
  }
  sockfd = 0;

  // Clean up data reception thread
  if (g_data_thread_created) {
    log_info("Waiting for data reception thread to finish...");

    // Signal thread to stop first
    g_should_exit = true;
    g_connection_lost = true;

    // Stop audio playback to help thread exit
    if (opt_audio_enabled) {
      audio_stop_playback(&g_audio_context);
      audio_stop_capture(&g_audio_context);
    }

    // Give thread a moment to exit gracefully
    usleep(100000); // 100ms

    // Force close socket to break any blocking recv() calls
    if (sockfd > 0) {
      shutdown(sockfd, SHUT_RDWR);
      close(sockfd);
    }

    // Wait for thread to exit - but with a timeout check
    // Since macOS doesn't have pthread_timedjoin_np, we'll do a simple wait
    int wait_count = 0;
    while (wait_count < 20 && !g_data_thread_exited) { // Wait up to 2 seconds
      usleep(100000);                                  // 100ms
      wait_count++;
    }

    if (!g_data_thread_exited) {
      log_error("Data thread not responding - forcing cancellation");
      pthread_cancel(g_data_thread);
      pthread_join(g_data_thread, NULL); // Still need to join even after cancel
    } else {
      pthread_join(g_data_thread, NULL);
    }

    g_data_thread_created = false;

    if (opt_audio_enabled) {
      audio_destroy(&g_audio_context);
    }
    log_info("Data reception thread joined and context destroyed");
  }

  // Clean up webcam
  ascii_read_destroy();

  ascii_write_destroy();
  log_info("Client shutdown complete");
  log_destroy(); // Destroy logging last
}

static void sigint_handler(int sigint) {
  (void)(sigint);

  // If this is the second Ctrl-C, force exit
  static int sigint_count = 0;
  sigint_count++;

  if (sigint_count > 1) {
    printf("\nForce quit!\n");
    _exit(1); // Force immediate exit
  }

  printf("\nShutdown requested... (Press Ctrl-C again to force quit)\n");
  g_should_exit = true;
  g_connection_lost = true; // Signal all threads to exit

  // Close socket to interrupt recv operations
  if (sockfd > 0) {
    shutdown(sockfd, SHUT_RDWR); // More aggressive than close
    close(sockfd);
    sockfd = 0;
  }
}

static void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  // Terminal was resized, update dimensions and recalculate aspect ratio
  update_dimensions_to_terminal_size();

  // Send new size to server if connected
  if (sockfd > 0) {
    if (send_size_packet(sockfd, opt_width, opt_height) < 0) {
      log_warn("Failed to send size update to server: %s", network_error_string(errno));
    } else {
      log_debug("Sent size update to server: %ux%u", opt_width, opt_height);
    }
  }
}

#define MAX_RECONNECT_DELAY (5 * 1000 * 1000) // Maximum delay between reconnection attempts (microseconds)

static float get_reconnect_delay(unsigned int reconnect_attempt) {
  float delay = 0.01f + 0.2f * (reconnect_attempt - 1) * 1000 * 1000;
  if (delay > MAX_RECONNECT_DELAY)
    delay = (float)MAX_RECONNECT_DELAY;
  return delay;
}

// Audio volume control
#define AUDIO_VOLUME_BOOST 2.0f

static void handle_audio_packet(const void *data, size_t len) {
  if (!opt_audio_enabled || !data || len == 0) {
    return;
  }

  int num_samples = (int)(len / sizeof(float));
  if (num_samples > AUDIO_SAMPLES_PER_PACKET) {
    log_warn("Audio packet too large: %d samples", num_samples);
    return;
  }

  // Copy and apply volume boost
  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];
  const float *samples = (const float *)data;

  for (int i = 0; i < num_samples; i++) {
    audio_buffer[i] = samples[i] * AUDIO_VOLUME_BOOST;
    // Clamp to prevent distortion
    if (audio_buffer[i] > 1.0f)
      audio_buffer[i] = 1.0f;
    if (audio_buffer[i] < -1.0f)
      audio_buffer[i] = -1.0f;
  }

  audio_write_samples(&g_audio_context, audio_buffer, num_samples);
#ifdef AUDIO_DEBUG
  log_debug("Processed %d audio samples", num_samples);
#endif
}

// Removed maybe_size_update - clients control their own size

// Handle the new unified ASCII frame packet (server sends this now)
static void handle_ascii_frame_packet(const void *data, size_t len) {
  if (!data || len < sizeof(ascii_frame_packet_t)) {
    log_warn("Invalid ASCII frame packet size: %zu", len);
    return;
  }

  // Extract header from the packet
  ascii_frame_packet_t header;
  memcpy(&header, data, sizeof(ascii_frame_packet_t));

  // Convert from network byte order
  header.width = ntohl(header.width);
  header.height = ntohl(header.height);
  header.original_size = ntohl(header.original_size);
  header.compressed_size = ntohl(header.compressed_size);
  header.checksum = ntohl(header.checksum);
  header.flags = ntohl(header.flags);

  // Get the frame data (starts after the header)
  const char *frame_data_ptr = (const char *)data + sizeof(ascii_frame_packet_t);
  size_t frame_data_len = len - sizeof(ascii_frame_packet_t);

  char *frame_data = NULL;

  // Handle compression if needed
  if (header.flags & FRAME_FLAG_IS_COMPRESSED && header.compressed_size > 0) {
    // Compressed frame - decompress it
    if (frame_data_len != header.compressed_size) {
      log_error("Compressed frame size mismatch: expected %u, got %zu", header.compressed_size, frame_data_len);
      return;
    }

    SAFE_MALLOC(frame_data, header.original_size + 1, char *);

    // Decompress using zlib
    uLongf decompressed_size = header.original_size;
    int result = uncompress((Bytef *)frame_data, &decompressed_size, (const Bytef *)frame_data_ptr, frame_data_len);

    if (result != Z_OK || decompressed_size != header.original_size) {
      log_error("Decompression failed: zlib error %d, size %lu vs expected %u", result, decompressed_size,
                header.original_size);
      free(frame_data);
      return;
    }

    frame_data[header.original_size] = '\0';
#ifdef COMPRESSION_DEBUG
    log_debug("Decompressed frame: %zu -> %u bytes", frame_data_len, header.original_size);
#endif
  } else {
    // Uncompressed frame
    if (frame_data_len != header.original_size) {
      log_error("Uncompressed frame size mismatch: expected %u, got %zu", header.original_size, frame_data_len);
      return;
    }

    SAFE_MALLOC(frame_data, frame_data_len + 1, char *);
    memcpy(frame_data, frame_data_ptr, frame_data_len);
    frame_data[frame_data_len] = '\0';
  }

  // Verify checksum
  uint32_t actual_crc = asciichat_crc32(frame_data, header.original_size);
  if (actual_crc != header.checksum) {
    log_error("Frame checksum mismatch: got 0x%x, expected 0x%x", actual_crc, header.checksum);
    free(frame_data);
    return;
  }

  // Process and display the frame
  static uint32_t last_width = 0;
  static uint32_t last_height = 0;

  // Check if frame dimensions have changed
  if (header.width > 0 && header.height > 0) {
    if (header.width != last_width || header.height != last_height) {
      log_info("Frame size changed from %ux%u to %ux%u", last_width, last_height, header.width, header.height);
      last_width = header.width;
      last_height = header.height;
    }
  }

  // Position cursor at top-left and display frame (preserve last frame instead of clearing)
  printf("\033[H%s", frame_data);
  fflush(stdout);

  // Record frame for debugging (track client-side frame reception)
  frame_debug_record_frame(&g_client_frame_debug, frame_data, header.original_size);

  free(frame_data);
}

static void *data_reception_thread_func(void *arg) {
  (void)arg;

#ifdef DEBUG_THREADS
  log_debug("Data reception thread started");
#endif
  log_info("CLIENT: Data reception thread started");

  while (!g_should_exit) {
    if (sockfd == 0) {
      log_debug("CLIENT: Waiting for socket connection (sockfd=0)");
      usleep(10 * 1000);
      continue;
    }
    log_debug("CLIENT: About to receive packet from server (sockfd=%d)", sockfd);

    packet_type_t type;
    void *data;
    size_t len;

    int result = receive_packet(sockfd, &type, &data, &len);
    if (result < 0) {
      log_error("CLIENT: Failed to receive packet, errno=%d (%s)", errno, strerror(errno));
      g_connection_lost = true;
      break;
    } else if (result == 0) {
      log_info("CLIENT: Server closed connection");
      g_connection_lost = true;
      break;
    }

    log_debug("CLIENT: Received packet type=%d, len=%zu", type, len);

    switch (type) {
    case PACKET_TYPE_ASCII_FRAME:
      // Unified frame packet from server
      log_debug("CLIENT: Processing ASCII frame packet (len=%zu)", len);
      handle_ascii_frame_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO:
      handle_audio_packet(data, len);
      break;

    case PACKET_TYPE_PING:
      // Respond with PONG
      if (send_pong_packet(sockfd) < 0) {
        log_error("Failed to send PONG response");
      }
      break;

    case PACKET_TYPE_PONG:
      // INFO: Nothing to do here. We win. Server acknowledged our PING
      // log_debug("Received PONG from server");
      break;

    case PACKET_TYPE_CLEAR_CONSOLE:
      // Server requested console clear
      console_clear();
      log_info("Console cleared by server");
      break;

    case PACKET_TYPE_SERVER_STATE:
      handle_server_state_packet(data, len);
      break;

    default:
      log_warn("Unknown packet type: %d", type);
      break;
    }

    buffer_pool_free(data, len);
  }

  log_info("CLIENT: Data reception thread stopped (g_should_exit=%d, g_connection_lost=%d)", g_should_exit,
           g_connection_lost);
#ifdef DEBUG_THREADS
  log_debug("Data reception thread stopped");
#endif
  g_data_thread_exited = true;
  return NULL;
}

static void *ping_thread_func(void *arg) {
  (void)arg;

#ifdef DEBUG_THREADS
  log_debug("Ping thread started");
#endif

  while (!g_should_exit && !g_connection_lost) {
    // Check if socket is still valid before sending
    if (sockfd <= 0) {
      log_debug("Socket closed, exiting ping thread");
      break;
    }

    // Send ping packet every 3 seconds to keep connection alive (server timeout is 5 seconds)
    if (send_ping_packet(sockfd) < 0) {
      log_debug("Failed to send ping packet");
      // Set connection lost flag so main loop knows to reconnect
      g_connection_lost = true;
      break;
    }

    // Wait 3 seconds before next ping
    for (int i = 0; i < 3 && !g_should_exit && !g_connection_lost && sockfd > 0; i++) {
      usleep(1000 * 1000); // 1 second
    }
  }

#ifdef DEBUG_THREADS
  log_debug("Ping thread stopped");
#endif
  g_ping_thread_exited = true;
  return NULL;
}

static void *webcam_capture_thread_func(void *arg) {
  (void)arg;

  struct timespec last_capture_time = {0, 0};

  log_info("Webcam capture thread started");

  while (!g_should_exit && !g_connection_lost) {
    if (sockfd <= 0) {
      usleep(100 * 1000); // Wait for connection
      continue;
    }

    // Frame rate limiting
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - last_capture_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - last_capture_time.tv_nsec) / 1000000;

    if (elapsed_ms < FRAME_INTERVAL_MS) {
      usleep((FRAME_INTERVAL_MS - elapsed_ms) * 1000);
      continue;
    }

    // Capture raw image from webcam
    image_t *image = webcam_read();
    if (!image) {
      log_info("No frame available from webcam yet (webcam_read returned NULL)");
      usleep(10000); // 10ms delay before retry
      continue;
    }

    log_info("[CLIENT CAPTURE] Webcam frame: %dx%d, aspect: %.3f", image->w, image->h,
             (float)image->w / (float)image->h);

    // Resize image to a reasonable size for network transmission
    // We want to send images large enough for the server to resize for any client
    // But not so large that they waste bandwidth
    // Let's target 400x300 as a reasonable maximum that gives server flexibility
    // This is roughly 2x typical terminal sizes
    // Always maintain original aspect ratio

    ssize_t max_width = 800;  // Reduced size to avoid large packets
    ssize_t max_height = 600; // This is fine for macbook pros using Kitty.app
    ssize_t resized_width, resized_height;

    // Calculate dimensions maintaining aspect ratio
    float img_aspect = (float)image->w / (float)image->h;

    // Fit within max bounds while maintaining aspect ratio
    if (image->w > max_width || image->h > max_height) {
      // Image needs resizing
      if ((float)max_width / (float)max_height > img_aspect) {
        // Max box is wider than image aspect - fit to height
        resized_height = max_height;
        resized_width = (ssize_t)(max_height * img_aspect);
      } else {
        // Max box is taller than image aspect - fit to width
        resized_width = max_width;
        resized_height = (ssize_t)(max_width / img_aspect);
      }
    } else {
      // Image is already small enough, send as-is
      resized_width = image->w;
      resized_height = image->h;
    }

    // log_info("[CLIENT RESIZE] Max: %ldx%ld, Resized to: %ldx%ld, aspect: %.3f", max_width, max_height, resized_width,
    //          resized_height, (float)resized_width / (float)resized_height);

    image_t *resized = NULL;
    if (image->w != resized_width || image->h != resized_height) {
      resized = image_new(resized_width, resized_height);
      if (resized) {
        image_resize(image, resized);
        image_destroy(image);
        image = resized;
      }
    }

    // Serialize image data for transmission
    // Format: [width:4][height:4][rgb_data:w*h*3]
    size_t rgb_size = (size_t)image->w * (size_t)image->h * sizeof(rgb_t);
    size_t packet_size = sizeof(uint32_t) * 2 + rgb_size; // width + height + pixels

    // Check packet size before allocation
    if (packet_size > MAX_PACKET_SIZE) {
      log_error("Packet too large: %zu bytes (max %d)", packet_size, MAX_PACKET_SIZE);
      image_destroy(image);
      continue;
    }

    uint8_t *packet_data = NULL;
    SAFE_MALLOC(packet_data, packet_size, uint8_t *);
    if (!packet_data) {
      log_error("Failed to allocate packet buffer of size %zu", packet_size);
      image_destroy(image);
      continue;
    }

    // Pack the image data
    uint32_t width_net = htonl(image->w);
    uint32_t height_net = htonl(image->h);
    memcpy(packet_data, &width_net, sizeof(uint32_t));
    memcpy(packet_data + sizeof(uint32_t), &height_net, sizeof(uint32_t));
    memcpy(packet_data + sizeof(uint32_t) * 2, image->pixels, rgb_size);

    // Check if socket is still valid before sending
    if (sockfd <= 0) {
      log_debug("Socket closed, stopping video send");
      free(packet_data);
      image_destroy(image);
      break;
    }

    // Send image data to server via IMAGE_FRAME packet
    log_info("[CLIENT SEND] Sending frame: %dx%d, size=%zu bytes", image->w, image->h, packet_size);
    if (send_packet(sockfd, PACKET_TYPE_IMAGE_FRAME, packet_data, packet_size) < 0) {
      log_error("Failed to send video frame to server: %s", strerror(errno));
      // This is likely why we're disconnecting - set connection lost
      g_connection_lost = true;
      free(packet_data);
      image_destroy(image);
      break;
    }

    // Update capture time
    last_capture_time = current_time;

    free(packet_data);
    image_destroy(image);
  }

  log_info("Webcam capture thread stopped");
  g_capture_thread_exited = true;
  return NULL;
}

static void *audio_capture_thread_func(void *arg) {
  (void)arg;

  log_info("Audio capture thread started");

  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];

  // Audio batching buffers
  float batch_buffer[AUDIO_BATCH_SAMPLES]; // Buffer for accumulating samples
  int batch_samples_collected = 0;         // How many samples we've collected
  int batch_chunks_collected = 0;          // How many chunks we've collected

  // Audio processing components
  static noise_gate_t noise_gate;
  static highpass_filter_t hp_filter;
  static bool processors_initialized = false;

  // Initialize audio processors on first run
  if (!processors_initialized) {
    noise_gate_init(&noise_gate, AUDIO_SAMPLE_RATE); // Use correct sample rate
    noise_gate_set_params(&noise_gate, 0.01f, 2.0f, 50.0f, 0.9f);

    highpass_filter_init(&hp_filter, 80.0f, AUDIO_SAMPLE_RATE); // Use correct sample rate

    processors_initialized = true;
  }

  while (!g_should_exit && !g_connection_lost) {
    if (sockfd <= 0) {
      usleep(100 * 1000); // Wait for connection
      continue;
    }

    // Read audio samples from microphone
    int samples_read = audio_read_samples(&g_audio_context, audio_buffer, AUDIO_SAMPLES_PER_PACKET);

    if (samples_read > 0) {
      // Apply audio processing chain
      // 1. High-pass filter to remove low-frequency rumble
      highpass_filter_process_buffer(&hp_filter, audio_buffer, samples_read);

      // 2. Noise gate to eliminate background noise
      noise_gate_process_buffer(&noise_gate, audio_buffer, samples_read);

      // 3. Soft clipping to prevent harsh distortion
      soft_clip_buffer(audio_buffer, samples_read, 0.95f);

      // Only batch if gate is open (reduces network traffic for silence)
      if (noise_gate_is_open(&noise_gate)) {
        // Copy processed samples to batch buffer
        memcpy(&batch_buffer[batch_samples_collected], audio_buffer, samples_read * sizeof(float));
        batch_samples_collected += samples_read;
        batch_chunks_collected++;

        // Send batch when we have enough chunks or buffer is full
        if (batch_chunks_collected >= AUDIO_BATCH_COUNT ||
            batch_samples_collected + AUDIO_SAMPLES_PER_PACKET > AUDIO_BATCH_SAMPLES) {

          (void)send_audio_packet; // suppress warning about unused function
          if (send_audio_batch_packet(sockfd, batch_buffer, batch_samples_collected, batch_chunks_collected) < 0) {
            log_debug("Failed to send audio batch to server");
            // Don't set g_connection_lost here as receive thread will detect it
          }
#ifdef AUDIO_DEBUG
          else {
            log_debug("Sent audio batch: %d chunks, %d total samples (gate: %s)", batch_chunks_collected,
                      batch_samples_collected, noise_gate_is_open(&noise_gate) ? "open" : "closed");
          }
#endif
          // Reset batch counters
          batch_samples_collected = 0;
          batch_chunks_collected = 0;
        }
      } else if (batch_samples_collected > 0) {
        // Gate closed but we have pending samples - send what we have
        if (send_audio_batch_packet(sockfd, batch_buffer, batch_samples_collected, batch_chunks_collected) < 0) {
          log_debug("Failed to send final audio batch to server");
        }
#ifdef AUDIO_DEBUG
        else {
          log_debug("Sent final audio batch before silence: %d chunks, %d samples", batch_chunks_collected,
                    batch_samples_collected);
        }
#endif
        batch_samples_collected = 0;
        batch_chunks_collected = 0;
      }
    } else {
      // Small delay if no audio available
      usleep(5 * 1000); // 5ms
    }
  }

  log_info("Audio capture thread stopped");
  g_audio_capture_thread_exited = true;
  return NULL;
}

/* ============================================================================
 * Multi-User Packet Handlers
 * ============================================================================
 */

static void handle_server_state_packet(const void *data, size_t len) {
  if (!data || len != sizeof(server_state_packet_t)) {
    log_error("Invalid server state packet size: %zu", len);
    return;
  }

  const server_state_packet_t *state = (const server_state_packet_t *)data;

  // Convert from network byte order
  uint32_t connected_count = ntohl(state->connected_client_count);
  uint32_t active_count = ntohl(state->active_client_count);

  log_info("Server state: %u connected clients, %u active clients", connected_count, active_count);

  // Check if connected count changed - if so, clear console
  if (g_server_state_initialized) {
    if (g_last_active_count != active_count) {
      log_info("Active client count changed from %u to %u - clearing console", g_last_active_count, active_count);
      console_clear();
    }
  } else {
    // First state packet received
    g_server_state_initialized = true;
    log_info("Initial server state received: %u connected clients", connected_count);
  }

  g_last_active_count = active_count;
}

/* ============================================================================
 * Multi-User Local Capture Threads (Future Implementation)
 * ============================================================================
 */

// NOTE: Local capture thread functions will be implemented in future commits
// when actual local video/audio capture functionality is added to clients.

int main(int argc, char *argv[]) {
  log_init("client.log", LOG_DEBUG);
  atexit(log_destroy);

#ifdef DEBUG_MEMORY
  atexit(debug_memory_report);
#endif

  // Initialize global shared buffer pool
  data_buffer_pool_init_global();
  atexit(data_buffer_pool_cleanup_global);
  log_truncate_if_large(); /* Truncate if log is already too large */
  log_info("ASCII Chat client starting...");

  // Initialize frame debugging
  frame_debug_init(&g_client_frame_debug, "Client-FrameReceiver");
  g_frame_debug_enabled = true; // Enable debugging by default
  g_frame_debug_verbosity = 2;  // Show issues and warnings

  options_init(argc, argv);
  char *address = opt_address;
  int port = strtoint(opt_port);

  struct sockaddr_in serv_addr;

  // Cleanup nicely on Ctrl+C.
  signal(SIGINT, sigint_handler);

  // Handle terminal resize events
  signal(SIGWINCH, sigwinch_handler);

  // Ignore SIGPIPE - we'll handle write errors ourselves
  signal(SIGPIPE, SIG_IGN);

  // Initialize ASCII output for this connection
  ascii_write_init();

  // Disable terminal log output to prevent flickering with ASCII frame display
  log_set_terminal_output(false);

  // Initialize luminance palette for ASCII conversion
  precalc_luminance_palette();

  // Initialize webcam capture
  int webcam_index = opt_webcam_index;
  if (ascii_read_init(webcam_index) != ASCIICHAT_OK) {
    log_fatal("Failed to initialize webcam capture");
    ascii_write_destroy();
    exit(ASCIICHAT_ERR_WEBCAM);
  }
  log_info("Webcam initialized successfully");

  // Initialize audio if enabled
  if (opt_audio_enabled) {
    if (audio_init(&g_audio_context) != 0) {
      log_fatal("Failed to initialize audio system");
      ascii_write_destroy();
      exit(ASCIICHAT_ERR_AUDIO);
    }

    if (audio_start_playback(&g_audio_context) != 0) {
      log_error("Failed to start audio playback");
      audio_destroy(&g_audio_context);
      ascii_write_destroy();
      exit(ASCIICHAT_ERR_AUDIO);
    }

    if (audio_start_capture(&g_audio_context) != 0) {
      log_error("Failed to start audio capture");
      audio_destroy(&g_audio_context);
      ascii_write_destroy();
      exit(ASCIICHAT_ERR_AUDIO);
    }

    log_info("Audio system initialized with capture and playback");
  }

  /* Connection and reconnection loop */
  int reconnect_attempt = 0;

  while (!g_should_exit) {
    if (g_should_reconnect) {
      // Connection broken - will loop back to reconnection logic
      log_info("Connection terminated, preparing to reconnect...");
      if (reconnect_attempt == 0) {
        console_clear();
      }
      reconnect_attempt++;
    }

    if (g_first_connection || g_should_reconnect) {
      // Close any existing socket before attempting new connection
      if (0 > (sockfd = close_socket(sockfd))) {
        exit(ASCIICHAT_ERR_NETWORK);
      }

      if (reconnect_attempt > 0) {
        float delay = get_reconnect_delay(reconnect_attempt);
        log_info("Reconnection attempt #%d to %s:%d in %.2f seconds...", reconnect_attempt, address, port,
                 delay / 1000 / 1000);
        usleep(delay);
      } else {
        log_info("Connecting to %s:%d", address, port);
      }

      // try to open a socket
      if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        log_error("Error: could not create socket: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }

      // reserve memory space to store IP address
      memset(&serv_addr, '0', sizeof(serv_addr));

      // set type of address to IPV4
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_port = htons(port);

      // an error occurred when trying to set server address and port number
      if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
        log_error("Error: couldn't set the server address and port number: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }

      // Try to connect to the server
      if (!connect_with_timeout(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr), CONNECT_TIMEOUT)) {
        log_warn("Connection failed: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }

      // Connection successful!
      log_info("Connected to server %s:%d", address, port);
      log_info("CLIENT: Socket connection established (sockfd=%d)", sockfd);
      reconnect_attempt = 0; // Reset reconnection counter on successful connection

      struct sockaddr_in local_addr;
      socklen_t addr_len = sizeof(local_addr);
      if (getsockname(sockfd, (struct sockaddr *)&local_addr, &addr_len) == -1) {
        perror("getsockname");
        exit(1);
      }
      int local_port = ntohs(local_addr.sin_port);
      log_info("Local port: %d", local_port);

      g_my_client_id = local_port;

      // Send initial terminal size to server
      if (send_size_packet(sockfd, opt_width, opt_height) < 0) {
        log_error("Failed to send initial size to server: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }
      log_info("Sent initial size to server: %ux%u", opt_width, opt_height);

      // Send client join packet for multi-user support
      uint32_t my_capabilities = CLIENT_CAP_VIDEO; // Basic video capability
      if (opt_audio_enabled) {
        my_capabilities |= CLIENT_CAP_AUDIO; // Add audio if enabled
      }
      if (opt_color_output) {
        my_capabilities |= CLIENT_CAP_COLOR; // Add color if enabled
      }
      if (opt_stretch) {
        my_capabilities |= CLIENT_CAP_STRETCH; // Add stretch if enabled
      }

      char *os_username = getenv("USER");
      char *display_name = os_username;
      if (!os_username || strcmp(os_username, "") == 0) {
        display_name = ASCIICHAT_DEFAULT_DISPLAY_NAME;
      }
      char my_display_name[MAX_DISPLAY_NAME_LEN];
      int pid = getpid();
      snprintf(my_display_name, sizeof(my_display_name), "%s-%d", display_name, pid);

      if (send_client_join_packet(sockfd, my_display_name, my_capabilities) < 0) {
        log_error("Failed to send client join packet: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }
      log_info("Sent client join packet with display name: %s, capabilities: video=%s, audio=%s, color=%s, stretch=%s",
               my_display_name, (my_capabilities & CLIENT_CAP_VIDEO) ? "yes" : "no",
               (my_capabilities & CLIENT_CAP_AUDIO) ? "yes" : "no", (my_capabilities & CLIENT_CAP_COLOR) ? "yes" : "no",
               (my_capabilities & CLIENT_CAP_STRETCH) ? "yes" : "no");

      // Set socket keepalive to detect broken connections
      if (set_socket_keepalive(sockfd) < 0) {
        log_warn("Failed to set socket keepalive: %s", network_error_string(errno));
      }

      // Reset connection lost flag for new connection
      g_connection_lost = false;

      // Reset server state tracking for new connection
      g_server_state_initialized = false;
      g_last_active_count = 0;

      // Start data reception thread
      g_data_thread_exited = false; // Reset exit flag for new connection
      if (pthread_create(&g_data_thread, NULL, data_reception_thread_func, NULL) != 0) {
        log_error("Failed to create data reception thread");
        g_should_reconnect = true;
        continue;
      } else {
        g_data_thread_created = true;
      }

      // Start ping thread for keepalive
      g_ping_thread_exited = false; // Reset exit flag for new connection
      if (pthread_create(&g_ping_thread, NULL, ping_thread_func, NULL) != 0) {
        log_error("Failed to create ping thread");
        g_should_reconnect = true;
        continue;
      } else {
        g_ping_thread_created = true;
      }

      // Start webcam capture thread
      g_capture_thread_exited = false;
      if (pthread_create(&g_capture_thread, NULL, webcam_capture_thread_func, NULL) != 0) {
        log_error("Failed to create webcam capture thread");
        g_should_reconnect = true;
        continue;
      } else {
        g_capture_thread_created = true;
        log_info("Webcam capture thread started");

        // Notify server we're starting to send video
        if (send_stream_start_packet(sockfd, STREAM_TYPE_VIDEO) < 0) {
          log_error("Failed to send stream start packet");
        }
      }

      // Start audio capture thread if audio is enabled
      if (opt_audio_enabled) {
        g_audio_capture_thread_exited = false;
        if (pthread_create(&g_audio_capture_thread, NULL, audio_capture_thread_func, NULL) != 0) {
          log_error("Failed to create audio capture thread");
          // Non-fatal, continue without audio
        } else {
          g_audio_capture_thread_created = true;
          log_info("Audio capture thread started");

          // Notify server we're starting to send audio
          (void)safe_send_stream_stop_packet; // unused function - stop the compiler warning with this void
          if (safe_send_stream_start_packet(sockfd, STREAM_TYPE_AUDIO) < 0) {
            log_error("Failed to send audio stream start packet");
          }
        }
      }

      g_first_connection = false;
      g_should_reconnect = false;
    }

    // Connection monitoring loop - wait for connection to break or shutdown
    while (!g_should_exit && sockfd > 0 && !g_connection_lost) {
      // Check if data thread has exited (indicates connection lost)
      if (g_data_thread_exited) {
        log_info("Data thread exited, connection lost");
        break;
      }

      usleep(100 * 1000); // 0.1 second
    }

    if (g_should_exit) {
      log_info("Shutdown requested, exiting...");
      break;
    }

    // Connection broken, attempt to reconnect
    log_info("Connection lost. Attempting to reconnect...");
    g_should_reconnect = true;

    // Close the socket to signal threads to exit
    if (sockfd > 0) {
      close(sockfd);
      sockfd = 0;
    }

    // Clean up data thread for this connection
    if (g_data_thread_created) {
      pthread_join(g_data_thread, NULL);
      g_data_thread_created = false;
    }

    // Clean up ping thread for this connection
    if (g_ping_thread_created) {
      pthread_join(g_ping_thread, NULL);
      g_ping_thread_created = false;
    }

    // Clean up capture thread for this connection
    if (g_capture_thread_created) {
      pthread_join(g_capture_thread, NULL);
      g_capture_thread_created = false;
    }

    // Clean up audio capture thread for this connection
    if (g_audio_capture_thread_created) {
      pthread_join(g_audio_capture_thread, NULL);
      g_audio_capture_thread_created = false;
    }
  }

  shutdown_client();
  return 0;
}

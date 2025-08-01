#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "image.h"
#include "ascii.h"
#include "common.h"
#include "network.h"
#include "options.h"
#include "ringbuffer.h"
#include "compression.h"
#include "audio.h"

/* ============================================================================
 * Global State
 * ============================================================================
 */

static volatile bool g_should_exit = false;
static framebuffer_t *g_frame_buffer = NULL;
static pthread_t g_capture_thread;
static pthread_t g_audio_thread;
static bool g_audio_thread_created = false;
static volatile bool g_audio_send_failed = false;
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_socket_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_framebuffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static audio_context_t g_audio_context = {0};

/* Performance statistics */
typedef struct {
  uint64_t frames_captured;
  uint64_t frames_sent;
  uint64_t frames_dropped;
  uint64_t bytes_sent;
  double avg_capture_fps;
  double avg_send_fps;
} server_stats_t;

static server_stats_t g_stats = {0};

static int listenfd = 0;
static int connfd = 0;

/* ============================================================================
 * Signal Handlers
 * ============================================================================
 */

static void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  // Server terminal resize - we ignore this since we use client's terminal size
  // Only log that the event occurred
  log_debug("Server terminal resized (ignored - using client terminal size)");
}

static void sigint_handler(int sigint) {
  (void)(sigint);
  g_should_exit = true;
  log_info("Server shutdown requested");

  // Close listening socket to interrupt accept()
  if (listenfd > 0) {
    close(listenfd);
  }

  // Close client socket to interrupt send/recv operations
  if (connfd > 0) {
    close(connfd);
  }
}

/* ============================================================================
 * Frame Capture Thread
 * ============================================================================
 */

static void *webcam_capture_thread_func(void *arg) {
  (void)arg;

  struct timespec last_capture_time = {0, 0};
  uint64_t frames_captured = 0;

  log_info("Frame capture thread started");

  while (!g_should_exit) {
    if (connfd == 0) {
      usleep(100 * 1000);
      continue;
    }

    // Frame rate limiting
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - last_capture_time.tv_sec) * 999 +
                      (current_time.tv_nsec - last_capture_time.tv_nsec) / 999999;

    int frame_interval = get_frame_interval_ms();
    if (elapsed_ms < frame_interval) {
      usleep((frame_interval - elapsed_ms) * 1000);
      continue;
    }

    // Capture frame
    char *frame = ascii_read();
    if (!frame) {
      log_error("Failed to capture frame from webcam");
      usleep(100000); // 100ms delay before retry
      continue;
    }

    // Try to add frame to buffer (with mutex protection)
    pthread_mutex_lock(&g_framebuffer_mutex);
    bool buffered = g_frame_buffer ? framebuffer_write_frame(g_frame_buffer, frame) : false;
    pthread_mutex_unlock(&g_framebuffer_mutex);

    pthread_mutex_lock(&g_stats_mutex);
    g_stats.frames_captured++;
    if (!buffered) {
      g_stats.frames_dropped++;
      if (g_stats.frames_dropped % 100 == 0) {
        log_debug("Frame buffer full, dropped frame %lu", g_stats.frames_captured);
      }
    }
    pthread_mutex_unlock(&g_stats_mutex);

    free(frame);
    frames_captured++;
    last_capture_time = current_time;

    // Update FPS statistics every 30 frames
    if (frames_captured % 30 == 0) {
      double fps = 30000.0 / elapsed_ms; // Convert to FPS
      pthread_mutex_lock(&g_stats_mutex);
      g_stats.avg_capture_fps = (g_stats.avg_capture_fps * 0.9) + (fps * 0.1);
      pthread_mutex_unlock(&g_stats_mutex);
    }
  }

  log_info("Frame capture thread stopped");
  return NULL;
}

/* ============================================================================
 * Audio Thread
 * ============================================================================
 */

static void *audio_thread_func(void *arg) {
  (void)arg;

  log_info("Audio thread started");

  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];

  while (!g_should_exit && opt_audio_enabled) {
    if (connfd == 0) {
      usleep(10 * 1000);
      continue;
    }

    int samples_read = audio_read_samples(&g_audio_context, audio_buffer, AUDIO_SAMPLES_PER_PACKET);
    if (samples_read > 0) {
      pthread_mutex_lock(&g_socket_mutex);
      if (send_audio_packet(connfd, audio_buffer, samples_read) < 0) {
        pthread_mutex_unlock(&g_socket_mutex);
        log_error("Failed to send audio packet");
        g_audio_send_failed = true;
        break; // Exit on network error
      }
      pthread_mutex_unlock(&g_socket_mutex);
#ifdef AUDIO_DEBUG
      log_debug("Sent %d audio samples", samples_read);
#endif
    } else {
      // Only sleep if no audio available to reduce latency
      usleep(2 * 1000); // 2ms instead of 10ms when no audio
    }

    // Quick shutdown check without additional delay
    if (g_should_exit)
      break;
  }

  log_info("Audio thread stopped");
  return NULL;
}

/* ============================================================================
 * Client Size Handling
 * ============================================================================
 */

int receive_client_size(int sockfd, unsigned short *width, unsigned short *height) {
  // Try to peek for a packet header to see if there's a size packet
  packet_header_t header;
  ssize_t peeked = recv(sockfd, &header, sizeof(header), MSG_PEEK | MSG_DONTWAIT);
  if (peeked <= 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0; // No data available (non-blocking)
    }
    return -1; // Error or connection closed
  }

  if (peeked < (ssize_t)sizeof(header)) {
    return 0; // Not enough data for a complete header
  }

  // Check if this is a size packet
  uint32_t magic = ntohl(header.magic);
  uint16_t type = ntohs(header.type);

  if (magic != PACKET_MAGIC || type != PACKET_TYPE_SIZE) {
    return 0; // Not a size packet
  }

  // Receive the complete packet
  packet_type_t pkt_type;
  void *data;
  size_t len;

  int result = receive_packet(sockfd, &pkt_type, &data, &len);
  if (result <= 0) {
    return result; // Error or connection closed
  }

  if (pkt_type != PACKET_TYPE_SIZE || len != 4) {
    free(data);
    return 0; // Invalid size packet
  }

  const uint16_t *size_data = (const uint16_t *)data;
  *width = ntohs(size_data[0]);
  *height = ntohs(size_data[1]);

  free(data);
  return 1; // Successfully parsed size packet
}

void update_frame_buffer_for_size(unsigned short width, unsigned short height) {
  // Update global dimensions
  opt_width = width;
  opt_height = height;
  // Mark these dimensions as "auto" so aspect_ratio() will treat them as limits
  auto_width = 1;
  auto_height = 1;

  // Calculate required buffer size for this client size
  size_t required_size = FRAME_BUFFER_SIZE_BASE(width, height) * 3 / 2;
  if (required_size < FRAME_BUFFER_SIZE_MIN) {
    required_size = FRAME_BUFFER_SIZE_MIN;
  } else if (required_size > FRAME_BUFFER_SIZE_MAX) {
    required_size = FRAME_BUFFER_SIZE_MAX;
  }

  // Lock framebuffer access to prevent race conditions with capture thread
  pthread_mutex_lock(&g_framebuffer_mutex);

  // Check if we need to recreate the framebuffer
  if (!g_frame_buffer || g_frame_buffer->max_frame_size < required_size) {
    log_info("Recreating framebuffer for client size %ux%u (need %zu bytes, had %zu)", width, height, required_size,
             g_frame_buffer ? g_frame_buffer->max_frame_size : 0);

    // Create new buffer first to minimize the NULL window
    framebuffer_t *new_buffer = framebuffer_create(FRAME_BUFFER_CAPACITY, required_size);
    if (!new_buffer) {
      pthread_mutex_unlock(&g_framebuffer_mutex);
      log_fatal("Failed to create new frame buffer for size %ux%u", width, height);
      exit(ASCIICHAT_ERR_MALLOC);
    }

    // Atomically swap buffers to minimize race window
    framebuffer_t *old_buffer = g_frame_buffer;
    g_frame_buffer = new_buffer;

    // Destroy old buffer after swap
    if (old_buffer) {
      framebuffer_destroy(old_buffer);
    }
  } else {
    // Buffer is large enough, just clear existing frames
    ringbuffer_clear(g_frame_buffer->rb);
    log_info("Adjusted frame generation for client size: %ux%u (buffer reused)", width, height);
  }

  pthread_mutex_unlock(&g_framebuffer_mutex);
}

/* ============================================================================
 * Main Server Logic
 * ============================================================================
 */

int main(int argc, char *argv[]) {
  log_init("server.log", LOG_DEBUG);
  log_truncate_if_large(); /* Truncate if log is already too large */
  log_info("ASCII Chat server starting...");

  options_init(argc, argv);
  int port = strtoint(opt_port);
  unsigned short int webcam_index = opt_webcam_index;

  precalc_luminance_palette();
  precalc_rgb_palettes(weight_red, weight_green, weight_blue);

  // Handle terminal resize events
  signal(SIGWINCH, sigwinch_handler);
  // Handle Ctrl+C for cleanup
  signal(SIGINT, sigint_handler);
  // Ignore SIGPIPE
  signal(SIGPIPE, SIG_IGN);

  // Initialize webcam
  if (ascii_read_init(webcam_index) != ASCIICHAT_OK) {
    log_fatal("Failed to initialize webcam");
    exit(ASCIICHAT_OK);
  }

  // Create initial frame buffer with minimum size - will be resized when client connects
  g_frame_buffer = framebuffer_create(FRAME_BUFFER_CAPACITY, FRAME_BUFFER_SIZE_MIN);
  if (!g_frame_buffer) {
    log_fatal("Failed to create initial frame buffer");
    ascii_read_destroy();
    exit(ASCIICHAT_ERR_MALLOC);
  }
  log_info("Created initial framebuffer with %zu bytes (will resize for client)", FRAME_BUFFER_SIZE_MIN);

  // Initialize audio if enabled
  if (opt_audio_enabled) {
    if (audio_init(&g_audio_context) != 0) {
      log_fatal("Failed to initialize audio system");
      framebuffer_destroy(g_frame_buffer);
      ascii_read_destroy();
      exit(ASCIICHAT_ERR_AUDIO);
    }

    if (audio_start_capture(&g_audio_context) != 0) {
      log_error("Failed to start audio capture");
      audio_destroy(&g_audio_context);
      framebuffer_destroy(g_frame_buffer);
      ascii_read_destroy();
      exit(ASCIICHAT_ERR_AUDIO);
    }

    log_info("Audio system initialized and capture started");
  }

  // Start capture thread
  if (pthread_create(&g_capture_thread, NULL, webcam_capture_thread_func, NULL) != 0) {
    log_fatal("Failed to create capture thread");
    framebuffer_destroy(g_frame_buffer);
    ascii_read_destroy();
    exit(ASCIICHAT_ERR_THREAD);
  }

  // Start audio thread if enabled
  if (opt_audio_enabled) {
    if (pthread_create(&g_audio_thread, NULL, audio_thread_func, NULL) != 0) {
      log_fatal("Failed to create audio thread");
      framebuffer_destroy(g_frame_buffer);
      ascii_read_destroy();
      audio_destroy(&g_audio_context);
      exit(ASCIICHAT_ERR_THREAD);
    }
    g_audio_thread_created = true;
  }

  // Network setup
  struct sockaddr_in serv_addr;
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    log_fatal("Failed to create socket: %s", strerror(errno));
    exit(1);
  }

  log_info("Server listening on port %d", port);

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  // Set socket options
  int yes = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    log_fatal("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    perror("setsockopt");
    exit(ASCIICHAT_ERR_NETWORK);
  }

  // If we Set keep-alive on the listener before accept(), connfd will inherit it.
  if (set_socket_keepalive(listenfd) < 0) {
    log_warn("Failed to set keep-alive on listener: %s", strerror(errno));
  }

  // Bind socket
  if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    log_fatal("Socket bind failed: %s", strerror(errno));
    perror("Error: network bind failed");
    exit(1);
  }

  // Listen for connections
  if (listen(listenfd, 10) < 0) {
    log_fatal("Connection listen failed: %s", strerror(errno));
    exit(1);
  }

  struct timespec last_stats_time = {0, 0};

  // Main connection loop
  while (!g_should_exit) {
    log_info("Waiting for client connection...");

    // Accept network connection with timeout
    connfd = accept_with_timeout(listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);
    if (connfd < 0) {
      if (errno == ETIMEDOUT) {
        log_debug("Network accept timeout, continuing...");
        continue;
      }
      log_error("Network accept failed: %s", network_error_string(errno));
      continue;
    }

    // Log client connection
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    log_info("Client connected from %s:%d", client_ip, ntohs(client_addr.sin_port));

    // Receive initial size packet from client
    unsigned short client_width, client_height;
    packet_type_t type;
    void *data;
    size_t len;

    int result = receive_packet(connfd, &type, &data, &len);
    if (result <= 0) {
      log_error("Failed to receive initial size packet: %s", network_error_string(errno));
      connfd = close(connfd);
      continue;
    }

    if (type != PACKET_TYPE_SIZE || len != 4) {
      log_error("Expected size packet but got type=%d, len=%zu", type, len);
      free(data);
      connfd = close(connfd);
      continue;
    }

    const uint16_t *size_data = (const uint16_t *)data;
    client_width = ntohs(size_data[0]);
    client_height = ntohs(size_data[1]);
    free(data);

    // Update frame generation for client size
    update_frame_buffer_for_size(client_width, client_height);
    log_info("Client requested frame size: %ux%u", client_width, client_height);

    // Reset client-specific stats and flags
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.frames_sent = 0;
    g_stats.bytes_sent = 0;
    pthread_mutex_unlock(&g_stats_mutex);
    g_audio_send_failed = false; // Reset audio failure flag for new client

    // Allocate frame buffer dynamically based on color mode
    char *frame_buffer;
    SAFE_MALLOC(frame_buffer, FRAME_BUFFER_SIZE_FINAL, char *);
    uint64_t client_frames_sent = 0;

    // Client serving loop
    while (!g_should_exit) {
      // Check for size updates from client (non-blocking)
      unsigned short new_width, new_height;
      int size_result = receive_client_size(connfd, &new_width, &new_height);
      if (size_result > 0) {
        // Client sent a size update
        update_frame_buffer_for_size(new_width, new_height);
        log_info("Client updated frame size to: %ux%u", new_width, new_height);
      } else if (size_result < 0) {
        // Error receiving data, client likely disconnected
        log_info("Client disconnected while checking for size updates");
        break;
      }

      // Check if audio thread failed
      if (g_audio_send_failed) {
        log_info("Audio send failed, disconnecting client");
        break;
      }

      // Try to get a frame from buffer (with mutex protection)
      pthread_mutex_lock(&g_framebuffer_mutex);
      bool frame_available = g_frame_buffer ? framebuffer_read_frame(g_frame_buffer, frame_buffer) : false;
      pthread_mutex_unlock(&g_framebuffer_mutex);

      if (!frame_available) {
        // No frames available, wait a bit
        usleep(1000); // 1ms
        continue;
      }

      // Send frame with timeout (protected by socket mutex)
      size_t frame_len = strlen(frame_buffer);
      pthread_mutex_lock(&g_socket_mutex);
      int sent = send_compressed_frame(connfd, frame_buffer, frame_len);
      pthread_mutex_unlock(&g_socket_mutex);
      if (sent < 0) {
        log_error("Error: Network send of video packet failed: %s", network_error_string(errno));
        break;
      }

      // NOTE: this isn't actually an error, I don't think. send_with_timeout() supports partial sends.
      // if ((size_t)sent != frame_len) {
      //  log_error("Partial send: %zd of %zu bytes", sent, frame_len);
      //}

      // Update statistics
      pthread_mutex_lock(&g_stats_mutex);
      g_stats.frames_sent++;
      g_stats.bytes_sent += frame_len; // Approximate bytes sent (not counting packet overhead)
      pthread_mutex_unlock(&g_stats_mutex);

      client_frames_sent++;

      // Print periodic statistics
      struct timespec current_time;
      clock_gettime(CLOCK_MONOTONIC, &current_time);
      long stats_elapsed = (current_time.tv_sec - last_stats_time.tv_sec) * 1000 +
                           (current_time.tv_nsec - last_stats_time.tv_nsec) / 1000000;

      if (stats_elapsed >= 10000) { // Every 10 seconds
        pthread_mutex_lock(&g_stats_mutex);
        pthread_mutex_lock(&g_framebuffer_mutex);
        size_t buffer_size = g_frame_buffer ? ringbuffer_size(g_frame_buffer->rb) : 0;
        pthread_mutex_unlock(&g_framebuffer_mutex);
        log_info("Stats: captured=%lu, sent=%lu, dropped=%lu, buffer_size=%zu", g_stats.frames_captured,
                 g_stats.frames_sent, g_stats.frames_dropped, buffer_size);
        pthread_mutex_unlock(&g_stats_mutex);
        last_stats_time = current_time;
      }
    }

    connfd = close(connfd);
    log_info("Client disconnected after %lu frames", client_frames_sent);
    log_info("Closing connection.");
    log_info("---------------------");

    // Free the dynamically allocated frame buffer
    free(frame_buffer);
  }

  // Cleanup
  log_info("Server shutting down...");
  g_should_exit = true;

  // Wait for capture thread to finish
  pthread_join(g_capture_thread, NULL);

  // Wait for audio thread to finish if enabled
  if (opt_audio_enabled && g_audio_thread_created) {
    log_info("Waiting for audio thread to finish...");

    // Stop audio capture to help thread exit
    audio_stop_capture(&g_audio_context);

    // Wait for thread to exit
    pthread_join(g_audio_thread, NULL);

    audio_destroy(&g_audio_context);
    log_info("Audio thread joined and context destroyed");
  }

  // Cleanup resources
  pthread_mutex_lock(&g_framebuffer_mutex);
  if (g_frame_buffer) {
    framebuffer_destroy(g_frame_buffer);
    g_frame_buffer = NULL;
  }
  pthread_mutex_unlock(&g_framebuffer_mutex);

  ascii_read_destroy();
  close(listenfd);

  // Final statistics
  pthread_mutex_lock(&g_stats_mutex);
  log_info("Final stats: captured=%lu, sent=%lu, dropped=%lu", g_stats.frames_captured, g_stats.frames_sent,
           g_stats.frames_dropped);
  pthread_mutex_unlock(&g_stats_mutex);

  printf("Server shutdown complete.\n");

  // Destroy mutexes (do this before log_destroy in case logging uses them)
  pthread_mutex_destroy(&g_stats_mutex);
  pthread_mutex_destroy(&g_socket_mutex);
  pthread_mutex_destroy(&g_framebuffer_mutex);

  log_destroy();
  return 0;
}

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
static volatile bool g_capture_paused = false;       // Flag to pause capture during client operations
static volatile bool g_capture_thread_ready = false; // Flag to indicate capture thread is ready
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

/* ============================================================================
 * Multi-Client Support Structures
 * ============================================================================
 */

typedef struct {
  int socket;
  pthread_t receive_thread; // NEW: Thread for receiving client data
  pthread_t send_thread;    // Existing: Thread for sending data to client
  uint32_t client_id;
  char display_name[MAX_DISPLAY_NAME_LEN];
  char client_ip[INET_ADDRSTRLEN];
  int port;

  // Media capabilities
  bool can_send_video;
  bool can_send_audio;
  bool is_sending_video;
  bool is_sending_audio;

  // Stream dimensions
  unsigned short width, height;

  // Statistics
  bool active;
  time_t connected_at;
  uint64_t frames_sent;
  uint64_t frames_received; // NEW: Track incoming frames from this client

  // Buffers for incoming media (individual per client)
  framebuffer_t *incoming_video_buffer; // NEW: Buffer for this client's video
} client_info_t;

typedef struct {
  client_info_t clients[MAX_CLIENTS];
  int client_count;
  pthread_mutex_t mutex;
  uint32_t next_client_id; // NEW: For assigning unique IDs
} client_manager_t;

// Global multi-client state
static client_manager_t g_client_manager = {0};
static pthread_mutex_t g_client_manager_mutex = PTHREAD_MUTEX_INITIALIZER;

static server_stats_t g_stats = {0};

static int listenfd = 0;
static int connfd = 0;

/* ============================================================================
 * Multi-Client Function Declarations
 * ============================================================================
 */

// Thread functions
void *client_receive_thread_func(void *arg);
void *client_send_thread_func(void *arg);

// Client management functions
int add_client(int socket, const char *client_ip, int port);
int remove_client(uint32_t client_id);

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

  // Signal that capture thread is ready
  g_capture_thread_ready = true;

  while (!g_should_exit) {
    if (connfd == 0 || g_capture_paused) {
      usleep(100 * 1000);
      continue;
    }

    // Frame rate limiting
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - last_capture_time.tv_sec) * 1000 +
                      (current_time.tv_nsec - last_capture_time.tv_nsec) / 1000000;

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
    size_t frame_len = strlen(frame);

    // Lock both mutexes in consistent order: stats first, then framebuffer
    pthread_mutex_lock(&g_stats_mutex);
    pthread_mutex_lock(&g_framebuffer_mutex);

    // Extra safety check - make sure framebuffer exists and isn't being cleared
    bool buffered = false;
    if (g_frame_buffer && g_frame_buffer->rb && !g_capture_paused) {
      buffered = framebuffer_write_frame(g_frame_buffer, frame, frame_len);
    }

    pthread_mutex_unlock(&g_framebuffer_mutex);

    // Update stats while we still hold the stats mutex
    g_stats.frames_captured++;
    if (!buffered) {
      g_stats.frames_dropped++;
      if (g_stats.frames_dropped % 100 == 0) {
        log_debug("Frame buffer full, dropped frame %lu", g_stats.frames_captured);
      }
    }

    pthread_mutex_unlock(&g_stats_mutex);

    if (strcmp(frame, ASCIICHAT_WEBCAM_ERROR_STRING) == 0) {
      usleep(100000); // 100ms delay before retry
    }

    // Free the original frame from ascii_read() - framebuffer_write_frame made a copy
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

  log_info("Frame capture thread exiting (g_should_exit=%d)", g_should_exit);
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

  // DON'T clear the framebuffer when just updating size!
  // This causes use-after-free bugs when frames are in flight.
  // The capture thread will automatically start generating frames
  // with the new size, and old frames will naturally drain out.
  log_info("Updated frame size to: %ux%u (buffer not cleared)", width, height);
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
    exit(ASCIICHAT_ERR_WEBCAM);
  }

  // Create frame buffer - no need for size specification anymore
  g_frame_buffer = framebuffer_create(FRAME_BUFFER_CAPACITY);
  if (!g_frame_buffer) {
    log_fatal("Failed to create frame buffer");
    ascii_read_destroy();
    exit(ASCIICHAT_ERR_MALLOC);
  }
  log_info("Created framebuffer with capacity for %d frames", FRAME_BUFFER_CAPACITY);

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

  // Wait for capture thread to be ready before accepting connections
  log_info("Waiting for capture thread to initialize...");
  while (!g_capture_thread_ready && !g_should_exit) {
    usleep(10000); // 10ms
  }
  log_info("Capture thread ready");

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

  struct timespec last_stats_time;
  clock_gettime(CLOCK_MONOTONIC, &last_stats_time);

  // Initialize client manager
  memset(&g_client_manager, 0, sizeof(g_client_manager));
  pthread_mutex_init(&g_client_manager.mutex, NULL);
  g_client_manager.next_client_id = 0;

  // Main multi-client connection loop
  while (!g_should_exit) {
    log_info("Waiting for client connections... (%d/%d clients)", g_client_manager.client_count, MAX_CLIENTS);

    // Accept network connection with timeout
    connfd = accept_with_timeout(listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);
    if (connfd < 0) {
      if (errno == ETIMEDOUT) {
        // Check for disconnected clients during timeout
        pthread_mutex_lock(&g_client_manager_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
          client_info_t *client = &g_client_manager.clients[i];
          if (client->active && client->socket <= 0) { // Check socket status for disconnected clients
            log_info("Cleaning up disconnected client %u", client->client_id);
            // Wait for threads to finish
            pthread_join(client->receive_thread, NULL);
            pthread_join(client->send_thread, NULL);
            remove_client(client->client_id);
          }
        }
        pthread_mutex_unlock(&g_client_manager_mutex);
        continue;
      }
      log_error("Network accept failed: %s", network_error_string(errno));
      continue;
    }

    // Log client connection
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    log_info("New client connected from %s:%d", client_ip, client_port);

    // Add client to multi-client manager
    int client_id = add_client(connfd, client_ip, client_port);
    if (client_id < 0) {
      log_error("Failed to add client, rejecting connection");
      close(connfd);
      continue;
    }

    log_info("Client %d added successfully, total clients: %d", client_id, g_client_manager.client_count);
    connfd = 0; // Reset since ownership transferred to client manager

    // Multi-client mode: Each client manages its own size via protocol packets
    // Frame size will be determined per-client by their individual threads

    // Reset client-specific stats and flags
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.frames_sent = 0;
    g_stats.bytes_sent = 0;
    pthread_mutex_unlock(&g_stats_mutex);
    g_audio_send_failed = false; // Reset audio failure flag for new client

    // Resume capture thread now that client is ready
    g_capture_paused = false;

    // No more fixed-size buffers needed!
    uint64_t client_frames_sent = 0;

    // Client serving loop
    log_debug("Starting client serving loop");
    struct timespec last_size_check = {0, 0};

    // Reset stats time for this client
    clock_gettime(CLOCK_MONOTONIC, &last_stats_time);

    while (!g_should_exit) {

      // Check for size updates from client (non-blocking, but rate limited)
      struct timespec current_time;
      clock_gettime(CLOCK_MONOTONIC, &current_time);
      long size_check_elapsed = (current_time.tv_sec - last_size_check.tv_sec) * 1000 +
                                (current_time.tv_nsec - last_size_check.tv_nsec) / 1000000;

      if (size_check_elapsed >= 100) { // Check for size updates at most every 100ms
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
        last_size_check = current_time;
      }

      // Check if audio thread failed
      if (g_audio_send_failed) {
        log_info("Audio send failed, disconnecting client");
        break;
      }

      // Try to get a frame from buffer
      frame_t frame = {.magic = 0, .size = 0, .data = NULL};
      pthread_mutex_lock(&g_framebuffer_mutex);
      bool frame_available = false;
      if (g_frame_buffer) {
        frame_available = framebuffer_read_frame(g_frame_buffer, &frame);
      }
      pthread_mutex_unlock(&g_framebuffer_mutex);

      if (!frame_available) {
        // No frames available, wait a bit
        usleep(1000); // 1ms
        continue;
      }

      // Send frame with timeout (protected by socket mutex)
      // Add safety check for frame data
      if (!frame.data || frame.size == 0) {
        log_error("Invalid frame: data=%p, size=%zu", frame.data, frame.size);
        // Free frame data if it exists but size is 0
        free(frame.data);
        continue;
      }

      // Save frame size before freeing
      size_t frame_size_for_stats = frame.size;

      pthread_mutex_lock(&g_socket_mutex);
      int sent = send_compressed_frame(connfd, frame.data, frame.size);
      pthread_mutex_unlock(&g_socket_mutex);

      // CRITICAL: Free the frame data copy after sending
      if (frame.magic == FRAME_MAGIC && frame.data) {
        frame.magic = FRAME_FREED; // Mark as freed before freeing
        free(frame.data);
        frame.data = NULL;
      } else if (frame.data) {
        log_error("Sent invalid frame! frame.magic=%d", frame.magic);
        free(frame.data);
        continue;
      }

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
      g_stats.bytes_sent += frame_size_for_stats; // Approximate bytes sent (not counting packet overhead)
      pthread_mutex_unlock(&g_stats_mutex);

      client_frames_sent++;

      // Print periodic statistics
      struct timespec stats_time;
      clock_gettime(CLOCK_MONOTONIC, &stats_time);

      if (client_frames_sent == 1) {
        // Initialize stats time on first frame
        last_stats_time = stats_time;
        log_debug("Initialized last_stats_time on first frame");
      }

      // Safety check - make sure we're still connected
      if (connfd <= 0) {
        log_error("Connection lost during frame send");
        break;
      }

      long sec_diff = stats_time.tv_sec - last_stats_time.tv_sec;
      long nsec_diff = stats_time.tv_nsec - last_stats_time.tv_nsec;
      long stats_elapsed = sec_diff * 1000 + nsec_diff / 1000000;

      if (stats_elapsed >= 1000) { // Every 1 second
        // Lock stats mutex first
        if (pthread_mutex_lock(&g_stats_mutex) != 0) {
          log_error("Failed to lock stats mutex!");
          continue;
        }

        // Lock framebuffer mutex second
        if (pthread_mutex_lock(&g_framebuffer_mutex) != 0) {
          log_error("Failed to lock framebuffer mutex!");
          pthread_mutex_unlock(&g_stats_mutex);
          continue;
        }
        size_t buffer_size = 0;
        if (g_frame_buffer && g_frame_buffer->rb) {
          buffer_size = ringbuffer_size(g_frame_buffer->rb);
        }

        // Copy stats while we hold the mutex to avoid race conditions
        uint64_t captured = g_stats.frames_captured;
        uint64_t sent = g_stats.frames_sent;
        uint64_t dropped = g_stats.frames_dropped;

        pthread_mutex_unlock(&g_framebuffer_mutex);
        pthread_mutex_unlock(&g_stats_mutex);
        // Log stats after releasing all mutexes
        log_info("Stats: captured=%lu, sent=%lu, dropped=%lu, buffer_size=%zu", captured, sent, dropped, buffer_size);
        last_stats_time = stats_time;
      }
    }

    // Pause capture thread before closing connection
    g_capture_paused = true;
    usleep(50000); // Give capture thread time to pause

    close(connfd);
    connfd = 0;
    log_info("Client disconnected after %lu frames", client_frames_sent);
    log_info("Closing connection.");
    log_info("---------------------");

    // Don't clear framebuffer here - it will be cleared when next client connects
    // This avoids race conditions with any frames that might still be in use
  }

  // Cleanup
  log_info("Server shutting down...");
  g_should_exit = true;

  log_debug("Waiting for capture thread to finish...");
  // Wait for capture thread to finish
  pthread_join(g_capture_thread, NULL);
  log_debug("Capture thread finished");

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
    log_debug("Destroying framebuffer at %p", g_frame_buffer);
    framebuffer_destroy(g_frame_buffer);
    g_frame_buffer = NULL;
  }
  pthread_mutex_unlock(&g_framebuffer_mutex);
  log_debug("Framebuffer cleanup complete");

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

/* ============================================================================
 * Multi-Client Thread Functions
 * ============================================================================
 */

// Thread function to handle incoming data from a specific client
void *client_receive_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in receive thread");
    return NULL;
  }

  log_info("Started receive thread for client %u (%s)", client->client_id, client->display_name);

  packet_type_t type;
  uint32_t sender_id;
  void *data;
  size_t len;

  while (!g_should_exit && client->active) {
    // Receive packet from this client
    int result = receive_packet_with_client(client->socket, &type, &sender_id, &data, &len);

    if (result <= 0) {
      if (result == 0) {
        log_info("Client %u disconnected", client->client_id);
      } else {
        log_error("Error receiving from client %u: %s", client->client_id, strerror(errno));
      }
      break;
    }

    // Handle different packet types from client
    switch (type) {
    case PACKET_TYPE_CLIENT_JOIN: {
      // Handle client join request
      if (len == sizeof(client_info_packet_t)) {
        const client_info_packet_t *join_info = (const client_info_packet_t *)data;
        strncpy(client->display_name, join_info->display_name, MAX_DISPLAY_NAME_LEN - 1);
        client->can_send_video = (join_info->capabilities & CLIENT_CAP_VIDEO) != 0;
        client->can_send_audio = (join_info->capabilities & CLIENT_CAP_AUDIO) != 0;
        log_info("Client %u joined: %s (video=%d, audio=%d)", client->client_id, client->display_name,
                 client->can_send_video, client->can_send_audio);
      }
      break;
    }

    case PACKET_TYPE_STREAM_START: {
      // Handle stream start request
      if (len == sizeof(uint32_t)) {
        uint32_t stream_type = ntohl(*(uint32_t *)data);
        if (stream_type & STREAM_TYPE_VIDEO) {
          client->is_sending_video = true;
          log_info("Client %u started video stream", client->client_id);
        }
        if (stream_type & STREAM_TYPE_AUDIO) {
          client->is_sending_audio = true;
          log_info("Client %u started audio stream", client->client_id);
        }
      }
      break;
    }

    case PACKET_TYPE_STREAM_STOP: {
      // Handle stream stop request
      if (len == sizeof(uint32_t)) {
        uint32_t stream_type = ntohl(*(uint32_t *)data);
        if (stream_type & STREAM_TYPE_VIDEO) {
          client->is_sending_video = false;
          log_info("Client %u stopped video stream", client->client_id);
        }
        if (stream_type & STREAM_TYPE_AUDIO) {
          client->is_sending_audio = false;
          log_info("Client %u stopped audio stream", client->client_id);
        }
      }
      break;
    }

    case PACKET_TYPE_VIDEO: {
      // Handle incoming video frame from client
      if (client->is_sending_video && data && len > 0) {
        // Store frame in client's buffer with metadata
        uint32_t timestamp = (uint32_t)time(NULL);
        if (client->incoming_video_buffer) {
          framebuffer_write_multi_frame(client->incoming_video_buffer, (const char *)data, len, client->client_id, 0,
                                        timestamp);
          client->frames_received++;
          log_debug("Stored video frame from client %u (size=%zu)", client->client_id, len);
        }
      }
      break;
    }

    case PACKET_TYPE_SIZE: {
      // Handle size update from client
      if (len == 4) {
        const uint16_t *size_data = (const uint16_t *)data;
        client->width = ntohs(size_data[0]);
        client->height = ntohs(size_data[1]);
        log_info("Client %u updated size to %ux%u", client->client_id, client->width, client->height);
      }
      break;
    }

    default:
      log_debug("Received unhandled packet type %d from client %u", type, client->client_id);
      break;
    }

    if (data) {
      free(data);
      data = NULL;
    }
  }

  // Mark client as inactive
  client->active = false;
  log_info("Receive thread for client %u terminated", client->client_id);
  return NULL;
}

// Thread function to send data to a specific client
void *client_send_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  if (!client || client->socket <= 0) {
    log_error("Invalid client info in send thread");
    return NULL;
  }

  log_info("Started send thread for client %u (%s)", client->client_id, client->display_name);

  while (!g_should_exit && client->active) {
    // Send server's own video frames to this client
    frame_t frame = {.magic = 0, .size = 0, .data = NULL};
    pthread_mutex_lock(&g_framebuffer_mutex);
    bool frame_available = false;
    if (g_frame_buffer) {
      frame_available = framebuffer_read_frame(g_frame_buffer, &frame);
    }
    pthread_mutex_unlock(&g_framebuffer_mutex);

    if (frame_available && frame.data && frame.size > 0) {
      // Send frame to this client
      int sent = send_video_packet(client->socket, frame.data, frame.size);
      if (sent >= 0) {
        client->frames_sent++;
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.bytes_sent += frame.size;
        pthread_mutex_unlock(&g_stats_mutex);
      } else {
        log_error("Failed to send frame to client %u", client->client_id);
        // Client might be disconnected
        break;
      }

      // Free frame data
      if (frame.magic == FRAME_MAGIC && frame.data) {
        frame.magic = FRAME_FREED;
        free(frame.data);
        frame.data = NULL;
      }
    } else {
      // No frames available, wait a bit
      usleep(1000); // 1ms
    }

    // TODO: Also send video frames from OTHER clients to this client
    // This would implement the multi-user video routing
  }

  log_info("Send thread for client %u terminated", client->client_id);
  return NULL;
}

// Client management functions
int add_client(int socket, const char *client_ip, int port) {
  pthread_mutex_lock(&g_client_manager_mutex);

  if (g_client_manager.client_count >= MAX_CLIENTS) {
    pthread_mutex_unlock(&g_client_manager_mutex);
    log_error("Maximum client limit reached (%d)", MAX_CLIENTS);
    return -1;
  }

  // Find empty slot
  int slot = -1;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!g_client_manager.clients[i].active) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    pthread_mutex_unlock(&g_client_manager_mutex);
    log_error("No available client slots");
    return -1;
  }

  // Initialize client
  client_info_t *client = &g_client_manager.clients[slot];
  memset(client, 0, sizeof(client_info_t));

  client->socket = socket;
  client->client_id = ++g_client_manager.next_client_id;
  strncpy(client->client_ip, client_ip, sizeof(client->client_ip) - 1);
  client->port = port;
  client->active = true;
  client->connected_at = time(NULL);
  snprintf(client->display_name, sizeof(client->display_name), "Client%u", client->client_id);

  // Create individual video buffer for this client
  client->incoming_video_buffer = framebuffer_create(16); // 16 frame buffer per client
  if (!client->incoming_video_buffer) {
    log_error("Failed to create video buffer for client %u", client->client_id);
    pthread_mutex_unlock(&g_client_manager_mutex);
    return -1;
  }

  g_client_manager.client_count++;
  pthread_mutex_unlock(&g_client_manager_mutex);

  // Start threads for this client
  if (pthread_create(&client->receive_thread, NULL, client_receive_thread_func, client) != 0) {
    log_error("Failed to create receive thread for client %u", client->client_id);
    remove_client(client->client_id);
    return -1;
  }

  if (pthread_create(&client->send_thread, NULL, client_send_thread_func, client) != 0) {
    log_error("Failed to create send thread for client %u", client->client_id);
    // Signal receive thread to exit gracefully and wait for it
    client->active = false;
    pthread_join(client->receive_thread, NULL);
    remove_client(client->client_id);
    return -1;
  }

  log_info("Added client %u from %s:%d", client->client_id, client_ip, port);
  return client->client_id;
}

int remove_client(uint32_t client_id) {
  pthread_mutex_lock(&g_client_manager_mutex);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    if (client->active && client->client_id == client_id) {
      client->active = false;

      // Clean up client resources
      if (client->socket > 0) {
        close(client->socket);
        client->socket = 0;
      }

      if (client->incoming_video_buffer) {
        framebuffer_destroy(client->incoming_video_buffer);
        client->incoming_video_buffer = NULL;
      }

      g_client_manager.client_count--;

      log_info("Removed client %u (%s)", client_id, client->display_name);
      pthread_mutex_unlock(&g_client_manager_mutex);
      return 0;
    }
  }

  pthread_mutex_unlock(&g_client_manager_mutex);
  log_error("Client %u not found for removal", client_id);
  return -1;
}

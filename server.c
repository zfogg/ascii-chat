/* Feature test macros for POSIX functions */
#define _GNU_SOURCE

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

/* ============================================================================
 * Global State
 * ============================================================================
 */

static volatile bool g_should_exit = false;
static framebuffer_t *g_frame_buffer = NULL;
static pthread_t g_capture_thread;
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

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

/* ============================================================================
 * Signal Handlers
 * ============================================================================
 */

void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  // Server terminal resize - we ignore this since we use client's terminal size
  // Only log that the event occurred
  log_debug("Server terminal resized (ignored - using client terminal size)");
}

void sigint_handler(int sigint) {
  (void)(sigint);
  g_should_exit = true;
  log_info("Server shutdown requested");
}

/* ============================================================================
 * Frame Capture Thread
 * ============================================================================
 */

void *capture_thread_func(void *arg) {
  (void)arg;

  struct timespec last_capture_time = {0, 0};
  uint64_t frames_captured = 0;

  log_info("Frame capture thread started");

  while (!g_should_exit) {
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

    // Try to add frame to buffer
    bool buffered = framebuffer_write_frame(g_frame_buffer, frame);

    pthread_mutex_lock(&g_stats_mutex);
    g_stats.frames_captured++;
    if (!buffered) {
      g_stats.frames_dropped++;
      log_debug("Frame buffer full, dropped frame %lu", g_stats.frames_captured);
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
 * Client Size Handling
 * ============================================================================
 */

int receive_client_size(int connfd, unsigned short *width, unsigned short *height) {
  char buffer[SIZE_MESSAGE_MAX_LEN];
  
  // Use recv with MSG_DONTWAIT to make it non-blocking
  ssize_t received = recv(connfd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
  if (received <= 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0; // No data available (non-blocking)
    }
    return -1; // Error or connection closed
  }
  
  buffer[received] = '\0';
  
  // Look for SIZE message in the received data
  char *size_msg = strstr(buffer, SIZE_MESSAGE_PREFIX);
  if (!size_msg) {
    return 0; // No size message found
  }
  
  // Parse the size message
  if (parse_size_message(size_msg, width, height) == 0) {
    return 1; // Successfully parsed size message
  }
  
  return 0; // Failed to parse
}

void update_frame_buffer_for_size(unsigned short width, unsigned short height) {
  // Update global dimensions
  opt_width = width;
  opt_height = height;
  
  // Recreate frame buffer with new size
  framebuffer_destroy(g_frame_buffer);
  g_frame_buffer = framebuffer_create(FRAME_BUFFER_CAPACITY, FRAME_BUFFER_SIZE_FINAL);
  if (!g_frame_buffer) {
    log_fatal("Failed to create frame buffer for new size %ux%u", width, height);
    exit(ASCIICHAT_ERR_MALLOC);
  }
  
  log_info("Updated frame generation for client size: %ux%u", width, height);
}

/* ============================================================================
 * Main Server Logic
 * ============================================================================
 */

int main(int argc, char *argv[]) {
  log_init("server.log", LOG_DEBUG);
  log_info("ASCII Chat server starting...");

  options_init(argc, argv);
  int port = strtoint(opt_port);
  unsigned short int webcam_index = opt_webcam_index;

  // Set default dimensions for initial frame buffer (will be updated by client)
  opt_width = 80;
  opt_height = 24;

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

  // Create frame buffer (more capacity for colored mode to handle frame size
  // variations)
  g_frame_buffer = framebuffer_create(FRAME_BUFFER_CAPACITY, FRAME_BUFFER_SIZE_FINAL);
  if (!g_frame_buffer) {
    log_fatal("Failed to create frame buffer");
    ascii_read_destroy();
    exit(ASCIICHAT_ERR_MALLOC);
  }

  // Start capture thread
  if (pthread_create(&g_capture_thread, NULL, capture_thread_func, NULL) != 0) {
    log_fatal("Failed to create capture thread");
    framebuffer_destroy(g_frame_buffer);
    ascii_read_destroy();
    exit(ASCIICHAT_ERR_THREAD);
  }

  // Network setup
  int listenfd = 0, connfd = 0;
  struct sockaddr_in serv_addr;
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    log_fatal("Failed to create socket: %s", strerror(errno));
    exit(1);
  }

  log_info("Server listening on port %d", port);
  printf("Running server on port %d\n", port);

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

    // Set keep-alive
    if (set_socket_keepalive(connfd) < 0) {
      log_warn("Failed to set keep-alive: %s", strerror(errno));
    }
    
    // Receive initial size message from client
    unsigned short client_width, client_height;
    char size_buffer[SIZE_MESSAGE_MAX_LEN];
    ssize_t size_received = recv_with_timeout(connfd, size_buffer, sizeof(size_buffer) - 1, RECV_TIMEOUT);
    if (size_received <= 0) {
      log_error("Failed to receive client size: %s", network_error_string(errno));
      close(connfd);
      continue;
    }
    
    size_buffer[size_received] = '\0';
    if (parse_size_message(size_buffer, &client_width, &client_height) != 0) {
      log_error("Failed to parse client size message: %s", size_buffer);
      close(connfd);
      continue;
    }
    
    // Update frame generation for client size
    update_frame_buffer_for_size(client_width, client_height);
    log_info("Client requested frame size: %ux%u", client_width, client_height);

    // Reset client-specific stats
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.frames_sent = 0;
    g_stats.bytes_sent = 0;
    pthread_mutex_unlock(&g_stats_mutex);

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
      
      // Try to get a frame from buffer
      if (!framebuffer_read_frame(g_frame_buffer, frame_buffer)) {
        // No frames available, wait a bit
        usleep(1000); // 1ms
        continue;
      }

      // Send frame with timeout
      size_t frame_len = strlen(frame_buffer);
      ssize_t sent = send_with_timeout(connfd, frame_buffer, frame_len, SEND_TIMEOUT);
      if (sent < 0) {
        log_error("Error: Network send of frame failed: %s", network_error_string(errno));
        break;
      }

      if ((size_t)sent != frame_len) {
        log_error("Partial send: %zd of %zu bytes", sent, frame_len);
      }

      // Update statistics
      pthread_mutex_lock(&g_stats_mutex);
      g_stats.frames_sent++;
      g_stats.bytes_sent += sent;
      pthread_mutex_unlock(&g_stats_mutex);

      client_frames_sent++;

      // Print periodic statistics
      struct timespec current_time;
      clock_gettime(CLOCK_MONOTONIC, &current_time);
      long stats_elapsed = (current_time.tv_sec - last_stats_time.tv_sec) * 1000 +
                           (current_time.tv_nsec - last_stats_time.tv_nsec) / 1000000;

      if (stats_elapsed >= 10000) { // Every 10 seconds
        pthread_mutex_lock(&g_stats_mutex);
        log_info("Stats: captured=%lu, sent=%lu, dropped=%lu, buffer_size=%zu", g_stats.frames_captured,
                 g_stats.frames_sent, g_stats.frames_dropped, ringbuffer_size(g_frame_buffer->rb));
        pthread_mutex_unlock(&g_stats_mutex);
        last_stats_time = current_time;
      }
    }

    close(connfd);
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

  // Cleanup resources
  framebuffer_destroy(g_frame_buffer);
  ascii_read_destroy();
  close(listenfd);

  // Final statistics
  pthread_mutex_lock(&g_stats_mutex);
  log_info("Final stats: captured=%lu, sent=%lu, dropped=%lu", g_stats.frames_captured, g_stats.frames_sent,
           g_stats.frames_dropped);
  pthread_mutex_unlock(&g_stats_mutex);

  printf("Server shutdown complete.\n");
  log_destroy();
  return 0;
}

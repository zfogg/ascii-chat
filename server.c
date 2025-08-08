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

static server_stats_t g_stats = {0};

static int listenfd = 0;
static int connfd = 0;

/* Multi-client support */
#define MAX_CLIENTS 10

typedef struct {
  int socket;
  pthread_t receive_thread;        // Thread for receiving client data
  pthread_t send_thread;           // Thread for sending data to client
  uint32_t client_id;
  char display_name[CLIENT_NAME_MAX];
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
  uint64_t frames_received;        // Track incoming frames from this client
  
  // Buffers for incoming media
  framebuffer_t *incoming_video_buffer; // Buffer for this client's video
  audio_ring_buffer_t *incoming_audio_buffer; // Buffer for this client's audio
} client_info_t;

typedef struct {
  client_info_t clients[MAX_CLIENTS];
  int client_count;
  uint32_t next_client_id;         // For assigning unique IDs
  pthread_mutex_t mutex;
} client_manager_t;

typedef struct {
  framebuffer_t *composite_video_buffer; // Mixed video from all clients
  audio_ring_buffer_t *mixed_audio_buffer; // Mixed audio from all clients
  pthread_t video_mixer_thread;
  pthread_t audio_mixer_thread;
  pthread_mutex_t mixer_mutex;
} stream_mixer_t;

static client_manager_t g_clients = {0};
static stream_mixer_t g_mixer = {0};

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
 * Multi-Client Management
 * ============================================================================
 */

static void initialize_client_manager(void) {
  pthread_mutex_init(&g_clients.mutex, NULL);
  g_clients.client_count = 0;
  g_clients.next_client_id = 1; // Start client IDs from 1
  
  // Initialize all client slots as inactive
  for (int i = 0; i < MAX_CLIENTS; i++) {
    g_clients.clients[i].active = false;
    g_clients.clients[i].socket = -1;
  }
}

static void destroy_client_manager(void) {
  pthread_mutex_lock(&g_clients.mutex);
  
  // Clean up all active clients
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_clients.clients[i].active) {
      client_info_t *client = &g_clients.clients[i];
      
      // Close socket
      if (client->socket > 0) {
        close(client->socket);
        client->socket = -1;
      }
      
      // Clean up buffers
      if (client->incoming_video_buffer) {
        framebuffer_destroy(client->incoming_video_buffer);
        client->incoming_video_buffer = NULL;
      }
      
      if (client->incoming_audio_buffer) {
        audio_ring_buffer_destroy(client->incoming_audio_buffer);
        client->incoming_audio_buffer = NULL;
      }
      
      client->active = false;
    }
  }
  
  g_clients.client_count = 0;
  pthread_mutex_unlock(&g_clients.mutex);
  pthread_mutex_destroy(&g_clients.mutex);
}

static client_info_t* find_available_client_slot(void) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!g_clients.clients[i].active) {
      return &g_clients.clients[i];
    }
  }
  return NULL;
}

static void *client_receive_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  packet_type_t packet_type;
  void *packet_data = NULL;
  size_t packet_len = 0;
  
  log_info("Client receive thread started for %s:%d (ID: %u)", 
           client->client_ip, client->port, client->client_id);
  
  while (!g_should_exit && client->active) {
    // Receive packet from client
    int result = receive_packet(client->socket, &packet_type, &packet_data, &packet_len);
    
    if (result <= 0) {
      if (result == 0) {
        log_info("Client %u disconnected", client->client_id);
      } else {
        log_warn("Failed to receive from client %u: %s", 
                 client->client_id, network_error_string(errno));
      }
      break;
    }
    
    // Process packet based on type
    switch (packet_type) {
      case PACKET_TYPE_VIDEO:
        if (client->is_sending_video && client->incoming_video_buffer) {
          // Extract stream header and video data
          if (packet_len > sizeof(stream_header_t)) {
            stream_header_t *header = (stream_header_t *)packet_data;
            char *video_data = (char *)packet_data + sizeof(stream_header_t);
            size_t video_len = packet_len - sizeof(stream_header_t);
            
            // Store in client's video buffer with metadata
            framebuffer_write_multi_frame(client->incoming_video_buffer, 
                                         video_data, video_len,
                                         ntohl(header->client_id),
                                         ntohl(header->sequence), 
                                         ntohl(header->timestamp));
            client->frames_received++;
          }
        }
        break;
        
      case PACKET_TYPE_AUDIO:
        if (client->is_sending_audio && client->incoming_audio_buffer) {
          // Extract stream header and audio data
          if (packet_len > sizeof(stream_header_t)) {
            char *audio_data = (char *)packet_data + sizeof(stream_header_t);
            size_t audio_len = packet_len - sizeof(stream_header_t);
            
            // Store in client's audio buffer
            int samples = audio_len / sizeof(float);
            audio_ring_buffer_write(client->incoming_audio_buffer, 
                                   (float *)audio_data, samples);
          }
        }
        break;
        
      case PACKET_TYPE_STREAM_START: {
        if (packet_len >= 2 * sizeof(uint32_t)) {
          uint32_t *data = (uint32_t *)packet_data;
          uint32_t stream_type = ntohl(data[1]);
          
          if (stream_type == STREAM_TYPE_VIDEO) {
            client->is_sending_video = true;
            log_info("Client %u started sending video", client->client_id);
          } else if (stream_type == STREAM_TYPE_AUDIO) {
            client->is_sending_audio = true;
            log_info("Client %u started sending audio", client->client_id);
          }
        }
        break;
      }
      
      case PACKET_TYPE_STREAM_STOP: {
        if (packet_len >= 2 * sizeof(uint32_t)) {
          uint32_t *data = (uint32_t *)packet_data;
          uint32_t stream_type = ntohl(data[1]);
          
          if (stream_type == STREAM_TYPE_VIDEO) {
            client->is_sending_video = false;
            log_info("Client %u stopped sending video", client->client_id);
          } else if (stream_type == STREAM_TYPE_AUDIO) {
            client->is_sending_audio = false;
            log_info("Client %u stopped sending audio", client->client_id);
          }
        }
        break;
      }
        
      default:
        log_warn("Unexpected packet type %d from client %u", packet_type, client->client_id);
        break;
    }
    
    // Free packet data
    if (packet_data) {
      free(packet_data);
      packet_data = NULL;
    }
  }
  
  // Cleanup
  if (packet_data) {
    free(packet_data);
  }
  
  log_info("Client receive thread finished for %u", client->client_id);
  return NULL;
}

static void *client_send_thread_func(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  char *frame_buffer = NULL;
  SAFE_MALLOC(frame_buffer, FRAME_BUFFER_SIZE_FINAL, char *);
  
  log_info("Client send thread started for %s:%d (ID: %u)", 
           client->client_ip, client->port, client->client_id);
  
  while (!g_should_exit && client->active) {
    // Get frame from shared buffer (mixed from all clients)
    if (!framebuffer_read_frame(g_frame_buffer, (frame_t *)frame_buffer)) {
      usleep(1000); // 1ms wait
      continue;
    }
    
    frame_t *frame = (frame_t *)frame_buffer;
    
    // Send frame to this specific client
    ssize_t sent = send_with_timeout(client->socket, frame->data, frame->size, SEND_TIMEOUT);
    
    if (sent < 0) {
      log_warn("Client %u (%s:%d) send failed: %s", 
               client->client_id, client->client_ip, client->port, 
               network_error_string(errno));
      break;
    }
    
    client->frames_sent++;
    
    // Free frame data
    if (frame->data) {
      free(frame->data);
      frame->data = NULL;
    }
  }
  
  // Cleanup
  free(frame_buffer);
  
  log_info("Client send thread finished for %u", client->client_id);
  return NULL;
}

static int add_client(int sockfd, struct sockaddr_in *addr) {
  pthread_mutex_lock(&g_clients.mutex);
  
  if (g_clients.client_count >= MAX_CLIENTS) {
    pthread_mutex_unlock(&g_clients.mutex);
    log_warn("Server full, rejecting client");
    return -1; // Server full
  }
  
  // Find empty slot
  client_info_t *client = find_available_client_slot();
  if (!client) {
    pthread_mutex_unlock(&g_clients.mutex);
    return -1;
  }
  
  // Initialize client
  memset(client, 0, sizeof(*client));
  client->socket = sockfd;
  client->client_id = g_clients.next_client_id++;
  client->active = true;
  client->connected_at = time(NULL);
  client->frames_sent = 0;
  client->frames_received = 0;
  client->port = ntohs(addr->sin_port);
  inet_ntop(AF_INET, &addr->sin_addr, client->client_ip, sizeof(client->client_ip));
  
  // Set default capabilities
  client->can_send_video = true;
  client->can_send_audio = true;
  client->is_sending_video = false;
  client->is_sending_audio = false;
  
  // Create buffers for incoming streams
  client->incoming_video_buffer = framebuffer_create(32); // 32 frame buffer
  client->incoming_audio_buffer = audio_ring_buffer_create();
  
  if (!client->incoming_video_buffer || !client->incoming_audio_buffer) {
    log_error("Failed to create buffers for client %u", client->client_id);
    if (client->incoming_video_buffer) {
      framebuffer_destroy(client->incoming_video_buffer);
    }
    if (client->incoming_audio_buffer) {
      audio_ring_buffer_destroy(client->incoming_audio_buffer);
    }
    client->active = false;
    pthread_mutex_unlock(&g_clients.mutex);
    return -1;
  }
  
  // Create threads
  if (pthread_create(&client->receive_thread, NULL, client_receive_thread_func, client) != 0) {
    log_error("Failed to create receive thread for client %u", client->client_id);
    framebuffer_destroy(client->incoming_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->active = false;
    pthread_mutex_unlock(&g_clients.mutex);
    return -1;
  }
  
  if (pthread_create(&client->send_thread, NULL, client_send_thread_func, client) != 0) {
    log_error("Failed to create send thread for client %u", client->client_id);
    pthread_cancel(client->receive_thread);
    framebuffer_destroy(client->incoming_video_buffer);
    audio_ring_buffer_destroy(client->incoming_audio_buffer);
    client->active = false;
    pthread_mutex_unlock(&g_clients.mutex);
    return -1;
  }
  
  g_clients.client_count++;
  pthread_mutex_unlock(&g_clients.mutex);
  
  // Send client join notification to client
  send_client_join_packet(sockfd, client->client_id, client->display_name,
                         CLIENT_CAP_VIDEO | CLIENT_CAP_AUDIO,
                         client->width, client->height);
  
  log_info("Added client %u (%s:%d) - total clients: %d", 
           client->client_id, client->client_ip, client->port, g_clients.client_count);
  return 0;
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

  // Initialize client manager for multi-user support
  initialize_client_manager();
  log_info("Initialized multi-client manager (max %d clients)", MAX_CLIENTS);

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

  // Multi-client connection loop
  while (!g_should_exit) {
    log_info("Waiting for client connections (active: %d/%d)...", g_clients.client_count, MAX_CLIENTS);

    // Accept network connection with timeout
    int new_connfd = accept_with_timeout(listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);
    if (new_connfd < 0) {
      if (errno == ETIMEDOUT) {
        log_debug("Network accept timeout, continuing...");
        continue;
      }
      log_error("Network accept failed: %s", network_error_string(errno));
      continue;
    }

    // Log client connection attempt
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    log_info("Client connecting from %s:%d", client_ip, ntohs(client_addr.sin_port));

    // Try to add client to multi-client manager
    if (add_client(new_connfd, &client_addr) < 0) {
      log_warn("Failed to add client %s:%d (server may be full)", 
               client_ip, ntohs(client_addr.sin_port));
      close(new_connfd);
      continue;
    }

    // Client successfully added and threads started
    log_info("Client %s:%d added successfully", client_ip, ntohs(client_addr.sin_port));
  }

  // Cleanup
  log_info("Server shutting down...");
  g_should_exit = true;

  // Clean up all clients first
  destroy_client_manager();
  log_info("Client manager cleaned up");

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

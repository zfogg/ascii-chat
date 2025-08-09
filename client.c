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

#include "ascii.h"
#include "common.h"
#include "network.h"
#include "options.h"
#include "compression.h"
#include "audio.h"
#include "ringbuffer.h"
#include "image.h"
#include "webcam.h"
#include "aspect_ratio.h"

static int sockfd = 0;
static volatile bool g_should_exit = false;
static volatile bool g_first_connection = true;
static volatile bool g_should_reconnect = false;
static volatile bool g_connection_lost = false;

static audio_context_t g_audio_context = {0};

// Compression state
static volatile bool g_expecting_compressed_frame = false;
static compressed_frame_header_t g_compression_header = {0};
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

/* ============================================================================
 * Multi-User Client State
 * ============================================================================
 */

// Multi-user client state
static uint32_t g_my_client_id = 0;

// Remote client tracking (up to MAX_CLIENTS)
typedef struct {
  uint32_t client_id;
  char display_name[MAX_DISPLAY_NAME_LEN];
  bool is_active;
  time_t last_seen;
} remote_client_info_t;

static remote_client_info_t g_remote_clients[MAX_CLIENTS];
static int g_remote_client_count = 0;
static pthread_mutex_t g_remote_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Function Declarations
 * ============================================================================
 */

// Multi-user packet handlers
static void handle_client_list_packet(const void *data, size_t len);

// Ping thread for connection keepalive
static void *ping_thread_func(void *arg);

// Webcam capture thread
static void *webcam_capture_thread_func(void *arg);

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

    // Stop audio playback to help thread exit
    if (opt_audio_enabled) {
      audio_stop_playback(&g_audio_context);
    }

    // Give thread a moment to exit gracefully
    usleep(100000); // 100ms

    // Force close socket to break any blocking recv() calls
    if (sockfd > 0) {
      close(sockfd);
      sockfd = 0;
    }

    // Wait for thread to exit
    int join_result = pthread_join(g_data_thread, NULL);
    if (join_result != 0) {
      log_error("Data thread join failed: %d", join_result);
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
  printf("\nShutdown requested...\n");
  g_should_exit = true;

  // Close socket to interrupt recv operations
  close_socket(sockfd);
  sockfd = 0;
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

static void handle_video_header_packet(const void *data, size_t len) {
  if (!data || len != sizeof(compressed_frame_header_t)) {
    log_warn("Invalid video header packet size: %zu", len);
    return;
  }

  memcpy(&g_compression_header, data, sizeof(compressed_frame_header_t));

  // Validate magic number
  if (g_compression_header.magic != COMPRESSION_FRAME_MAGIC) {
    log_error("Invalid compression magic: 0x%08x", g_compression_header.magic);
    return;
  }

  g_expecting_compressed_frame = true;
#ifdef COMPRESSION_DEBUG
  log_debug("Received compression header: %s, original_size=%u, compressed_size=%u",
            g_compression_header.compressed_size == 0 ? "uncompressed" : "compressed",
            g_compression_header.original_size, g_compression_header.compressed_size);
#endif
}

// Track last frame dimensions to detect size changes
static int g_last_frame_width = 0;
static int g_last_frame_height = 0;

static void handle_video_packet(const void *data, size_t len) {
  if (!data || len == 0) {
    return;
  }

  char *frame_data = NULL;
  int frame_width = 0;
  int frame_height = 0;

  if (g_expecting_compressed_frame) {
    // This is compressed/uncompressed frame data with header
    g_expecting_compressed_frame = false;

    // Get dimensions from header
    frame_width = g_compression_header.width;
    frame_height = g_compression_header.height;

    if (g_compression_header.compressed_size == 0) {
      // Uncompressed frame
      if (len != g_compression_header.original_size) {
        log_error("Uncompressed frame size mismatch: expected %u, got %zu", g_compression_header.original_size, len);
        return;
      }

      SAFE_MALLOC(frame_data, len + 1, char *);
      memcpy(frame_data, data, len);
      frame_data[len] = '\0';

    } else {
      // Compressed frame - decompress it
      if (len != g_compression_header.compressed_size) {
        log_error("Compressed frame size mismatch: expected %u, got %zu", g_compression_header.compressed_size, len);
        return;
      }

      SAFE_MALLOC(frame_data, g_compression_header.original_size + 1, char *);

      // Decompress using zlib
      uLongf decompressed_size = g_compression_header.original_size;
      int result = uncompress((Bytef *)frame_data, &decompressed_size, (const Bytef *)data, len);

      if (result != Z_OK || decompressed_size != g_compression_header.original_size) {
        log_error("Decompression failed: zlib error %d, size %lu vs expected %u", result, decompressed_size,
                  g_compression_header.original_size);
        free(frame_data);
        return;
      }

      frame_data[g_compression_header.original_size] = '\0';
#ifdef COMPRESSION_DEBUG
      log_debug("Decompressed frame: %zu -> %u bytes", len, g_compression_header.original_size);
#endif
    }

    // Verify checksum
    uint32_t received_checksum = calculate_crc32(frame_data, g_compression_header.original_size);
    if (received_checksum != g_compression_header.checksum) {
      log_error("Frame checksum mismatch: expected 0x%08x, got 0x%08x", g_compression_header.checksum,
                received_checksum);
      free(frame_data);
      return;
    }

  } else {
    // Legacy uncompressed frame (fallback for old protocol)
    log_debug("Got legacy uncompressed frame (no header), dimensions unknown");
    SAFE_MALLOC(frame_data, len + 1, char *);

    memcpy(frame_data, data, len);
    frame_data[len] = '\0';
  }

  // Process the frame
  if (strcmp(frame_data, ASCIICHAT_WEBCAM_ERROR_STRING) == 0) {
    log_error("Server reported webcam failure: %s", frame_data);
    usleep(1000 * 1000);
  } else {
    // Check if frame dimensions have changed
    if (frame_width > 0 && frame_height > 0) {
      if (g_last_frame_width != 0 && g_last_frame_height != 0 &&
          (g_last_frame_width != frame_width || g_last_frame_height != frame_height)) {
        // Frame dimensions changed, clear console to avoid artifacts
        console_clear();
      }
      g_last_frame_width = frame_width;
      g_last_frame_height = frame_height;
    }

    ascii_write(frame_data);
  }

  free(frame_data);
}

static void *data_reception_thread_func(void *arg) {
  (void)arg;

#ifdef DEBUG_THREADS
  log_debug("Data reception thread started");
#endif

  while (!g_should_exit) {
    if (sockfd == 0) {
      usleep(10 * 1000);
      continue;
    }

    packet_type_t type;
    void *data;
    size_t len;

    int result = receive_packet(sockfd, &type, &data, &len);
    if (result < 0) {
      log_error("Failed to receive packet");
      g_connection_lost = true;
      break;
    } else if (result == 0) {
      log_info("Server closed connection");
      g_connection_lost = true;
      break;
    }

    switch (type) {
    case PACKET_TYPE_VIDEO_HEADER:
      handle_video_header_packet(data, len);
      break;

    case PACKET_TYPE_VIDEO:
      handle_video_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO:
      handle_audio_packet(data, len);
      break;

    case PACKET_TYPE_PING:
      // Respond with PONG
      if (send_pong_packet(sockfd) < 0) {
        log_error("Failed to send PONG response");
      } else {
        log_debug("Sent PONG response to server PING");
      }
      break;

    case PACKET_TYPE_PONG:
      // Server acknowledged our PING
      // log_debug("Received PONG from server");
      break;

    // Multi-user protocol packets
    case PACKET_TYPE_CLIENT_LIST:
      handle_client_list_packet(data, len);
      break;

    case PACKET_TYPE_CLEAR_CONSOLE:
      // Server requested console clear
      console_clear();
      break;

    default:
      log_warn("Unknown packet type: %d", type);
      break;
    }

    free(data);
    // usleep(1 * 1000); // TODO: figure out if I need this. I don't think I do.
  }

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
    if (sockfd <= 0) {
      usleep(1000 * 1000); // 1 second
      continue;
    }

    // Send ping packet every 3 seconds to keep connection alive (server timeout is 5 seconds)
    if (send_ping_packet(sockfd) < 0) {
      log_debug("Failed to send ping packet");
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

    int frame_interval = get_frame_interval_ms();
    if (elapsed_ms < frame_interval) {
      usleep((frame_interval - elapsed_ms) * 1000);
      continue;
    }

    // Capture raw image from webcam
    image_t *image = webcam_read();
    if (!image) {
      log_debug("No frame available from webcam yet");
      usleep(10000); // 10ms delay before retry
      continue;
    }

    // Resize image to a reasonable size for network transmission
    // We need to account for terminal character aspect ratio (chars are ~2x taller than wide)
    // So we send images with 2x the width to compensate
    // Target: opt_width*2 x opt_height, but maintain webcam aspect ratio

    ssize_t target_width = opt_width * 2; // e.g., 220
    ssize_t target_height = opt_height;   // e.g., 70
    ssize_t resized_width, resized_height;

    // Use aspect_ratio2 which doesn't apply terminal character correction
    aspect_ratio2(image->w, image->h, target_width, target_height, &resized_width, &resized_height);

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
    size_t rgb_size = image->w * image->h * sizeof(rgb_t);
    size_t packet_size = sizeof(uint32_t) * 2 + rgb_size; // width + height + pixels

    uint8_t *packet_data = malloc(packet_size);
    if (!packet_data) {
      log_error("Failed to allocate packet buffer");
      image_destroy(image);
      continue;
    }

    // Pack the image data
    uint32_t width_net = htonl(image->w);
    uint32_t height_net = htonl(image->h);
    memcpy(packet_data, &width_net, sizeof(uint32_t));
    memcpy(packet_data + sizeof(uint32_t), &height_net, sizeof(uint32_t));
    memcpy(packet_data + sizeof(uint32_t) * 2, image->pixels, rgb_size);

    // Send image data to server via VIDEO packet
    if (send_video_packet(sockfd, (char *)packet_data, packet_size) < 0) {
      log_debug("Failed to send video frame to server");
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

/* ============================================================================
 * Multi-User Packet Handlers
 * ============================================================================
 */

static void handle_client_list_packet(const void *data, size_t len) {
  if (!data || len != sizeof(client_list_packet_t)) {
    log_error("Invalid client list packet size: %zu", len);
    return;
  }

  const client_list_packet_t *client_list = (const client_list_packet_t *)data;

  pthread_mutex_lock(&g_remote_clients_mutex);

  // Clear existing remote clients
  memset(g_remote_clients, 0, sizeof(g_remote_clients));
  g_remote_client_count = 0;

  // Add clients from server list (excluding ourselves)
  for (uint32_t i = 0; i < client_list->client_count && i < MAX_CLIENTS; i++) {
    const client_info_packet_t *client_info = &client_list->clients[i];

    if (client_info->client_id != g_my_client_id && g_remote_client_count < MAX_CLIENTS) {
      remote_client_info_t *remote = &g_remote_clients[g_remote_client_count];
      remote->client_id = client_info->client_id;
      strncpy(remote->display_name, client_info->display_name, MAX_DISPLAY_NAME_LEN - 1);
      remote->display_name[MAX_DISPLAY_NAME_LEN - 1] = '\0';
      remote->is_active = true;
      remote->last_seen = time(NULL);
      g_remote_client_count++;
    }
  }

  pthread_mutex_unlock(&g_remote_clients_mutex);

  log_info("Updated client list: %d remote clients connected", g_remote_client_count);
  for (int i = 0; i < g_remote_client_count; i++) {
    log_info("  - Client %u: %s", g_remote_clients[i].client_id, g_remote_clients[i].display_name);
  }
}

/* ============================================================================
 * Multi-User Local Capture Threads (Future Implementation)
 * ============================================================================
 */

// NOTE: Local capture thread functions will be implemented in future commits
// when actual local video/audio capture functionality is added to clients.

int main(int argc, char *argv[]) {
  log_init("client.log", LOG_DEBUG);
  log_truncate_if_large(); /* Truncate if log is already too large */
  log_info("ASCII Chat client starting...");

  options_init(argc, argv);
  char *address = opt_address;
  int port = strtoint(opt_port);

  struct sockaddr_in serv_addr;

  // Cleanup nicely on Ctrl+C.
  signal(SIGINT, sigint_handler);

  // Handle terminal resize events
  signal(SIGWINCH, sigwinch_handler);

  // Initialize ASCII output for this connection
  ascii_write_init();

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

    log_info("Audio system initialized and playback started");
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
      printf("Connected successfully!\n");
      log_info("Connected to server %s:%d", address, port);
      reconnect_attempt = 0; // Reset reconnection counter on successful connection

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
      log_debug("Client opt_stretch=%d, sending stretch capability=%s", opt_stretch,
                (my_capabilities & CLIENT_CAP_STRETCH) ? "yes" : "no");

      char my_display_name[MAX_DISPLAY_NAME_LEN];
      snprintf(my_display_name, sizeof(my_display_name), "ClientUser");

      if (send_client_join_packet(sockfd, my_display_name, my_capabilities) < 0) {
        log_error("Failed to send client join packet: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }
      log_info("Sent client join packet with capabilities: video=%s, audio=%s, color=%s, stretch=%s",
               (my_capabilities & CLIENT_CAP_VIDEO) ? "yes" : "no", (my_capabilities & CLIENT_CAP_AUDIO) ? "yes" : "no",
               (my_capabilities & CLIENT_CAP_COLOR) ? "yes" : "no",
               (my_capabilities & CLIENT_CAP_STRETCH) ? "yes" : "no");

      // Set socket keepalive to detect broken connections
      if (set_socket_keepalive(sockfd) < 0) {
        log_warn("Failed to set socket keepalive: %s", network_error_string(errno));
      }

      // Reset connection lost flag for new connection
      g_connection_lost = false;

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
  }

  shutdown_client();
  return 0;
}

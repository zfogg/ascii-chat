/**
 * @file session/host.c
 * @brief 🏠 Server-side session hosting implementation
 * @ingroup session
 *
 * Implements the session host abstraction for server-side client management
 * and session coordination.
 *
 * NOTE: This is a stub implementation that provides the API structure.
 * Full implementation will integrate with existing server code in a future phase.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "host.h"
#include "common.h"
#include "options/options.h"
#include "asciichat_errno.h"
#include "platform/socket.h"
#include "platform/mutex.h"
#include "platform/thread.h"
#include "log/logging.h"
#include "network/packet.h"
#include "ringbuffer.h"
#include "session/audio.h"
#include "audio/opus_codec.h"
#include "util/time.h"

#include <string.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#endif

/* ============================================================================
 * Session Host Constants
 * ============================================================================ */

/** @brief Default maximum clients */
#define SESSION_HOST_DEFAULT_MAX_CLIENTS 32

/* ============================================================================
 * Session Host Internal Types
 * ============================================================================ */

/**
 * @brief Internal client record structure
 */
typedef struct {
  uint32_t client_id;
  socket_t socket;
  char ip_address[64];
  int port;
  bool active;
  bool video_active;
  bool audio_active;
  uint64_t connected_at;

  /** @brief Incoming video frame buffer (for host render thread) */
  image_t *incoming_video;

  /** @brief Incoming audio ringbuffer (written by receive loop, read by render thread) */
  ringbuffer_t *incoming_audio;
} session_host_client_t;

/* ============================================================================
 * Session Host Context Structure
 * ============================================================================ */

/**
 * @brief Internal session host structure
 *
 * Contains server state, client list, and callback configuration.
 */
struct session_host {
  /** @brief Port to listen on */
  int port;

  /** @brief IPv4 bind address */
  char ipv4_address[64];

  /** @brief IPv6 bind address */
  char ipv6_address[64];

  /** @brief Maximum clients */
  int max_clients;

  /** @brief Encryption enabled */
  bool encryption_enabled;

  /** @brief Key path */
  char key_path[512];

  /** @brief Password */
  char password[256];

  /** @brief Event callbacks */
  session_host_callbacks_t callbacks;

  /** @brief User data for callbacks */
  void *user_data;

  /** @brief IPv4 listen socket */
  socket_t socket_v4;

  /** @brief IPv6 listen socket */
  socket_t socket_v6;

  /** @brief Server is running */
  bool running;

  /** @brief Client array */
  session_host_client_t *clients;

  /** @brief Current client count */
  int client_count;

  /** @brief Next client ID counter */
  uint32_t next_client_id;

  /** @brief Client list mutex */
  mutex_t clients_mutex;

  /** @brief Accept thread handle */
  asciichat_thread_t accept_thread;

  /** @brief Accept thread is running */
  bool accept_thread_running;

  /** @brief Receive thread handle */
  asciichat_thread_t receive_thread;

  /** @brief Receive thread is running */
  bool receive_thread_running;

  /** @brief Render thread handle (for video mixing and audio distribution) */
  asciichat_thread_t render_thread;

  /** @brief Render thread is running */
  bool render_thread_running;

  /** @brief Audio context for mixing (host only) */
  session_audio_ctx_t *audio_ctx;

  /** @brief Opus decoder for decoding incoming Opus audio */
  opus_codec_t *opus_decoder;

  /** @brief Context is initialized */
  bool initialized;
};

/* ============================================================================
 * Session Host Lifecycle Functions
 * ============================================================================ */

session_host_t *session_host_create(const session_host_config_t *config) {
  if (!config) {
    SET_ERRNO(ERROR_INVALID_PARAM, "session_host_create: NULL config");
    return NULL;
  }

  // Allocate host
  session_host_t *host = SAFE_CALLOC(1, sizeof(session_host_t), session_host_t *);

  // Copy configuration
  host->port = config->port > 0 ? config->port : OPT_PORT_INT_DEFAULT;
  host->max_clients = config->max_clients > 0 ? config->max_clients : SESSION_HOST_DEFAULT_MAX_CLIENTS;
  host->encryption_enabled = config->encryption_enabled;

  if (config->ipv4_address) {
    SAFE_STRNCPY(host->ipv4_address, config->ipv4_address, sizeof(host->ipv4_address));
  }

  if (config->ipv6_address) {
    SAFE_STRNCPY(host->ipv6_address, config->ipv6_address, sizeof(host->ipv6_address));
  }

  if (config->key_path) {
    SAFE_STRNCPY(host->key_path, config->key_path, sizeof(host->key_path));
  }

  if (config->password) {
    SAFE_STRNCPY(host->password, config->password, sizeof(host->password));
  }

  host->callbacks = config->callbacks;
  host->user_data = config->user_data;

  // Initialize sockets to invalid
  host->socket_v4 = INVALID_SOCKET_VALUE;
  host->socket_v6 = INVALID_SOCKET_VALUE;
  host->running = false;

  // Allocate client array
  host->clients = SAFE_CALLOC((size_t)host->max_clients, sizeof(session_host_client_t), session_host_client_t *);

  host->client_count = 0;
  host->next_client_id = 1;

  // Initialize mutex
  if (mutex_init(&host->clients_mutex) != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize clients mutex");
    SAFE_FREE(host->clients);
    SAFE_FREE(host);
    return NULL;
  }

  host->initialized = true;
  return host;
}

void session_host_destroy(session_host_t *host) {
  if (!host) {
    return;
  }

  // Stop if running
  if (host->running) {
    session_host_stop(host);
  }

  // Clean up audio resources
  if (host->audio_ctx) {
    session_audio_destroy(host->audio_ctx);
    host->audio_ctx = NULL;
  }
  if (host->opus_decoder) {
    opus_codec_destroy(host->opus_decoder);
    host->opus_decoder = NULL;
  }

  // Close sockets
  if (host->socket_v4 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
  }
  if (host->socket_v6 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v6);
    host->socket_v6 = INVALID_SOCKET_VALUE;
  }

  // Free client array and per-client resources
  if (host->clients) {
    for (int i = 0; i < host->max_clients; i++) {
      if (host->clients[i].incoming_video) {
        image_destroy(host->clients[i].incoming_video);
        host->clients[i].incoming_video = NULL;
      }
      if (host->clients[i].incoming_audio) {
        ringbuffer_destroy(host->clients[i].incoming_audio);
        host->clients[i].incoming_audio = NULL;
      }
    }
    SAFE_FREE(host->clients);
  }

  // Destroy mutex
  mutex_destroy(&host->clients_mutex);

  // Clear sensitive data
  memset(host->password, 0, sizeof(host->password));

  host->initialized = false;
  SAFE_FREE(host);
}

/* ============================================================================
 * Session Host Server Control Functions
 * ============================================================================ */

/**
 * @brief Create and bind a listening socket on the given address and port
 * @return Socket on success, INVALID_SOCKET_VALUE on failure
 */
static socket_t create_listen_socket(const char *address, int port) {
  struct addrinfo hints, *result = NULL, *rp = NULL;
  char port_str[16];

  if (!address) address = "0.0.0.0";

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;     // IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  snprintf(port_str, sizeof(port_str), "%d", port);

  int s = getaddrinfo(address, port_str, &hints, &result);
  if (s != 0) {
    SET_ERRNO(ERROR_NETWORK, "getaddrinfo failed: %s", gai_strerror(s));
    return INVALID_SOCKET_VALUE;
  }

  socket_t listen_sock = INVALID_SOCKET_VALUE;
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    listen_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (listen_sock == INVALID_SOCKET_VALUE) {
      continue;
    }

    // Set SO_REUSEADDR to allow rebinding quickly after restart
    int reuse = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0) {
      log_warn("setsockopt SO_REUSEADDR failed");
    }

    if (bind(listen_sock, rp->ai_addr, (int)rp->ai_addrlen) == 0) {
      break; // Success
    }

    socket_close(listen_sock);
    listen_sock = INVALID_SOCKET_VALUE;
  }

  freeaddrinfo(result);

  if (listen_sock == INVALID_SOCKET_VALUE) {
    SET_ERRNO_SYS(ERROR_NETWORK_BIND, "Failed to bind listen socket on %s:%d", address, port);
    return INVALID_SOCKET_VALUE;
  }

  if (listen(listen_sock, SOMAXCONN) != 0) {
    SET_ERRNO_SYS(ERROR_NETWORK_BIND, "listen() failed on %s:%d", address, port);
    socket_close(listen_sock);
    return INVALID_SOCKET_VALUE;
  }

  return listen_sock;
}

/**
 * @brief Accept loop thread - continuously accept incoming client connections
 * @param arg session_host_t pointer
 * @return NULL
 *
 * This thread runs in a loop accepting connections on the listen socket.
 * For each accepted connection, it adds the client to the host and invokes callbacks.
 */
static void *accept_loop_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;
  if (!host) return NULL;

  log_info("Accept loop started");

  // Use select() to handle accept with timeout
  struct timeval tv;
  fd_set readfds;
  int max_fd;

  while (host->accept_thread_running && host->running) {
    // Set timeout to 1 second
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    max_fd = 0;

    if (host->socket_v4 != INVALID_SOCKET_VALUE) {
      FD_SET(host->socket_v4, &readfds);
      max_fd = (int)host->socket_v4;
    }

    // Wait for incoming connections
    int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);
    if (activity < 0) {
      if (errno != EINTR) {
        log_error("select() failed");
      }
      continue;
    }

    if (activity == 0) {
      // Timeout - check if we should exit
      continue;
    }

    // Check for incoming connections
    if (host->socket_v4 != INVALID_SOCKET_VALUE && FD_ISSET(host->socket_v4, &readfds)) {
      struct sockaddr_in client_addr;
      socklen_t client_addr_len = sizeof(client_addr);

      // Accept incoming connection
      socket_t client_socket = accept(host->socket_v4, (struct sockaddr *)&client_addr, &client_addr_len);
      if (client_socket == INVALID_SOCKET_VALUE) {
        log_warn("accept() failed");
        continue;
      }

      // Get client IP and port
      char client_ip[64];
      inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
      int client_port = ntohs(client_addr.sin_port);

      log_info("New connection from %s:%d", client_ip, client_port);

      // Add client to host
      uint32_t client_id = session_host_add_client(host, client_socket, client_ip, client_port);
      if (client_id == 0) {
        log_error("Failed to add client");
        socket_close(client_socket);
      }
    }
  }

  log_info("Accept loop stopped");
  return NULL;
}

/**
 * @brief Receive loop thread - continuously receive packets from connected clients
 * @param arg session_host_t pointer
 * @return NULL
 *
 * This thread runs in a loop listening for packets from all connected clients.
 * When a packet arrives, it processes it based on the packet type.
 */
static void *receive_loop_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;
  if (!host) return NULL;

  log_info("Receive loop started");

  struct timeval tv;
  fd_set readfds;
  socket_t max_fd;

  while (host->receive_thread_running && host->running) {
    // Set timeout to 1 second
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO(&readfds);
    max_fd = INVALID_SOCKET_VALUE;

    // Add all active client sockets to the set
    mutex_lock(&host->clients_mutex);
    for (int i = 0; i < host->max_clients; i++) {
      if (host->clients[i].active && host->clients[i].socket != INVALID_SOCKET_VALUE) {
        FD_SET(host->clients[i].socket, &readfds);
        if (max_fd == INVALID_SOCKET_VALUE || host->clients[i].socket > max_fd) {
          max_fd = host->clients[i].socket;
        }
      }
    }
    mutex_unlock(&host->clients_mutex);

    // If no clients, just wait for timeout
    if (max_fd == INVALID_SOCKET_VALUE) {
      continue;
    }

    // Wait for data on any client socket
    int activity = select((int)max_fd + 1, &readfds, NULL, NULL, &tv);
    if (activity < 0) {
      if (errno != EINTR) {
        log_error("select() failed in receive loop");
      }
      continue;
    }

    if (activity == 0) {
      // Timeout - check if we should exit
      continue;
    }

    // Check each client for incoming data
    mutex_lock(&host->clients_mutex);
    for (int i = 0; i < host->max_clients; i++) {
      if (!host->clients[i].active || host->clients[i].socket == INVALID_SOCKET_VALUE) {
        continue;
      }

      if (!FD_ISSET(host->clients[i].socket, &readfds)) {
        continue;
      }

      // Try to receive packet from this client
      packet_type_t ptype;
      void *data = NULL;
      size_t len = 0;
      asciichat_error_t result = packet_receive(host->clients[i].socket, &ptype, &data, &len);

      if (result != ASCIICHAT_OK) {
        log_warn("packet_receive failed from client %u: %d", host->clients[i].client_id, result);
        // Client disconnected or error - will be cleaned up by timeout mechanism
        continue;
      }

      // Process packet based on type
      uint32_t client_id = host->clients[i].client_id;
      mutex_unlock(&host->clients_mutex);

      switch (ptype) {
      case PACKET_TYPE_IMAGE_FRAME:
        // Client sent a video frame - convert to image and invoke callback
        if (host->callbacks.on_frame_received && data && len > 0) {
          // TODO: Parse frame data and create image_t, invoke callback
          log_debug_every(500000, "Frame received from client %u (size=%zu)", client_id, len);
        }
        break;

      case PACKET_TYPE_AUDIO:
        // Client sent audio data - invoke callback
        if (host->callbacks.on_audio_received && data && len > 0) {
          // TODO: Parse audio data, invoke callback
          log_debug_every(1000000, "Audio received from client %u (size=%zu)", client_id, len);
        }
        break;

      case PACKET_TYPE_STREAM_START:
        log_info("Client %u started streaming", client_id);
        mutex_lock(&host->clients_mutex);
        for (int j = 0; j < host->max_clients; j++) {
          if (host->clients[j].client_id == client_id) {
            host->clients[j].video_active = true;
            break;
          }
        }
        mutex_unlock(&host->clients_mutex);
        break;

      case PACKET_TYPE_STREAM_STOP:
        log_info("Client %u stopped streaming", client_id);
        mutex_lock(&host->clients_mutex);
        for (int j = 0; j < host->max_clients; j++) {
          if (host->clients[j].client_id == client_id) {
            host->clients[j].video_active = false;
            break;
          }
        }
        mutex_unlock(&host->clients_mutex);
        break;

      case PACKET_TYPE_PING:
        // Respond with PONG
        log_debug_every(1000000, "PING from client %u", client_id);
        // TODO: Send PONG response
        break;

      case PACKET_TYPE_CLIENT_LEAVE:
        log_info("Client %u requested disconnect", client_id);
        // Mark for removal (will be handled separately to avoid deadlock)
        break;

      default:
        log_warn("Unknown packet type %u from client %u", ptype, client_id);
        break;
      }

      // Free packet data
      if (data) {
        SAFE_FREE(data);
      }

      mutex_lock(&host->clients_mutex);
    }
    mutex_unlock(&host->clients_mutex);
  }

  log_info("Receive loop stopped");
  return NULL;
}

/**
 * @brief Host render thread - mixes media and broadcasts to participants
 *
 * DESIGN: Reuses patterns from server/render.c
 * - Collects video frames from all participants (60 FPS)
 * - Broadcasts mixed ASCII frame to all participants
 * - Mixes audio from all participants (100 FPS)
 * - Broadcasts mixed audio to all participants
 */
static void *host_render_thread(void *arg) {
  session_host_t *host = (session_host_t *)arg;
  if (!host) {
    SET_ERRNO(ERROR_INVALID_PARAM, "host_render_thread: invalid host");
    return NULL;
  }

  log_info("Host render thread started");

  uint64_t last_video_render_ns = 0;
  uint64_t last_audio_render_ns = 0;

  while (host->render_thread_running && host->running) {
    uint64_t now_ns = time_get_ns();

    // VIDEO RENDERING (60 FPS = 16.7ms)
    if (time_elapsed_ns(last_video_render_ns, now_ns) >= NS_PER_MS_INT * 16) {
      // TODO: Collect video frames from all participants
      // TODO: Generate mixed ASCII frame using create_mixed_ascii_frame_for_client()
      // TODO: Broadcast frame to all participants via send_ascii_frame_packet()
      log_debug_every(1000000, "Video render cycle");
      last_video_render_ns = now_ns;
    }

    // AUDIO RENDERING (100 FPS = 10ms)
    if (time_elapsed_ns(last_audio_render_ns, now_ns) >= NS_PER_MS_INT * 10) {
      // TODO: Mix audio from all participants
      // TODO: Encode with Opus
      // TODO: Broadcast mixed audio via av_send_audio_opus_batch()
      log_debug_every(1000000, "Audio render cycle");
      last_audio_render_ns = now_ns;
    }

    // Small sleep to prevent busy-loop
    platform_sleep_ms(1);
  }

  log_info("Host render thread stopped");
  return NULL;
}

asciichat_error_t session_host_start(session_host_t *host) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_start: invalid host");
  }

  if (host->running) {
    return ASCIICHAT_OK; // Already running
  }

  // Create listen socket(s)
  // For simplicity, we bind to IPv4 if specified, otherwise default to 0.0.0.0
  const char *bind_address = host->ipv4_address[0] ? host->ipv4_address : "0.0.0.0";

  host->socket_v4 = create_listen_socket(bind_address, host->port);
  if (host->socket_v4 == INVALID_SOCKET_VALUE) {
    log_error("Failed to create IPv4 listen socket");
    if (host->callbacks.on_error) {
      host->callbacks.on_error(host, ERROR_NETWORK_BIND, "Failed to create listen socket", host->user_data);
    }
    return GET_ERRNO();
  }

  host->running = true;
  log_info("Session host listening on %s:%d", bind_address, host->port);

  // Spawn accept loop thread
  host->accept_thread_running = true;
  if (asciichat_thread_create(&host->accept_thread, accept_loop_thread, host) != 0) {
    log_error("Failed to spawn accept loop thread");
    host->accept_thread_running = false;
    if (host->callbacks.on_error) {
      host->callbacks.on_error(host, ERROR_THREAD, "Failed to spawn accept loop thread", host->user_data);
    }
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
    host->running = false;
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn accept loop thread");
  }

  // Spawn receive loop thread
  host->receive_thread_running = true;
  if (asciichat_thread_create(&host->receive_thread, receive_loop_thread, host) != 0) {
    log_error("Failed to spawn receive loop thread");
    host->receive_thread_running = false;
    host->accept_thread_running = false;
    asciichat_thread_join(&host->accept_thread, NULL);
    if (host->callbacks.on_error) {
      host->callbacks.on_error(host, ERROR_THREAD, "Failed to spawn receive loop thread", host->user_data);
    }
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
    host->running = false;
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn receive loop thread");
  }

  // Spawn render thread (optional - can be started later)
  // This thread handles video mixing and audio distribution
  return ASCIICHAT_OK;
}

void session_host_stop(session_host_t *host) {
  if (!host || !host->initialized || !host->running) {
    return;
  }

  // Stop render thread if running
  if (host->render_thread_running) {
    host->render_thread_running = false;
    asciichat_thread_join(&host->render_thread, NULL);
    log_info("Render thread joined");
  }

  // Stop receive loop thread (reads from client sockets)
  if (host->receive_thread_running) {
    host->receive_thread_running = false;
    asciichat_thread_join(&host->receive_thread, NULL);
    log_info("Receive loop thread joined");
  }

  // Stop accept loop thread (before closing listen socket)
  if (host->accept_thread_running) {
    host->accept_thread_running = false;
    asciichat_thread_join(&host->accept_thread, NULL);
    log_info("Accept loop thread joined");
  }

  // Disconnect all clients
  mutex_lock(&host->clients_mutex);
  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active) {
      // Invoke callback
      if (host->callbacks.on_client_leave) {
        host->callbacks.on_client_leave(host, host->clients[i].client_id, host->user_data);
      }

      // Close socket
      if (host->clients[i].socket != INVALID_SOCKET_VALUE) {
        socket_close(host->clients[i].socket);
        host->clients[i].socket = INVALID_SOCKET_VALUE;
      }

      host->clients[i].active = false;
    }
  }
  host->client_count = 0;
  mutex_unlock(&host->clients_mutex);

  // Close listen sockets
  if (host->socket_v4 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
  }
  if (host->socket_v6 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v6);
    host->socket_v6 = INVALID_SOCKET_VALUE;
  }

  host->running = false;
}

bool session_host_is_running(session_host_t *host) {
  if (!host || !host->initialized) {
    return false;
  }
  return host->running;
}

/* ============================================================================
 * Session Host Client Management Functions
 * ============================================================================ */

uint32_t session_host_add_client(session_host_t *host, socket_t socket, const char *ip, int port) {
  if (!host || !host->initialized) {
    return 0;
  }

  mutex_lock(&host->clients_mutex);

  // Check if we have room
  if (host->client_count >= host->max_clients) {
    mutex_unlock(&host->clients_mutex);
    SET_ERRNO(ERROR_SESSION_FULL, "Maximum clients reached");
    return 0;
  }

  // Find empty slot
  for (int i = 0; i < host->max_clients; i++) {
    if (!host->clients[i].active) {
      host->clients[i].client_id = host->next_client_id++;
      host->clients[i].socket = socket;
      if (ip) {
        SAFE_STRNCPY(host->clients[i].ip_address, ip, sizeof(host->clients[i].ip_address));
      }
      host->clients[i].port = port;
      host->clients[i].active = true;
      host->clients[i].video_active = false;
      host->clients[i].audio_active = false;
      host->clients[i].connected_at = (uint64_t)time(NULL);

      // Allocate media buffers
      host->clients[i].incoming_video = image_new(480, 270);  // Network-optimal size (HD preview)
      host->clients[i].incoming_audio = ringbuffer_create(sizeof(float), 960 * 10);  // ~200ms buffer @ 48kHz

      if (!host->clients[i].incoming_video || !host->clients[i].incoming_audio) {
        // Cleanup on allocation failure
        if (host->clients[i].incoming_video) {
          image_destroy(host->clients[i].incoming_video);
          host->clients[i].incoming_video = NULL;
        }
        if (host->clients[i].incoming_audio) {
          ringbuffer_destroy(host->clients[i].incoming_audio);
          host->clients[i].incoming_audio = NULL;
        }
        host->clients[i].active = false;
        mutex_unlock(&host->clients_mutex);
        SET_ERRNO(ERROR_MEMORY, "Failed to allocate media buffers for client");
        return 0;
      }

      uint32_t client_id = host->clients[i].client_id;
      host->client_count++;

      mutex_unlock(&host->clients_mutex);

      // Invoke callback
      if (host->callbacks.on_client_join) {
        host->callbacks.on_client_join(host, client_id, host->user_data);
      }

      return client_id;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return 0;
}

asciichat_error_t session_host_remove_client(session_host_t *host, uint32_t client_id) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_remove_client: invalid host");
  }

  mutex_lock(&host->clients_mutex);

  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == client_id) {
      // Invoke callback before removing
      mutex_unlock(&host->clients_mutex);
      if (host->callbacks.on_client_leave) {
        host->callbacks.on_client_leave(host, client_id, host->user_data);
      }
      mutex_lock(&host->clients_mutex);

      // Close socket
      if (host->clients[i].socket != INVALID_SOCKET_VALUE) {
        socket_close(host->clients[i].socket);
        host->clients[i].socket = INVALID_SOCKET_VALUE;
      }

      // Clean up media buffers
      if (host->clients[i].incoming_video) {
        image_destroy(host->clients[i].incoming_video);
        host->clients[i].incoming_video = NULL;
      }
      if (host->clients[i].incoming_audio) {
        ringbuffer_destroy(host->clients[i].incoming_audio);
        host->clients[i].incoming_audio = NULL;
      }

      host->clients[i].active = false;
      host->client_count--;

      mutex_unlock(&host->clients_mutex);
      return ASCIICHAT_OK;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return SET_ERRNO(ERROR_NOT_FOUND, "Client not found: %u", client_id);
}

asciichat_error_t session_host_find_client(session_host_t *host, uint32_t client_id, session_host_client_info_t *info) {
  if (!host || !host->initialized || !info) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_find_client: invalid parameter");
  }

  mutex_lock(&host->clients_mutex);

  for (int i = 0; i < host->max_clients; i++) {
    if (host->clients[i].active && host->clients[i].client_id == client_id) {
      info->client_id = host->clients[i].client_id;
      SAFE_STRNCPY(info->ip_address, host->clients[i].ip_address, sizeof(info->ip_address));
      info->port = host->clients[i].port;
      info->video_active = host->clients[i].video_active;
      info->audio_active = host->clients[i].audio_active;
      info->connected_at = host->clients[i].connected_at;

      mutex_unlock(&host->clients_mutex);
      return ASCIICHAT_OK;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return SET_ERRNO(ERROR_NOT_FOUND, "Client not found: %u", client_id);
}

int session_host_get_client_count(session_host_t *host) {
  if (!host || !host->initialized) {
    return 0;
  }
  return host->client_count;
}

int session_host_get_client_ids(session_host_t *host, uint32_t *ids, int max_ids) {
  if (!host || !host->initialized || !ids || max_ids <= 0) {
    return 0;
  }

  mutex_lock(&host->clients_mutex);

  int count = 0;
  for (int i = 0; i < host->max_clients && count < max_ids; i++) {
    if (host->clients[i].active) {
      ids[count++] = host->clients[i].client_id;
    }
  }

  mutex_unlock(&host->clients_mutex);
  return count;
}

/* ============================================================================
 * Session Host Broadcast Functions
 * ============================================================================ */

asciichat_error_t session_host_broadcast_frame(session_host_t *host, const char *frame) {
  if (!host || !host->initialized || !frame) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_broadcast_frame: invalid parameter");
  }

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_host_broadcast_frame: not running");
  }

  // TODO: Implement actual broadcast using network packet sending

  return SET_ERRNO(ERROR_NOT_SUPPORTED, "session_host_broadcast_frame: not implemented yet");
}

asciichat_error_t session_host_send_frame(session_host_t *host, uint32_t client_id, const char *frame) {
  if (!host || !host->initialized || !frame) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_send_frame: invalid parameter");
  }

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_host_send_frame: not running");
  }

  // TODO: Implement actual send using network packet sending

  (void)client_id; // Suppress unused warning until implemented

  return SET_ERRNO(ERROR_NOT_SUPPORTED, "session_host_send_frame: not implemented yet");
}

/* ============================================================================
 * Session Host Render Thread Functions
 * ============================================================================ */

asciichat_error_t session_host_start_render(session_host_t *host) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_start_render: invalid host");
  }

  if (!host->running) {
    return SET_ERRNO(ERROR_INVALID_STATE, "session_host_start_render: not running");
  }

  if (host->render_thread_running) {
    return ASCIICHAT_OK;  // Already running
  }

  // Create audio context for mixing (host mode = true)
  if (!host->audio_ctx) {
    host->audio_ctx = session_audio_create(true);
    if (!host->audio_ctx) {
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to create audio context");
    }
  }

  // Create Opus decoder for decoding incoming Opus audio (48kHz)
  if (!host->opus_decoder) {
    host->opus_decoder = opus_codec_create_decoder(48000);
    if (!host->opus_decoder) {
      session_audio_destroy(host->audio_ctx);
      host->audio_ctx = NULL;
      return SET_ERRNO(ERROR_INVALID_STATE, "Failed to create Opus decoder");
    }
  }

  // Spawn render thread
  host->render_thread_running = true;
  if (asciichat_thread_create(&host->render_thread, host_render_thread, host) != 0) {
    log_error("Failed to spawn render thread");
    host->render_thread_running = false;
    return SET_ERRNO(ERROR_THREAD, "Failed to spawn render thread");
  }

  log_info("Host render thread started");
  return ASCIICHAT_OK;
}

void session_host_stop_render(session_host_t *host) {
  if (!host || !host->initialized) {
    return;
  }

  if (!host->render_thread_running) {
    return;
  }

  // Signal thread to stop
  host->render_thread_running = false;

  // Wait for thread to complete
  asciichat_thread_join(&host->render_thread, NULL);

  // Clean up audio resources
  if (host->audio_ctx) {
    session_audio_destroy(host->audio_ctx);
    host->audio_ctx = NULL;
  }
  if (host->opus_decoder) {
    opus_codec_destroy(host->opus_decoder);
    host->opus_decoder = NULL;
  }

  log_info("Host render thread stopped");
}

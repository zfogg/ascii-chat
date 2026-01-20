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
#include "asciichat_errno.h"
#include "platform/socket.h"
#include "platform/mutex.h"

#include <string.h>
#include <time.h>

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
  host->port = config->port > 0 ? config->port : 27224;
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

  // Close sockets
  if (host->socket_v4 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v4);
    host->socket_v4 = INVALID_SOCKET_VALUE;
  }
  if (host->socket_v6 != INVALID_SOCKET_VALUE) {
    socket_close(host->socket_v6);
    host->socket_v6 = INVALID_SOCKET_VALUE;
  }

  // Free client array
  if (host->clients) {
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

asciichat_error_t session_host_start(session_host_t *host) {
  if (!host || !host->initialized) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "session_host_start: invalid host");
  }

  if (host->running) {
    return ASCIICHAT_OK; // Already running
  }

  // TODO: Implement actual server start logic using existing server code
  // This would involve:
  // 1. Creating listen sockets
  // 2. Binding to port
  // 3. Starting accept thread
  //
  // For now, this is a stub that sets up the structure

  log_info("session_host_start: stub implementation - would listen on port %d", host->port);

  // Invoke error callback since this is not implemented yet
  if (host->callbacks.on_error) {
    host->callbacks.on_error(host, ERROR_NOT_SUPPORTED, "Server start not implemented yet (stub)", host->user_data);
  }

  return SET_ERRNO(ERROR_NOT_SUPPORTED, "session_host_start: not implemented yet");
}

void session_host_stop(session_host_t *host) {
  if (!host || !host->initialized || !host->running) {
    return;
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

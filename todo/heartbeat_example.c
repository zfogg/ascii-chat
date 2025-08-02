// Example: Heartbeat mechanism
// Add to network.c and network.h

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../common.h"
#include "../network.h"

// network.h additions:
#define PING_MESSAGE "PING\n"
#define PONG_MESSAGE "PONG\n"
#define HEARTBEAT_INTERVAL 15 // seconds
#define HEARTBEAT_TIMEOUT 5   // seconds

int send_ping(int sockfd);
int wait_for_pong(int sockfd, int timeout_seconds);
bool is_connection_alive(int sockfd);

// network.c implementation:
int send_ping(int sockfd) {
  const char *ping = PING_MESSAGE;
  ssize_t sent = send_with_timeout(sockfd, ping, strlen(ping), SEND_TIMEOUT);
  if (sent != (ssize_t)strlen(ping)) {
    return -1;
  }
  return 0;
}

int wait_for_pong(int sockfd, int timeout_seconds) {
  char buffer[16];
  ssize_t received = recv_with_timeout(sockfd, buffer, sizeof(buffer) - 1, timeout_seconds);

  if (received <= 0) {
    return -1; // Timeout or error
  }

  buffer[received] = '\0';
  if (strncmp(buffer, PONG_MESSAGE, strlen(PONG_MESSAGE)) == 0) {
    return 0; // Valid pong received
  }

  return -1; // Invalid response
}

bool is_connection_alive(int sockfd) {
  if (send_ping(sockfd) < 0) {
    return false;
  }

  return wait_for_pong(sockfd, HEARTBEAT_TIMEOUT) == 0;
}

#if defined(ENABLE_HEARTBEAT)
// client.c modifications:
// Add to client receive loop:
void handle_incoming_message(char *buffer, int length) {
  if (strncmp(buffer, PING_MESSAGE, strlen(PING_MESSAGE)) == 0) {
    // Respond to ping with pong
    send_with_timeout(sockfd, PONG_MESSAGE, strlen(PONG_MESSAGE), SEND_TIMEOUT);
    log_debug("Responded to server ping");
    return;
  }

  // Otherwise, it's a regular frame
  ascii_write(buffer);
}

// server.c modifications:
// Add heartbeat monitoring to client thread:
void *client_handler_thread_with_heartbeat(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  char *frame_buffer;
  SAFE_MALLOC(frame_buffer, FRAME_BUFFER_SIZE_FINAL, char *);
  time_t last_heartbeat = time(NULL);

  while (!g_should_exit && client->active) {
    // Check if heartbeat is needed
    time_t now = time(NULL);
    if (now - last_heartbeat >= HEARTBEAT_INTERVAL) {
      if (!is_connection_alive(client->socket)) {
        log_warn("Client %s:%d failed heartbeat check", client->client_ip, client->port);
        break;
      }
      last_heartbeat = now;
      log_debug("Heartbeat OK for client %s:%d", client->client_ip, client->port);
    }

    // Send frame logic...
    if (!framebuffer_read_frame(g_frame_buffer, frame_buffer)) {
      usleep(1000);
      continue;
    }

    size_t frame_len = strlen(frame_buffer);
    ssize_t sent = send_with_timeout(client->socket, frame_buffer, frame_len, SEND_TIMEOUT);

    if (sent < 0) {
      log_warn("Send failed to client %s:%d: %s", client->client_ip, client->port, network_error_string(errno));
      break;
    }

    client->frames_sent++;
  }

  // Cleanup...
  close(client->socket);
  client->active = false;
  free(frame_buffer);
  return NULL;
}
#endif
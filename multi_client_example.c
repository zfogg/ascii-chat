// Example: Multi-client server enhancement
// TODO: add this to server.c

#if defined(ENABLE_MULTI_CLIENT)
#define MAX_CLIENTS 10

typedef struct {
  int socket;
  pthread_t thread;
  char client_ip[INET_ADDRSTRLEN];
  int port;
  unsigned short width, height;
  bool active;
  time_t connected_at;
  uint64_t frames_sent;
} client_info_t;

typedef struct {
  client_info_t clients[MAX_CLIENTS];
  int client_count;
  pthread_mutex_t mutex;
} client_manager_t;

static client_manager_t g_clients = {0};

void *client_handler_thread(void *arg) {
  client_info_t *client = (client_info_t *)arg;
  char *frame_buffer = (char *)malloc(FRAME_BUFFER_SIZE_FINAL);

  log_info("Client handler started for %s:%d", client->client_ip, client->port);

  while (!g_should_exit && client->active) {
    // Get frame from shared buffer
    if (!framebuffer_read_frame(g_frame_buffer, frame_buffer)) {
      usleep(1000); // 1ms wait
      continue;
    }

    // Send frame to this specific client
    size_t frame_len = strlen(frame_buffer);
    ssize_t sent = send_with_timeout(client->socket, frame_buffer, frame_len, SEND_TIMEOUT);

    if (sent < 0) {
      log_warn("Client %s:%d disconnected: %s", client->client_ip, client->port, network_error_string(errno));
      break;
    }

    client->frames_sent++;
  }

  // Cleanup
  close(client->socket);
  client->active = false;
  free(frame_buffer);

  pthread_mutex_lock(&g_clients.mutex);
  g_clients.client_count--;
  pthread_mutex_unlock(&g_clients.mutex);

  log_info("Client handler finished for %s:%d (%lu frames sent)", client->client_ip, client->port, client->frames_sent);
  return NULL;
}

int add_client(int sockfd, struct sockaddr_in *addr) {
  pthread_mutex_lock(&g_clients.mutex);

  if (g_clients.client_count >= MAX_CLIENTS) {
    pthread_mutex_unlock(&g_clients.mutex);
    return -1; // Server full
  }

  // Find empty slot
  client_info_t *client = NULL;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!g_clients.clients[i].active) {
      client = &g_clients.clients[i];
      break;
    }
  }

  // Initialize client
  client->socket = sockfd;
  client->active = true;
  client->connected_at = time(NULL);
  client->frames_sent = 0;
  client->port = ntohs(addr->sin_port);
  inet_ntop(AF_INET, &addr->sin_addr, client->client_ip, sizeof(client->client_ip));

  // Create thread
  if (pthread_create(&client->thread, NULL, client_handler_thread, client) != 0) {
    client->active = false;
    pthread_mutex_unlock(&g_clients.mutex);
    return -1;
  }

  g_clients.client_count++;
  pthread_mutex_unlock(&g_clients.mutex);

  log_info("Added client %s:%d (total: %d)", client->client_ip, client->port, g_clients.client_count);
  return 0;
}

// Replace main connection loop with:
while (!g_should_exit) {
  connfd = accept_with_timeout(listenfd, (struct sockaddr *)&client_addr, &client_len, ACCEPT_TIMEOUT);
  if (connfd < 0) {
    if (errno == ETIMEDOUT)
      continue;
    log_error("Accept failed: %s", network_error_string(errno));
    continue;
  }

  // Try to add client
  if (add_client(connfd, &client_addr) < 0) {
    log_warn("Server full or thread creation failed, rejecting client");
    close(connfd);
  }
}
#endif
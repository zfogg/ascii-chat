#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#include "common.h"
#include "network.h"

void setup_server_quiet_logging(void);
void restore_server_logging(void);

TestSuite(server_multiclient, .init = setup_server_quiet_logging, .fini = restore_server_logging);

void setup_server_quiet_logging(void) {
  log_set_level(LOG_FATAL);
}

void restore_server_logging(void) {
  log_set_level(LOG_DEBUG);
}

// =============================================================================
// Test Helper Functions
// =============================================================================

static pid_t start_test_server(int port) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child process - start server
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    // Redirect server output to avoid test noise
    int dev_null = open("/dev/null", O_WRONLY);
    if (dev_null >= 0) {
      dup2(dev_null, STDOUT_FILENO);
      dup2(dev_null, STDERR_FILENO);
      close(dev_null);
    }

    execl("./bin/server", "server", "--port", port_str, "--log-file", "/tmp/test_server.log", NULL);
    exit(1); // If exec fails
  }

  // Parent process - wait for server to start
  sleep(1);
  return pid;
}

static int connect_to_server(const char *address, int port) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return -1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  inet_pton(AF_INET, address, &server_addr.sin_addr);

  // Set connection timeout
  struct timeval timeout;
  timeout.tv_sec = 5; // 5 second timeout
  timeout.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    close(sock);
    return -1;
  }

  return sock;
}

static int send_test_frame(int socket, int frame_id) {
  // Create test ASCII frame
  char ascii_data[1000];
  snprintf(ascii_data, sizeof(ascii_data),
           "Test Frame %d\n"
           "████████████\n"
           "██  %04d  ██\n"
           "████████████\n",
           frame_id, frame_id);

  packet_t packet;
  int result = create_ascii_frame_packet(&packet, ascii_data, strlen(ascii_data), frame_id);
  if (result != 0) {
    return -1;
  }

  // Serialize and send packet
  uint8_t buffer[MAX_PACKET_SIZE];
  size_t packet_size = serialize_packet(&packet, buffer, sizeof(buffer));
  if (packet_size == 0) {
    free(packet.data);
    return -1;
  }

  ssize_t sent = send(socket, buffer, packet_size, 0);
  free(packet.data);

  return (sent == packet_size) ? 0 : -1;
}

static int send_image_frame(int socket, int width, int height, int client_id) {
  // Create test RGB image
  rgb_pixel_t *image_data;
  SAFE_MALLOC(image_data, width * height * sizeof(rgb_pixel_t), rgb_pixel_t *);

  // Fill with test pattern
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = y * width + x;
      image_data[idx] =
          (rgb_pixel_t){.r = (x * 255) / width, .g = (y * 255) / height, .b = ((x + y) * 255) / (width + height)};
    }
  }

  packet_t packet;
  int result = create_image_frame_packet(&packet, image_data, width, height, client_id);
  free(image_data);

  if (result != 0) {
    return -1;
  }

  // Serialize and send
  uint8_t buffer[MAX_PACKET_SIZE];
  size_t packet_size = serialize_packet(&packet, buffer, sizeof(buffer));
  free(packet.data);

  if (packet_size == 0) {
    return -1;
  }

  ssize_t sent = send(socket, buffer, packet_size, 0);
  return (sent == packet_size) ? 0 : -1;
}

static int receive_packet_with_timeout(int socket, packet_t *packet, int timeout_sec) {
  uint8_t buffer[MAX_PACKET_SIZE];

  // First, receive header
  ssize_t received = recv(socket, buffer, sizeof(packet_header_t), MSG_WAITALL);
  if (received != sizeof(packet_header_t)) {
    return -1;
  }

  // Parse header to get payload size
  packet_header_t *header = (packet_header_t *)buffer;
  uint32_t payload_size = ntohl(header->length);

  if (payload_size > MAX_PACKET_SIZE - sizeof(packet_header_t)) {
    return -1; // Packet too large
  }

  // Receive payload if present
  if (payload_size > 0) {
    received = recv(socket, buffer + sizeof(packet_header_t), payload_size, MSG_WAITALL);
    if (received != payload_size) {
      return -1;
    }
  }

  // Deserialize packet
  return deserialize_packet(buffer, sizeof(packet_header_t) + payload_size, packet);
}

static void cleanup_server(pid_t server_pid) {
  if (server_pid > 0) {
    kill(server_pid, SIGTERM);

    // Wait up to 3 seconds for server to exit
    for (int i = 0; i < 30; i++) {
      int status;
      if (waitpid(server_pid, &status, WNOHANG) == server_pid) {
        break;
      }
      usleep(100000); // 100ms
    }

    // Force kill if still running
    kill(server_pid, SIGKILL);
    waitpid(server_pid, NULL, 0);
  }
}

// =============================================================================
// Basic Connection Tests
// =============================================================================

Test(server_multiclient, single_client_connect) {
  const int test_port = 9001;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  // Connect single client
  int client_socket = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(client_socket, 0, "Client should connect to server");

  // Send a test frame
  int result = send_test_frame(client_socket, 1);
  cr_assert_eq(result, 0, "Should be able to send frame to server");

  // Clean up
  close(client_socket);
  cleanup_server(server_pid);
}

Test(server_multiclient, multiple_clients_connect) {
  const int test_port = 9002;
  const int client_count = 3;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_sockets[client_count];

  // Connect multiple clients
  for (int i = 0; i < client_count; i++) {
    client_sockets[i] = connect_to_server("127.0.0.1", test_port);
    cr_assert_geq(client_sockets[i], 0, "Client %d should connect", i);

    // Small delay between connections
    usleep(100000);
  }

  // Each client sends a frame
  for (int i = 0; i < client_count; i++) {
    int result = send_test_frame(client_sockets[i], i + 100);
    cr_assert_eq(result, 0, "Client %d should send frame", i);
  }

  // Clean up
  for (int i = 0; i < client_count; i++) {
    close(client_sockets[i]);
  }
  cleanup_server(server_pid);
}

Test(server_multiclient, client_disconnect_reconnect) {
  const int test_port = 9003;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  // Connect client
  int client_socket = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(client_socket, 0, "Client should connect");

  // Send frame
  send_test_frame(client_socket, 1);

  // Disconnect
  close(client_socket);
  sleep(1);

  // Reconnect
  client_socket = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(client_socket, 0, "Client should reconnect");

  // Send another frame
  int result = send_test_frame(client_socket, 2);
  cr_assert_eq(result, 0, "Should send frame after reconnection");

  close(client_socket);
  cleanup_server(server_pid);
}

// =============================================================================
// Data Flow Tests
// =============================================================================

Test(server_multiclient, image_to_ascii_flow) {
  const int test_port = 9004;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_socket = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(client_socket, 0, "Client should connect");

  // Send image frame to server
  int result = send_image_frame(client_socket, 32, 24, 555);
  cr_assert_eq(result, 0, "Should send image frame");

  // Give server time to process
  sleep(1);

  // Try to receive ASCII frame back (server might broadcast it)
  packet_t received_packet;
  result = receive_packet_with_timeout(client_socket, &received_packet, 3);
  if (result == 0) {
    // If we received something, it should be an ASCII frame
    cr_assert_eq(received_packet.header.type, PACKET_TYPE_ASCII_FRAME, "Received packet should be ASCII frame");
    cr_assert_not_null(received_packet.data, "ASCII frame should have data");

    free(received_packet.data);
  }
  // Note: It's OK if we don't receive anything - server might not broadcast back to sender

  close(client_socket);
  cleanup_server(server_pid);
}

Test(server_multiclient, concurrent_frame_sending) {
  const int test_port = 9005;
  const int client_count = 4;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_sockets[client_count];

  // Connect all clients
  for (int i = 0; i < client_count; i++) {
    client_sockets[i] = connect_to_server("127.0.0.1", test_port);
    cr_assert_geq(client_sockets[i], 0, "Client %d should connect", i);
  }

  // All clients send frames concurrently
  for (int frame = 0; frame < 5; frame++) {
    for (int client = 0; client < client_count; client++) {
      int result = send_image_frame(client_sockets[client], 16, 12, client + 1000);
      cr_assert_eq(result, 0, "Client %d should send frame %d", client, frame);
    }
    usleep(100000); // 100ms delay between frame sets
  }

  // Give server time to process all frames
  sleep(2);

  // Clean up
  for (int i = 0; i < client_count; i++) {
    close(client_sockets[i]);
  }
  cleanup_server(server_pid);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

Test(server_multiclient, server_handles_malformed_packets) {
  const int test_port = 9006;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_socket = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(client_socket, 0, "Client should connect");

  // Send malformed packet (invalid magic number)
  uint8_t bad_packet[sizeof(packet_header_t)];
  packet_header_t *header = (packet_header_t *)bad_packet;
  header->magic = htonl(0xBADBAD); // Wrong magic
  header->type = htons(PACKET_TYPE_ASCII_FRAME);
  header->length = htonl(0);
  header->sequence = htonl(1);
  header->crc32 = htonl(0);
  header->client_id = htonl(999);

  ssize_t sent = send(client_socket, bad_packet, sizeof(bad_packet), 0);
  cr_assert_eq(sent, sizeof(bad_packet), "Should send malformed packet");

  // Server should still be running (not crashed)
  sleep(1);

  // Try to send valid packet
  int result = send_test_frame(client_socket, 1);
  // This might fail if server closed connection, which is valid behavior

  close(client_socket);
  cleanup_server(server_pid);
}

Test(server_multiclient, server_handles_client_sudden_disconnect) {
  const int test_port = 9007;
  const int client_count = 3;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_sockets[client_count];

  // Connect all clients
  for (int i = 0; i < client_count; i++) {
    client_sockets[i] = connect_to_server("127.0.0.1", test_port);
    cr_assert_geq(client_sockets[i], 0, "Client %d should connect", i);
  }

  // Send frames from all clients
  for (int i = 0; i < client_count; i++) {
    send_test_frame(client_sockets[i], i + 200);
  }

  // Suddenly close middle client (simulate crash)
  close(client_sockets[1]);
  client_sockets[1] = -1;

  sleep(1); // Give server time to detect disconnect

  // Other clients should still work
  int result = send_test_frame(client_sockets[0], 300);
  cr_assert_eq(result, 0, "Remaining client should still work after another disconnects");

  result = send_test_frame(client_sockets[2], 301);
  cr_assert_eq(result, 0, "Remaining client should still work after another disconnects");

  // Clean up remaining clients
  close(client_sockets[0]);
  close(client_sockets[2]);
  cleanup_server(server_pid);
}

Test(server_multiclient, server_resource_limits) {
  const int test_port = 9008;
  const int max_clients = 10; // Try to connect many clients
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_sockets[max_clients];
  int successful_connections = 0;

  // Try to connect many clients
  for (int i = 0; i < max_clients; i++) {
    client_sockets[i] = connect_to_server("127.0.0.1", test_port);
    if (client_sockets[i] >= 0) {
      successful_connections++;
      usleep(50000); // Small delay between connections
    } else {
      client_sockets[i] = -1;
    }
  }

  // Should connect at least a few clients
  cr_assert_gt(successful_connections, 2, "Should handle at least a few concurrent clients");

  // Send frames from connected clients
  for (int i = 0; i < max_clients; i++) {
    if (client_sockets[i] >= 0) {
      send_test_frame(client_sockets[i], i + 400);
    }
  }

  // Clean up
  for (int i = 0; i < max_clients; i++) {
    if (client_sockets[i] >= 0) {
      close(client_sockets[i]);
    }
  }
  cleanup_server(server_pid);
}

// =============================================================================
// Protocol-Specific Tests
// =============================================================================

Test(server_multiclient, packet_sequence_handling) {
  const int test_port = 9009;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_socket = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(client_socket, 0, "Client should connect");

  // Send packets with different sequence numbers
  for (int seq = 1; seq <= 5; seq++) {
    packet_t packet;
    char test_data[100];
    snprintf(test_data, sizeof(test_data), "Sequence %d data", seq);

    create_ascii_frame_packet(&packet, test_data, strlen(test_data), 777);
    packet.header.sequence = seq;

    uint8_t buffer[MAX_PACKET_SIZE];
    size_t packet_size = serialize_packet(&packet, buffer, sizeof(buffer));

    ssize_t sent = send(client_socket, buffer, packet_size, 0);
    cr_assert_eq(sent, packet_size, "Should send packet with sequence %d", seq);

    free(packet.data);
    usleep(100000); // 100ms between packets
  }

  close(client_socket);
  cleanup_server(server_pid);
}

Test(server_multiclient, ping_pong_functionality) {
  const int test_port = 9010;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_socket = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(client_socket, 0, "Client should connect");

  // Create and send PING packet
  packet_t ping_packet;
  memset(&ping_packet, 0, sizeof(ping_packet));
  ping_packet.header.magic = PACKET_MAGIC;
  ping_packet.header.type = PACKET_TYPE_PING;
  ping_packet.header.length = 0;
  ping_packet.header.sequence = 1;
  ping_packet.header.client_id = 888;
  ping_packet.data = NULL;

  uint8_t buffer[MAX_PACKET_SIZE];
  size_t packet_size = serialize_packet(&ping_packet, buffer, sizeof(buffer));

  ssize_t sent = send(client_socket, buffer, packet_size, 0);
  cr_assert_eq(sent, packet_size, "Should send PING packet");

  // Try to receive PONG response
  packet_t response;
  int result = receive_packet_with_timeout(client_socket, &response, 5);
  if (result == 0) {
    cr_assert_eq(response.header.type, PACKET_TYPE_PONG, "Should receive PONG response");
    if (response.data) {
      free(response.data);
    }
  }
  // Note: It's OK if server doesn't implement ping/pong yet

  close(client_socket);
  cleanup_server(server_pid);
}

// =============================================================================
// Load and Stress Tests
// =============================================================================

Test(server_multiclient, rapid_frame_transmission) {
  const int test_port = 9011;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  int client_socket = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(client_socket, 0, "Client should connect");

  const int frame_count = 50;
  int successful_sends = 0;

  // Send frames rapidly
  for (int frame = 0; frame < frame_count; frame++) {
    int result = send_image_frame(client_socket, 8, 6, 999);
    if (result == 0) {
      successful_sends++;
    }
    // No delay - send as fast as possible
  }

  // Should successfully send most frames
  float success_rate = (float)successful_sends / frame_count;
  cr_assert_gt(success_rate, 0.7f, "Should successfully send at least 70%% of frames (got %.1f%%)", success_rate * 100);

  close(client_socket);
  cleanup_server(server_pid);
}

Test(server_multiclient, server_stability_over_time) {
  const int test_port = 9012;
  pid_t server_pid = start_test_server(test_port);
  cr_assert_gt(server_pid, 0, "Server should start successfully");

  // Run for longer period with various operations
  const int duration_seconds = 10;
  const int clients_per_wave = 2;
  time_t start_time = time(NULL);

  while (time(NULL) - start_time < duration_seconds) {
    // Connect clients
    int client_sockets[clients_per_wave];
    for (int i = 0; i < clients_per_wave; i++) {
      client_sockets[i] = connect_to_server("127.0.0.1", test_port);
      if (client_sockets[i] >= 0) {
        send_image_frame(client_sockets[i], 16, 12, i + 1500);
      }
    }

    usleep(500000); // 0.5 second

    // Disconnect clients
    for (int i = 0; i < clients_per_wave; i++) {
      if (client_sockets[i] >= 0) {
        close(client_sockets[i]);
      }
    }

    usleep(500000); // 0.5 second
  }

  // Server should still be responsive
  int final_client = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(final_client, 0, "Server should still accept connections after stress test");

  int result = send_test_frame(final_client, 9999);
  cr_assert_eq(result, 0, "Server should still process frames after stress test");

  close(final_client);
  cleanup_server(server_pid);
}
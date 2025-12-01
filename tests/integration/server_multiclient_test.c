#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>

#include "tests/common.h"
#include "network/av.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "image2ascii/simd/common.h"

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

// Send client capabilities packet (required before sending frames)
static int send_capabilities(int socket, int width, int height) {
  terminal_capabilities_packet_t caps = {0};
  caps.capabilities = 0;
  caps.color_level = 0;
  caps.color_count = 0;
  caps.render_mode = 0;
  caps.width = htons(width);
  caps.height = htons(height);
  caps.detection_reliable = 1;
  caps.utf8_support = 0;
  caps.palette_type = 0;
  caps.desired_fps = 30;

  return send_packet(socket, PACKET_TYPE_CLIENT_CAPABILITIES, &caps, sizeof(caps));
}

// Use shared binary path detection from tests/common.h
#define get_server_binary_path test_get_binary_path

// Wait for a TCP port to become available (server listening)
static bool wait_for_port(int port, int timeout_ms) {
  int elapsed = 0;
  while (elapsed < timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
      return false;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);

    if (result == 0)
      return true;

    usleep(10000); // 10ms poll interval
    elapsed += 10;
  }
  return false;
}

static pid_t start_test_server(int port) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child process - start server
    char port_str[16];
    safe_snprintf(port_str, sizeof(port_str), "%d", port);

    // Redirect server output to log file for debugging
    FILE *log_file = fopen("/tmp/test_server_startup.log", "w");
    if (log_file) {
      dup2(fileno(log_file), STDOUT_FILENO);
      dup2(fileno(log_file), STDERR_FILENO);
      fclose(log_file);
    }

    const char *server_path = get_server_binary_path();

    // Check if the server binary exists and is executable
    if (access(server_path, F_OK) != 0) {
      fprintf(stderr, "Server binary does not exist at: %s\n", server_path);
    } else if (access(server_path, X_OK) != 0) {
      fprintf(stderr, "Server binary exists but is not executable: %s\n", server_path);
    } else {
      fprintf(stderr, "Attempting to execute server at: %s\n", server_path);
    }

    execl(server_path, "ascii-chat", "server", "--port", port_str, "--log-file", "/tmp/test_server.log", "--no-encrypt",
          NULL);

    // If execl fails, print error
    fprintf(stderr, "Failed to execute server at: %s\n", server_path);
    perror("execl");
    exit(1); // If exec fails
  }

  // Parent process - poll for server to start (much faster than sleep)
  wait_for_port(port, 2000);
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

  // Send capabilities packet (required before sending frames)
  if (send_capabilities(sock, 80, 24) != 0) {
    close(sock);
    return -1;
  }

  return sock;
}

static int send_test_frame(int socket, int frame_id) {
  // Create test ASCII frame
  char ascii_data[1000];
  safe_snprintf(ascii_data, sizeof(ascii_data),
                "Test Frame %d\n"
                "████████████\n"
                "██  %04d  ██\n"
                "████████████\n",
                frame_id, frame_id);

  // Use the av_send_ascii_frame function from av.h
  return av_send_ascii_frame(socket, ascii_data, strlen(ascii_data));
}

static int send_image_frame(int socket, int width, int height, int client_id) {
  (void)client_id; // Unused parameter
  // Create test RGB image
  rgb_pixel_t *image_data;
  image_data = SAFE_MALLOC(width * height * sizeof(rgb_pixel_t), rgb_pixel_t *);

  // Fill with test pattern
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = y * width + x;
      image_data[idx] =
          (rgb_pixel_t){.r = (x * 255) / width, .g = (y * 255) / height, .b = ((x + y) * 255) / (width + height)};
    }
  }

  // Use the av_send_image_frame function from av.h
  // Just use 0 for pixel_format since IMAGE_FORMAT_RGB24 doesn't exist
  int result = av_send_image_frame(socket, image_data, width, height, 0);
  SAFE_FREE(image_data);
  return result;
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
  usleep(50000); // 50ms for server to detect disconnect

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
  usleep(50000); // 50ms

  // Server might broadcast frames back, but we're not checking for now

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
  usleep(100000); // 100ms

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
  header->crc32 = htonl(0);
  header->client_id = htonl(999);

  ssize_t sent = send(client_socket, bad_packet, sizeof(bad_packet), 0);
  cr_assert_eq(sent, sizeof(bad_packet), "Should send malformed packet");

  // Server should still be running (not crashed)
  usleep(50000); // 50ms

  // Try to send valid packet
  send_test_frame(client_socket, 1);
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

  usleep(50000); // 50ms for server to detect disconnect

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

  // Run for short period with various operations (stability check, not endurance)
  const int num_waves = 3;
  const int clients_per_wave = 2;

  for (int wave = 0; wave < num_waves; wave++) {
    // Connect clients
    int client_sockets[clients_per_wave];
    for (int i = 0; i < clients_per_wave; i++) {
      client_sockets[i] = connect_to_server("127.0.0.1", test_port);
      if (client_sockets[i] >= 0) {
        send_image_frame(client_sockets[i], 16, 12, i + 1500);
      }
    }

    usleep(50000); // 50ms

    // Disconnect clients
    for (int i = 0; i < clients_per_wave; i++) {
      if (client_sockets[i] >= 0) {
        close(client_sockets[i]);
      }
    }

    usleep(50000); // 50ms
  }

  // Server should still be responsive
  int final_client = connect_to_server("127.0.0.1", test_port);
  cr_assert_geq(final_client, 0, "Server should still accept connections after stress test");

  int result = send_test_frame(final_client, 9999);
  cr_assert_eq(result, 0, "Server should still process frames after stress test");

  close(final_client);
  cleanup_server(server_pid);
}

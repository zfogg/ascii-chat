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

#include "ascii.h"
#include "common.h"
#include "network.h"
#include "options.h"
#include "compression.h"

static int sockfd = 0;
static volatile bool g_should_exit = false;
static volatile bool g_first_connection = true;
static volatile bool g_should_reconnect = false;

static volatile int last_frame_width = 0;
static volatile int last_frame_height = 0;

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
  if (0 > (sockfd = close_socket(sockfd))) {
    exit(ASCIICHAT_ERR_NETWORK);
  }
  ascii_write_destroy();
  log_destroy();
  log_info("Client shutdown complete");
}

static void sigint_handler(int sigint) {
  (void)(sigint);
  printf("\nShutdown requested...\n");
  g_should_exit = true;
}

static void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  // Terminal was resized, update dimensions and recalculate aspect ratio
  update_dimensions_to_terminal_size();

  // Send new size to server if connected
  if (sockfd > 0) {
    if (send_size_message(sockfd, opt_width, opt_height) < 0) {
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

int main(int argc, char *argv[]) {
  log_init("client.log", LOG_DEBUG);
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
      if (send_size_message(sockfd, opt_width, opt_height) < 0) {
        log_error("Failed to send initial size to server: %s", network_error_string(errno));
        g_should_reconnect = true;
        continue; // try to connect again
      }
      log_info("Sent initial size to server: %ux%u", opt_width, opt_height);

      // Set socket keepalive to detect broken connections
      if (set_socket_keepalive(sockfd) < 0) {
        log_warn("Failed to set socket keepalive: %s", network_error_string(errno));
      }

      g_first_connection = false;
      g_should_reconnect = false;
    }

    // Allocate receive buffer on heap instead of stack to avoid stack overflow
    char *recvBuff = NULL;
    int read_result = 0;

    // Frame receiving loop - continue until connection breaks or shutdown requested
    size_t frame_size;
    compressed_frame_header_t header;
    while (!g_should_exit && 0 < (read_result = recv_compressed_frame(sockfd, &recvBuff, &frame_size, &header))) {
      recvBuff[frame_size] = '\0'; // Null-terminate the received data, making it a valid C string.
      if (strcmp(recvBuff, ASCIICHAT_WEBCAM_ERROR_STRING) == 0) {
        log_error("Server reported webcam failure: %s", recvBuff);
        usleep(1000 * 1000); // 1 second delay then read the socket again
        continue;
      }
      ascii_write(recvBuff);
      free(recvBuff);
      recvBuff = NULL;
      if (header.width != last_frame_width || header.height != last_frame_height) {
        // If we get ever a frame of a different size, our terminal might have
        // gotten smaller in width, so we were printing to an area that we now
        // won't be. There will be ascii in that area to clear.
        console_clear();
        last_frame_width = header.width;
        last_frame_height = header.height;
      }
    }

    free(recvBuff);

    if (g_should_exit) {
      log_info("Shutdown requested, exiting...");
      break;
    }

    // Handle connection termination or shutdown
    if (read_result <= 0) {
      if (read_result == 0) {
        log_info("Server closed connection gracefully");
        log_info("Server disconnected. Attempting to reconnect...");
      } else {
        log_warn("Network receive failed: %s", network_error_string(errno));
        log_info("Connection lost. Attempting to reconnect...");
      }
      g_should_reconnect = true;
      continue;
    }
  }

  shutdown_client();
  return 0;
}

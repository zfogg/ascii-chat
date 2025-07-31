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
#include "client.h"
#include "common.h"
#include "network.h"
#include "options.h"
#include "compression.h"

int sockfd = 0;
static volatile bool g_should_exit = false;

static void shutdown_client(bool log) {
  if (log)
    log_info("Closing tcp socket connection");
  if (sockfd > 0)
    close(sockfd);
  ascii_write_destroy();
  if (log)
    log_info("Client shutdown complete");
  log_destroy();
}

void sigint_handler(int sigint) {
  (void)(sigint);
  printf("\nShutdown requested...\n");
  g_should_exit = true;
}

void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  // Terminal was resized, update dimensions and recalculate aspect ratio
  recalculate_aspect_ratio_on_resize();

  // Send new size to server if connected
  if (sockfd > 0) {
    if (send_size_message(sockfd, opt_width, opt_height) < 0) {
      log_warn("Failed to send size update to server: %s", network_error_string(errno));
    } else {
      log_debug("Sent size update to server: %ux%u", opt_width, opt_height);
    }
  }
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

  /* Connection and reconnection loop */
  int reconnect_attempt = 0;
  const int max_reconnect_delay = 50; // Maximum delay between reconnection attempts (seconds)

  while (!g_should_exit) {
    // Close any existing socket before attempting new connection
    if (sockfd > 0) {
      close(sockfd);
      sockfd = 0;
    }

    // try to open a socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      log_error("Error: could not create socket: %s", network_error_string(errno));
      reconnect_attempt++;
      int delay = (reconnect_attempt < max_reconnect_delay) ? reconnect_attempt : max_reconnect_delay;
      log_info("Retrying in %d seconds...", delay);

      for (int i = 0; i < delay && !g_should_exit; i++)
        sleep(1);
      continue; // try to connect again
    }

    // reserve memory space to store IP address
    memset(&serv_addr, '0', sizeof(serv_addr));

    log_info("Connecting on port %d", port);

    // set type of address to IPV4
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // an error occurred when trying to set server address and port number
    if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
      log_error("Error: couldn't set the server address and port number: %s", network_error_string(errno));
      reconnect_attempt++;
      int delay = (reconnect_attempt < max_reconnect_delay) ? reconnect_attempt : max_reconnect_delay;
      log_info("Retrying in %d seconds...", delay);
      for (int i = 0; i < delay && !g_should_exit; i++)
        sleep(1);
      continue; // try to connect again
    }

    if (reconnect_attempt > 0) {
      log_info("Reconnection attempt #%d to %s:%d", reconnect_attempt, address, port);
    } else {
      log_info("Connecting to %s:%d", address, port);
    }
    printf("Attempting to connect...\n");

    // Try to connect to the server
    if (!connect_with_timeout(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr), CONNECT_TIMEOUT)) {
      log_warn("Connection failed: %s", network_error_string(errno));
      reconnect_attempt++;
      int delay = (reconnect_attempt < max_reconnect_delay) ? reconnect_attempt : max_reconnect_delay;
      log_info("Connection failed, retrying in %d seconds...", delay);
      for (int i = 0; i < delay && !g_should_exit; i++)
        sleep(1);
      continue; // try to connect again
    }

    // Connection successful!
    printf("Connected successfully!\n");
    log_info("Connected to server %s:%d", address, port);
    reconnect_attempt = 0; // Reset reconnection counter on successful connection

    // Send initial terminal size to server
    if (send_size_message(sockfd, opt_width, opt_height) < 0) {
      log_error("Failed to send initial size to server: %s", network_error_string(errno));
      continue; // try to connect again
    }
    log_info("Sent initial size to server: %ux%u", opt_width, opt_height);

    // Set socket keepalive to detect broken connections
    if (set_socket_keepalive(sockfd) < 0) {
      log_warn("Failed to set socket keepalive: %s", network_error_string(errno));
    }

    // Initialize ASCII output for this connection
    ascii_write_init();

    // Allocate receive buffer on heap instead of stack to avoid stack overflow
    char *recvBuff = NULL;

    int read_result = 0;
    bool connection_broken = false;

    // Frame receiving loop - continue until connection breaks or shutdown requested
    size_t frame_size;
    while (!g_should_exit && !connection_broken &&
           0 < (read_result = recv_compressed_frame(sockfd, &recvBuff, &frame_size))) {
      recvBuff[frame_size] = '\0'; // Null-terminate the received data, making it a valid C string.
      if (strcmp(recvBuff, ASCIICHAT_WEBCAM_ERROR_STRING) == 0) {
        log_error("Server reported webcam failure: %s", recvBuff);
        sleep(1);
        connection_broken = true;
        break;
      }
      ascii_write(recvBuff);
    }

    // Handle connection termination or shutdown
    if (!g_should_exit && read_result <= 0) {
      if (read_result == 0) {
        log_info("Server closed connection gracefully");
        printf("Server disconnected. Attempting to reconnect...\n");
      } else {
        log_warn("Network receive failed: %s", network_error_string(errno));
        printf("Connection lost. Attempting to reconnect...\n");
      }
      connection_broken = true;
    }

    // Clean up allocated memory before next iteration
    free(recvBuff);
    ascii_write_destroy();

    if (g_should_exit) {
      log_info("Shutdown requested, exiting...");
      break;
    }

    // Connection broken - will loop back to reconnection logic
    log_info("Connection terminated, preparing to reconnect...");
  }

  shutdown_client(true);
  return 0;
}

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
#include "options.h"

int sockfd = 0;

static void shutdown_client(bool log) {
  if (log)
    log_info("Closing tcp socket connection");
  close(sockfd);
  ascii_write_destroy();
  if (log)
    log_info("Client shutdown complete");
  log_destroy();
}

void sigint_handler(int sigint) {
  (void)(sigint);
  shutdown_client(true);
  exit(0);
}

void sigwinch_handler(int sigwinch) {
  (void)(sigwinch);
  // Terminal was resized, update dimensions and recalculate aspect ratio
  recalculate_aspect_ratio_on_resize();
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

  /* read from the socket as long as the size of the read is > 0 */
  while (1) {
    // try to open a socket
    // error creating socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      log_fatal("\n Error: could not create socket");
      shutdown_client(false);
      return 1;
    }

    // reserve memory space to store IP address
    memset(&serv_addr, '0', sizeof(serv_addr));

    // set type of address to IPV4 and port to 5000
    log_info("Connecting on port %d", port);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // an error occurred when trying to set server address and port number
    if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
      log_fatal("Error: couldn't set the server address and port number");
      shutdown_client(false);
      return 1;
    }

    int read_result = 0;
    ascii_write_init();

    printf("Attempting to connect...\n");
    // failed when trying to connect to the server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      log_fatal("%s", "Error: server socket connect() failed");
      shutdown_client(false);
      exit(1);
    }

    // Allocate frame buffer on heap instead of stack to avoid stack overflow
    char *frame_buffer;
    SAFE_MALLOC(frame_buffer, FRAME_BUFFER_SIZE_FINAL, char *);

    // Allocate receive buffer on heap instead of stack to avoid stack overflow
    char *recvBuff;
    SAFE_MALLOC(recvBuff, FRAME_BUFFER_SIZE_FINAL, char *);

    while (0 < (read_result = read(sockfd, recvBuff, FRAME_BUFFER_SIZE_FINAL - 1))) {
      recvBuff[read_result] = 0; // Null-terminate the received data, making it a valid C string.
      if (strcmp(recvBuff, "Webcam capture failed\n") == 0) {
        log_error("Error: %s", recvBuff);
        shutdown_client(false);
        exit(ASCIICHAT_ERR_WEBCAM);
      }
      ascii_write(recvBuff);
    } // while read()ing result from socket into recvBuff

    // Clean up allocated memory
    free(frame_buffer);
    free(recvBuff);
  }

  shutdown_client(true);
  return 0;
}

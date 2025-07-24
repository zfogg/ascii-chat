#include <arpa/inet.h>
#include <stdbool.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "ascii.h"
#include "options.h"


void sigwinch_handler(int sigwinch) {
    (void) (sigwinch);
    // Terminal was resized, update dimensions
    recalculate_aspect_ratio_on_resize();
    // printf("sigwinch_handler: opt_width: %d, opt_height: %d\n", opt_width, opt_height);
}

void sigint_handler(int sigint) {
    (void) (sigint);
    ascii_read_destroy();
    printf("Cleaning up and exiting...\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    options_init(argc, argv);
    int port = strtoint(opt_port);
    unsigned short int webcam_index = opt_webcam_index;

    // Handle terminal resize events
    signal(SIGWINCH, sigwinch_handler);
    // Handle Ctrl+C for cleanup
    signal(SIGINT, sigint_handler);

    // file descriptors for I/O
    int listenfd = 0,
        connfd   = 0;

    struct sockaddr_in serv_addr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0); // initialize socket

    printf("Running server on port %d\n", port);

    serv_addr.sin_family = AF_INET; // set address family to IPV4, AF_INET6 would be IPV6
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // set address for socket
    serv_addr.sin_port = htons(port); // set port for socket

    // bind socket based on address and ports set in serv_addr
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error: network bind failed");
        exit(1);
    }

    int yes = 1;

    // lose the pesky "Address already in use" error message
    if (setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    // ignore SIGPIPE signal
    signal(SIGPIPE, SIG_IGN);

    ascii_read_init(webcam_index);

    // listen on socket listenfd with max backlog of 10 connections
    if (listen(listenfd, 10) < 0) {
        perror("Error: network listen failed");
        exit(1);
    }

    while(1) {
        printf("1) Waiting for a connection...\n");
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
        printf("2) Connection initiated, sending data.\n");

        char *frame = NULL;

        for (int conn = 0; true;) {
            frame = ascii_read(); /* malloc happens here */
            conn = send(connfd, frame, strlen(frame), 0);
            if (conn == -1) {
                free(frame);
                fprintf(stderr, "%s", "\n Error: send failed \n");
                break;
            }
            free(frame);
        }

        close(connfd);
        printf("3) Closing connection.\n---------------------\n");
    }
}

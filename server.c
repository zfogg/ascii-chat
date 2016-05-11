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


int main(int argc, char *argv[]) {
    options_init(argc, argv);
    int port = strtoint(opt_port);

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
    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    int yes = 1;

    // lose the pesky "Address already in use" error message
    if (setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    // ignore SIGPIPE signal
    signal(SIGPIPE, SIG_IGN);

    // listen on socket listenfd with max backlog of 10 connections
    listen(listenfd, 10);
    while(1) {
        printf("1) Waiting for a connection...\n");
        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
        printf("2) Connection initiated, sending data.\n");

        ascii_read_init();
        char *frame = NULL;

        for (int conn = 0; true;) {
            frame = ascii_read(); /* malloc happens here */
            conn = send(connfd, frame, strlen(frame), 0);
            if (conn == -1) {
                break;
            }
            free(frame);
        }

        close(connfd);
        printf("3) Closing connection.\n---------------------\n");
    }
}

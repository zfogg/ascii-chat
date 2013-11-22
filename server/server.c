#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "../headers/ascii.h"


int main(int argc, char *argv[]) {
    // file descriptors for I/O
    int listenfd = 0,
        connfd   = 0;

    struct sockaddr_in serv_addr = { 0 };

    listenfd = socket(AF_INET, SOCK_STREAM, 0); // initialize socket

    serv_addr.sin_family = AF_INET; // set address family to IPV4, AF_INET6 would be IPV6
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // set address for socket
    serv_addr.sin_port = htons(5000); // set port for socket

    // bind socket based on address and ports set in serv_addr
    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    int yes = 1;

    // lose the pesky "Address already in use" error message
    if (setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    // listen on socket listenfd with max backlog of 10 connections
    listen(listenfd, 10);

    ascii_read_init();

    while(1) {
        printf("1) Waiting for a connection...\n");

        connfd = accept(listenfd, (struct sockaddr*)NULL, NULL); // accept a connection

        printf("2) Connection initiated, sending data.\n");

        // Send ASCII of every filename argument.
        for (int i = 1; i < argc; i++) {
            char *frame = ascii_read();
            int conn_status = send(connfd, frame, strlen(frame), 0);
            free(frame);

            // Check if connection is broken.
            if (conn_status == -1)
                break;
        }

        printf("3) Closing connection.\n---------------------\n");
        close(connfd);
    }
}

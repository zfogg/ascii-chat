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
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "ascii.h"
#include "client.h"
#include "options.h"


int sockfd = 0;

void sigint_handler(int sigint) {
    (void) (sigint);
    ascii_write_destroy();
    printf("Closing connection and exiting . . .\n");
    close(sockfd);
    exit(0);
}


int main(int argc, char *argv[]) {
    options_init(argc, argv);
    char *address = opt_address;
    int   port    = strtoint(opt_port);

    char recvBuff[40000];
    struct sockaddr_in serv_addr;

    struct timespec
    sleep_start = {
        .tv_sec  = 3,
        .tv_nsec = 0
    },
    sleep_stop = {
        .tv_sec  = 0,
        .tv_nsec = 0
    };

    // Cleanup nicely on Ctrl+C.
    signal(SIGINT, sigint_handler);

    /* read from the socket as long as the size of the read is > 0 */
    while(1) {
        // try to open a socket
        memset(recvBuff, '0',sizeof(recvBuff));
        // error creating socket
        if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("\n Error: could not create socket \n");
            return 1;
        }

        // reserve memory space to store IP address
        memset(&serv_addr, '0', sizeof(serv_addr));

        // set type of address to IPV4 and port to 5000
        printf("Connecting on port %d\n", port);

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        // an error occurred when trying to set server address and port number
        if(inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
            printf("\n Error: inet_pton \n");
            return 1;
        }

        int read_result = 0;
        ascii_write_init();

        printf("Attempting to connect...\n");
        // failed when trying to connect to the server
        if(connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            printf("\n Error: connect failed \n");
        }

        while (0 < (read_result = read(sockfd, recvBuff, sizeof(recvBuff)-1))) {
            recvBuff[read_result] = 0;
            ascii_write(recvBuff);
        }

        console_clear();

        nanosleep((struct timespec *)&sleep_start,(struct timespec *)&sleep_stop);
    }

    return 0;
}


#ifndef NETWORK_H
#define NETWORK_H

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>

// Timeout constants (in seconds)
#define CONNECT_TIMEOUT 10
#define SEND_TIMEOUT 5
#define RECV_TIMEOUT 5
#define ACCEPT_TIMEOUT 30

// Keep-alive settings
#define KEEPALIVE_IDLE 60
#define KEEPALIVE_INTERVAL 10
#define KEEPALIVE_COUNT 3

// Network utility functions
int set_socket_timeout(int sockfd, int timeout_seconds);
int set_socket_nonblocking(int sockfd);
int set_socket_keepalive(int sockfd);
bool connect_with_timeout(int sockfd, const struct sockaddr *addr,
                          socklen_t addrlen, int timeout_seconds);
ssize_t send_with_timeout(int sockfd, const void *buf, size_t len,
                          int timeout_seconds);
ssize_t recv_with_timeout(int sockfd, void *buf, size_t len,
                          int timeout_seconds);
int accept_with_timeout(int listenfd, struct sockaddr *addr, socklen_t *addrlen,
                        int timeout_seconds);

// Error handling
const char *network_error_string(int error_code);

#endif // NETWORK_H
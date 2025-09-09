#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "network.h"
#include "common.h"

void setup_quiet_test_logging(void);
void restore_test_logging(void);

TestSuite(network, .init = setup_quiet_test_logging, .fini = restore_test_logging);

void setup_quiet_test_logging(void) {
    log_set_level(LOG_FATAL);
}

void restore_test_logging(void) {
    log_set_level(LOG_DEBUG);
}

static int create_test_socket(void) {
    return socket(AF_INET, SOCK_STREAM, 0);
}

Test(network, set_socket_timeout_valid) {
    int sockfd = create_test_socket();
    cr_assert_geq(sockfd, 0);

    int result = set_socket_timeout(sockfd, 1);
    cr_assert_eq(result, 0);

    close(sockfd);
}

Test(network, set_socket_timeout_invalid_socket) {
    int result = set_socket_timeout(-1, 1);
    cr_assert_eq(result, -1);
}

Test(network, set_socket_keepalive_valid) {
    int sockfd = create_test_socket();
    cr_assert_geq(sockfd, 0);

    int result = set_socket_keepalive(sockfd);
    cr_assert_eq(result, 0);

    close(sockfd);
}

Test(network, set_socket_keepalive_invalid_socket) {
    int result = set_socket_keepalive(-1);
    cr_assert_eq(result, -1);
}

Test(network, set_socket_nonblocking_valid) {
    int sockfd = create_test_socket();
    cr_assert_geq(sockfd, 0);

    int result = set_socket_nonblocking(sockfd);
    cr_assert_eq(result, 0);

    close(sockfd);
}

Test(network, set_socket_nonblocking_invalid_socket) {
    int result = set_socket_nonblocking(-1);
    cr_assert_eq(result, -1);
}

Test(network, connect_with_timeout_invalid_socket) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(8080);

    bool result = connect_with_timeout(-1, (struct sockaddr *)&addr, sizeof(addr), 1);
    cr_assert_eq(result, false);
}

Test(network, send_with_timeout_invalid_socket) {
    const char *data = "test data";
    ssize_t result = send_with_timeout(-1, data, strlen(data), 1);
    cr_assert_eq(result, -1);
}

Test(network, recv_with_timeout_invalid_socket) {
    char buffer[1024];
    ssize_t result = recv_with_timeout(-1, buffer, sizeof(buffer), 1);
    cr_assert_eq(result, -1);
}

Test(network, parse_size_message_valid) {
    unsigned short width, height;
    const char *message = "SIZE:80,24\n";

    int result = parse_size_message(message, &width, &height);
    cr_assert_eq(result, 0);
    cr_assert_eq(width, 80);
    cr_assert_eq(height, 24);
}

Test(network, parse_size_message_invalid_format) {
    unsigned short width, height;
    const char *message = "INVALID:80,24\n";

    int result = parse_size_message(message, &width, &height);
    cr_assert_eq(result, -1);
}

Test(network, parse_size_message_null_pointers) {
    int result = parse_size_message("SIZE:80,24\n", NULL, NULL);
    cr_assert_eq(result, -1);
}

Test(network, send_packet_invalid_socket) {
    const char *data = "test data";
    int result = send_packet(-1, PACKET_TYPE_PING, data, strlen(data));
    cr_assert_eq(result, -1);
}

Test(network, receive_packet_invalid_socket) {
    packet_type_t type;
    void *data;
    size_t len;

    int result = receive_packet(-1, &type, &data, &len);
    cr_assert_eq(result, -1);
}

Test(network, send_audio_data_invalid_socket) {
    float samples[256];
    memset(samples, 0, sizeof(samples));

    int result = send_audio_data(-1, samples, 256);
    cr_assert_eq(result, -1);
}

Test(network, receive_audio_data_invalid_socket) {
    float samples[256];

    int result = receive_audio_data(-1, samples, 256);
    cr_assert_eq(result, -1);
}

Test(network, send_client_join_packet_invalid_socket) {
    int result = send_client_join_packet(-1, "TestUser", CLIENT_CAP_VIDEO);
    cr_assert_eq(result, -1);
}

Test(network, send_ping_packet_invalid_socket) {
    int result = send_ping_packet(-1);
    cr_assert_eq(result, -1);
}

Test(network, send_pong_packet_invalid_socket) {
    int result = send_pong_packet(-1);
    cr_assert_eq(result, -1);
}

Test(network, network_error_string_valid_codes) {
    const char *error1 = network_error_string(0);
    cr_assert_not_null(error1);

    const char *error2 = network_error_string(-1);
    cr_assert_not_null(error2);

    const char *error3 = network_error_string(100);
    cr_assert_not_null(error3);
}

Test(network, random_size_messages) {
    srand(42);

    for (int i = 0; i < 100; i++) {
        unsigned short width = (rand() % 1000) + 1;
        unsigned short height = (rand() % 1000) + 1;

        char message[64];
        snprintf(message, sizeof(message), "SIZE:%u,%u\n", width, height);

        unsigned short parsed_width, parsed_height;
        int result = parse_size_message(message, &parsed_width, &parsed_height);

        cr_assert_eq(result, 0);
        cr_assert_eq(parsed_width, width);
        cr_assert_eq(parsed_height, height);
    }
}

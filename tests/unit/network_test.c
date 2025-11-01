#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "tests/common.h"
#include "tests/logging.h"
#include "network/network.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/av.h"

// Use the enhanced macro to create complete test suite with debug logging and stdout/stderr enabled
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(network, LOG_DEBUG, LOG_DEBUG, false, false);

// Stub implementations for client-specific functions
int send_audio_data(int sockfd, const float *samples, int num_samples) {
  (void)sockfd;
  (void)samples;
  (void)num_samples;
  return -1; // Stub - not implemented in library
}

int receive_audio_data(int sockfd, float *samples, int num_samples) {
  (void)sockfd;
  (void)samples;
  (void)num_samples;
  return -1; // Stub - not implemented in library
}

int send_client_join_packet(int sockfd, const char *username, uint32_t capabilities) {
  (void)sockfd;
  (void)username;
  (void)capabilities;
  return -1; // Stub - client-specific function
}

int parse_size_message(const char *message, unsigned short *width, unsigned short *height) {
  return av_parse_size_message(message, width, height);
}

static int create_test_socket(void) {
  return socket(AF_INET, SOCK_STREAM, 0);
}

// =============================================================================
// Invalid Socket Parameter Tests - Parameterized
// =============================================================================

typedef enum {
  OP_SET_TIMEOUT,
  OP_SET_KEEPALIVE,
  OP_SET_NONBLOCKING,
  OP_CONNECT_TIMEOUT,
  OP_SEND_TIMEOUT,
  OP_RECV_TIMEOUT,
  OP_SEND_PACKET,
  OP_RECEIVE_PACKET,
  OP_SEND_AUDIO,
  OP_RECEIVE_AUDIO,
  OP_SEND_CLIENT_JOIN,
  OP_SEND_PING,
  OP_SEND_PONG
} socket_operation_t;

typedef struct {
  socket_operation_t operation;
  const char *description;
} invalid_socket_test_case_t;

static invalid_socket_test_case_t invalid_socket_cases[] = {
    {OP_SET_TIMEOUT, "set_socket_timeout with invalid socket"},
    {OP_SET_KEEPALIVE, "set_socket_keepalive with invalid socket"},
    {OP_SET_NONBLOCKING, "set_socket_nonblocking with invalid socket"},
    {OP_CONNECT_TIMEOUT, "connect_with_timeout with invalid socket"},
    {OP_SEND_TIMEOUT, "send_with_timeout with invalid socket"},
    {OP_RECV_TIMEOUT, "recv_with_timeout with invalid socket"},
    {OP_SEND_PACKET, "send_packet with invalid socket"},
    {OP_RECEIVE_PACKET, "receive_packet with invalid socket"},
    {OP_SEND_AUDIO, "send_audio_data with invalid socket"},
    {OP_RECEIVE_AUDIO, "receive_audio_data with invalid socket"},
    {OP_SEND_CLIENT_JOIN, "send_client_join_packet with invalid socket"},
    {OP_SEND_PING, "send_ping_packet with invalid socket"},
    {OP_SEND_PONG, "send_pong_packet with invalid socket"},
};

ParameterizedTestParameters(network, invalid_socket_operations) {
  return cr_make_param_array(invalid_socket_test_case_t, invalid_socket_cases,
                             sizeof(invalid_socket_cases) / sizeof(invalid_socket_cases[0]));
}

ParameterizedTest(invalid_socket_test_case_t *tc, network, invalid_socket_operations) {
  int result;
  ssize_t sresult;
  bool bresult;

  switch (tc->operation) {
  case OP_SET_TIMEOUT:
    result = set_socket_timeout(-1, 1);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  case OP_SET_KEEPALIVE:
    result = set_socket_keepalive(-1);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  case OP_SET_NONBLOCKING:
    result = set_socket_nonblocking(-1);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  case OP_CONNECT_TIMEOUT: {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(8080);
    bresult = connect_with_timeout(-1, (struct sockaddr *)&addr, sizeof(addr), 1);
    cr_assert_eq(bresult, false, "%s should fail", tc->description);
    break;
  }
  case OP_SEND_TIMEOUT: {
    const char *data = "test data";
    sresult = send_with_timeout(-1, data, strlen(data), 1);
    cr_assert_eq(sresult, -1, "%s should fail", tc->description);
    break;
  }
  case OP_RECV_TIMEOUT: {
    char buffer[1024];
    sresult = recv_with_timeout(-1, buffer, sizeof(buffer), 1);
    cr_assert_eq(sresult, -1, "%s should fail", tc->description);
    break;
  }
  case OP_SEND_PACKET: {
    const char *data = "test data";
    result = send_packet(-1, PACKET_TYPE_PING, data, strlen(data));
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  }
  case OP_RECEIVE_PACKET: {
    packet_type_t type;
    void *data;
    size_t len;
    result = receive_packet(-1, &type, &data, &len);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  }
  case OP_SEND_AUDIO: {
    float samples[256];
    memset(samples, 0, sizeof(samples));
    result = send_audio_data(-1, samples, 256);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  }
  case OP_RECEIVE_AUDIO: {
    float samples[256];
    result = receive_audio_data(-1, samples, 256);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  }
  case OP_SEND_CLIENT_JOIN:
    result = send_client_join_packet(-1, "TestUser", CLIENT_CAP_VIDEO);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  case OP_SEND_PING:
    result = send_ping_packet(-1);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  case OP_SEND_PONG:
    result = send_pong_packet(-1);
    cr_assert_eq(result, -1, "%s should fail", tc->description);
    break;
  }
}

// =============================================================================
// Valid Socket Operation Tests
// =============================================================================

Test(network, set_socket_timeout_valid) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  int result = set_socket_timeout(sockfd, 1);
  cr_assert_eq(result, 0);

  close(sockfd);
}

// Replaced by parameterized test: invalid_socket_operations

Test(network, set_socket_keepalive_valid) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  int result = set_socket_keepalive(sockfd);
  cr_assert_eq(result, 0);

  close(sockfd);
}

// Replaced by parameterized test: invalid_socket_operations

Test(network, set_socket_nonblocking_valid) {
  int sockfd = create_test_socket();
  cr_assert_geq(sockfd, 0);

  int result = set_socket_nonblocking(sockfd);
  cr_assert_eq(result, 0);

  close(sockfd);
}

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

// Parameterized test for parse_size_message
typedef struct {
  char message[32];
  bool use_null_pointers;
  int expected_result;
  unsigned short expected_width;
  unsigned short expected_height;
  char description[64];
} parse_size_message_test_case_t;

static parse_size_message_test_case_t parse_size_message_cases[] = {
    {"SIZE:80,24\n", false, 0, 80, 24, "Valid message"},  {"SIZE:160,48\n", false, 0, 160, 48, "Valid large size"},
    {"SIZE:1,1\n", false, 0, 1, 1, "Valid minimal size"}, {"INVALID:80,24\n", false, -1, 0, 0, "Invalid format"},
    {"SIZE:80\n", false, -1, 0, 0, "Missing dimension"},  {"80,24\n", false, -1, 0, 0, "Missing SIZE prefix"},
    {"SIZE:80,24\n", true, -1, 0, 0, "NULL pointers"},
};

ParameterizedTestParameters(network, parse_size_message_variations) {
  return cr_make_param_array(parse_size_message_test_case_t, parse_size_message_cases,
                             sizeof(parse_size_message_cases) / sizeof(parse_size_message_cases[0]));
}

ParameterizedTest(parse_size_message_test_case_t *tc, network, parse_size_message_variations) {
  unsigned short width = 0, height = 0;
  int result;

  if (tc->use_null_pointers) {
    result = parse_size_message(tc->message, NULL, NULL);
  } else {
    result = parse_size_message(tc->message, &width, &height);
  }

  cr_assert_eq(result, tc->expected_result, "%s: expected result %d, got %d", tc->description, tc->expected_result,
               result);

  if (tc->expected_result == 0 && !tc->use_null_pointers) {
    cr_assert_eq(width, tc->expected_width, "%s: expected width %d, got %d", tc->description, tc->expected_width,
                 width);
    cr_assert_eq(height, tc->expected_height, "%s: expected height %d, got %d", tc->description, tc->expected_height,
                 height);
  }
}

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

// Replaced by parameterized test: invalid_socket_operations

Test(network, network_error_string_valid_codes) {
  const char *error1 = network_error_string(0);
  cr_assert_not_null(error1);

  const char *error2 = network_error_string(-1);
  cr_assert_not_null(error2);

  const char *error3 = network_error_string(100);
  cr_assert_not_null(error3);
}

Test(network, random_size_messages) {
  log_debug("Starting random_size_messages test");
  srand(42);

  for (int i = 0; i < 100; i++) {
    if (i % 20 == 0) {
      log_debug("Processing iteration %d/100", i);
    }
    unsigned short width = (rand() % 1000) + 1;
    unsigned short height = (rand() % 1000) + 1;

    char message[64];
    safe_snprintf(message, sizeof(message), "SIZE:%u,%u\n", width, height);

    unsigned short parsed_width, parsed_height;
    int result = parse_size_message(message, &parsed_width, &parsed_height);

    cr_assert_eq(result, 0);
    cr_assert_eq(parsed_width, width);
    cr_assert_eq(parsed_height, height);
  }
  log_debug("random_size_messages test completed");
}

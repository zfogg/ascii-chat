#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <libplatform/compiler.h>
#include <libplatform/system.h>
#include <libplatform/terminal.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/acds_server.h>
#include <ascii-chat/source_root.h>
#include <ascii-chat/buffer.h>
#include <sodium.h>

#ifndef _WIN32
#include <sys/socket.h>

TEST_CASE("tcp_transport/send_failure_preserves_nested_system_context", "[tcp_transport]") {
  int sockets[2];
  ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_transport_t *transport = acip_tcp_transport_create("send-failure-context", sockets[0], NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(transport);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
  asciichat_errno_scope_t scope = errno_checkpoint();
  ASCIICHAT_REQUIRE_EQ(packet_send_via_transport(transport, PACKET_TYPE_STREAM_START, NULL, 0, 0), ERROR_NETWORK);
  asciichat_error_context_t wrapper;
  ASCIICHAT_REQUIRE(errno_peek_since(scope, &wrapper));
  ASCIICHAT_REQUIRE_EQ(wrapper.code, ERROR_NETWORK);
  ASCIICHAT_REQUIRE_STR_EQ(wrapper.context_message, "TCP packet transmission failed");
  ASCIICHAT_REQUIRE_STR_EQ(wrapper.function, "tcp_sendv");
  asciichat_error_context_t frames[ASCIICHAT_ERRNO_MAX_DEPTH];
  size_t count = errno_snapshot(scope.thread_id, frames, ASCIICHAT_ERRNO_MAX_DEPTH).copied;
  bool found_send_cause = false;
  for (size_t i = 0; i < count && i < ASCIICHAT_ERRNO_MAX_DEPTH; ++i) {
    if (frames[i].error_id == wrapper.cause_id) {
      ASCIICHAT_REQUIRE_EQ(frames[i].code, ERROR_NETWORK);
      found_send_cause = true;
    }
  }
  ASCIICHAT_REQUIRE(found_send_cause);
  ASCIICHAT_REQUIRE_NOT(acip_transport_is_connected(transport));
  acip_transport_destroy(transport);
  socket_close(sockets[0]);
  CLEAR_ERRNO_ALL();
}

TEST_CASE("tcp_transport/peer_eof_marks_transport_disconnected", "[tcp_transport]") {
  int sockets[2];
  ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_transport_t *transport = acip_tcp_transport_create("eof-test", sockets[0], NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(transport);
  socket_close(sockets[1]);

  void *buffer = NULL;
  void *allocated = NULL;
  size_t length = 0;
  ASCIICHAT_REQUIRE_EQ(acip_transport_recv(transport, &buffer, &length, &allocated), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_NOT(acip_transport_is_connected(transport));

  acip_transport_destroy(transport);
  socket_close(sockets[0]);
}

TEST_CASE("tcp_transport/socket_ownership_survives_peer_eof", "[tcp_transport]") {
  for (int owned = 0; owned < 2; ++owned) {
    int sockets[2];
    ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    acip_transport_t *transport = owned ? acip_tcp_transport_create_owned("owned-eof", sockets[0], NULL)
                                        : acip_tcp_transport_create("borrowed-eof", sockets[0], NULL);
    ASCIICHAT_REQUIRE_NOT_NULL(transport);
    socket_close(sockets[1]);
    void *buffer = NULL, *allocated = NULL;
    size_t length = 0;
    ASCIICHAT_REQUIRE_EQ(acip_transport_recv(transport, &buffer, &length, &allocated), ERROR_NETWORK);
    ASCIICHAT_REQUIRE_NOT(acip_transport_is_connected(transport));
    acip_transport_destroy(transport);
    struct sockaddr_storage address;
    socklen_t size = sizeof(address);
    int result = socket_getsockname(sockets[0], (struct sockaddr *)&address, &size);
    ASCIICHAT_REQUIRE_EQ(result == 0, !owned);
    if (!owned)
      socket_close(sockets[0]);
  }
}

// Keep a peer open with an incomplete packet to distinguish idle from stream damage.
static void check_timeout(size_t header_bytes, bool payload_byte, bool retryable) {
  int sockets[2];
  ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_transport_t *transport = acip_tcp_transport_create("timeout-test", sockets[0], NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(transport);
  transport->receive_timeout_ns = 10 * 1000000ULL;
  packet_header_t header = {0};
  header.magic = HOST_TO_NET_U64(PACKET_MAGIC);
  header.type = HOST_TO_NET_U16(PACKET_TYPE_PING);
  header.length = HOST_TO_NET_U32(8);
  if (header_bytes)
    ASCIICHAT_REQUIRE_EQ(socket_send(sockets[1], &header, header_bytes, 0), (ssize_t)header_bytes);
  if (payload_byte)
    ASCIICHAT_REQUIRE_EQ(socket_send(sockets[1], "x", 1, 0), 1);
  CLEAR_ERRNO_ALL();
  void *buffer = NULL;
  void *allocated = NULL;
  size_t length = 0;
  ASCIICHAT_REQUIRE_EQ(acip_transport_recv(transport, &buffer, &length, &allocated),
               retryable ? ERROR_NETWORK_TIMEOUT : ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(acip_transport_is_connected(transport), retryable);
  ASCIICHAT_REQUIRE(HAS_ERRNO_CODE(ERROR_NETWORK_TIMEOUT));
  acip_transport_destroy(transport);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
}
TEST_CASE("tcp_transport/idle_header_timeout_is_retryable", "[tcp_transport]") {
  check_timeout(0, false, true);
}
TEST_CASE("tcp_transport/partial_header_timeout_disconnects", "[tcp_transport]") {
  check_timeout(1, false, false);
}
TEST_CASE("tcp_transport/missing_payload_timeout_disconnects", "[tcp_transport]") {
  check_timeout(sizeof(packet_header_t), false, false);
}
TEST_CASE("tcp_transport/partial_payload_timeout_disconnects", "[tcp_transport]") {
  check_timeout(sizeof(packet_header_t), true, false);
}

TEST_CASE("tcp_transport/discovery_rejections_preserve_codes", "[tcp_transport]") {
  ASCIICHAT_REQUIRE(sodium_init() >= 0);
  for (int operation = 0; operation < 3; ++operation) {
    int sockets[2];
    ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    acds_client_t client = {.connected = true};
    client.transport = acip_tcp_transport_create("discovery-rejection", sockets[0], NULL);
    ASCIICHAT_REQUIRE_NOT_NULL(client.transport);
    ASCIICHAT_REQUIRE_EQ(acip_server_send_error(sockets[1], ACIP_ERROR_RATE_LIMITED, "private server context"), ASCIICHAT_OK);
    asciichat_error_t result;
    if (operation == 0) {
      acds_session_create_params_t params = {.max_participants = 2};
      crypto_sign_keypair(params.identity_pubkey, params.identity_seckey);
      acds_session_create_result_t response;
      result = acds_session_create(&client, &params, &response);
    } else if (operation == 1) {
      acds_session_lookup_result_t response;
      result = acds_session_lookup(&client, "blue-mountain-tiger", &response);
    } else {
      acds_session_join_params_t params = {.session_string = "blue-mountain-tiger"};
      crypto_sign_keypair(params.identity_pubkey, params.identity_seckey);
      acds_session_join_result_t response;
      result = acds_session_join(&client, &params, &response);
    }
    ASCIICHAT_REQUIRE_EQ(result, ERROR_RATE_LIMITED);
    ASCIICHAT_REQUIRE(HAS_ERRNO_CODE(ERROR_RATE_LIMITED));
    acip_transport_destroy(client.transport);
    socket_close(sockets[0]);
    socket_close(sockets[1]);
    CLEAR_ERRNO_ALL();
  }
}
TEST_CASE("tcp_transport/discovery_joined_rejection_is_failure", "[tcp_transport]") {
  ASCIICHAT_REQUIRE(sodium_init() >= 0);
  int sockets[2];
  ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_session_joined_t wire = {.success = 0, .error_code = ACIP_ERROR_INVALID_PASSWORD};
  ASCIICHAT_REQUIRE_EQ(packet_send(sockets[1], PACKET_TYPE_ACIP_SESSION_JOINED, &wire, sizeof(wire)), ASCIICHAT_OK);
  acds_client_t client = {.connected = true};
  client.transport = acip_tcp_transport_create("discovery-join-rejection", sockets[0], NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(client.transport);
  acds_session_join_params_t params = {.session_string = "blue-mountain-tiger"};
  crypto_sign_keypair(params.identity_pubkey, params.identity_seckey);
  acds_session_join_result_t response;
  ASCIICHAT_REQUIRE_EQ(acds_session_join(&client, &params, &response), ERROR_INVALID_PASSWORD);
  ASCIICHAT_REQUIRE_NOT(response.success);
  acip_transport_destroy(client.transport);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
}
TEST_CASE("tcp_transport/error_response_hides_internal_details", "[tcp_transport]") {
  int sockets[2];
  ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  ASCIICHAT_REQUIRE_EQ(packet_send_error(sockets[0], NULL, ERROR_FILE_NOT_FOUND, "private/path and subprocess details"),
               ASCIICHAT_OK);
  packet_envelope_t envelope = {0};
  ASCIICHAT_REQUIRE_EQ(receive_packet_secure_with_timeout(sockets[1], NULL, false, &envelope, NS_PER_SEC_INT),
               PACKET_RECV_SUCCESS);
  asciichat_error_t code;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1];
  ASCIICHAT_REQUIRE_EQ(packet_parse_error_message((const uint8_t *)envelope.data + sizeof(packet_header_t),
                                          envelope.len - sizeof(packet_header_t), &code, message, sizeof(message),
                                          NULL,
               ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(code, ERROR_INTERNAL);
  ASCIICHAT_REQUIRE_STR_EQ(message, asciichat_error_string(ERROR_INTERNAL));
  buffer_release(envelope.allocated_buffer);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
}

TEST_CASE("tcp_transport/error_response_hides_errno_stack_locations", "[tcp_transport]") {
  int sockets[2];
  ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  CLEAR_ERRNO_ALL();
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "inner packet failure");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "outer client failure");
  ASCIICHAT_REQUIRE_EQ(packet_send_error(sockets[0], NULL, ERROR_NETWORK_PROTOCOL, NULL), ASCIICHAT_OK);

  packet_envelope_t envelope = {0};
  ASCIICHAT_REQUIRE_EQ(receive_packet_secure_with_timeout(sockets[1], NULL, false, &envelope, NS_PER_SEC_INT),
               PACKET_RECV_SUCCESS);
  asciichat_error_t code;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1];
  ASCIICHAT_REQUIRE_EQ(packet_parse_error_message((const uint8_t *)envelope.data + sizeof(packet_header_t),
                                          envelope.len - sizeof(packet_header_t), &code, message, sizeof(message),
                                          NULL,
               ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(code, ERROR_NETWORK_PROTOCOL);
  ASCIICHAT_REQUIRE_STR_EQ(message, asciichat_error_string(ERROR_NETWORK_PROTOCOL));
  ASCIICHAT_REQUIRE_NULL(strstr(message, "outer client failure"));
  ASCIICHAT_REQUIRE_NULL(strstr(message, "inner packet failure"));
  ASCIICHAT_REQUIRE_NULL(strstr(message, "tcp_transport_test.c"));

  buffer_release(envelope.allocated_buffer);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
}

TEST_CASE("tcp_transport/acip_error_response_hides_errno_stack_locations", "[tcp_transport]") {
  int sockets[2];
  ASCIICHAT_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_transport_t *transport = acip_tcp_transport_create("acip-error-stack", sockets[0], NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(transport);
  CLEAR_ERRNO_ALL();
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "socket read failed");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "packet validation failed");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "ACIP message rejected");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "session negotiation failed");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "client connection failed");
  ASCIICHAT_REQUIRE_EQ(acip_send_error(transport, ERROR_NETWORK_PROTOCOL, NULL), ASCIICHAT_OK);

  packet_envelope_t envelope = {0};
  ASCIICHAT_REQUIRE_EQ(receive_packet_secure_with_timeout(sockets[1], NULL, false, &envelope, NS_PER_SEC_INT),
               PACKET_RECV_SUCCESS);
  asciichat_error_t code;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1];
  ASCIICHAT_REQUIRE_EQ(packet_parse_error_message((const uint8_t *)envelope.data + sizeof(packet_header_t),
                                          envelope.len - sizeof(packet_header_t), &code, message, sizeof(message),
                                          NULL,
               ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(code, ERROR_NETWORK_PROTOCOL);
  ASCIICHAT_REQUIRE_STR_EQ(message, asciichat_error_string(ERROR_NETWORK_PROTOCOL));
  ASCIICHAT_REQUIRE_NULL(strstr(message, "client connection failed"));
  ASCIICHAT_REQUIRE_NULL(strstr(message, "session negotiation failed"));
  ASCIICHAT_REQUIRE_NULL(strstr(message, "ACIP message rejected"));
  ASCIICHAT_REQUIRE_NULL(strstr(message, "packet validation failed"));
  ASCIICHAT_REQUIRE_NULL(strstr(message, "socket read failed"));
  if (ASCIICHAT_SOURCE_ROOT[0])
    ASCIICHAT_REQUIRE_NULL(strstr(message, ASCIICHAT_SOURCE_ROOT));

  buffer_release(envelope.allocated_buffer);
  acip_transport_destroy(transport);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
}
#endif

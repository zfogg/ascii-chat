#include <criterion/criterion.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/platform/abstraction.h>
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
#include <errno.h>

Test(tcp_transport, send_failure_preserves_nested_system_context) {
  int sockets[2];
  cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_transport_t *transport = acip_tcp_transport_create("send-failure-context", sockets[0], NULL);
  cr_assert_not_null(transport);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
  asciichat_errno_scope_t scope = errno_checkpoint();
  cr_assert_eq(packet_send_via_transport(transport, PACKET_TYPE_STREAM_START, NULL, 0, 0), ERROR_NETWORK);
  asciichat_error_context_t wrapper;
  cr_assert(errno_peek_since(scope, &wrapper));
  cr_assert_eq(wrapper.code, ERROR_NETWORK);
  cr_assert_str_eq(wrapper.context_message, "TCP packet transmission failed");
  cr_assert_str_eq(wrapper.function, "tcp_sendv");
  asciichat_error_context_t frames[ASCIICHAT_ERRNO_MAX_DEPTH];
  size_t count = errno_snapshot(scope.thread_id, frames, ASCIICHAT_ERRNO_MAX_DEPTH);
  bool found_system_cause = false;
  for (size_t i = 0; i < count && i < ASCIICHAT_ERRNO_MAX_DEPTH; ++i) {
    if (frames[i].error_id > scope.after_id && frames[i].has_system_error) {
      cr_assert_eq(frames[i].system_errno, EPIPE);
      cr_assert_str_eq(frames[i].function, "socket_sendv");
      cr_assert_str_eq(frames[i].context_message, "sendmsg failed");
      cr_assert_eq(wrapper.cause_id, frames[i].error_id);
      found_system_cause = true;
    }
  }
  cr_assert(found_system_cause);
  cr_assert_not(acip_transport_is_connected(transport));
  acip_transport_destroy(transport);
  socket_close(sockets[0]);
  CLEAR_ERRNO_ALL();
}

Test(tcp_transport, peer_eof_marks_transport_disconnected) {
  int sockets[2];
  cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_transport_t *transport = acip_tcp_transport_create("eof-test", sockets[0], NULL);
  cr_assert_not_null(transport);
  socket_close(sockets[1]);

  void *buffer = NULL;
  void *allocated = NULL;
  size_t length = 0;
  cr_assert_eq(acip_transport_recv(transport, &buffer, &length, &allocated), ERROR_NETWORK);
  cr_assert_not(acip_transport_is_connected(transport));

  acip_transport_destroy(transport);
  socket_close(sockets[0]);
}

Test(tcp_transport, socket_ownership_survives_peer_eof) {
  for (int owned = 0; owned < 2; ++owned) {
    int sockets[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    acip_transport_t *transport = owned ? acip_tcp_transport_create_owned("owned-eof", sockets[0], NULL)
                                        : acip_tcp_transport_create("borrowed-eof", sockets[0], NULL);
    cr_assert_not_null(transport);
    socket_close(sockets[1]);
    void *buffer = NULL, *allocated = NULL;
    size_t length = 0;
    cr_assert_eq(acip_transport_recv(transport, &buffer, &length, &allocated), ERROR_NETWORK);
    cr_assert_not(acip_transport_is_connected(transport));
    acip_transport_destroy(transport);
    struct sockaddr_storage address;
    socklen_t size = sizeof(address);
    int result = socket_getsockname(sockets[0], (struct sockaddr *)&address, &size);
    cr_assert_eq(result == 0, !owned);
    if (!owned)
      socket_close(sockets[0]);
  }
}

// Keep a peer open with an incomplete packet to distinguish idle from stream damage.
static void check_timeout(size_t header_bytes, bool payload_byte, bool retryable) {
  int sockets[2];
  cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_transport_t *transport = acip_tcp_transport_create("timeout-test", sockets[0], NULL);
  cr_assert_not_null(transport);
  transport->receive_timeout_ns = 10 * 1000000ULL;
  packet_header_t header = {0};
  header.magic = HOST_TO_NET_U64(PACKET_MAGIC);
  header.type = HOST_TO_NET_U16(PACKET_TYPE_PING);
  header.length = HOST_TO_NET_U32(8);
  if (header_bytes)
    cr_assert_eq(socket_send(sockets[1], &header, header_bytes, 0), (ssize_t)header_bytes);
  if (payload_byte)
    cr_assert_eq(socket_send(sockets[1], "x", 1, 0), 1);
  CLEAR_ERRNO_ALL();
  void *buffer = NULL;
  void *allocated = NULL;
  size_t length = 0;
  cr_assert_eq(acip_transport_recv(transport, &buffer, &length, &allocated),
               retryable ? ERROR_NETWORK_TIMEOUT : ERROR_NETWORK);
  cr_assert_eq(acip_transport_is_connected(transport), retryable);
  cr_assert(HAS_ERRNO_CODE(ERROR_NETWORK_TIMEOUT));
  acip_transport_destroy(transport);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
}
Test(tcp_transport, idle_header_timeout_is_retryable) {
  check_timeout(0, false, true);
}
Test(tcp_transport, partial_header_timeout_disconnects) {
  check_timeout(1, false, false);
}
Test(tcp_transport, missing_payload_timeout_disconnects) {
  check_timeout(sizeof(packet_header_t), false, false);
}
Test(tcp_transport, partial_payload_timeout_disconnects) {
  check_timeout(sizeof(packet_header_t), true, false);
}

Test(tcp_transport, discovery_rejections_preserve_codes) {
  cr_assert(sodium_init() >= 0);
  for (int operation = 0; operation < 3; ++operation) {
    int sockets[2];
    cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    acds_client_t client = {.connected = true};
    client.transport = acip_tcp_transport_create("discovery-rejection", sockets[0], NULL);
    cr_assert_not_null(client.transport);
    cr_assert_eq(acip_server_send_error(sockets[1], ACIP_ERROR_RATE_LIMITED, "private server context"), ASCIICHAT_OK);
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
    cr_assert_eq(result, ERROR_RATE_LIMITED);
    cr_assert(HAS_ERRNO_CODE(ERROR_RATE_LIMITED));
    acip_transport_destroy(client.transport);
    socket_close(sockets[0]);
    socket_close(sockets[1]);
    CLEAR_ERRNO_ALL();
  }
}
Test(tcp_transport, discovery_joined_rejection_is_failure) {
  cr_assert(sodium_init() >= 0);
  int sockets[2];
  cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_session_joined_t wire = {.success = 0, .error_code = ACIP_ERROR_INVALID_PASSWORD};
  cr_assert_eq(packet_send(sockets[1], PACKET_TYPE_ACIP_SESSION_JOINED, &wire, sizeof(wire)), ASCIICHAT_OK);
  acds_client_t client = {.connected = true};
  client.transport = acip_tcp_transport_create("discovery-join-rejection", sockets[0], NULL);
  cr_assert_not_null(client.transport);
  acds_session_join_params_t params = {.session_string = "blue-mountain-tiger"};
  crypto_sign_keypair(params.identity_pubkey, params.identity_seckey);
  acds_session_join_result_t response;
  cr_assert_eq(acds_session_join(&client, &params, &response), ERROR_INVALID_PASSWORD);
  cr_assert_not(response.success);
  acip_transport_destroy(client.transport);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
}
Test(tcp_transport, error_response_hides_internal_details) {
  int sockets[2];
  cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  cr_assert_eq(packet_send_error(sockets[0], NULL, ERROR_FILE_NOT_FOUND, "private/path and subprocess details"),
               ASCIICHAT_OK);
  packet_envelope_t envelope = {0};
  cr_assert_eq(receive_packet_secure_with_timeout(sockets[1], NULL, false, &envelope, NS_PER_SEC_INT),
               PACKET_RECV_SUCCESS);
  asciichat_error_t code;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1];
  cr_assert_eq(packet_parse_error_message((const uint8_t *)envelope.data + sizeof(packet_header_t),
                                          envelope.len - sizeof(packet_header_t), &code, message, sizeof(message),
                                          NULL),
               ASCIICHAT_OK);
  cr_assert_eq(code, ERROR_INTERNAL);
  cr_assert_str_eq(message, asciichat_error_string(ERROR_INTERNAL));
  buffer_release(envelope.allocated_buffer);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
}

Test(tcp_transport, error_response_hides_errno_stack_locations) {
  int sockets[2];
  cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  CLEAR_ERRNO_ALL();
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "inner packet failure");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "outer client failure");
  cr_assert_eq(packet_send_error(sockets[0], NULL, ERROR_NETWORK_PROTOCOL, NULL), ASCIICHAT_OK);

  packet_envelope_t envelope = {0};
  cr_assert_eq(receive_packet_secure_with_timeout(sockets[1], NULL, false, &envelope, NS_PER_SEC_INT),
               PACKET_RECV_SUCCESS);
  asciichat_error_t code;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1];
  cr_assert_eq(packet_parse_error_message((const uint8_t *)envelope.data + sizeof(packet_header_t),
                                          envelope.len - sizeof(packet_header_t), &code, message, sizeof(message),
                                          NULL),
               ASCIICHAT_OK);
  cr_assert_eq(code, ERROR_NETWORK_PROTOCOL);
  cr_assert_str_eq(message, asciichat_error_string(ERROR_NETWORK_PROTOCOL));
  cr_assert_null(strstr(message, "outer client failure"));
  cr_assert_null(strstr(message, "inner packet failure"));
  cr_assert_null(strstr(message, "tcp_transport_test.c"));

  buffer_release(envelope.allocated_buffer);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
}

Test(tcp_transport, acip_error_response_hides_errno_stack_locations) {
  int sockets[2];
  cr_assert_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  acip_transport_t *transport = acip_tcp_transport_create("acip-error-stack", sockets[0], NULL);
  cr_assert_not_null(transport);
  CLEAR_ERRNO_ALL();
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "socket read failed");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "packet validation failed");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "ACIP message rejected");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "session negotiation failed");
  (void)SET_ERRNO(ERROR_NETWORK_PROTOCOL, "client connection failed");
  cr_assert_eq(acip_send_error(transport, ERROR_NETWORK_PROTOCOL, NULL), ASCIICHAT_OK);

  packet_envelope_t envelope = {0};
  cr_assert_eq(receive_packet_secure_with_timeout(sockets[1], NULL, false, &envelope, NS_PER_SEC_INT),
               PACKET_RECV_SUCCESS);
  asciichat_error_t code;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1];
  cr_assert_eq(packet_parse_error_message((const uint8_t *)envelope.data + sizeof(packet_header_t),
                                          envelope.len - sizeof(packet_header_t), &code, message, sizeof(message),
                                          NULL),
               ASCIICHAT_OK);
  cr_assert_eq(code, ERROR_NETWORK_PROTOCOL);
  cr_assert_str_eq(message, asciichat_error_string(ERROR_NETWORK_PROTOCOL));
  cr_assert_null(strstr(message, "client connection failed"));
  cr_assert_null(strstr(message, "session negotiation failed"));
  cr_assert_null(strstr(message, "ACIP message rejected"));
  cr_assert_null(strstr(message, "packet validation failed"));
  cr_assert_null(strstr(message, "socket read failed"));
  if (ASCIICHAT_SOURCE_ROOT[0])
    cr_assert_null(strstr(message, ASCIICHAT_SOURCE_ROOT));

  buffer_release(envelope.allocated_buffer);
  acip_transport_destroy(transport);
  socket_close(sockets[0]);
  socket_close(sockets[1]);
  CLEAR_ERRNO_ALL();
}
#endif

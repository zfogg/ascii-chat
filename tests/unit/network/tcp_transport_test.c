#include <criterion/criterion.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/endian.h>

#ifndef _WIN32
#include <sys/socket.h>

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
#endif

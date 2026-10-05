#include <criterion/criterion.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/platform/abstraction.h>

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
#endif

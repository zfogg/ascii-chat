#include "session/connection.h"
#include <ascii-chat/common.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <string.h>

#include <ascii-chat/network/webrtc/webrtc.h>

/* Keep ICE setup out of this lifecycle fixture while exercising the production
 * transport queue, connectivity state, method dispatch, and interruption. */
static webrtc_state_t connected_peer(webrtc_peer_connection_t *peer) {
  (void)peer;
  return WEBRTC_STATE_CONNECTED;
}
static bool open_channel(webrtc_data_channel_t *channel) {
  (void)channel;
  return true;
}
#define webrtc_get_state connected_peer
#define webrtc_datachannel_is_open open_channel
#include "../../../lib/network/webrtc/transport.c"
#undef webrtc_get_state
#undef webrtc_datachannel_is_open

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)

typedef struct {
  session_connection_t connection;
  socket_t socket;
  acip_transport_t *transport;
  asciichat_thread_t worker;
  bool started;
  bool joined;
  bool fail_join;
  int releases;
  atomic_t packets;
} owner_t;

static void *receive_packets(void *context) {
  owner_t *owner = context;
  char byte;
  while (socket_recv(owner->socket, &byte, 1, 0) == 1)
    atomic_fetch_add_int(&owner->packets, 1);
  return NULL;
}
static void interrupt_owner(void *context) {
  owner_t *owner = context;
  log_info("Interrupting owner socket=%llu transport_socket=%llu", (unsigned long long)owner->socket,
           (unsigned long long)acip_transport_get_socket(owner->transport));
  session_connection_interrupt_transport(owner->transport);
}
static asciichat_error_t join_owner(void *context) {
  owner_t *owner = context;
  if (owner->fail_join)
    return SET_ERRNO(ERROR_THREAD, "Injected join failure");
  if (owner->started) {
    asciichat_error_t result = asciichat_thread_join(&owner->worker, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    owner->started = false;
  }
  owner->joined = true;
  return ASCIICHAT_OK;
}
static void release_owner(void *context) {
  owner_t *owner = context;
  CHECK(owner->joined && !owner->started);
  acip_transport_destroy(owner->transport);
  owner->transport = NULL;
  owner->socket = INVALID_SOCKET_VALUE;
  owner->releases++;
}
static void initialize(owner_t *owner, socket_t socket) {
  memset(owner, 0, sizeof(*owner));
  owner->socket = socket;
  session_connection_init(&owner->connection, (session_connection_ops_t){interrupt_owner, join_owner, release_owner},
                          owner);
  owner->transport = acip_tcp_transport_create_owned("owner_test", socket, NULL);
  CHECK(owner->transport);
}
static socket_t connect_peer(socket_t listener, struct sockaddr_in *address, socket_t *peer) {
  *peer = socket_create("owner_test_peer", AF_INET, SOCK_STREAM, 0);
  CHECK(*peer != INVALID_SOCKET_VALUE);
  CHECK(socket_connect(*peer, (struct sockaddr *)address, sizeof(*address)) == 0);
  socket_t accepted = socket_accept(listener, NULL, NULL, "owner_test_accepted");
  CHECK(accepted != INVALID_SOCKET_VALUE);
  return accepted;
}
static void progress(owner_t *owner, socket_t peer) {
  int before = atomic_load_int(&owner->packets);
  CHECK(socket_send(peer, "x", 1, 0) == 1);
  for (int n = 0; n < 2000 && atomic_load_int(&owner->packets) == before; n++)
    platform_sleep_ms(1);
  CHECK(atomic_load_int(&owner->packets) == before + 1);
}
static void *foreign_stop(void *context) {
  owner_t *owner = context;
  CHECK(session_connection_stop(&owner->connection) == ERROR_INVALID_STATE);
  return NULL;
}
typedef struct {
  acip_transport_t *transport;
  atomic_t entered;
  asciichat_error_t result;
} webrtc_wait_t;

static void *wait_for_webrtc(void *context) {
  webrtc_wait_t *wait = context;
  void *data = NULL, *allocation = NULL;
  size_t length = 0;
  atomic_store_bool(&wait->entered, true);
  wait->result = acip_transport_recv(wait->transport, &data, &length, &allocation);
  buffer_release(allocation);
  return NULL;
}

static void webrtc_interrupt_test(void) {
  webrtc_transport_data_t data[2] = {0};
  acip_transport_t transports[2] = {0};
  for (int i = 0; i < 2; i++) {
    data[i].peer_conn = (void *)&data[i];
    data[i].data_channel = (void *)&data[i];
    data[i].is_connected = true;
    data[i].recv_queue = ringbuffer_create(sizeof(webrtc_recv_msg_t), WEBRTC_RECV_QUEUE_SIZE);
    CHECK(data[i].recv_queue);
    CHECK(mutex_init(&data[i].queue_mutex, "webrtc_test_queue") == ASCIICHAT_OK);
    CHECK(mutex_init(&data[i].state_mutex, "webrtc_test_state") == ASCIICHAT_OK);
    CHECK(cond_init(&data[i].queue_cond, "webrtc_test_receive") == ASCIICHAT_OK);
    transports[i].methods = &webrtc_methods;
    transports[i].impl_data = &data[i];
  }
  webrtc_wait_t wait = {.transport = &transports[0]};
  asciichat_thread_t worker;
  CHECK(asciichat_thread_create(&worker, "blocked_webrtc", wait_for_webrtc, &wait) == ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  while (!atomic_load_bool(&wait.entered) && time_elapsed_ns(start, time_get_ns()) < NS_PER_SEC_INT)
    platform_sleep_ms(1);
  CHECK(atomic_load_bool(&wait.entered));
  platform_sleep_ms(10);
  acip_transport_interrupt(&transports[0]);
  acip_transport_interrupt(&transports[0]);
  CHECK(asciichat_thread_join_timeout(&worker, NULL, NS_PER_SEC_INT) == ASCIICHAT_OK);
  CHECK(wait.result != ASCIICHAT_OK);
  CHECK(!acip_transport_is_connected(&transports[0]));
  CHECK(acip_transport_is_connected(&transports[1]));
  for (int i = 0; i < 2; i++) {
    cond_destroy(&data[i].queue_cond);
    mutex_destroy(&data[i].state_mutex);
    mutex_destroy(&data[i].queue_mutex);
    ringbuffer_destroy(data[i].recv_queue);
  }
}

static void count_interrupt(acip_transport_t *transport) {
  int *count = transport->impl_data;
  ++*count;
}

void test_session_protocol(void);

int main(void) {
  log_init(NULL, LOG_DEBUG, false, false);
  log_set_terminal_output(true);
  test_session_protocol();
  webrtc_interrupt_test();
  int interrupted[2] = {0};
  const acip_transport_methods_t dispatch_methods = {.interrupt = count_interrupt};
  acip_transport_t dispatch[2] = {{.methods = &dispatch_methods, .impl_data = &interrupted[0]},
                                  {.methods = &dispatch_methods, .impl_data = &interrupted[1]}};
  acip_transport_interrupt(NULL);
  session_connection_interrupt_transport(&dispatch[0]);
  CHECK(interrupted[0] == 1 && interrupted[1] == 0);
  acip_transport_interrupt(&dispatch[1]);
  CHECK(interrupted[0] == 1 && interrupted[1] == 1);
  const acip_transport_methods_t no_interrupt = {0};
  acip_transport_t unsupported = {.methods = &no_interrupt};
  acip_transport_interrupt(&unsupported);
  CHECK(socket_init() == ASCIICHAT_OK);
  socket_t listener = socket_create("owners_listener", AF_INET, SOCK_STREAM, 0);
  CHECK(listener != INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  CHECK(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  CHECK(socket_listen(listener, 4) == 0);
  socklen_t length = sizeof(address);
  CHECK(socket_getsockname(listener, (struct sockaddr *)&address, &length) == 0);
  owner_t owners[2];
  socket_t peers[2];
  for (int i = 0; i < 2; i++) {
    initialize(&owners[i], connect_peer(listener, &address, &peers[i]));
    CHECK(asciichat_thread_create(&owners[i].worker, "owner_receive", receive_packets, &owners[i]) == ASCIICHAT_OK);
    owners[i].started = true;
    CHECK(session_connection_start(&owners[i].connection) == ASCIICHAT_OK);
    progress(&owners[i], peers[i]);
  }
  owners[0].fail_join = true;
  CHECK(session_connection_stop(&owners[0].connection) == ERROR_THREAD);
  CHECK(owners[0].transport && owners[0].releases == 0);
  CHECK(atomic_load_int(&owners[1].connection.state) == SESSION_CONNECTION_RUNNING);
  progress(&owners[1], peers[1]);
  owners[0].fail_join = false;
  CHECK(session_connection_stop(&owners[0].connection) == ASCIICHAT_OK);
  CHECK(session_connection_stop(&owners[0].connection) == ASCIICHAT_OK);
  CHECK(owners[0].releases == 1);
  progress(&owners[1], peers[1]);
  asciichat_thread_t stopper;
  CHECK(asciichat_thread_create(&stopper, "foreign_stop", foreign_stop, &owners[1]) == ASCIICHAT_OK);
  CHECK(asciichat_thread_join(&stopper, NULL) == ASCIICHAT_OK);
  CHECK(owners[1].releases == 0 && owners[1].transport);
  CHECK(session_connection_stop(&owners[1].connection) == ASCIICHAT_OK);
  CHECK(owners[1].releases == 1);
  for (int i = 0; i < 2; i++)
    socket_close(peers[i]);
  // Partial startup follows the same release path, even with no worker.
  socket_t peer;
  initialize(&owners[0], connect_peer(listener, &address, &peer));
  session_connection_request_stop(&owners[0].connection);
  CHECK(session_connection_start(&owners[0].connection) == ERROR_INVALID_STATE);
  CHECK(session_connection_stop(&owners[0].connection) == ASCIICHAT_OK);
  CHECK(owners[0].releases == 1);
  socket_close(peer);
  socket_close(listener);
  log_info("PASS: two live TCP owners, isolated shutdown, retained failed join, foreign stop, partial startup");
  return 0;
}

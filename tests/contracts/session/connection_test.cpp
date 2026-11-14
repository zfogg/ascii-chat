#include "../worker_assert.h"
#include <ascii-chat/tests/catch2_test_helpers.hpp>
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

typedef struct {
  session_connection_t connection;
  socket_t socket;
  acip_transport_t *transport;
  platform_thread_t worker;
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
    asciichat_error_t result = thread_join(&owner->worker, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    owner->started = false;
  }
  owner->joined = true;
  return ASCIICHAT_OK;
}
static void release_owner(void *context) {
  owner_t *owner = context;
  ASCIICHAT_REQUIRE(owner->joined && !owner->started);
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
  ASCIICHAT_REQUIRE(owner->transport);
}
static socket_t connect_peer(socket_t listener, struct sockaddr_in *address, socket_t *peer) {
  *peer = socket_create("owner_test_peer", AF_INET, SOCK_STREAM, 0);
  ASCIICHAT_REQUIRE_NE(*peer, INVALID_SOCKET_VALUE);
  ASCIICHAT_REQUIRE_EQ(socket_connect(*peer, (struct sockaddr *)address, sizeof(*address)), 0);
  socket_t accepted = socket_accept(listener, NULL, NULL, "owner_test_accepted");
  ASCIICHAT_REQUIRE_NE(accepted, INVALID_SOCKET_VALUE);
  return accepted;
}
static void progress(owner_t *owner, socket_t peer) {
  int before = atomic_load_int(&owner->packets);
  ASCIICHAT_REQUIRE_EQ(socket_send(peer, "x", 1, 0), 1);
  for (int n = 0; n < 2000 && atomic_load_int(&owner->packets) == before; n++)
    platform_sleep_ms(1);
  ASCIICHAT_REQUIRE_EQ(atomic_load_int(&owner->packets), before + 1);
}
static void *foreign_stop(void *context) {
  owner_t *owner = context;
  ASCIICHAT_WORKER_ASSERT((session_connection_stop(&owner->connection)) == (ERROR_INVALID_STATE));
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
    ASCIICHAT_REQUIRE(data[i].recv_queue);
    ASCIICHAT_REQUIRE_EQ(mutex_init(&data[i].queue_mutex, "webrtc_test_queue"), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(mutex_init(&data[i].state_mutex, "webrtc_test_state"), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(cond_init(&data[i].queue_cond, "webrtc_test_receive"), ASCIICHAT_OK);
    transports[i].methods = &webrtc_methods;
    transports[i].impl_data = &data[i];
  }
  webrtc_wait_t wait = {.transport = &transports[0]};
  platform_thread_t worker;
  ASCIICHAT_REQUIRE_EQ(platform_thread_create(&worker, "blocked_webrtc", wait_for_webrtc, &wait), ASCIICHAT_OK);
  uint64_t start = time_get_ns();
  while (!atomic_load_bool(&wait.entered) && time_elapsed_ns(start, time_get_ns()) < NS_PER_SEC_INT)
    platform_sleep_ms(1);
  ASCIICHAT_REQUIRE(atomic_load_bool(&wait.entered));
  platform_sleep_ms(10);
  acip_transport_interrupt(&transports[0]);
  acip_transport_interrupt(&transports[0]);
  ASCIICHAT_REQUIRE_EQ(thread_join_timeout(&worker, NULL, NS_PER_SEC_INT), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(wait.result, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(!acip_transport_is_connected(&transports[0]));
  ASCIICHAT_REQUIRE(acip_transport_is_connected(&transports[1]));
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

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  ASCIICHAT_REQUIRE_EQ(socket_init(), ASCIICHAT_OK);
}
static void teardown(void) {
  socket_cleanup();
  log_destroy();
}
TEST_CASE("session_connections/protocol_dispatch_and_packets", "[session_connections]") {
  test_session_protocol();
}
TEST_CASE("session_connections/webrtc_interrupt_cancels_pending_io", "[session_connections]") {
  webrtc_interrupt_test();
}

static atomic_t closed_peer_sends, closed_peer_receives, closed_peer_releases;
static bool peer_disconnected(acip_transport_t *transport) {
  (void)transport;
  return false;
}
static asciichat_error_t peer_closed_recv(acip_transport_t *transport, void **data, size_t *length, void **allocation) {
  (void)transport;
  (void)data;
  (void)length;
  (void)allocation;
  atomic_fetch_add_int(&closed_peer_receives, 1);
  return SET_ERRNO(ERROR_NETWORK, "fixture peer closed");
}
static asciichat_error_t peer_closed_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  (void)data;
  (void)length;
  atomic_fetch_add_int(&closed_peer_sends, 1);
  return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "dead backend cannot drain reliable output");
}
static asciichat_error_t peer_close(acip_transport_t *transport) {
  (void)transport;
  return ASCIICHAT_OK;
}
static asciichat_error_t peer_packet(network_connection_t *connection, packet_type_t type, const void *data,
                                     size_t length, void *context) {
  (void)connection;
  (void)type;
  (void)data;
  (void)length;
  (void)context;
  return ASCIICHAT_OK;
}
static void peer_released(network_connection_t *connection, void *context) {
  (void)connection;
  (void)context;
  atomic_fetch_add_int(&closed_peer_releases, 1);
}

TEST_CASE("session_connections/disconnected_peer_preserves_receive_cause_without_queuing_rejection", "[session_connections]") {
  const acip_transport_methods_t methods = {
      .recv = peer_closed_recv, .send = peer_closed_send, .close = peer_close, .is_connected = peer_disconnected};
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  transport->methods = &methods;
  network_connection_t *connection = NULL;
  ASCIICHAT_REQUIRE_EQ(network_connection_adopt(transport, &connection), ASCIICHAT_OK);
  const network_connection_callbacks_t callbacks = {.packet = peer_packet};
  atomic_store_int(&closed_peer_sends, 0);
  ASCIICHAT_REQUIRE_EQ(network_connection_run(connection, &callbacks, NULL), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(atomic_load_int(&closed_peer_sends), 0);
  network_connection_destroy(&connection);
  ASCIICHAT_REQUIRE_NULL(connection);

  // Inbound owners must also drain promptly rather than enqueue a rejection
  // behind output retained by a disconnected backend.
  CLEAR_ERRNO();
  atomic_store_int(&closed_peer_receives, 0);
  atomic_store_int(&closed_peer_releases, 0);
  network_server_config_t config = {
      .bind_ipv4 = true, .ipv4_address = "127.0.0.1", .callbacks = {.packet = peer_packet, .closed = peer_released}};
  network_server_t *server = NULL;
  ASCIICHAT_REQUIRE_EQ(network_server_create(&config, &server), ASCIICHAT_OK);
  transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  transport->methods = &methods;
  ASCIICHAT_REQUIRE_EQ(network_server_adopt(server, transport, "closed-peer", false), ASCIICHAT_OK);
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (!atomic_load_int(&closed_peer_releases) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  ASCIICHAT_REQUIRE_EQ(atomic_load_int(&closed_peer_releases), 1);
  ASCIICHAT_REQUIRE_EQ(atomic_load_int(&closed_peer_receives), 1);
  ASCIICHAT_REQUIRE_EQ(atomic_load_int(&closed_peer_sends), 0);
  ASCIICHAT_REQUIRE_EQ(network_server_stop(server), ASCIICHAT_OK);
  network_server_destroy(&server);
  ASCIICHAT_REQUIRE_NULL(server);
}

TEST_CASE("session_connections/retains_owners_until_workers_join", "[session_connections]") {
  int interrupted[2] = {0};
  const acip_transport_methods_t dispatch_methods = {.interrupt = count_interrupt};
  acip_transport_t dispatch[2] = {{.methods = &dispatch_methods, .impl_data = &interrupted[0]},
                                  {.methods = &dispatch_methods, .impl_data = &interrupted[1]}};
  acip_transport_interrupt(NULL);
  session_connection_interrupt_transport(&dispatch[0]);
  ASCIICHAT_REQUIRE(interrupted[0] == 1 && interrupted[1] == 0);
  acip_transport_interrupt(&dispatch[1]);
  ASCIICHAT_REQUIRE(interrupted[0] == 1 && interrupted[1] == 1);
  const acip_transport_methods_t no_interrupt = {0};
  acip_transport_t unsupported = {.methods = &no_interrupt};
  acip_transport_interrupt(&unsupported);
  socket_t listener = socket_create("owners_listener", AF_INET, SOCK_STREAM, 0);
  ASCIICHAT_REQUIRE_NE(listener, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  ASCIICHAT_REQUIRE_EQ(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)), 0);
  ASCIICHAT_REQUIRE_EQ(socket_listen(listener, 4), 0);
  socklen_t length = sizeof(address);
  ASCIICHAT_REQUIRE_EQ(socket_getsockname(listener, (struct sockaddr *)&address, &length), 0);
  owner_t owners[2];
  socket_t peers[2];
  for (int i = 0; i < 2; i++) {
    initialize(&owners[i], connect_peer(listener, &address, &peers[i]));
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&owners[i].worker, "owner_receive", receive_packets, &owners[i]), ASCIICHAT_OK);
    owners[i].started = true;
    ASCIICHAT_REQUIRE_EQ(session_connection_start(&owners[i].connection), ASCIICHAT_OK);
    progress(&owners[i], peers[i]);
  }
  owners[0].fail_join = true;
  ASCIICHAT_REQUIRE_EQ(session_connection_stop(&owners[0].connection), ERROR_THREAD);
  ASCIICHAT_REQUIRE(owners[0].transport && owners[0].releases == 0);
  ASCIICHAT_REQUIRE_EQ(atomic_load_int(&owners[1].connection.state), SESSION_CONNECTION_RUNNING);
  progress(&owners[1], peers[1]);
  owners[0].fail_join = false;
  ASCIICHAT_REQUIRE_EQ(session_connection_stop(&owners[0].connection), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(session_connection_stop(&owners[0].connection), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(owners[0].releases, 1);
  progress(&owners[1], peers[1]);
  platform_thread_t stopper;
  ASCIICHAT_REQUIRE_EQ(platform_thread_create(&stopper, "foreign_stop", foreign_stop, &owners[1]), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(thread_join(&stopper, NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(owners[1].releases == 0 && owners[1].transport);
  ASCIICHAT_REQUIRE_EQ(session_connection_stop(&owners[1].connection), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(owners[1].releases, 1);
  for (int i = 0; i < 2; i++)
    socket_close(peers[i]);
  // Partial startup follows the same release path, even with no worker.
  socket_t peer;
  initialize(&owners[0], connect_peer(listener, &address, &peer));
  session_connection_request_stop(&owners[0].connection);
  ASCIICHAT_REQUIRE_EQ(session_connection_start(&owners[0].connection), ERROR_INVALID_STATE);
  ASCIICHAT_REQUIRE_EQ(session_connection_stop(&owners[0].connection), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(owners[0].releases, 1);
  socket_close(peer);
  socket_close(listener);
  log_info("PASS: two live TCP owners, isolated shutdown, retained failed join, foreign stop, partial startup");
}

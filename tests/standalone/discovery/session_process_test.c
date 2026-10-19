/** Regression checks for discovery processing against the production library. */
#include "discovery/session.h"
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <stdlib.h>

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      log_set_terminal_output(true);                                                                                   \
      log_error("Discovery check failed: %s (%s:%d)", #condition, __FILE__, __LINE__);                                 \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

static uint64_t now_ms = 100000;
static unsigned int transitions, sends, receives;
static bool connected = true;
static asciichat_error_t send_result = ASCIICHAT_OK, recv_result = ERROR_NETWORK_TIMEOUT;
static packet_type_t incoming_type = PACKET_TYPE_ACIP_PARTICIPANT_JOINED;

static uint64_t discovery_session_clock(void) {
  return now_ms * 1000;
}
static void discovery_session_transition(discovery_state_t state, void *data) {
  discovery_session_t *session = data;
  CHECK(session->state == state);
  transitions++;
}
static bool discovery_session_connected(acip_transport_t *transport) {
  (void)transport;
  return connected;
}
static bool discovery_session_pending(acip_transport_t *transport) {
  (void)transport;
  return true;
}
static asciichat_error_t discovery_session_send(acip_transport_t *transport, const void *data, size_t len) {
  (void)transport;
  CHECK(data && len >= sizeof(packet_header_t));
  sends++;
  return send_result;
}
static asciichat_error_t discovery_session_receive(acip_transport_t *transport, void **buffer, size_t *len,
                                                   void **allocation) {
  (void)transport;
  receives++;
  if (recv_result != ASCIICHAT_OK)
    return recv_result;
  packet_header_t *header = buffer_alloc(NULL, sizeof(*header));
  CHECK(header);
  *header = (packet_header_t){.magic = HOST_TO_NET_U64(PACKET_MAGIC), .type = HOST_TO_NET_U16(incoming_type)};
  *buffer = *allocation = header;
  *len = sizeof(*header);
  return ASCIICHAT_OK;
}
static discovery_session_t discovery_session_fixture(discovery_state_t state) {
  return (discovery_session_t){
      .state = state, .on_state_change = discovery_session_transition, .handoff_last_sample_ms = now_ms};
}
static asciichat_error_t discovery_session_step(discovery_session_t *session, int64_t timeout_ns) {
  session->callback_user_data = session;
  return discovery_session_process(session, timeout_ns);
}

typedef struct {
  socket_t listener;
  int scenario;
} discovery_session_server_t;

static void *discovery_session_server(void *data) {
  discovery_session_server_t *server = data;
  socket_t socket = socket_accept(server->listener, NULL, NULL, "discovery-test-peer");
  CHECK(socket != INVALID_SOCKET_VALUE);
  CHECK(socket_set_timeout(socket, 5 * NS_PER_SEC_INT) == 0);
  packet_type_t type = 0;
  void *payload = NULL;
  size_t len = 0;
  CHECK(receive_packet(socket, &type, &payload, &len) == ASCIICHAT_OK);
  CHECK(type == PACKET_TYPE_PROTOCOL_VERSION && len == sizeof(protocol_version_packet_t));
  CHECK(((protocol_version_packet_t *)payload)->supports_encryption == 1);
  buffer_release(payload);
  payload = NULL;
  protocol_version_packet_t version = {.protocol_version = HOST_TO_NET_U16(1),
                                       .supports_encryption = server->scenario != 3};
  packet_type_t response_type = server->scenario == 1 ? PACKET_TYPE_PONG : PACKET_TYPE_PROTOCOL_VERSION;
  size_t response_size = server->scenario == 2 ? 1 : sizeof(version);
  CHECK(packet_send(socket, response_type, &version, response_size) == ASCIICHAT_OK);
  if (server->scenario == 0) {
    CHECK(receive_packet(socket, &type, &payload, &len) == ASCIICHAT_OK);
    CHECK(type == PACKET_TYPE_CRYPTO_CAPABILITIES && len == sizeof(crypto_capabilities_packet_t));
    buffer_release(payload);
    payload = NULL;
  }
  socket_close(socket);
  return NULL;
}

static void discovery_session_check_tcp(int scenario) {
  discovery_session_server_t server = {.scenario = scenario};
  server.listener = socket_create("discovery-test-listener", AF_INET, SOCK_STREAM, 0);
  CHECK(server.listener != INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET};
  address.sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK);
  CHECK(socket_bind(server.listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  CHECK(socket_listen(server.listener, 1) == 0);
  socklen_t len = sizeof(address);
  CHECK(socket_getsockname(server.listener, (struct sockaddr *)&address, &len) == 0);
  asciichat_thread_t thread;
  CHECK(asciichat_thread_create(&thread, "discovery-test-server", discovery_session_server, &server) == 0);
  discovery_session_t session = discovery_session_fixture(DISCOVERY_STATE_CONNECTING_HOST);
  SAFE_STRNCPY(session.host_address, "127.0.0.1", sizeof(session.host_address));
  session.host_port = NET_TO_HOST_U16(address.sin_port);
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK);
  CHECK(session.participant_ctx);
  if (scenario == 0) {
    CHECK(session.state == DISCOVERY_STATE_ACTIVE);
    CHECK(session_participant_is_connected(session.participant_ctx));
  } else {
    CHECK(session.state == DISCOVERY_STATE_FAILED && session.error == ERROR_NETWORK);
    CHECK(!session_participant_is_connected(session.participant_ctx));
    CHECK(discovery_session_step(&session, 0) == ERROR_NETWORK);
  }
  CHECK(asciichat_thread_join(&thread, NULL) == 0);
  session_participant_destroy(session.participant_ctx);
  session.participant_ctx = NULL;
  socket_close(server.listener);
}

static void discovery_session_check_negotiation(void) {
  discovery_session_t session = discovery_session_fixture(DISCOVERY_STATE_NEGOTIATING);
  session.negotiate.state = NEGOTIATE_STATE_WAITING_PEER;
  session.negotiate.peer_quality_received = true;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK);
  CHECK(session.state == DISCOVERY_STATE_FAILED && session.error == ERROR_INVALID_STATE);
  CHECK(discovery_session_step(&session, 0) == ERROR_INVALID_STATE);

  session = discovery_session_fixture(DISCOVERY_STATE_NEGOTIATING);
  session.negotiate.state = NEGOTIATE_STATE_WAITING_PEER;
  session.negotiate.peer_quality_received = true;
  session.negotiate.our_quality.detection_complete = true;
  session.negotiate.our_quality.has_public_ip = true;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK);
  CHECK(session.is_host && session.state == DISCOVERY_STATE_STARTING_HOST);
  CHECK(!strcmp(session.host_address, "127.0.0.1"));

  session = discovery_session_fixture(DISCOVERY_STATE_NEGOTIATING);
  session.negotiate.state = NEGOTIATE_STATE_WAITING_PEER;
  session.negotiate.peer_quality_received = true;
  session.negotiate.our_quality.detection_complete = true;
  session.negotiate.peer_quality.has_public_ip = true;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK);
  CHECK(!session.is_host && session.state == DISCOVERY_STATE_NEGOTIATING);
  SAFE_STRNCPY(session.negotiate.peer_quality.public_address, "127.0.0.2",
               sizeof(session.negotiate.peer_quality.public_address));
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK);
  CHECK(!session.is_host && session.state == DISCOVERY_STATE_CONNECTING_HOST);
  CHECK(!strcmp(session.host_address, "127.0.0.2"));
}

int main(void) {
  CHECK(asciichat_shared_init(NULL, false, true) == ASCIICHAT_OK);
  CHECK(options_state_init() == ASCIICHAT_OK);
  options_t opts = options_t_new();
  CHECK(options_state_set(&opts) == ASCIICHAT_OK);
  app_callbacks_t callbacks = {.platform_get_monotonic_time_us = discovery_session_clock};
  app_callbacks_register(&callbacks);
  acip_transport_methods_t methods = {.send = discovery_session_send,
                                      .recv = discovery_session_receive,
                                      .is_connected = discovery_session_connected,
                                      .has_pending_data = discovery_session_pending};
  acip_transport_t transport = {.methods = &methods};

  CHECK(discovery_session_process(NULL, 0) == ERROR_INVALID_PARAM);
  discovery_session_t session = discovery_session_fixture((discovery_state_t)-1);
  CHECK(discovery_session_step(&session, 0) == ERROR_INVALID_STATE);
  discovery_state_t idle_states[] = {DISCOVERY_STATE_INIT, DISCOVERY_STATE_CONNECTING_ACDS,
                                     DISCOVERY_STATE_CREATING_SESSION, DISCOVERY_STATE_JOINING_SESSION,
                                     DISCOVERY_STATE_ENDED};
  for (size_t i = 0; i < sizeof(idle_states) / sizeof(idle_states[0]); i++) {
    session = discovery_session_fixture(idle_states[i]);
    CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == idle_states[i]);
  }
  session = discovery_session_fixture(DISCOVERY_STATE_FAILED);
  CHECK(discovery_session_step(&session, 0) == ERROR_NETWORK);
  session.error = ERROR_MEMORY;
  CHECK(discovery_session_step(&session, 0) == ERROR_MEMORY);

  // Keepalive precedes dispatch even for ended sessions, and updates only on success.
  session = discovery_session_fixture(DISCOVERY_STATE_ENDED);
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns() - 5 * NS_PER_SEC_INT;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && sends == 1);
  uint64_t last_ping = session.last_acds_keepalive_ns;
  CHECK(last_ping != 0);
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && sends == 1);
  uint64_t overdue_ping = time_get_ns() - 5 * NS_PER_SEC_INT;
  session.last_acds_keepalive_ns = overdue_ping;
  send_result = ERROR_NETWORK;
  CHECK(discovery_session_step(&session, 0) == ERROR_NETWORK && sends == 2);
  CHECK(session.last_acds_keepalive_ns == overdue_ping && session.state == DISCOVERY_STATE_ENDED);
  send_result = ASCIICHAT_OK;

  session = discovery_session_fixture(DISCOVERY_STATE_WAITING_PEER);
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns();
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && receives == 0);
  CHECK(discovery_session_step(&session, 1) == ASCIICHAT_OK && receives == 1);
  CHECK(session.state == DISCOVERY_STATE_WAITING_PEER);
  recv_result = ASCIICHAT_OK;
  incoming_type = PACKET_TYPE_PONG;
  CHECK(discovery_session_step(&session, 1) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_WAITING_PEER);
  incoming_type = PACKET_TYPE_ACIP_PARTICIPANT_JOINED;
  transitions = 0;
  CHECK(discovery_session_step(&session, 1) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_NEGOTIATING);
  CHECK(transitions == 1);

  // A ready DataChannel takes precedence over signaling receive failures.
  session = discovery_session_fixture(DISCOVERY_STATE_CONNECTING_HOST);
  session.session_type = SESSION_TYPE_WEBRTC;
  session.webrtc_transport_ready = true;
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns();
  recv_result = ERROR_NETWORK;
  unsigned int previous_receives = receives;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_ACTIVE);
  CHECK(receives == previous_receives);
  session.state = DISCOVERY_STATE_CONNECTING_HOST;
  session.webrtc_transport_ready = false;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_CONNECTING_HOST);
  connected = false;
  CHECK(discovery_session_step(&session, 0) == ERROR_NETWORK && session.state == DISCOVERY_STATE_FAILED);
  CHECK(session.error == ERROR_NETWORK);
  connected = true;

  // Active signaling receive failures are currently ignored for hosts.
  session = discovery_session_fixture(DISCOVERY_STATE_ACTIVE);
  session.session_type = SESSION_TYPE_WEBRTC;
  session.is_host = true;
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns();
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_ACTIVE);
  session.is_host = false;
  session.webrtc_transport_ready = true;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_ACTIVE);
  opts.prefer_webrtc = true;
  CHECK(options_state_set(&opts) == ASCIICHAT_OK);
  session.webrtc_transport_ready = false;
  CHECK(discovery_session_step(&session, 0) == ERROR_NETWORK && session.state == DISCOVERY_STATE_FAILED);

  session = discovery_session_fixture(DISCOVERY_STATE_MIGRATING);
  session.migration.detection_time_ms = now_ms - 30000;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_MIGRATING);
  now_ms++;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_FAILED);
  CHECK(discovery_session_step(&session, 0) == ERROR_NETWORK);
  session.state = DISCOVERY_STATE_MIGRATING;
  session.migration.state = MIGRATION_STATE_COMPLETE;
  CHECK(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_ACTIVE);
  discovery_session_check_negotiation();
  CHECK(socket_init() == ASCIICHAT_OK);
  for (int scenario = 0; scenario < 4; scenario++)
    discovery_session_check_tcp(scenario);
  socket_cleanup();
  log_info("Discovery session processing checks passed");
  return 0;
}

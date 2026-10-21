#include "../worker_assert.h"
#include <criterion/criterion.h>
/** Regression checks for discovery processing against the production library. */
#include "discovery/session.h"
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/acip/send.h>
#include "session/host.h"
#include "session/protocol.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/path.h>
#include <stdlib.h>

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
  cr_assert_eq(session->state, state);
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
  cr_assert(data && len >= sizeof(packet_header_t));
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
  cr_assert(header);
  *header = (packet_header_t){.magic = HOST_TO_NET_U64(PACKET_MAGIC), .type = HOST_TO_NET_U16(incoming_type)};
  *buffer = *allocation = header;
  *len = sizeof(*header);
  return ASCIICHAT_OK;
}
static discovery_session_t discovery_session_fixture(discovery_state_t state) {
  return (discovery_session_t){
      .state = state, .on_state_change = discovery_session_transition, .handoff_last_sample_ms = now_ms};
}
static void discovery_test_pong(void *context) {
  discovery_session_host_pong(context);
}

static void discovery_check_pong_clock(acip_transport_t *transport) {
  discovery_session_t session = discovery_session_fixture(DISCOVERY_STATE_ACTIVE);
  session_participant_config_t config = {.address = "127.0.0.1", .port = 1};
  session.participant_ctx = session_participant_create(&config);
  cr_assert(session.participant_ctx);
  session.liveness.ping_interval_ms = 5000;
  session.liveness.timeout_ms = 10000;
  session.liveness.max_failures = 3;
  session_protocol_t protocol;
  session_protocol_init(&protocol, transport, (session_protocol_media_t){0},
                        (acip_client_callbacks_t){.on_pong = discovery_test_pong, .app_ctx = &session});
  for (int reply = 0; reply < 4; ++reply) {
    session.liveness.last_ping_sent_ms = now_ms;
    session.liveness.ping_in_flight = true;
    session.liveness.consecutive_failures = 2;
    cr_assert_eq(session_protocol_dispatch(&protocol, PACKET_TYPE_PONG, NULL, 0), ASCIICHAT_OK);
    cr_assert_eq(atomic_load_u64(&session.liveness.last_pong_received_ms), now_ms);
    cr_assert_eq(discovery_session_check_host_alive(&session), ASCIICHAT_OK);
    cr_assert(!session.liveness.ping_in_flight && session.liveness.consecutive_failures == 0);
    now_ms += 15000;
  }
  session_participant_destroy(session.participant_ctx);
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
  cr_assert_worker((socket) != (INVALID_SOCKET_VALUE));
  cr_assert_worker((socket_set_timeout(socket, 5 * NS_PER_SEC_INT)) == (0));
  packet_type_t type = 0;
  void *payload = NULL;
  size_t len = 0;
  cr_assert_worker((receive_packet(socket, &type, &payload, &len)) == (ASCIICHAT_OK));
  cr_assert_worker(type == PACKET_TYPE_PROTOCOL_VERSION && len == sizeof(protocol_version_packet_t));
  cr_assert_worker((((protocol_version_packet_t *)payload)->supports_encryption) == (1));
  buffer_release(payload);
  payload = NULL;
  protocol_version_packet_t version = {.protocol_version = HOST_TO_NET_U16(1),
                                       .supports_encryption = server->scenario != 3};
  packet_type_t response_type = server->scenario == 1 ? PACKET_TYPE_PONG : PACKET_TYPE_PROTOCOL_VERSION;
  size_t response_size = server->scenario == 2 ? 1 : sizeof(version);
  cr_assert_worker((packet_send(socket, response_type, &version, response_size)) == (ASCIICHAT_OK));
  socket_close(socket);
  return NULL;
}

static void discovery_session_check_tcp(int scenario) {
  char *config_dir = get_config_dir();
  const char *test_home = SAFE_GETENV("ASCII_CHAT_TEST_HOME");
  cr_assert(config_dir && test_home && strncmp(config_dir, test_home, strlen(test_home)) == 0);
  SAFE_FREE(config_dir);
  discovery_session_server_t server = {.scenario = scenario};
  server.listener = socket_create("discovery-test-listener", AF_INET, SOCK_STREAM, 0);
  cr_assert_neq(server.listener, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET};
  address.sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK);
  cr_assert_eq(socket_bind(server.listener, (struct sockaddr *)&address, sizeof(address)), 0);
  cr_assert_eq(socket_listen(server.listener, 1), 0);
  socklen_t len = sizeof(address);
  cr_assert_eq(socket_getsockname(server.listener, (struct sockaddr *)&address, &len), 0);
  discovery_session_t session = discovery_session_fixture(DISCOVERY_STATE_CONNECTING_HOST);
  SAFE_STRNCPY(session.host_address, "127.0.0.1", sizeof(session.host_address));
  session.host_port = NET_TO_HOST_U16(address.sin_port);
  if (scenario == 0) {
    // The positive case completes the real shared host's security negotiation.
    socket_close(server.listener);
    session_host_config_t config = {.port = session.host_port, .ipv4_address = "127.0.0.1", .max_clients = 1};
    session_host_t *host = session_host_create(&config);
    cr_assert(host && session_host_start(host) == ASCIICHAT_OK);
    asciichat_error_t result = discovery_session_step(&session, 0);
    if (result != ASCIICHAT_OK) {
      asciichat_error_context_t error;
      log_error("Host connection result %d: %s", result,
                HAS_ERRNO(&error) ? error.context_message : "no error context");
    }
    cr_assert_eq(result, ASCIICHAT_OK);
    cr_assert(session.state == DISCOVERY_STATE_ACTIVE && session.participant_ctx);
    cr_assert(session_participant_is_connected(session.participant_ctx));
    session_participant_destroy(session.participant_ctx);
    cr_assert_eq(session_host_destroy(host), ASCIICHAT_OK);
    return;
  }
  asciichat_thread_t thread;
  cr_assert_eq(asciichat_thread_create(&thread, "discovery-test-server", discovery_session_server, &server), 0);
  asciichat_error_t result = discovery_session_step(&session, 0);
  cr_assert(session.participant_ctx);
  cr_assert(result != ASCIICHAT_OK && session.state == DISCOVERY_STATE_FAILED && session.error == result);
  cr_assert(!session_participant_is_connected(session.participant_ctx));
  cr_assert_eq(discovery_session_step(&session, 0), result);
  cr_assert_eq(asciichat_thread_join(&thread, NULL), 0);
  session_participant_destroy(session.participant_ctx);
  session.participant_ctx = NULL;
  socket_close(server.listener);
}

static void discovery_session_check_negotiation(void) {
  discovery_session_t session = discovery_session_fixture(DISCOVERY_STATE_NEGOTIATING);
  session.negotiate.state = NEGOTIATE_STATE_WAITING_PEER;
  session.negotiate.peer_quality_received = true;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert(session.state == DISCOVERY_STATE_FAILED && session.error == ERROR_INVALID_STATE);
  cr_assert_eq(discovery_session_step(&session, 0), ERROR_INVALID_STATE);

  session = discovery_session_fixture(DISCOVERY_STATE_NEGOTIATING);
  session.negotiate.state = NEGOTIATE_STATE_WAITING_PEER;
  session.negotiate.peer_quality_received = true;
  session.negotiate.our_quality.detection_complete = true;
  session.negotiate.our_quality.has_public_ip = true;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert(session.is_host && session.state == DISCOVERY_STATE_STARTING_HOST);
  cr_assert(!strcmp(session.host_address, "127.0.0.1"));

  session = discovery_session_fixture(DISCOVERY_STATE_NEGOTIATING);
  session.negotiate.state = NEGOTIATE_STATE_WAITING_PEER;
  session.negotiate.peer_quality_received = true;
  session.negotiate.our_quality.detection_complete = true;
  session.negotiate.peer_quality.has_public_ip = true;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert(!session.is_host && session.state == DISCOVERY_STATE_NEGOTIATING);
  SAFE_STRNCPY(session.negotiate.peer_quality.public_address, "127.0.0.2",
               sizeof(session.negotiate.peer_quality.public_address));
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert(!session.is_host && session.state == DISCOVERY_STATE_CONNECTING_HOST);
  cr_assert(!strcmp(session.host_address, "127.0.0.2"));
}

Test(discovery_session_process, state_dispatch_keepalive_and_migration, .timeout = 30) {
  cr_assert_eq(asciichat_shared_init(NULL, false, true), ASCIICHAT_OK);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t opts = options_t_new();
  cr_assert_eq(options_state_set(&opts), ASCIICHAT_OK);
  app_callbacks_t callbacks = {.platform_get_monotonic_time_us = discovery_session_clock};
  app_callbacks_register(&callbacks);
  acip_transport_methods_t methods = {.send = discovery_session_send,
                                      .recv = discovery_session_receive,
                                      .is_connected = discovery_session_connected,
                                      .has_pending_data = discovery_session_pending};
  acip_transport_t transport = {.methods = &methods};
  discovery_check_pong_clock(&transport);

  cr_assert_eq(discovery_session_process(NULL, 0), ERROR_INVALID_PARAM);
  discovery_session_t session = discovery_session_fixture((discovery_state_t)-1);
  cr_assert_eq(discovery_session_step(&session, 0), ERROR_INVALID_STATE);
  discovery_state_t idle_states[] = {DISCOVERY_STATE_INIT, DISCOVERY_STATE_CONNECTING_ACDS,
                                     DISCOVERY_STATE_CREATING_SESSION, DISCOVERY_STATE_JOINING_SESSION,
                                     DISCOVERY_STATE_ENDED};
  for (size_t i = 0; i < sizeof(idle_states) / sizeof(idle_states[0]); i++) {
    session = discovery_session_fixture(idle_states[i]);
    cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == idle_states[i]);
  }
  session = discovery_session_fixture(DISCOVERY_STATE_FAILED);
  cr_assert_eq(discovery_session_step(&session, 0), ERROR_NETWORK);
  session.error = ERROR_MEMORY;
  cr_assert_eq(discovery_session_step(&session, 0), ERROR_MEMORY);

  // Keepalive precedes dispatch even for ended sessions, and updates only on success.
  // Some monotonic clocks start at process initialization, so establish enough
  // history before constructing an overdue timestamp by subtraction.
  uint64_t elapsed = time_get_ns();
  if (elapsed < 5 * NS_PER_SEC_INT)
    platform_sleep_ns(5 * NS_PER_SEC_INT - elapsed + NS_PER_MS_INT);
  session = discovery_session_fixture(DISCOVERY_STATE_ENDED);
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns() - 5 * NS_PER_SEC_INT;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && sends == 1);
  uint64_t last_ping = session.last_acds_keepalive_ns;
  cr_assert_neq(last_ping, 0);
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && sends == 1);
  uint64_t overdue_ping = time_get_ns() - 5 * NS_PER_SEC_INT;
  session.last_acds_keepalive_ns = overdue_ping;
  send_result = ERROR_NETWORK;
  cr_assert(discovery_session_step(&session, 0) == ERROR_NETWORK && sends == 2);
  cr_assert(session.last_acds_keepalive_ns == overdue_ping && session.state == DISCOVERY_STATE_ENDED);
  send_result = ASCIICHAT_OK;

  session = discovery_session_fixture(DISCOVERY_STATE_WAITING_PEER);
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns();
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && receives == 0);
  cr_assert(discovery_session_step(&session, 1) == ASCIICHAT_OK && receives == 1);
  cr_assert_eq(session.state, DISCOVERY_STATE_WAITING_PEER);
  recv_result = ASCIICHAT_OK;
  incoming_type = PACKET_TYPE_PONG;
  cr_assert(discovery_session_step(&session, 1) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_WAITING_PEER);
  incoming_type = PACKET_TYPE_ACIP_PARTICIPANT_JOINED;
  transitions = 0;
  cr_assert(discovery_session_step(&session, 1) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_NEGOTIATING);
  cr_assert_eq(transitions, 1);

  // A ready DataChannel takes precedence over signaling receive failures.
  session = discovery_session_fixture(DISCOVERY_STATE_CONNECTING_HOST);
  session.session_type = SESSION_TYPE_WEBRTC;
  session.webrtc_transport_ready = true;
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns();
  recv_result = ERROR_NETWORK;
  unsigned int previous_receives = receives;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_ACTIVE);
  cr_assert_eq(receives, previous_receives);
  session.state = DISCOVERY_STATE_CONNECTING_HOST;
  session.webrtc_transport_ready = false;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_CONNECTING_HOST);
  connected = false;
  cr_assert(discovery_session_step(&session, 0) == ERROR_NETWORK && session.state == DISCOVERY_STATE_FAILED);
  cr_assert_eq(session.error, ERROR_NETWORK);
  connected = true;

  // Active signaling receive failures are currently ignored for hosts.
  session = discovery_session_fixture(DISCOVERY_STATE_ACTIVE);
  session.session_type = SESSION_TYPE_WEBRTC;
  session.is_host = true;
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns();
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_ACTIVE);
  session.is_host = false;
  session.webrtc_transport_ready = true;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_ACTIVE);
  opts.prefer_webrtc = true;
  cr_assert_eq(options_state_set(&opts), ASCIICHAT_OK);
  session.webrtc_transport_ready = false;
  cr_assert(discovery_session_step(&session, 0) == ERROR_NETWORK && session.state == DISCOVERY_STATE_FAILED);

  session = discovery_session_fixture(DISCOVERY_STATE_MIGRATING);
  session.migration.detection_time_ms = now_ms - 30000;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_MIGRATING);
  now_ms++;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_FAILED);
  cr_assert_eq(discovery_session_step(&session, 0), ERROR_NETWORK);
  session.state = DISCOVERY_STATE_MIGRATING;
  session.migration.state = MIGRATION_STATE_COMPLETE;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_ACTIVE);
  options_state_destroy();
  asciichat_shared_destroy();
}

Test(discovery_session_process, tcp_establishment_and_negotiation, .timeout = 30) {
  cr_assert_eq(asciichat_shared_init(NULL, false, true), ASCIICHAT_OK);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  app_callbacks_t callbacks = {.platform_get_monotonic_time_us = discovery_session_clock};
  app_callbacks_register(&callbacks);
  discovery_session_check_negotiation();
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  for (int scenario = 0; scenario < 4; scenario++)
    discovery_session_check_tcp(scenario);
  socket_cleanup();
  options_state_destroy();
  asciichat_shared_destroy();
}

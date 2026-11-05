#include "../worker_assert.h"
#include <criterion/criterion.h>
/** Regression checks for discovery processing against the production library. */
#include "discovery/session.h"
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/acds_client.h>
#include "session/host.h"
#include "session/protocol.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/network/crc32.h>
#include <stdlib.h>
#include "discovery-service/server.h"
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/network/consensus/handoff.h>

static uint64_t now_ms = 100000;
static unsigned int transitions, sends, receives;
static bool connected = true;
static asciichat_error_t send_result = ASCIICHAT_OK, recv_result = ERROR_NETWORK_TIMEOUT;
static uint8_t incoming_storage[CONSENSUS_HANDOFF_HEADER];
static const void *incoming_payload = incoming_storage;
static size_t incoming_length;

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
/** @brief Inject a framed signaling packet or a controlled transport receive failure. */
static asciichat_error_t discovery_session_receive(acip_transport_t *transport, void **buffer, size_t *len,
                                                   void **allocation) {
  (void)transport;
  receives++;
  if (recv_result != ASCIICHAT_OK)
    return recv_result;
  packet_header_t *header = buffer_alloc(NULL, sizeof(*header) + incoming_length);
  cr_assert(header);
  *header = (packet_header_t){
      .magic = HOST_TO_NET_U64(PACKET_MAGIC),
      .type = HOST_TO_NET_U16(incoming_type),
      .length = HOST_TO_NET_U32((uint32_t)incoming_length),
      .crc32 = HOST_TO_NET_U32(incoming_length ? asciichat_crc32(incoming_payload, incoming_length) : 0)};
  if (incoming_length)
    memcpy(header + 1, incoming_payload, incoming_length);

  *buffer = *allocation = header;
  *len = sizeof(*header) + incoming_length;
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
  cr_assert_worker((((protocol_version_packet_t *)payload)->supports_encryption) == (ACIP_CRYPTO_FULL));
  buffer_release(payload);
  payload = NULL;
  protocol_version_packet_t version = {.protocol_version = HOST_TO_NET_U16(1),
                                       .supports_encryption =
                                           server->scenario == 3 ? ACIP_CRYPTO_NONE : ACIP_CRYPTO_FULL};
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
  cr_assert_eq(acthread_create(&thread, "discovery-test-server", discovery_session_server, &server), 0);
  asciichat_error_t result = discovery_session_step(&session, 0);
  cr_assert(session.participant_ctx);
  cr_assert(result != ASCIICHAT_OK && session.state == DISCOVERY_STATE_FAILED && session.error == result);
  cr_assert(!session_participant_is_connected(session.participant_ctx));
  cr_assert_eq(discovery_session_step(&session, 0), result);
  cr_assert_eq(thread_join(&thread, NULL), 0);
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

  // A terminal ACDS decision must survive dead-host liveness in this same step.
  recv_result = ASCIICHAT_OK;
  incoming_type = (packet_type_t)CONSENSUS_HANDOFF_PACKET;
  incoming_length = CONSENSUS_HANDOFF_HEADER;
  consensus_handoff_message_t decision = {.kind = HANDOFF_FAILED, .generation = 1};
  incoming_payload = incoming_storage;
  consensus_handoff_encode(&decision, incoming_storage);
  for (int type = SESSION_TYPE_DIRECT_TCP; type <= SESSION_TYPE_WEBRTC; ++type) {
    session = discovery_session_fixture(DISCOVERY_STATE_ACTIVE);
    session.session_type = type;
    session.acds_transport = &transport;
    session.last_acds_keepalive_ns = time_get_ns();
    transitions = 0;
    unsigned int before_sends = sends;
    cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
    cr_assert_eq(session.state, DISCOVERY_STATE_FAILED);
    cr_assert_eq(session.error, ERROR_NETWORK);
    cr_assert_eq(transitions, 1);
    cr_assert_eq(sends, before_sends, "Terminal failure must not send a new HOST_LOST");
    cr_assert_eq(discovery_session_step(&session, 0), ERROR_NETWORK);
    cr_assert_eq(transitions, 1);
  }
  incoming_length = 0;
  recv_result = ERROR_NETWORK_TIMEOUT;

  session = discovery_session_fixture(DISCOVERY_STATE_MIGRATING);
  session.host_loss_detected_ms = now_ms - 30000;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_MIGRATING);
  now_ms++;
  cr_assert(discovery_session_step(&session, 0) == ASCIICHAT_OK && session.state == DISCOVERY_STATE_FAILED);
  cr_assert_eq(discovery_session_step(&session, 0), ERROR_NETWORK_TIMEOUT);
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

Test(discovery_session_process, authoritative_designation_is_idempotent_and_session_scoped) {
  cr_assert_eq(asciichat_shared_init(NULL, false, true), ASCIICHAT_OK);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  app_callbacks_t callbacks = {.platform_get_monotonic_time_us = discovery_session_clock};
  app_callbacks_register(&callbacks);
  acip_transport_methods_t methods = {.send = discovery_session_send,
                                      .recv = discovery_session_receive,
                                      .is_connected = discovery_session_connected,
                                      .has_pending_data = discovery_session_pending};
  acip_transport_t transport = {.methods = &methods};
  discovery_session_t session = discovery_session_fixture(DISCOVERY_STATE_ACTIVE);
  session.is_host = true;
  session.participant_id[0] = session.host_id[0] = 1;
  session.session_id[0] = 2;
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns();
  acip_host_designated_t host = {.session_id = {2},
                                 .host_id = {1},
                                 .host_address = "127.0.0.1",
                                 .host_port = 3000,
                                 .generation = HOST_TO_NET_U32(3)};
  incoming_type = PACKET_TYPE_ACIP_HOST_DESIGNATED;
  incoming_payload = &host;
  incoming_length = sizeof(host);
  recv_result = ASCIICHAT_OK;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.state, DISCOVERY_STATE_ACTIVE);
  cr_assert_not(session.handoff_pending);
  cr_assert_eq(session.handoff_generation, 3);
  cr_assert_str_eq(session.host_address, "127.0.0.1");
  // Duplicate/conflicting same-generation events cannot tear down a healthy call.
  host.host_id[0] = 9;
  host.host_port = 4000;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.host_id[0], 1);
  cr_assert_eq(session.host_port, 3000);
  host.generation = HOST_TO_NET_U32(2);
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.host_id[0], 1);
  host.generation = HOST_TO_NET_U32(4);
  host.session_id[0] = 3;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.handoff_generation, 3);
  host.session_id[0] = 2;
  incoming_length--;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.handoff_generation, 3);
  incoming_length++;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.host_id[0], 9);
  cr_assert_eq(session.state, DISCOVERY_STATE_MIGRATING);
  cr_assert(session.handoff_pending);
  // Replay after a fresh join restores authority without starting migration.
  session = discovery_session_fixture(DISCOVERY_STATE_ACTIVE);
  session.session_id[0] = 2;
  session.host_id[0] = 9;
  session.is_host = true;
  session.acds_transport = &transport;
  session.last_acds_keepalive_ns = time_get_ns();
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.handoff_generation, 4);
  cr_assert_not(session.handoff_pending);
  cr_assert_eq(session.state, DISCOVERY_STATE_ACTIVE);
  // A duplicated commit must not queue another worker teardown either.
  consensus_handoff_message_t commit = {
      .kind = HANDOFF_COMMIT, .session = {2}, .participant = {7}, .generation = 5, .port = 4000};
  uint8_t bytes[CONSENSUS_HANDOFF_HEADER];
  consensus_handoff_encode(&commit, bytes);
  incoming_type = (packet_type_t)CONSENSUS_HANDOFF_PACKET;
  incoming_payload = bytes;
  incoming_length = sizeof(bytes);
  session.session_type = SESSION_TYPE_WEBRTC;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert(session.handoff_pending);
  session.handoff_pending = false;
  session.state = DISCOVERY_STATE_ACTIVE;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_not(session.handoff_pending);
  cr_assert_eq(session.state, DISCOVERY_STATE_ACTIVE);
  commit.generation = 4;
  consensus_handoff_encode(&commit, bytes);
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.handoff_generation, 5);
  cr_assert_not(session.handoff_pending);
  // TCP authority can arrive before its endpoint. Preserve the migration
  // deadline and wait rather than connecting to the previous host's address.
  session.session_type = SESSION_TYPE_DIRECT_TCP;
  commit.generation = 6;
  commit.participant[0] = 8;
  consensus_handoff_encode(&commit, bytes);
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.state, DISCOVERY_STATE_MIGRATING);
  cr_assert_not(session.handoff_pending);
  cr_assert_str_empty(session.host_address);
  incoming_type = PACKET_TYPE_PONG;
  incoming_length = 0;
  now_ms++;
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.state, DISCOVERY_STATE_MIGRATING);
  host.host_id[0] = 8;
  host.generation = HOST_TO_NET_U32(6);
  incoming_type = PACKET_TYPE_ACIP_HOST_DESIGNATED;
  incoming_payload = &host;
  incoming_length = sizeof(host);
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert(session.handoff_pending);
  cr_assert_eq(session.host_port, 4000);
  // A second committed round can replace a host without membership changing.
  session.state = DISCOVERY_STATE_ACTIVE;
  session.handoff_pending = false;
  commit.sequence = 1;
  commit.participant[0] = 9;
  consensus_handoff_encode(&commit, bytes);
  incoming_type = (packet_type_t)CONSENSUS_HANDOFF_PACKET;
  incoming_payload = bytes;
  incoming_length = sizeof(bytes);
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.host_id[0], 9);
  cr_assert_eq(session.host_authority_sequence, 1);
  commit.sequence = 0;
  commit.participant[0] = 8;
  consensus_handoff_encode(&commit, bytes);
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.host_id[0], 9);
  incoming_type = PACKET_TYPE_ACIP_HOST_DESIGNATED;
  incoming_payload = &host; // Delayed endpoint for the preceding committed round.
  incoming_length = sizeof(host);
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert_eq(session.host_id[0], 9);
  cr_assert_not(session.handoff_pending);
  host.host_id[0] = 9;
  host.sequence = HOST_TO_NET_U32(1);
  cr_assert_eq(discovery_session_step(&session, 0), ASCIICHAT_OK);
  cr_assert(session.handoff_pending);
  incoming_length = 0;
  incoming_payload = NULL;
  options_state_destroy();
  asciichat_shared_destroy();
}

typedef struct {
  acds_client_data_t client;
  atomic_t designated, extended, errors, opened;
  acip_host_designated_t last;
  uint16_t quic_port;
  char webtransport_url[512];
  bool fail_send;
} designation_peer_t;

static asciichat_error_t designation_send(acip_transport_t *transport, const void *data, size_t len) {
  designation_peer_t *peer = transport->impl_data;
  const packet_header_t *header = data;
  if (peer->fail_send)
    return ERROR_NETWORK;
  if (len < sizeof(*header))
    return ERROR_INVALID_PARAM;
  packet_type_t type = NET_TO_HOST_U16(header->type);
  if (type == PACKET_TYPE_ACIP_HOST_DESIGNATED && len == sizeof(*header) + sizeof(peer->last)) {
    memcpy(&peer->last, header + 1, sizeof(peer->last));
    atomic_fetch_add_int(&peer->designated, 1);
  } else if (type == PACKET_TYPE_ACIP_HOST_DESIGNATED && len > sizeof(*header) + sizeof(peer->last)) {
    memcpy(&peer->last, header + 1, sizeof(peer->last));
    if (acds_transport_metadata_decode((const uint8_t *)(header + 1) + sizeof(peer->last),
                                       len - sizeof(*header) - sizeof(peer->last), &peer->quic_port,
                                       peer->webtransport_url, sizeof(peer->webtransport_url)) != ASCIICHAT_OK)
      return ERROR_NETWORK_PROTOCOL;
    atomic_fetch_add_int(&peer->extended, 1);
  } else if (type == PACKET_TYPE_ERROR_MESSAGE) {
    atomic_fetch_add_int(&peer->errors, 1);
  }
  return ASCIICHAT_OK;
}
static asciichat_error_t designation_recv(acip_transport_t *transport, void **data, size_t *len, void **allocation) {
  (void)transport;
  (void)data;
  (void)len;
  (void)allocation;
  platform_sleep_ms(1);
  return ERROR_NETWORK_TIMEOUT;
}
static asciichat_error_t designation_close(acip_transport_t *transport) {
  (void)transport;
  return ASCIICHAT_OK;
}
static asciichat_error_t designation_open(network_connection_t *connection, void *data) {
  (void)data;
  acip_transport_t *transport = network_connection_transport(connection);
  designation_peer_t *peer = transport->impl_data;
  network_connection_set_context(connection, &peer->client);
  atomic_store_bool(&peer->opened, true);
  return ASCIICHAT_OK;
}
static asciichat_error_t designation_packet(network_connection_t *connection, packet_type_t type, const void *data,
                                            size_t length, void *context) {
  (void)connection;
  (void)type;
  (void)data;
  (void)length;
  (void)context;
  return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected packet in outbound designation fixture");
}
static acip_transport_t *designation_peer(network_server_t *server, const uint8_t session[16],
                                          const uint8_t participant[16], bool joined) {
  static const acip_transport_methods_t methods = {.send = designation_send,
                                                   .recv = designation_recv,
                                                   .close = designation_close,
                                                   .is_connected = discovery_session_connected};
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  designation_peer_t *peer = SAFE_CALLOC(1, sizeof(*peer), designation_peer_t *);
  transport->methods = &methods;
  transport->impl_data = peer;
  transport->user_data = &peer->client;
  peer->client.transport = transport;
  peer->client.joined_session = joined;
  memcpy(peer->client.session_id, session, 16);
  memcpy(peer->client.participant_id, participant, 16);
  cr_assert_eq(network_server_adopt(server, transport, "designation-fixture", true), ASCIICHAT_OK);
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (!atomic_load_bool(&peer->opened) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  cr_assert(atomic_load_bool(&peer->opened));
  return transport;
}

Test(discovery_session_process, host_fanout_authority_isolation_and_reconnect, .timeout = 30) {
  cr_assert_eq(asciichat_shared_init(NULL, false, true), ASCIICHAT_OK);
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  acds_server_t server = {0};
  cr_assert_eq(database_init(":memory:", &server.db), ASCIICHAT_OK);
  cr_assert_eq(mutex_init(&server.handoff_mutex, "designation-test"), ASCIICHAT_OK);
  server.handoff_mutex_initialized = true;
  acip_session_create_t request = {
      .session_type = SESSION_TYPE_WEBRTC, .max_participants = 8, .expose_ip_publicly = true};
  acip_session_created_t created;
  cr_assert_eq(database_session_create(server.db, &request, &server.config, &created), ASCIICHAT_OK);
  acip_session_join_t join = {.session_string_len = created.session_string_len};
  SAFE_STRNCPY(join.session_string, created.session_string, sizeof(join.session_string));
  acip_session_joined_t first;
  cr_assert_eq(database_session_join(server.db, &join, &server.config, &first), ASCIICHAT_OK);
  network_server_config_t config = {.bind_ipv4 = true,
                                    .ipv4_address = "127.0.0.1",
                                    .max_connections = 8,
                                    .callbacks = {.open = designation_open, .packet = designation_packet}};
  cr_assert_eq(network_server_create(&config, &server.network_server), ASCIICHAT_OK);
  uint8_t other[16] = {99}, outsider[16] = {98};
  acip_transport_t *host = designation_peer(server.network_server, created.session_id, first.participant_id, true);
  acip_transport_t *peer = designation_peer(server.network_server, created.session_id, other, true);
  acip_transport_t *foreign = designation_peer(server.network_server, outsider, other, true);
  acip_transport_t *unjoined = designation_peer(server.network_server, created.session_id, other, false);
  acip_host_announcement_t announcement = {
      .host_address = "127.0.0.1", .host_port = 3456, .generation = HOST_TO_NET_U32(1)};
  memcpy(announcement.session_id, created.session_id, 16);
  memcpy(announcement.host_id, first.participant_id, 16);
  acds_on_host_announcement(&announcement, host, "fixture", &server);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)host->impl_data)->designated), 1);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)peer->impl_data)->designated), 1);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)foreign->impl_data)->designated), 0);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)unjoined->impl_data)->designated), 0);
  // Duplicate announcements can repair delivery; recipients suppress repeat UX.
  acds_on_host_announcement(&announcement, host, "fixture", &server);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)peer->impl_data)->designated), 2);
  announcement.generation = HOST_TO_NET_U32(2);
  acds_on_host_announcement(&announcement, host, "fixture", &server);
  announcement.generation = HOST_TO_NET_U32(1);
  acds_on_host_announcement(&announcement, peer, "fixture", &server);
  memcpy(announcement.host_id, other, 16);
  acds_on_host_announcement(&announcement, peer, "fixture", &server);
  memcpy(announcement.host_id, first.participant_id, 16);
  acds_on_host_announcement(&announcement, foreign, "fixture", &server);
  announcement.host_port++;
  acds_on_host_announcement(&announcement, host, "fixture", &server);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)host->impl_data)->errors), 2);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)peer->impl_data)->errors), 2);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)peer->impl_data)->designated), 2);
  announcement.sequence = HOST_TO_NET_U32(1);
  announcement.host_port = 3456;
  acds_on_host_announcement(&announcement, host, "fixture", &server);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)peer->impl_data)->designated), 2);
  announcement.sequence = 0;
  // A rejoined connection receives the persisted current endpoint and generation.
  acip_transport_t *reconnected = designation_peer(server.network_server, created.session_id, other, true);
  acds_send_current_host(&server, reconnected, created.session_id);
  designation_peer_t *replay = reconnected->impl_data;
  cr_assert_eq(atomic_load_int(&replay->designated), 1);
  cr_assert_eq(replay->last.host_port, 3456);
  cr_assert_arr_eq(replay->last.host_id, first.participant_id, 16);
  cr_assert_eq(sqlite3_exec(server.db, "UPDATE sessions SET expose_ip_publicly = 0", NULL, NULL, NULL), SQLITE_OK);
  acds_send_current_host(&server, reconnected, created.session_id);
  cr_assert_eq(atomic_load_int(&replay->designated), 2);
  cr_assert_str_empty(replay->last.host_address);
  ((designation_peer_t *)peer->impl_data)->fail_send = true;
  announcement.host_port = 3456;
  acds_on_host_announcement(&announcement, host, "fixture", &server);
  cr_assert_eq(atomic_load_int(&replay->designated), 3, "A failed recipient must not stop fanout");
  cr_assert_str_empty(replay->last.host_address);
  cr_assert_eq(sqlite3_exec(server.db, "UPDATE sessions SET expose_ip_publicly = 1", NULL, NULL, NULL), SQLITE_OK);
  uint8_t metadata[ACDS_TRANSPORT_METADATA_MAX];
  size_t metadata_length = 0;
  cr_assert_eq(acds_transport_metadata_encode(4567, "https://example.test:4568/acip", metadata, sizeof(metadata),
                                              &metadata_length),
               ASCIICHAT_OK);
  int legacy_before = atomic_load_int(&replay->designated);
  acds_on_host_announcement_ex(&announcement, metadata, metadata_length, host, "fixture", &server);
  cr_assert_eq(atomic_load_int(&replay->extended), 1);
  cr_assert_eq(atomic_load_int(&replay->designated), legacy_before + 1);
  cr_assert_eq(replay->quic_port, 4567);
  cr_assert_str_eq(replay->webtransport_url, "https://example.test:4568/acip");
  metadata[7]++;
  int errors_before = atomic_load_int(&((designation_peer_t *)host->impl_data)->errors);
  acds_on_host_announcement_ex(&announcement, metadata, metadata_length, host, "fixture", &server);
  cr_assert_eq(atomic_load_int(&((designation_peer_t *)host->impl_data)->errors), errors_before + 1);
  cr_assert_eq(atomic_load_int(&replay->extended), 1);
  cr_assert_eq(sqlite3_exec(server.db, "UPDATE sessions SET expose_ip_publicly = 0", NULL, NULL, NULL), SQLITE_OK);
  acds_send_current_host(&server, reconnected, created.session_id);
  cr_assert_eq(atomic_load_int(&replay->extended), 1, "Private endpoints must not leak through the extension");
  cr_assert_str_empty(replay->last.host_address);
  cr_assert_eq(network_server_stop(server.network_server), ASCIICHAT_OK);
  network_server_destroy(&server.network_server);
  // Release handoff state through normal server cleanup.
  acds_server_shutdown(&server);
  socket_cleanup();
  asciichat_shared_destroy();
}

#include "../worker_assert.h"
#include <criterion/criterion.h>
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/network/websocket/internal.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/log/log.h>
#include <string.h>

typedef struct {
  network_server_t *server;
  atomic_t opened, packets, closing, closed, fail_closing;
  bool reject, stall;
  asciichat_error_t packet_rejection;
  asciichat_error_t result;
} fixture_t;

static asciichat_error_t opened(network_connection_t *connection, void *arg) {
  fixture_t *f = arg;
  cr_assert_worker(network_connection_transport(connection));
  network_connection_set_context(connection, f);
  atomic_fetch_add_int(&f->opened, 1);
  if (f->reject)
    return SET_ERRNO(ERROR_INVALID_STATE, "Injected partial startup failure");
  if (!f->stall)
    network_connection_ready(connection);
  return ASCIICHAT_OK;
}
static asciichat_error_t packet(network_connection_t *connection, packet_type_t type, const void *data, size_t length,
                                void *arg) {
  fixture_t *f = arg;
  cr_assert_worker((network_connection_context(connection)) == (f));
  cr_assert_worker(type == PACKET_TYPE_PING && length == sizeof(uint64_t));
  atomic_fetch_add_int(&f->packets, 1);
  if (f->packet_rejection != ASCIICHAT_OK)
    return SET_ERRNO(f->packet_rejection, "Private admission fixture detail");
  return packet_send_via_transport(network_connection_transport(connection), PACKET_TYPE_PONG, data, length, 0);
}
static asciichat_error_t closing(network_connection_t *connection, void *arg) {
  fixture_t *f = arg;
  cr_assert_worker(network_connection_transport(connection));
  cr_assert_worker((network_connection_context(connection)) == (f));
  atomic_fetch_add_int(&f->closing, 1);
  if (atomic_load_bool(&f->fail_closing))
    return SET_ERRNO(ERROR_THREAD, "Injected producer join failure");
  return ASCIICHAT_OK;
}
static void closed(network_connection_t *connection, void *arg) {
  fixture_t *f = arg;
  cr_assert_worker(!network_connection_transport(connection));
  cr_assert_worker((network_connection_state(connection)) == (NETWORK_CLOSED));
  atomic_fetch_add_int(&f->closed, 1);
}
static void *run(void *arg) {
  fixture_t *f = arg;
  f->result = network_server_run(f->server);
  return NULL;
}
static uint16_t unused_port(void) {
  socket_t probe = socket_create("port_probe", AF_INET, SOCK_STREAM, 0);
  cr_assert_neq(probe, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  cr_assert_eq(socket_bind(probe, (struct sockaddr *)&address, sizeof(address)), 0);
  socklen_t length = sizeof(address);
  cr_assert_eq(socket_getsockname(probe, (struct sockaddr *)&address, &length), 0);
  uint16_t port = NET_TO_HOST_U16(address.sin_port);
  socket_close(probe);
  return port;
}
static void upgrade_websocket(socket_t peer, uint16_t port) {
  char request[512];
  safe_snprintf(request, sizeof(request),
                "GET / HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Protocol: acip\r\n\r\n",
                port);
  socket_buffer_t bytes = {request, strlen(request)};
  cr_assert_eq(socket_sendv_all(peer, &bytes, 1, NS_PER_SEC_INT), ASCIICHAT_OK);
  cr_assert_eq(socket_set_timeout(peer, 2 * NS_PER_SEC_INT), 0);
  char response[1024] = {0};
  size_t used = 0;
  while (!strstr(response, "\r\n\r\n")) {
    ssize_t count = socket_recv(peer, response + used, sizeof(response) - used - 1, 0);
    cr_assert(count > 0);
    used += (size_t)count;
    response[used] = '\0';
    cr_assert(used < sizeof(response) - 1);
  }
  cr_assert(strstr(response, " 101 "));
}
static void wait_count(atomic_t *count, int expected) {
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (atomic_load_int(count) < expected && time_get_ns() < deadline)
    platform_sleep_ms(1);
  cr_assert_eq(atomic_load_int(count), expected);
}
static void stop_visit(network_connection_t *connection, void *arg) {
  fixture_t *f = arg;
  cr_assert_eq(network_connection_context(connection), f);
  network_connection_request_stop(connection);
  // A registry visitor pins application state even while the close path runs.
  platform_sleep_ms(20);
  cr_assert_eq(network_connection_context(connection), f);
}
typedef struct rtc_endpoint {
  struct rtc_endpoint *other;
  webrtc_peer_connection_t *pc;
  webrtc_data_channel_t *dc;
  atomic_t opened;
} rtc_endpoint_t;
static void rtc_description(webrtc_peer_connection_t *pc, const char *sdp, const char *type, void *arg) {
  (void)pc;
  rtc_endpoint_t *endpoint = arg;
  cr_assert_eq(webrtc_set_remote_description(endpoint->other->pc, sdp, type), ASCIICHAT_OK);
}
static void rtc_candidate(webrtc_peer_connection_t *pc, const char *candidate, const char *mid, void *arg) {
  (void)pc;
  rtc_endpoint_t *endpoint = arg;
  cr_assert_eq(webrtc_add_remote_candidate(endpoint->other->pc, candidate, mid), ASCIICHAT_OK);
}
static void rtc_opened(webrtc_data_channel_t *dc, void *arg) {
  rtc_endpoint_t *endpoint = arg;
  endpoint->dc = dc;
  atomic_store_int(&endpoint->opened, 1);
}
static void rtc_pair(rtc_endpoint_t endpoints[2]) {
  for (int i = 0; i < 2; ++i) {
    endpoints[i].other = &endpoints[1 - i];
    webrtc_config_t config = {.bind_address = "127.0.0.1",
                              .on_local_description = rtc_description,
                              .on_local_candidate = rtc_candidate,
                              .on_datachannel_open = rtc_opened,
                              .user_data = &endpoints[i]};
    cr_assert_eq(webrtc_create_peer_connection(&config, &endpoints[i].pc), ASCIICHAT_OK);
  }
  cr_assert_eq(webrtc_create_datachannel(endpoints[0].pc, "acip", &endpoints[0].dc), ASCIICHAT_OK);
  cr_assert_eq(webrtc_create_offer(endpoints[0].pc), ASCIICHAT_OK);
  for (int i = 0; i < 2; ++i)
    wait_count(&endpoints[i].opened, 1);
}
static void rtc_cleanup(rtc_endpoint_t endpoints[2]) {
  for (int i = 0; i < 2; ++i) {
    if (endpoints[i].dc)
      webrtc_datachannel_destroy(endpoints[i].dc);
    if (endpoints[i].pc)
      webrtc_peer_connection_destroy(endpoints[i].pc);
  }
}
static void scenario(int backend, bool reject, bool stall, int corrupt, bool retained) {
  bool websocket = backend == 1, rtc = backend == 2;
  rtc_endpoint_t endpoints[2] = {0};
  log_warn("Connection scenario: backend=%d reject=%d stall=%d corrupt=%d", backend, reject, stall, corrupt);
  fixture_t f = {.reject = reject, .stall = stall};
  atomic_store_bool(&f.fail_closing, retained);
  uint16_t tcp = unused_port(), ws = unused_port();
  while (ws == tcp)
    ws = unused_port();
  network_server_config_t config = {.port = tcp,
                                    .ipv4_address = "127.0.0.1",
                                    .bind_ipv4 = true,
                                    .websocket_enabled = !rtc,
                                    .websocket_port = ws,
                                    .max_connections = 4,
                                    .handshake_timeout_ms = 250,
                                    .partial_timeout_ms = 250,
                                    .callbacks = {opened, packet, closing, closed},
                                    .user_data = &f};
  cr_assert_eq(network_server_create(&config, &f.server), ASCIICHAT_OK);
  cr_assert_eq(network_server_start(f.server), ASCIICHAT_OK);
  asciichat_thread_t thread;
  cr_assert_eq(acthread_create(&thread, "connection_fixture", run, &f), ASCIICHAT_OK);
  platform_sleep_ms(50);
  char endpoint[128];
  safe_snprintf(endpoint, sizeof(endpoint), "%s://127.0.0.1:%u", websocket ? "ws" : "tcp", websocket ? ws : tcp);
  network_connection_t *client = NULL;
  socket_t fragmented = INVALID_SOCKET_VALUE;
  asciichat_error_t open_result = ASCIICHAT_OK;
  if (rtc) {
    rtc_pair(endpoints);
    acip_transport_t *remote = acip_webrtc_transport_create(endpoints[1].pc, endpoints[1].dc, NULL);
    cr_assert(remote);
    cr_assert_eq(network_server_adopt(f.server, remote, "local-rtc-peer", true), ASCIICHAT_OK);
    acip_transport_t *local = acip_webrtc_transport_create(endpoints[0].pc, endpoints[0].dc, NULL);
    cr_assert(local);
    cr_assert_eq(network_connection_adopt(local, &client), ASCIICHAT_OK);
  } else if (corrupt == 2) {
    fragmented = socket_create("fragmented_peer", AF_INET, SOCK_STREAM, 0);
    cr_assert_neq(fragmented, INVALID_SOCKET_VALUE);
    struct sockaddr_in address = {.sin_family = AF_INET,
                                  .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK),
                                  .sin_port = HOST_TO_NET_U16(websocket ? ws : tcp)};
    cr_assert_eq(socket_connect(fragmented, (struct sockaddr *)&address, sizeof(address)), 0);
    if (websocket)
      upgrade_websocket(fragmented, ws);
  } else {
    open_result = network_connection_open("connection_test", endpoint, tcp, NULL, &client, NULL);
  }
  cr_assert(open_result == ASCIICHAT_OK || (reject && open_result == ERROR_NETWORK_CONNECT));
  wait_count(&f.opened, 1);
  uint64_t partial_started = time_get_ns();
  if (!reject && !stall) {
    uint64_t payload = 42;
    if (corrupt) {
      packet_header_t header = {.magic = HOST_TO_NET_U64(corrupt == 1 ? 0 : PACKET_MAGIC),
                                .type = HOST_TO_NET_U16(PACKET_TYPE_PING)};
      acip_transport_t *transport = network_connection_transport(client);
      if (rtc) {
        cr_assert_eq(
            webrtc_datachannel_send(endpoints[0].dc, (const uint8_t *)&header, corrupt == 2 ? 5 : sizeof(header)),
            ASCIICHAT_OK);
      } else if (corrupt == 2) {
        uint8_t frame[11] = {0x82, 0x85, 0, 0, 0, 0};
        memcpy(frame + 6, &header, 5);
        socket_buffer_t partial = {websocket ? (const void *)frame : (const void *)&header,
                                   websocket ? sizeof(frame) : 5};
        cr_assert_eq(socket_sendv_all(fragmented, &partial, 1, NS_PER_SEC_INT), ASCIICHAT_OK);
      } else {
        cr_assert_eq(acip_transport_send(transport, &header, corrupt == 2 ? 5 : sizeof(header)), ASCIICHAT_OK);
      }
    } else {
      acip_transport_t *transport = network_connection_transport(client);
      cr_assert_eq(packet_send_via_transport(transport, PACKET_TYPE_PING, &payload, sizeof(payload), 0), ASCIICHAT_OK);
      wait_count(&f.packets, 1);
      packet_type_t type;
      void *data = NULL, *allocation = NULL;
      size_t length = 0;
      cr_assert_eq(packet_receive_via_transport(transport, &type, &data, &length, &allocation), ASCIICHAT_OK);
      cr_assert(type == PACKET_TYPE_PONG && length == sizeof(payload) && !memcmp(data, &payload, length));
      buffer_release(allocation);
      network_server_foreach(f.server, stop_visit, &f);
    }
  }
  if (retained) {
    wait_count(&f.closing, 1);
    cr_assert_eq(atomic_load_int(&f.closed), 0);
    cr_assert_eq(network_server_connection_count(f.server), 1);
    network_server_request_stop(f.server);
    cr_assert_eq(thread_join(&thread, NULL), ASCIICHAT_OK);
    cr_assert_eq(network_server_stop(f.server), ERROR_THREAD);
    network_server_destroy(&f.server);
    cr_assert(f.server); // Failed joins preserve the owning runtime and dependencies.
    atomic_store_bool(&f.fail_closing, false);
    cr_assert_eq(network_server_stop(f.server), ASCIICHAT_OK);
    cr_assert_eq(atomic_load_int(&f.closed), 1);
    network_connection_destroy(&client);
    network_server_destroy(&f.server);
    cr_assert(!f.server);
    rtc_cleanup(endpoints);
    return;
  }
  wait_count(&f.closed, 1);
  if (corrupt == 2)
    cr_assert(time_get_ns() - partial_started >= 200 * NS_PER_MS_INT);
  cr_assert_eq(atomic_load_int(&f.closing), 1);
  cr_assert_eq(atomic_load_int(&f.packets), (!reject && !stall && !corrupt ? 1 : 0));
  uint64_t drained = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (network_server_connection_count(f.server) && time_get_ns() < drained)
    platform_sleep_ms(1);
  cr_assert_eq(network_server_connection_count(f.server), 0);
  network_connection_destroy(&client);
  network_connection_destroy(&client);
  if (fragmented != INVALID_SOCKET_VALUE)
    socket_close(fragmented);
  network_server_request_stop(f.server);
  cr_assert_eq(thread_join(&thread, NULL), ASCIICHAT_OK);
  cr_assert_eq(f.result, ASCIICHAT_OK);
  cr_assert_eq(network_server_stop(f.server), ASCIICHAT_OK);
  network_server_destroy(&f.server);
  cr_assert(!f.server);
  rtc_cleanup(endpoints);
}
typedef struct {
  unsigned calls;
  unsigned cancel_after;
} cancellation_t;

static bool cancel_connect(void *data) {
  cancellation_t *cancel = data;
  cr_assert(cancel);
  return ++cancel->calls >= cancel->cancel_after;
}

static void establishment_controls(void) {
  cancellation_t cancel = {.cancel_after = 1};
  network_connect_config_t config = {
      .timeout_ms = 120, .should_exit_callback = cancel_connect, .callback_data = &cancel};
  network_connection_t *connection = NULL;
  cr_assert_neq(network_connection_open("cancelled", "tcp://127.0.0.1", 1, &config, &connection, NULL), ASCIICHAT_OK);
  cr_assert(cancel.calls == 1 && !connection);
  cancel.calls = 0;
  cr_assert_neq(network_connection_open("cancelled", "ws://127.0.0.1:1", 1, &config, &connection, NULL), ASCIICHAT_OK);
  cr_assert(cancel.calls == 1 && !connection);

  acds_client_config_t discovery;
  acds_client_config_init_defaults(&discovery);
  discovery.timeout_ms = config.timeout_ms;
  discovery.should_exit_callback = cancel_connect;
  discovery.callback_data = &cancel;
  acds_client_t client;
  cancel.calls = 0;
  cr_assert_neq(acds_client_connect(&client, &discovery), ASCIICHAT_OK);
  cr_assert(cancel.calls == 1 && !client.connected && !client.connection && !client.transport);
}

static void stalled_upgrade_deadline(void) {
  network_connection_t *connection = NULL;
  cancellation_t cancel = {.cancel_after = 4};
  network_connect_config_t config = {.timeout_ms = 120, .callback_data = &cancel};
  // A listening TCP peer that never upgrades must obey the WebSocket deadline.
  socket_t listener = socket_create("stalled_upgrade", AF_INET, SOCK_STREAM, 0);
  cr_assert_neq(listener, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  cr_assert_eq(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)), 0);
  cr_assert_eq(socket_listen(listener, 16), 0);
  socklen_t length = sizeof(address);
  cr_assert_eq(socket_getsockname(listener, (struct sockaddr *)&address, &length), 0);
  char endpoint[128];
  safe_snprintf(endpoint, sizeof(endpoint), "ws://127.0.0.1:%u", NET_TO_HOST_U16(address.sin_port));
  config.should_exit_callback = NULL;
  uint64_t start = time_get_ns();
  cr_assert_neq(network_connection_open("deadline", endpoint, 1, &config, &connection, NULL), ASCIICHAT_OK);
  uint64_t elapsed = time_get_ns() - start;
  cr_assert(elapsed >= 100 * NS_PER_MS_INT && elapsed < 2 * NS_PER_SEC_INT && !connection);
  socket_close(listener);
}

static void cancelled_upgrade(void) {
  network_connection_t *connection = NULL;
  cancellation_t cancel = {.cancel_after = 4};
  network_connect_config_t config = {.timeout_ms = 120, .callback_data = &cancel};
  // A listening TCP peer that never upgrades must obey the WebSocket deadline.
  socket_t listener = socket_create("stalled_upgrade", AF_INET, SOCK_STREAM, 0);
  cr_assert_neq(listener, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  cr_assert_eq(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)), 0);
  cr_assert_eq(socket_listen(listener, 16), 0);
  socklen_t length = sizeof(address);
  cr_assert_eq(socket_getsockname(listener, (struct sockaddr *)&address, &length), 0);
  char endpoint[128];
  safe_snprintf(endpoint, sizeof(endpoint), "ws://127.0.0.1:%u", NET_TO_HOST_U16(address.sin_port));
  config.timeout_ms = 5000;
  config.should_exit_callback = cancel_connect;
  uint64_t start = time_get_ns();
  cr_assert_neq(network_connection_open("cancel_upgrade", endpoint, 1, &config, &connection, NULL), ASCIICHAT_OK);
  cr_assert(cancel.calls >= 4 && time_get_ns() - start < 2 * NS_PER_SEC_INT && !connection);
  socket_close(listener);
}

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
}

static void teardown(void) {
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  cr_assert_eq(stats.outstanding, 0, "Test leaked buffer owners");
  buffer_system_cleanup();
  cr_assert_eq(buffer_process_reserved(), 0);
  socket_cleanup();
  log_destroy();
}

TestSuite(network_connections, .init = setup, .fini = teardown, .timeout = 30);

Test(network_connections, establishment_controls) {
  establishment_controls();
}

Test(network_connections, tcp_packet_rejection_is_delivered_before_close) {
  fixture_t f = {.packet_rejection = ERROR_SESSION_FULL};
  uint16_t port = unused_port();
  network_server_config_t config = {.port = port,
                                    .ipv4_address = "127.0.0.1",
                                    .bind_ipv4 = true,
                                    .max_connections = 4,
                                    .callbacks = {opened, packet, closing, closed},
                                    .user_data = &f};
  cr_assert_eq(network_server_create(&config, &f.server), ASCIICHAT_OK);
  cr_assert_eq(network_server_start(f.server), ASCIICHAT_OK);
  asciichat_thread_t thread;
  cr_assert_eq(acthread_create(&thread, "rejection_fixture", run, &f), ASCIICHAT_OK);
  char endpoint[64];
  safe_snprintf(endpoint, sizeof(endpoint), "tcp://127.0.0.1:%u", port);
  network_connection_t *client = NULL;
  cr_assert_eq(network_connection_open("rejection_client", endpoint, port, NULL, &client, NULL), ASCIICHAT_OK);
  acip_transport_t *transport = network_connection_transport(client);
  uint64_t ping = 7;
  cr_assert_eq(packet_send_via_transport(transport, PACKET_TYPE_PING, &ping, sizeof(ping), 0), ASCIICHAT_OK);
  packet_type_t type;
  void *data = NULL, *allocation = NULL;
  size_t length = 0;
  asciichat_error_t received = packet_receive_via_transport(transport, &type, &data, &length, &allocation);
  cr_expect_eq(received, ASCIICHAT_OK);
  if (received == ASCIICHAT_OK) {
    cr_expect_eq(type, PACKET_TYPE_ERROR_MESSAGE);
    asciichat_error_t code = ASCIICHAT_OK;
    char message[MAX_ERROR_MESSAGE_LENGTH + 1] = {0};
    cr_expect_eq(packet_parse_error_message(data, length, &code, message, sizeof(message), NULL), ASCIICHAT_OK);
    cr_expect_eq(code, ERROR_SESSION_FULL);
    cr_expect_str_eq(message, asciichat_error_string(ERROR_SESSION_FULL));
    cr_expect_null(strstr(message, "Private admission"));
  }
  buffer_release(allocation);
  wait_count(&f.closed, 1);
  network_connection_destroy(&client);
  network_server_request_stop(f.server);
  cr_assert_eq(thread_join(&thread, NULL), ASCIICHAT_OK);
  cr_assert_eq(f.result, ASCIICHAT_OK);
  network_server_destroy(&f.server);
  cr_assert_null(f.server);
}

Test(network_connections, tcp_echo) {
  scenario(0, false, false, 0, false);
}

Test(network_connections, tcp_reject) {
  scenario(0, true, false, 0, false);
}

Test(network_connections, tcp_stall) {
  scenario(0, false, true, 0, false);
}

Test(network_connections, tcp_failed_join) {
  scenario(0, false, false, 1, false);
}

Test(network_connections, tcp_partial_start) {
  scenario(0, false, false, 2, false);
}

Test(network_connections, tcp_close) {
  scenario(0, false, false, 0, true);
}

Test(network_connections, websocket_echo) {
  scenario(1, false, false, 0, false);
}

Test(network_connections, websocket_reject) {
  scenario(1, true, false, 0, false);
}

Test(network_connections, websocket_stall) {
  scenario(1, false, true, 0, false);
}

Test(network_connections, websocket_failed_join) {
  scenario(1, false, false, 1, false);
}

Test(network_connections, websocket_partial_start) {
  scenario(1, false, false, 2, false);
}

Test(network_connections, websocket_close) {
  scenario(1, false, false, 0, true);
}

typedef struct {
  acip_transport_t *transport;
  atomic_t entered, returned, release;
  asciichat_error_t results[8];
} concurrent_close_t;

typedef struct {
  concurrent_close_t *fixture;
  size_t index;
} concurrent_close_worker_t;

static void *held_websocket_service(void *arg) {
  concurrent_close_t *fixture = arg;
  while (!atomic_load_bool(&fixture->release))
    platform_sleep_ms(1);
  return NULL;
}

static void *close_websocket_concurrently(void *arg) {
  concurrent_close_worker_t *worker = arg;
  atomic_fetch_add_int(&worker->fixture->entered, 1);
  worker->fixture->results[worker->index] = acip_transport_close(worker->fixture->transport);
  atomic_fetch_add_int(&worker->fixture->returned, 1);
  return NULL;
}

Test(network_connections, websocket_concurrent_close) {
  acip_websocket_runtime_t *runtime = NULL;
  cr_assert_eq(acip_websocket_runtime_create(&runtime), ASCIICHAT_OK);
  acip_transport_t *transport = acip_websocket_client_transport_create("concurrent_close", "ws://127.0.0.1:1", NULL);
  cr_assert_not_null(transport);
  cr_assert_eq(acip_transport_close(transport), ASCIICHAT_OK);

  // Retire the original thread before replacing its handle. Close interrupts
  // it, but the transport owner retains responsibility for joining it.
  concurrent_close_t fixture = {.transport = transport};
  websocket_transport_data_t *ws = transport->impl_data;
  cr_assert_eq(thread_join(&ws->service_thread, NULL), ASCIICHAT_OK);
  ws->service_running = false;
  // Keep a replacement thread blocked: concurrent close calls must return
  // without trying to join this owner-managed service thread.
  cr_assert_eq(acthread_create(&ws->service_thread, "held_ws_service", held_websocket_service, &fixture), ASCIICHAT_OK);
  ws->service_running = true;
  asciichat_thread_t threads[8];
  concurrent_close_worker_t workers[8];
  for (size_t i = 0; i < 8; ++i) {
    workers[i] = (concurrent_close_worker_t){.fixture = &fixture, .index = i};
    cr_assert_eq(acthread_create(&threads[i], "concurrent_ws_close", close_websocket_concurrently, &workers[i]),
                 ASCIICHAT_OK);
  }
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (atomic_load_int(&fixture.returned) != 8 && time_get_ns() < deadline)
    platform_sleep_ms(1);
  int returned_before_release = atomic_load_int(&fixture.returned);
  bool owner_still_must_join = ws->service_running;
  // Always release the fixture before checking results so failures can unwind.
  atomic_store_bool(&fixture.release, true);
  cr_assert_eq(atomic_load_int(&fixture.entered), 8);
  for (size_t i = 0; i < 8; ++i) {
    cr_assert_eq(thread_join(&threads[i], NULL), ASCIICHAT_OK);
    cr_assert_eq(fixture.results[i], ASCIICHAT_OK);
  }
  cr_assert_eq(returned_before_release, 8, "close must not wait for the owner's service-thread join");
  cr_assert(owner_still_must_join, "close must preserve the owner's join responsibility");
  cr_assert_eq(acip_transport_close(transport), ASCIICHAT_OK);
  acip_transport_destroy(transport);
  acip_websocket_runtime_destroy(&runtime);
}

Test(network_connections, webrtc_echo) {
  cr_assert_eq(webrtc_init(), ASCIICHAT_OK);
  scenario(2, false, false, 0, false);
  webrtc_destroy();
}

Test(network_connections, webrtc_reject) {
  cr_assert_eq(webrtc_init(), ASCIICHAT_OK);
  scenario(2, true, false, 0, false);
  webrtc_destroy();
}

Test(network_connections, webrtc_failed_join) {
  cr_assert_eq(webrtc_init(), ASCIICHAT_OK);
  scenario(2, false, false, 1, false);
  webrtc_destroy();
}

Test(network_connections, webrtc_partial_start) {
  cr_assert_eq(webrtc_init(), ASCIICHAT_OK);
  scenario(2, false, false, 2, false);
  webrtc_destroy();
}

Test(network_connections, webrtc_close) {
  cr_assert_eq(webrtc_init(), ASCIICHAT_OK);
  scenario(2, false, false, 0, true);
  webrtc_destroy();
}

Test(network_connections, stalled_upgrade_deadline) {
  stalled_upgrade_deadline();
}

Test(network_connections, cancelled_upgrade) {
  cancelled_upgrade();
}

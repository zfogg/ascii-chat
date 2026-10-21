#include <criterion/criterion.h>
/* Capability packets must affect the frames delivered by the discovery host. */
#include "session/host.h"
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/negotiation.h>
#include "session/host_timing.h"
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/time.h>

#include "session/protocol.h"
#include <ascii-chat/common.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/palette.h>

typedef struct {
  session_protocol_t protocol;
  char *frame;
  unsigned frames;
} sink_t;

static asciichat_error_t receive_frame(const ascii_frame_packet_t *header, const char *text, void *context) {
  sink_t *sink = context;
  cr_assert(header->width == 40 && header->height == 12);
  SAFE_FREE(sink->frame);
  SAFE_STRDUP(sink->frame, text);
  sink->frames++;
  return ASCIICHAT_OK;
}

static asciichat_error_t send_frame(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  size_t length = 0;
  for (size_t i = 0; i < count; ++i)
    length += buffers[i].len;
  uint8_t *packet = SAFE_MALLOC(length, uint8_t *);
  size_t offset = 0;
  for (size_t i = 0; i < count; ++i) {
    memcpy(packet + offset, buffers[i].data, buffers[i].len);
    offset += buffers[i].len;
  }
  cr_assert(length >= sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  cr_assert_eq(NET_TO_HOST_U32(header.length), length - sizeof(header));
  if (NET_TO_HOST_U16(header.type) != PACKET_TYPE_ASCII_FRAME) {
    SAFE_FREE(packet);
    return ASCIICHAT_OK;
  }
  sink_t *sink = transport->impl_data;
  asciichat_error_t result = session_protocol_dispatch(&sink->protocol, PACKET_TYPE_ASCII_FRAME,
                                                       packet + sizeof(header), length - sizeof(header));
  SAFE_FREE(packet);
  return result;
}

static terminal_capabilities_packet_t capabilities(terminal_color_mode_t color, palette_type_t palette,
                                                   const char *custom) {
  terminal_capabilities_packet_t packet = {.width = HOST_TO_NET_U16(40),
                                           .height = HOST_TO_NET_U16(12),
                                           .color_level = HOST_TO_NET_U32(color),
                                           .utf8_support = HOST_TO_NET_U32(1),
                                           .palette_type = HOST_TO_NET_U32(palette)};
  if (custom)
    SAFE_STRNCPY(packet.palette_custom, custom, sizeof(packet.palette_custom));
  return packet;
}

static void patch(session_host_t *host, uint32_t id, const char *json) {
  uint8_t payload[ACIP_SETTINGS_MAX_SIZE];
  size_t length;
  acip_settings_header_t header = {.schema = 1, .request = 1, .expected_revision = UINT32_MAX};
  cr_assert_eq(acip_settings_encode(&header, json, payload, sizeof(payload), &length), ASCIICHAT_OK);
  cr_assert_eq(session_host_receive_packet(host, id, PACKET_TYPE_ACIP_SETTINGS_SYNC, payload, length), ASCIICHAT_OK);
}
static void update(session_host_t *host, uint32_t id, const terminal_capabilities_packet_t *caps) {
  cr_assert_eq(session_host_receive_packet(host, id, PACKET_TYPE_CLIENT_CAPABILITIES, caps, sizeof(*caps)),
               ASCIICHAT_OK);
  acip_render_settings_t settings = {0};
  cr_assert_eq(acip_render_settings_capabilities(&settings, caps), ASCIICHAT_OK);
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  acip_render_settings_options(&settings, values);
  char *json = options_values_json(values, true);
  cr_assert(json);
  patch(host, id, json);
  SAFE_FREE(json);
  SAFE_FREE(values);
}
static asciichat_error_t render_tick(session_host_t *host) {
  time_sleep_ns(20 * NS_PER_MS_INT);
  return session_host_render_video(host);
}

/* Ignore ANSI control sequences and grid whitespace when checking palette use. */
static void check_palette(const char *frame, const char *allowed) {
  size_t characters = 0;
  for (const unsigned char *p = (const unsigned char *)frame; *p; ++p) {
    if (*p == '\033' && p[1] == '[') {
      p += 2;
      while (*p && (*p < 0x40 || *p > 0x7e))
        ++p;
      cr_assert(*p);
    } else if (*p != ' ' && *p != '\r' && *p != '\n') {
      cr_assert(strchr(allowed, *p));
      ++characters;
    }
  }
  cr_assert(characters > 0);
}

typedef struct {
  session_host_render_config_t config;
  sink_t sink;
} display_t;

static asciichat_error_t display_config(void *context, session_host_render_config_t *config) {
  *config = ((display_t *)context)->config;
  return ASCIICHAT_OK;
}

static void display_frame(void *context, const char *text, size_t size, int width, int height) {
  cr_assert_eq(size, strlen(text));
  ascii_frame_packet_t header = {.width = (uint32_t)width, .height = (uint32_t)height};
  cr_assert_eq(receive_frame(&header, text, &((display_t *)context)->sink), ASCIICHAT_OK);
}

static uint16_t unused_port(void) {
  socket_t probe = socket_create("render_port", AF_INET, SOCK_STREAM, 0);
  cr_assert_neq(probe, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  cr_assert_eq(socket_bind(probe, (struct sockaddr *)&address, sizeof(address)), 0);
  socklen_t length = sizeof(address);
  cr_assert_eq(socket_getsockname(probe, (struct sockaddr *)&address, &length), 0);
  uint16_t port = NET_TO_HOST_U16(address.sin_port);
  socket_close(probe);
  return port;
}
static network_connection_t *connect_host(uint16_t port) {
  network_connection_t *owner = NULL;
  cr_assert_eq(network_connection_open("host_render_peer", "127.0.0.1", port, NULL, &owner, NULL), ASCIICHAT_OK);
  cr_assert_eq(network_connection_prepare_security(owner, false, NULL), ASCIICHAT_OK);
  cr_assert_eq(network_client_negotiate(network_connection_transport(owner), network_connection_handshake(owner),
                                        network_connection_crypto_mode(owner), NETWORK_PROTOCOL_MEDIA),
               ASCIICHAT_OK);
  network_connection_ready(owner);
  return owner;
}
static void receive_render(sink_t *sink) {
  packet_type_t type;
  void *payload = NULL, *allocation = NULL;
  size_t size = 0;
  cr_assert_eq(packet_receive_via_transport(sink->protocol.transport, &type, &payload, &size, &allocation),
               ASCIICHAT_OK);
  cr_assert_eq(session_protocol_dispatch(&sink->protocol, type, payload, size), ASCIICHAT_OK);
  buffer_release(allocation);
}
static void test_ascii_only_display(const image_t *image) {
  session_host_config_t config = {.max_clients = 1};
  session_host_t *host = session_host_create(&config);
  cr_assert(host);
  uint32_t source = session_host_add_memory_participant(host);
  cr_assert(source && session_host_inject_frame(host, source, image) == ASCIICHAT_OK);
  display_t display = {.config = {.width = 40,
                                  .height = 12,
                                  .caps = {.color_level = TERM_COLOR_NONE, .render_mode = RENDER_MODE_HALF_BLOCK},
                                  .palette = get_builtin_palette(PALETTE_BLOCKS)->chars}};
  session_host_render_target_t target = {
      .get_config = display_config, .render_frame = display_frame, .context = &display};
  cr_assert_eq(session_host_set_render_target(host, &target), ASCIICHAT_OK);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  cr_assert(display.sink.frame);
  for (const unsigned char *p = (const unsigned char *)display.sink.frame; *p; ++p)
    cr_assert(*p < 128);
  SAFE_FREE(display.sink.frame);
  session_host_destroy(host);
}

static void test_tcp_delivery(const image_t *image) {
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  session_host_config_t config = {.port = unused_port(), .ipv4_address = "127.0.0.1", .max_clients = 2};
  session_host_t *host = session_host_create(&config);
  cr_assert(host && session_host_start(host) == ASCIICHAT_OK);
  network_connection_t *owner = connect_host(config.port);
  uint32_t client = 0;
  cr_assert(session_host_get_client_ids(host, &client, 1) == 1 && client);
  uint32_t source = session_host_add_memory_participant(host);
  cr_assert(source && session_host_inject_frame(host, source, image) == ASCIICHAT_OK);
  sink_t sink = {0};
  session_protocol_init(&sink.protocol, network_connection_transport(owner),
                        (session_protocol_media_t){.on_frame = receive_frame, .context = &sink},
                        (acip_client_callbacks_t){0});
  terminal_capabilities_packet_t caps = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "TT");
  update(host, client, &caps);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  while (sink.frames < 1)
    receive_render(&sink);
  cr_assert_eq(sink.frames, 1);
  check_palette(sink.frame, "T");
  session_host_render_target_t target = {.client_id = client};
  cr_assert_eq(session_host_set_render_target(host, &target), ASCIICHAT_OK);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  while (sink.frames < 2)
    receive_render(&sink);
  cr_assert_eq(sink.frames, 2);
  check_palette(sink.frame, "T");
  network_connection_destroy(&owner);
  cr_assert_eq(session_host_destroy(host), ASCIICHAT_OK);
  socket_cleanup();
  SAFE_FREE(sink.frame);
}

static asciichat_error_t accept_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  (void)data;
  (void)length;
  return ASCIICHAT_OK;
}
static asciichat_error_t reject_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  (void)data;
  (void)length;
  return ERROR_NETWORK;
}
static void count_destroy(acip_transport_t *transport) {
  ++*(int *)transport->user_data;
}
static void test_transport_ownership(void) {
  session_host_config_t config = {.max_clients = 1};
  session_host_t *host = session_host_create(&config);
  cr_assert(host);
  int destroyed = 0;
  const acip_transport_methods_t good = {.send = accept_send, .destroy_impl = count_destroy};
  const acip_transport_methods_t bad = {.send = reject_send, .destroy_impl = count_destroy};
  acip_transport_t *original = SAFE_CALLOC(1, sizeof(*original), acip_transport_t *);
  original->methods = &good;
  original->user_data = &destroyed;
  uint32_t id = session_host_add_client(host, original, ACIP_TRANSPORT_OWNED, "owned", 0);
  cr_assert(id);
  cr_assert_eq(session_host_set_client_transport(host, id, original, ACIP_TRANSPORT_BORROWED), ERROR_INVALID_STATE);
  acip_transport_t *candidate = SAFE_CALLOC(1, sizeof(*candidate), acip_transport_t *);
  candidate->methods = &bad;
  candidate->user_data = &destroyed;
  cr_assert_eq(session_host_set_client_transport(host, id, candidate, ACIP_TRANSPORT_OWNED), ERROR_NETWORK);
  cr_assert(session_host_get_client_transport(host, id) == original && destroyed == 0);
  acip_transport_destroy(candidate);
  cr_assert_eq(destroyed, 1);
  cr_assert_eq(session_host_set_client_transport(host, id, NULL, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  cr_assert(!session_host_get_client_transport(host, id) && destroyed == 2);
  session_host_destroy(host);
  cr_assert_eq(destroyed, 2);
}

typedef struct {
  acip_transport_t *transport;
  asciichat_error_t result;
  atomic_t entered;
} receive_wait_t;
static void *receive_wait(void *context) {
  receive_wait_t *wait = context;
  void *data = NULL, *allocation = NULL;
  size_t size = 0;
  atomic_store_bool(&wait->entered, true);
  wait->result = acip_transport_recv(wait->transport, &data, &size, &allocation);
  buffer_release(allocation);
  return NULL;
}
static void test_listener_interrupt(void) {
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  session_host_config_t config = {.port = unused_port(), .ipv4_address = "127.0.0.1", .max_clients = 1};
  session_host_t *host = session_host_create(&config);
  cr_assert(host && session_host_start(host) == ASCIICHAT_OK);
  network_connection_t *peer = connect_host(config.port);
  acip_transport_t *transport = network_connection_transport(peer);
  transport->receive_timeout_ns = 5 * NS_PER_SEC_INT;
  receive_wait_t wait = {.transport = transport};
  asciichat_thread_t thread;
  cr_assert_eq(acthread_create(&thread, "accepted_receive", receive_wait, &wait), ASCIICHAT_OK);
  while (!atomic_load_bool(&wait.entered))
    platform_sleep_ms(1);
  acip_transport_interrupt(transport);
  cr_assert_eq(thread_join_timeout(&thread, NULL, NS_PER_SEC_INT), ASCIICHAT_OK);
  cr_assert(wait.result != ASCIICHAT_OK && !acip_transport_is_connected(transport));
  cr_assert_eq(session_host_stop(host), ASCIICHAT_OK);
  cr_assert_eq(session_host_stop(host), ASCIICHAT_OK);
  network_connection_destroy(&peer);
  cr_assert_eq(session_host_destroy(host), ASCIICHAT_OK);
  socket_cleanup();
}

static void test_host_listener(void) {
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  session_host_config_t invalid = {.port = UINT16_MAX + 1};
  session_host_t *invalid_host = session_host_create(&invalid);
  cr_assert(invalid_host && session_host_start(invalid_host) == ERROR_INVALID_PARAM);
  session_host_destroy(invalid_host);
  uint16_t port = unused_port();
  session_host_config_t config = {.port = port, .ipv4_address = "127.0.0.1", .max_clients = 1};
  session_host_t *host = session_host_create(&config);
  cr_assert(host && session_host_start(host) == ASCIICHAT_OK);
  network_connection_t *peer_owner = connect_host(port);
  acip_transport_t *peer = network_connection_transport(peer_owner);
  uint32_t id = 0;
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (!session_host_get_client_ids(host, &id, 1) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  cr_assert(id);
  cr_assert_eq(packet_send_via_transport(peer, PACKET_TYPE_PING, NULL, 0, 0), ASCIICHAT_OK);
  packet_type_t type = 0;
  void *payload = NULL, *allocation = NULL;
  size_t length = 0;
  cr_assert_eq(packet_receive_via_transport(peer, &type, &payload, &length, &allocation), ASCIICHAT_OK);
  cr_assert(type == PACKET_TYPE_PONG && length == 0);
  buffer_release(allocation);
  // An over-capacity peer is rejected through transport cleanup on the library loop.
  network_connection_t *excess_owner = NULL;
  cr_assert_eq(network_connection_open("host_excess_peer", "127.0.0.1", port, NULL, &excess_owner, NULL), ASCIICHAT_OK);
  acip_transport_t *excess = network_connection_transport(excess_owner);
  cr_assert_neq(packet_receive_via_transport(excess, &type, &payload, &length, &allocation), ASCIICHAT_OK);
  buffer_release(allocation);
  network_connection_destroy(&excess_owner);
  cr_assert_eq(packet_send_via_transport(peer, PACKET_TYPE_PING, NULL, 0, 0), ASCIICHAT_OK);
  cr_assert_eq(packet_receive_via_transport(peer, &type, &payload, &length, &allocation), ASCIICHAT_OK);
  cr_assert_eq(type, PACKET_TYPE_PONG);
  buffer_release(allocation);
  session_host_stop(host);
  cr_assert(!session_host_is_running(host));
  cr_assert_neq(packet_receive_via_transport(peer, &type, &payload, &length, &allocation), ASCIICHAT_OK);
  buffer_release(allocation);
  network_connection_destroy(&peer_owner);
  session_host_destroy(host);
  socket_cleanup();
}

static void test_host_reentry(void) {
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  uint16_t port = unused_port();
  session_host_config_t config = {.port = port, .ipv4_address = "127.0.0.1", .max_clients = 1};
  session_host_t *host = session_host_create(&config);
  cr_assert(host && session_host_start(host) == ASCIICHAT_OK);
  for (int reentry = 0; reentry < 3; ++reentry) {
    uint32_t source = session_host_add_memory_participant(host);
    cr_assert(source);
    cr_assert_eq(session_host_start_render(host), ASCIICHAT_OK);
    session_host_stop_render(host);
    cr_assert_eq(session_host_remove_client(host, source), ASCIICHAT_OK);
    cr_assert_eq(session_host_get_client_count(host), 0);
  }
  session_host_destroy(host);
  socket_cleanup();
}

static void test_audio_cadence(void) {
  uint64_t deadline = 0;
  const uint64_t start = NS_PER_SEC_INT;
  cr_assert(!session_host_audio_due(&deadline, start));
  unsigned ticks = 0;
  // A 21 ms scheduler must still consume 500 frames over ten seconds.
  for (uint64_t elapsed = 21; elapsed < 10000; elapsed += 21)
    while (session_host_audio_due(&deadline, start + elapsed * NS_PER_MS_INT))
      ticks++;
  while (session_host_audio_due(&deadline, start + 10 * NS_PER_SEC_INT))
    ticks++;
  cr_assert_eq(ticks, 500);
  cr_assert_eq(deadline, start + 10 * NS_PER_SEC_INT + 20 * NS_PER_MS_INT);
  cr_assert(session_host_audio_due(&deadline, start + 11 * NS_PER_SEC_INT));
  cr_assert(!session_host_audio_due(&deadline, start + 11 * NS_PER_SEC_INT));
  cr_assert_eq(deadline, start + 11 * NS_PER_SEC_INT + 20 * NS_PER_MS_INT);
}

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  *values = options_t_new();
  values->no_encrypt = true;
  cr_assert_eq(options_state_set(values), ASCIICHAT_OK);
  SAFE_FREE(values);
  ascii_simd_init();
}

static void teardown(void) {
  options_state_destroy();
  log_destroy();
}

TestSuite(host_render, .init = setup, .fini = teardown, .timeout = 60);

Test(host_render, recipient_settings_and_delivery, .timeout = 60) {
  session_host_config_t config = {.max_clients = 4};
  session_host_t *host = session_host_create(&config);
  cr_assert(host);
  uint32_t source = session_host_add_memory_participant(host);
  cr_assert(source);
  image_t *image = image_new(8, 4);
  cr_assert(image);
  for (int i = 0; i < image->w * image->h; ++i)
    image->pixels[i] = (rgb_pixel_t){.r = (uint8_t)(40 + i * 6), .g = 25, .b = 100};
  cr_assert_eq(session_host_inject_frame(host, source, image), ASCIICHAT_OK);

  sink_t sinks[3] = {0};
  acip_transport_t transports[3];
  uint32_t ids[3];
  const acip_transport_methods_t methods = {.sendv = send_frame};
  for (int i = 0; i < 3; ++i) {
    transports[i] = (acip_transport_t){.methods = &methods, .impl_data = &sinks[i]};
    session_protocol_init(&sinks[i].protocol, &transports[i],
                          (session_protocol_media_t){.on_frame = receive_frame, .context = &sinks[i]},
                          (acip_client_callbacks_t){0});
    ids[i] = session_host_add_client(host, NULL, ACIP_TRANSPORT_BORROWED, "test", 0);
    cr_assert(ids[i]);
    uint8_t query[ACIP_SETTINGS_HEADER_SIZE];
    size_t query_length;
    acip_settings_header_t header = {.schema = 1, .expected_revision = UINT32_MAX};
    cr_assert_eq(acip_settings_encode(&header, "", query, sizeof(query), &query_length), ASCIICHAT_OK);
    cr_assert_eq(session_host_receive_packet(host, ids[i], PACKET_TYPE_ACIP_SETTINGS_SYNC, query, query_length),
                 ERROR_INVALID_STATE);
    CLEAR_ERRNO();
    cr_assert_eq(session_host_set_client_transport(host, ids[i], &transports[i], ACIP_TRANSPORT_BORROWED),
                 ASCIICHAT_OK);
    cr_assert_eq(session_host_set_client_transport(host, ids[i], NULL, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
    cr_assert_eq(session_host_get_client_transport(host, ids[i]), NULL);
    cr_assert_eq(session_host_set_client_transport(host, ids[i], &transports[i], ACIP_TRANSPORT_BORROWED),
                 ASCIICHAT_OK);
  }
  display_t display = {
      .config = {.caps = {.color_level = TERM_COLOR_NONE}, .palette = "ZZ", .width = 40, .height = 12}};
  session_host_render_target_t target = {
      .get_config = display_config, .render_frame = display_frame, .context = &display};
  cr_assert_eq(session_host_set_render_target(host, &target), ASCIICHAT_OK);

  terminal_capabilities_packet_t caps[3] = {capabilities(TERM_COLOR_TRUECOLOR, PALETTE_CUSTOM, "XY"),
                                            capabilities(TERM_COLOR_256, PALETTE_CUSTOM, "XY"),
                                            capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "XY")};
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  cr_assert(strstr(sinks[0].frame, "\033[38;2;"));
  cr_assert(strstr(sinks[1].frame, "\033[38;5;"));
  cr_assert(!strstr(sinks[1].frame, "\033[38;2;"));
  cr_assert(!strstr(sinks[2].frame, "\033[38;"));
  for (int i = 0; i < 3; ++i)
    check_palette(sinks[i].frame, "XY");

  // Palette-only updates at identical dimensions must not share another client's grid.
  caps[0] = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "XX");
  caps[1] = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "YY");
  caps[2] = capabilities(TERM_COLOR_NONE, PALETTE_MINIMAL, NULL);
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  check_palette(sinks[0].frame, "X");
  check_palette(sinks[1].frame, "Y");
  check_palette(sinks[2].frame, PALETTE_CHARS_MINIMAL);

  // Filters also distinguish cache entries and must leave the shared source intact.
  caps[0] = caps[1] = caps[2] = capabilities(TERM_COLOR_TRUECOLOR, PALETTE_STANDARD, NULL);
  caps[1].color_filter = COLOR_FILTER_GREEN;
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  cr_assert_neq(strcmp(sinks[0].frame, sinks[1].frame), 0);
  cr_assert_eq(strcmp(sinks[0].frame, sinks[2].frame), 0);

  caps[1] = capabilities(TERM_COLOR_16, PALETTE_STANDARD, NULL);
  caps[2] = capabilities(TERM_COLOR_256, PALETTE_STANDARD, NULL);
  caps[2].render_mode = HOST_TO_NET_U32(RENDER_MODE_BACKGROUND);
  update(host, ids[1], &caps[1]);
  update(host, ids[2], &caps[2]);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  cr_assert(strstr(sinks[1].frame, "\033["));
  cr_assert(!strstr(sinks[1].frame, "\033[38;"));
  cr_assert(strstr(sinks[2].frame, "\033[48;5;"));
  caps[2].render_mode = HOST_TO_NET_U32(RENDER_MODE_HALF_BLOCK);
  update(host, ids[2], &caps[2]);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  cr_assert(strstr(sinks[2].frame, "\xe2\x96\x80"));

  // Invalid runtime transactions leave all recipients' last committed values intact.
  for (int i = 0; i < 3; ++i) {
    caps[i] = capabilities(TERM_COLOR_NONE, PALETTE_STANDARD, NULL);
    update(host, ids[i], &caps[i]);
  }
  patch(host, ids[1], "{\"color_mode\":999,\"palette_type\":6,\"palette_custom\":\"XX\"}");
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  cr_assert_eq(strcmp(sinks[0].frame, sinks[1].frame), 0);
  cr_assert_eq(strcmp(sinks[0].frame, sinks[2].frame), 0);
  patch(host, ids[1], "{\"palette_type\":6,\"palette_custom\":\"\"}");
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  cr_assert_eq(strcmp(sinks[0].frame, sinks[1].frame), 0);

  cr_assert_eq(display.sink.frames, 7);
  check_palette(display.sink.frame, "Z");
  // A local backend also picks up changed rendering settings at the next tick.
  display.config.caps.color_level = TERM_COLOR_TRUECOLOR;
  display.config.palette = "LL";
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  cr_assert_eq(display.sink.frames, 8);
  cr_assert(strstr(display.sink.frame, "\033[38;2;"));
  check_palette(display.sink.frame, "L");
  SAFE_FREE(display.sink.frame);

  // Browser hosts use a local client as the target, with no terminal display.
  target = (session_host_render_target_t){.client_id = ids[1]};
  cr_assert_eq(session_host_set_render_target(host, &target), ASCIICHAT_OK);
  caps[1] = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "WW");
  update(host, ids[1], &caps[1]);
  cr_assert_eq(render_tick(host), ASCIICHAT_OK);
  check_palette(sinks[1].frame, "W");
  cr_assert_eq(display.sink.frames, 8);
  for (int i = 0; i < 3; ++i) {
    cr_assert_eq(sinks[i].frames, 9); // Local clients receive exactly one frame per tick.
    SAFE_FREE(sinks[i].frame);
  }
  cr_assert_eq(session_host_remove_client(host, ids[1]), ASCIICHAT_OK);
  cr_assert_eq(session_host_set_render_target(host, &target), ERROR_NOT_FOUND);
  cr_assert_eq(session_host_set_render_target(host, NULL), ASCIICHAT_OK);
  session_host_destroy(host);
  image_destroy(image);
}

Test(host_render, audio_cadence) {
  test_audio_cadence();
}

Test(host_render, reentry) {
  test_host_reentry();
}

Test(host_render, transport_ownership) {
  test_transport_ownership();
}

Test(host_render, listener) {
  test_host_listener();
}

Test(host_render, listener_interrupt) {
  test_listener_interrupt();
}

Test(host_render, tcp_delivery) {
  image_t *image = image_new(8, 4);
  cr_assert_not_null(image);
  for (int i = 0; i < image->w * image->h; ++i)
    image->pixels[i] = (rgb_pixel_t){.r = (uint8_t)(40 + i * 6), .g = 25, .b = 100};
  test_tcp_delivery(image);
  image_destroy(image);
}

Test(host_render, ascii_only_display) {
  image_t *image = image_new(8, 4);
  cr_assert_not_null(image);
  for (int i = 0; i < image->w * image->h; ++i)
    image->pixels[i] = (rgb_pixel_t){.r = (uint8_t)(40 + i * 6), .g = 25, .b = 100};
  test_ascii_only_display(image);
  image_destroy(image);
}

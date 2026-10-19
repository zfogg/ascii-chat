/* Capability packets must affect the frames delivered by the discovery host. */
#include "session/host.h"
#include "session/protocol.h"
#include <ascii-chat/common.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/palette.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "host render line %d: %s", __LINE__, #expr);                                       \
  } while (0)

typedef struct {
  session_protocol_t protocol;
  char *frame;
  unsigned frames;
} sink_t;

static asciichat_error_t receive_frame(const ascii_frame_packet_t *header, const char *text, void *context) {
  sink_t *sink = context;
  CHECK(header->width == 40 && header->height == 12);
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
  CHECK(length >= sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  CHECK(NET_TO_HOST_U32(header.length) == length - sizeof(header));
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
  CHECK(acip_settings_encode(&header, json, payload, sizeof(payload), &length) == ASCIICHAT_OK);
  CHECK(session_host_receive_packet(host, id, PACKET_TYPE_ACIP_SETTINGS_SYNC, payload, length) == ASCIICHAT_OK);
}
static void update(session_host_t *host, uint32_t id, const terminal_capabilities_packet_t *caps) {
  CHECK(session_host_receive_packet(host, id, PACKET_TYPE_CLIENT_CAPABILITIES, caps, sizeof(*caps)) == ASCIICHAT_OK);
  acip_render_settings_t settings = {0};
  CHECK(acip_render_settings_capabilities(&settings, caps) == ASCIICHAT_OK);
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  acip_render_settings_options(&settings, values);
  char *json = options_values_json(values, true);
  CHECK(json);
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
      CHECK(*p);
    } else if (*p != ' ' && *p != '\r' && *p != '\n') {
      CHECK(strchr(allowed, *p));
      ++characters;
    }
  }
  CHECK(characters > 0);
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
  CHECK(size == strlen(text));
  ascii_frame_packet_t header = {.width = (uint32_t)width, .height = (uint32_t)height};
  CHECK(receive_frame(&header, text, &((display_t *)context)->sink) == ASCIICHAT_OK);
}

static void test_tcp_delivery(const image_t *image) {
  CHECK(socket_init() == ASCIICHAT_OK);
  socket_t listener = socket_create("host_render_listener", AF_INET, SOCK_STREAM, 0);
  CHECK(listener != INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  CHECK(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  CHECK(socket_listen(listener, 1) == 0);
  socklen_t length = sizeof(address);
  CHECK(socket_getsockname(listener, (struct sockaddr *)&address, &length) == 0);
  socket_t peer = socket_create("host_render_peer", AF_INET, SOCK_STREAM, 0);
  CHECK(peer != INVALID_SOCKET_VALUE);
  CHECK(socket_connect(peer, (struct sockaddr *)&address, sizeof(address)) == 0);
  socket_t accepted = socket_accept(listener, NULL, NULL, "host_render_accepted");
  CHECK(accepted != INVALID_SOCKET_VALUE);
  socket_close(listener);

  session_host_config_t config = {.max_clients = 2};
  session_host_t *host = session_host_create(&config);
  CHECK(host);
  uint32_t source = session_host_add_memory_participant(host);
  CHECK(source && session_host_inject_frame(host, source, image) == ASCIICHAT_OK);
  uint32_t client = session_host_add_client(host, accepted, "127.0.0.1", 0);
  CHECK(client);
  acip_transport_t *tcp = session_host_get_client_transport(host, client);
  CHECK(tcp && acip_transport_get_socket(tcp) == accepted);

  sink_t sink = {0};
  acip_transport_t *receiver = acip_tcp_transport_create("host_render_receiver", peer, NULL);
  CHECK(receiver);
  session_protocol_init(&sink.protocol, receiver,
                        (session_protocol_media_t){.on_frame = receive_frame, .context = &sink},
                        (acip_client_callbacks_t){0});
  // Temporarily replacing a borrowed transport must preserve the TCP wrapper.
  const acip_transport_methods_t methods = {.sendv = send_frame};
  acip_transport_t alternate = {.methods = &methods, .impl_data = &sink};
  CHECK(session_host_set_client_transport(host, client, &alternate) == ASCIICHAT_OK);
  CHECK(session_host_set_client_transport(host, client, NULL) == ASCIICHAT_OK);
  CHECK(session_host_get_client_transport(host, client) == tcp);

  terminal_capabilities_packet_t caps = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "TT");
  update(host, client, &caps);
  CHECK(render_tick(host) == ASCIICHAT_OK);
  while (sink.frames < 1)
    CHECK(session_protocol_receive(&sink.protocol) == ASCIICHAT_OK);
  CHECK(sink.frames == 1);
  check_palette(sink.frame, "T");
  session_host_render_target_t target = {.client_id = client};
  CHECK(session_host_set_render_target(host, &target) == ASCIICHAT_OK);
  CHECK(render_tick(host) == ASCIICHAT_OK);
  while (sink.frames < 2)
    CHECK(session_protocol_receive(&sink.protocol) == ASCIICHAT_OK);
  CHECK(sink.frames == 2);
  check_palette(sink.frame, "T");

  CHECK(session_host_remove_client(host, client) == ASCIICHAT_OK);
  CHECK(session_host_get_client_transport(host, client) == NULL);
  session_host_destroy(host);
  acip_transport_destroy(receiver);
  socket_close(peer);
  socket_cleanup();
  SAFE_FREE(sink.frame);
}

int main(void) {
  log_init(NULL, LOG_WARN, false, false);
  CHECK(options_state_init() == ASCIICHAT_OK);
  ascii_simd_init();
  session_host_config_t config = {.max_clients = 4};
  session_host_t *host = session_host_create(&config);
  CHECK(host);
  uint32_t source = session_host_add_memory_participant(host);
  CHECK(source);
  image_t *image = image_new(8, 4);
  CHECK(image);
  for (int i = 0; i < image->w * image->h; ++i)
    image->pixels[i] = (rgb_pixel_t){.r = (uint8_t)(40 + i * 6), .g = 25, .b = 100};
  CHECK(session_host_inject_frame(host, source, image) == ASCIICHAT_OK);

  sink_t sinks[3] = {0};
  acip_transport_t transports[3];
  uint32_t ids[3];
  const acip_transport_methods_t methods = {.sendv = send_frame};
  for (int i = 0; i < 3; ++i) {
    transports[i] = (acip_transport_t){.methods = &methods, .impl_data = &sinks[i]};
    session_protocol_init(&sinks[i].protocol, &transports[i],
                          (session_protocol_media_t){.on_frame = receive_frame, .context = &sinks[i]},
                          (acip_client_callbacks_t){0});
    ids[i] = session_host_add_client(host, INVALID_SOCKET_VALUE, "test", 0);
    CHECK(ids[i]);
    CHECK(session_host_set_client_transport(host, ids[i], &transports[i]) == ASCIICHAT_OK);
  }
  display_t display = {
      .config = {.caps = {.color_level = TERM_COLOR_NONE}, .palette = "ZZ", .width = 40, .height = 12}};
  session_host_render_target_t target = {
      .get_config = display_config, .render_frame = display_frame, .context = &display};
  CHECK(session_host_set_render_target(host, &target) == ASCIICHAT_OK);

  terminal_capabilities_packet_t caps[3] = {capabilities(TERM_COLOR_TRUECOLOR, PALETTE_CUSTOM, "XY"),
                                            capabilities(TERM_COLOR_256, PALETTE_CUSTOM, "XY"),
                                            capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "XY")};
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  CHECK(render_tick(host) == ASCIICHAT_OK);
  CHECK(strstr(sinks[0].frame, "\033[38;2;"));
  CHECK(strstr(sinks[1].frame, "\033[38;5;"));
  CHECK(!strstr(sinks[1].frame, "\033[38;2;"));
  CHECK(!strstr(sinks[2].frame, "\033[38;"));
  for (int i = 0; i < 3; ++i)
    check_palette(sinks[i].frame, "XY");

  // Palette-only updates at identical dimensions must not share another client's grid.
  caps[0] = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "XX");
  caps[1] = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "YY");
  caps[2] = capabilities(TERM_COLOR_NONE, PALETTE_MINIMAL, NULL);
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  CHECK(render_tick(host) == ASCIICHAT_OK);
  check_palette(sinks[0].frame, "X");
  check_palette(sinks[1].frame, "Y");
  check_palette(sinks[2].frame, PALETTE_CHARS_MINIMAL);

  // Filters also distinguish cache entries and must leave the shared source intact.
  caps[0] = caps[1] = caps[2] = capabilities(TERM_COLOR_TRUECOLOR, PALETTE_STANDARD, NULL);
  caps[1].color_filter = COLOR_FILTER_GREEN;
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  CHECK(render_tick(host) == ASCIICHAT_OK);
  CHECK(strcmp(sinks[0].frame, sinks[1].frame) != 0);
  CHECK(strcmp(sinks[0].frame, sinks[2].frame) == 0);

  caps[1] = capabilities(TERM_COLOR_16, PALETTE_STANDARD, NULL);
  caps[2] = capabilities(TERM_COLOR_256, PALETTE_STANDARD, NULL);
  caps[2].render_mode = HOST_TO_NET_U32(RENDER_MODE_BACKGROUND);
  update(host, ids[1], &caps[1]);
  update(host, ids[2], &caps[2]);
  CHECK(render_tick(host) == ASCIICHAT_OK);
  CHECK(strstr(sinks[1].frame, "\033["));
  CHECK(!strstr(sinks[1].frame, "\033[38;"));
  CHECK(strstr(sinks[2].frame, "\033[48;5;"));
  caps[2].render_mode = HOST_TO_NET_U32(RENDER_MODE_HALF_BLOCK);
  update(host, ids[2], &caps[2]);
  CHECK(render_tick(host) == ASCIICHAT_OK);
  CHECK(strstr(sinks[2].frame, "\xe2\x96\x80"));

  // Invalid runtime transactions leave all recipients' last committed values intact.
  for (int i = 0; i < 3; ++i) {
    caps[i] = capabilities(TERM_COLOR_NONE, PALETTE_STANDARD, NULL);
    update(host, ids[i], &caps[i]);
  }
  patch(host, ids[1], "{\"color_mode\":999,\"palette_type\":6,\"palette_custom\":\"XX\"}");
  CHECK(render_tick(host) == ASCIICHAT_OK);
  CHECK(strcmp(sinks[0].frame, sinks[1].frame) == 0);
  CHECK(strcmp(sinks[0].frame, sinks[2].frame) == 0);
  patch(host, ids[1], "{\"palette_type\":6,\"palette_custom\":\"\"}");
  CHECK(render_tick(host) == ASCIICHAT_OK);
  CHECK(strcmp(sinks[0].frame, sinks[1].frame) == 0);

  CHECK(display.sink.frames == 7);
  check_palette(display.sink.frame, "Z");
  // A local backend also picks up changed rendering settings at the next tick.
  display.config.caps.color_level = TERM_COLOR_TRUECOLOR;
  display.config.palette = "LL";
  CHECK(render_tick(host) == ASCIICHAT_OK);
  CHECK(display.sink.frames == 8);
  CHECK(strstr(display.sink.frame, "\033[38;2;"));
  check_palette(display.sink.frame, "L");
  SAFE_FREE(display.sink.frame);

  // Browser hosts use a local client as the target, with no terminal display.
  target = (session_host_render_target_t){.client_id = ids[1]};
  CHECK(session_host_set_render_target(host, &target) == ASCIICHAT_OK);
  caps[1] = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "WW");
  update(host, ids[1], &caps[1]);
  CHECK(render_tick(host) == ASCIICHAT_OK);
  check_palette(sinks[1].frame, "W");
  CHECK(display.sink.frames == 8);
  for (int i = 0; i < 3; ++i) {
    CHECK(sinks[i].frames == 9); // Local clients receive exactly one frame per tick.
    SAFE_FREE(sinks[i].frame);
  }
  CHECK(session_host_remove_client(host, ids[1]) == ASCIICHAT_OK);
  CHECK(session_host_set_render_target(host, &target) == ERROR_NOT_FOUND);
  CHECK(session_host_set_render_target(host, NULL) == ASCIICHAT_OK);
  session_host_destroy(host);
  test_tcp_delivery(image);
  image_destroy(image);
  options_state_destroy();
  log_info("PASS: per-recipient color, palettes, filters, live updates, and rejected runtime transactions");
  log_destroy();
  return 0;
}

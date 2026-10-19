/* Capability packets must affect the frames delivered by the discovery host. */
#include "session/host.h"
#include "session/protocol.h"
#include <ascii-chat/common.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
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

static void update(session_host_t *host, uint32_t id, const terminal_capabilities_packet_t *caps) {
  CHECK(session_host_receive_packet(host, id, PACKET_TYPE_CLIENT_CAPABILITIES, caps, sizeof(*caps)) == ASCIICHAT_OK);
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

  terminal_capabilities_packet_t caps[3] = {capabilities(TERM_COLOR_TRUECOLOR, PALETTE_CUSTOM, "XY"),
                                            capabilities(TERM_COLOR_256, PALETTE_CUSTOM, "XY"),
                                            capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "XY")};
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  CHECK(session_host_render_video(host) == ASCIICHAT_OK);
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
  CHECK(session_host_render_video(host) == ASCIICHAT_OK);
  check_palette(sinks[0].frame, "X");
  check_palette(sinks[1].frame, "Y");
  check_palette(sinks[2].frame, PALETTE_CHARS_MINIMAL);

  // Filters also distinguish cache entries and must leave the shared source intact.
  caps[0] = caps[1] = caps[2] = capabilities(TERM_COLOR_TRUECOLOR, PALETTE_STANDARD, NULL);
  caps[1].color_filter = COLOR_FILTER_GREEN;
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  CHECK(session_host_render_video(host) == ASCIICHAT_OK);
  CHECK(strcmp(sinks[0].frame, sinks[1].frame) != 0);
  CHECK(strcmp(sinks[0].frame, sinks[2].frame) == 0);

  caps[1] = capabilities(TERM_COLOR_16, PALETTE_STANDARD, NULL);
  caps[2] = capabilities(TERM_COLOR_256, PALETTE_STANDARD, NULL);
  caps[2].render_mode = HOST_TO_NET_U32(RENDER_MODE_BACKGROUND);
  update(host, ids[1], &caps[1]);
  update(host, ids[2], &caps[2]);
  CHECK(session_host_render_video(host) == ASCIICHAT_OK);
  CHECK(strstr(sinks[1].frame, "\033["));
  CHECK(!strstr(sinks[1].frame, "\033[38;"));
  CHECK(strstr(sinks[2].frame, "\033[48;5;"));
  caps[2].render_mode = HOST_TO_NET_U32(RENDER_MODE_HALF_BLOCK);
  update(host, ids[2], &caps[2]);
  CHECK(session_host_render_video(host) == ASCIICHAT_OK);
  CHECK(strstr(sinks[2].frame, "\xe2\x96\x80"));

  // Untrusted enum values and unterminated/empty palettes resolve to safe defaults.
  caps[0] = capabilities(TERM_COLOR_NONE, PALETTE_STANDARD, NULL);
  caps[1] = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, NULL);
  memset(caps[1].palette_custom, 'X', sizeof(caps[1].palette_custom));
  caps[1].color_level = caps[1].render_mode = HOST_TO_NET_U32(UINT32_MAX);
  caps[1].color_filter = UINT8_MAX;
  caps[2] = capabilities(TERM_COLOR_NONE, PALETTE_BLOCKS, NULL);
  caps[2].utf8_support = 0;
  for (int i = 0; i < 3; ++i)
    update(host, ids[i], &caps[i]);
  CHECK(session_host_render_video(host) == ASCIICHAT_OK);
  CHECK(strcmp(sinks[0].frame, sinks[1].frame) == 0);
  CHECK(strcmp(sinks[0].frame, sinks[2].frame) == 0);
  caps[1] = capabilities(TERM_COLOR_NONE, PALETTE_CUSTOM, "");
  update(host, ids[1], &caps[1]);
  CHECK(session_host_render_video(host) == ASCIICHAT_OK);
  CHECK(strcmp(sinks[0].frame, sinks[1].frame) == 0);

  session_host_destroy(host);
  image_destroy(image);
  for (int i = 0; i < 3; ++i) {
    CHECK(sinks[i].frames == 7);
    SAFE_FREE(sinks[i].frame);
  }
  options_state_destroy();
  log_info("PASS: per-recipient color, palettes, filters, live updates, and malformed capabilities");
  log_destroy();
  return 0;
}

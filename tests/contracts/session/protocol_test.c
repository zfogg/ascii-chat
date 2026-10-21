#include <criterion/criterion.h>
#include "session/protocol.h"
#include "session/frame_validation.h"
#include "session/participant.h"
#include <ascii-chat/common.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <string.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/network/compression.h>

typedef struct {
  size_t frames, samples, decodes, pongs, controls;
  bool fail_audio;
} sink_t;

static asciichat_error_t frame(const ascii_frame_packet_t *header, const char *text, void *context) {
  sink_t *sink = context;
  cr_assert(header->width == 4 && header->height == 1);
  cr_assert_eq(strcmp(text, "abcd"), 0);
  sink->frames++;
  return ASCIICHAT_OK;
}

static asciichat_error_t samples(const float *data, size_t count, void *context) {
  sink_t *sink = context;
  if (sink->fail_audio)
    return SET_ERRNO(ERROR_AUDIO, "Injected playback failure");
  cr_assert(count == 2 && data[0] > 0.99f && data[1] < -0.99f);
  sink->samples += count;
  return ASCIICHAT_OK;
}

static asciichat_error_t decode(const void *data, size_t length, float *out, size_t capacity, size_t *count,
                                void *context) {
  sink_t *sink = context;
  cr_assert(length == 1 && *(const uint8_t *)data == 42 && capacity >= 2);
  out[0] = 1.0f;
  out[1] = -1.0f;
  *count = 2;
  sink->decodes++;
  return ASCIICHAT_OK;
}

static void pong(void *context) {
  ((sink_t *)context)->controls++;
}

static asciichat_error_t send_vectors(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  cr_assert(count == 2 && buffers[0].len == sizeof(packet_header_t));
  packet_header_t header;
  memcpy(&header, buffers[0].data, sizeof(header));
  cr_assert(NET_TO_HOST_U16(header.type) == PACKET_TYPE_PONG && NET_TO_HOST_U32(header.length) == 0);
  ((sink_t *)transport->impl_data)->pongs++;
  return ASCIICHAT_OK;
}

typedef struct {
  unsigned calls;
  bool fail;
  options_t values;
} dimensions_sink_t;
static asciichat_error_t send_dimensions(const options_t *values, uint32_t request, void *context) {
  (void)request;
  dimensions_sink_t *sink = context;
  sink->calls++;
  sink->values = *values;
  return sink->fail ? ERROR_NETWORK : ASCIICHAT_OK;
}
static void test_dimensions(void) {
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("width", 80), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("height", 24), ASCIICHAT_OK);
  uint64_t revisions[2] = {UINT64_MAX, UINT64_MAX};
  terminal_size_t client = {0}, discovery = {0};
  dimensions_sink_t sinks[2] = {0};
  cr_assert_eq(session_protocol_update_settings(&client, &revisions[0], send_dimensions, &sinks[0]), ASCIICHAT_OK);
  cr_assert_eq(session_protocol_update_settings(&discovery, &revisions[1], send_dimensions, &sinks[1]), ASCIICHAT_OK);
  cr_assert(sinks[0].calls == 1 && sinks[1].calls == 1);
  cr_assert(sinks[1].values.width == 80 && sinks[1].values.height == 24);
  cr_assert_eq(session_protocol_update_settings(&client, &revisions[0], send_dimensions, &sinks[0]), ASCIICHAT_OK);
  cr_assert_eq(sinks[0].calls, 1);
  cr_assert_eq(options_set_int("width", 100), ASCIICHAT_OK);
  sinks[0].fail = true;
  cr_assert_eq(session_protocol_update_settings(&client, &revisions[0], send_dimensions, &sinks[0]), ERROR_NETWORK);
  cr_assert_eq(client.cols, 80);
  cr_assert_eq(session_protocol_update_settings(&discovery, &revisions[1], send_dimensions, &sinks[1]), ASCIICHAT_OK);
  cr_assert(discovery.cols == 100 && client.cols == 80);
  cr_assert(sinks[1].values.width == 100 && sinks[1].values.height == 24);
  sinks[0].fail = false;
  cr_assert_eq(session_protocol_update_settings(&client, &revisions[0], send_dimensions, &sinks[0]), ASCIICHAT_OK);
  cr_assert(client.cols == 100 && sinks[0].calls == 3);
  options_state_destroy();
}

typedef struct {
  acip_render_settings_t host;
  uint8_t reply[ACIP_SETTINGS_MAX_SIZE];
  size_t reply_length;
  unsigned callbacks;
} settings_roundtrip_t;

static bool mock_transport_is_connected(acip_transport_t *transport) {
  (void)transport;
  return true;
}

static asciichat_error_t settings_send(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  cr_assert_eq(count, 2);
  packet_header_t packet;
  memcpy(&packet, buffers[0].data, sizeof(packet));
  cr_assert_eq(NET_TO_HOST_U16(packet.type), PACKET_TYPE_ACIP_SETTINGS_SYNC);
  settings_roundtrip_t *test = transport->impl_data;
  return acip_settings_request(&test->host, buffers[1].data, buffers[1].len, test->reply, sizeof(test->reply),
                               &test->reply_length);
}

static void settings_changed(session_participant_t *participant, const session_settings_t *effective, void *context) {
  settings_roundtrip_t *test = context;
  session_settings_t readback;
  // Calling a reader from the callback verifies that notifications run unlocked.
  cr_assert_eq(session_participant_get_settings(participant, &readback), ASCIICHAT_OK);
  cr_assert(readback.version == effective->version && readback.width == effective->width);
  test->callbacks++;
}

static void test_participant_settings(void) {
  settings_roundtrip_t test = {0};
  const acip_transport_methods_t methods = {.sendv = settings_send, .is_connected = mock_transport_is_connected};
  acip_transport_t transport = {.methods = &methods, .impl_data = &test};
  session_participant_config_t config = {
      .address = "127.0.0.1", .port = 1, .callbacks.on_settings_changed = settings_changed, .user_data = &test};
  session_participant_t *participant = session_participant_create(&config);
  cr_assert(participant);
  session_settings_t requested = {
      .width = 80, .height = 24, .color_mode = TERM_COLOR_TRUECOLOR, .palette_type = PALETTE_STANDARD, .fps = 30};
  cr_assert_eq(session_participant_request_settings(participant, &requested), ERROR_INVALID_STATE);
  CLEAR_ERRNO();
  cr_assert_eq(session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  cr_assert_eq(session_participant_request_settings(participant, &requested), ASCIICHAT_OK);
  cr_assert(test.host.width == 80 && test.callbacks == 0);
  cr_assert_eq(session_participant_receive_settings(participant, test.reply, test.reply_length), ASCIICHAT_OK);
  cr_assert_eq(test.callbacks, 1);
  requested.width = 55;
  requested.palette_type = PALETTE_CUSTOM;
  SAFE_STRNCPY(requested.palette_custom, " .+#", sizeof(requested.palette_custom));
  cr_assert_eq(session_participant_request_settings(participant, &requested), ASCIICHAT_OK);
  cr_assert_eq(session_participant_receive_settings(participant, test.reply, test.reply_length), ASCIICHAT_OK);
  cr_assert(test.host.width == 55 && test.callbacks == 2 && !strcmp(test.host.palette, " .+#"));
  cr_assert_eq(session_participant_set_transport(participant, NULL, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  memset(&test.host, 0, sizeof(test.host));
  cr_assert_eq(session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  requested.width = 70;
  cr_assert_eq(session_participant_request_settings(participant, &requested), ASCIICHAT_OK);
  cr_assert_eq(session_participant_receive_settings(participant, test.reply, test.reply_length), ASCIICHAT_OK);
  cr_assert(test.callbacks == 3 && test.host.width == 70);
  cr_assert_eq(session_participant_set_transport(participant, NULL, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  session_participant_destroy(participant);

  // The public initializer supplies AUTO; callers can submit it unchanged.
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  participant = session_participant_create(&config);
  cr_assert(participant);
  session_settings_init(&requested);
  cr_assert_eq(requested.color_mode, TERM_COLOR_AUTO);
  terminal_capabilities_t caps = detect_terminal_capabilities();
  int expected_color = caps.detection_reliable ? caps.color_level : TERM_COLOR_NONE;
  memset(&test.host, 0, sizeof(test.host));
  cr_assert_eq(session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  cr_assert_eq(session_participant_request_settings(participant, &requested), ASCIICHAT_OK);
  cr_assert_eq(requested.color_mode, TERM_COLOR_AUTO);
  cr_assert_eq(test.host.caps.color_level, expected_color);
  cr_assert_eq(session_participant_receive_settings(participant, test.reply, test.reply_length), ASCIICHAT_OK);
  session_settings_t effective;
  cr_assert_eq(session_participant_get_settings(participant, &effective), ASCIICHAT_OK);
  cr_assert(effective.color_mode == expected_color && test.callbacks == 4);
  cr_assert_eq(session_participant_set_transport(participant, NULL, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  session_participant_destroy(participant);
  options_state_destroy();
}

static void test_rendered_frame_validation(void) {
  cr_assert_eq(session_frame_validate(" ", 1, 0, 1), ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  cr_assert_eq(session_frame_validate(" ", 1, 16384, 16384), ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  cr_assert_eq(session_frame_validate(" ", (size_t)FRAME_RENDER_MAX_BYTES + 1, 1, 1), ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  struct {
    const char *text;
    uint32_t width, height;
  } valid[] = {
      {"    ", 4, 1},
      {"    \r\n    ", 4, 2},
      {"abcd\n", 4, 1},
      {"\033[38;2;0;255;128mabcd\033[0m", 4, 1},
      {"\033[48;5;255m \033[3b\033[m", 4, 1},
      {"\033[H\033[97;40mabcd\033[0m", 4, 1},
      {"界é ", 4, 1},
      {"e\xcc\x81\xcc\x80"
       "abc",
       4, 1},
      {"▀▄█ ", 4, 1},
      {"zzzz\n____\n|+|+", 4, 3},
  };
  for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
    cr_assert_eq(session_frame_validate(valid[i].text, strlen(valid[i].text), valid[i].width, valid[i].height),
                 ASCIICHAT_OK, "valid renderer contract case %zu", i);
  const char *invalid[] = {
      "abc",
      "abcde",
      "abcd\nabc",
      "abcd\nabcd",
      "abcd\n\n",
      "abcd\r",
      "abc\tabcd",
      "abc\a",
      "abc\033",
      "abc\033[",
      "abc\033[31",
      "abc\033]52;c;SGVsbG8=\a",
      "abc\033]0;title\a",
      "abc\033[6n",
      "abc\033[?25l",
      "abc\033Ppayload\033\\",
      "abc\033_Ga=T;AAAA\033\\",
      "abc\033[999999999999999999999b",
      "a\033[4b",
      "\033[4b",
      "abc\033[38;2;256;0;0md",
      "abc\033[38;5md",
      "abc\033[1;2;3;4;5;6;7;8;9;0;0;0;0;0;0;0;0md",
      "\033[8mabcd",
      "\033[31mabcd",
      "abc\033[2Jd",
      "abc\033[2;2Hd",
      "abc\xc0\xaf",
      "abc\xed\xa0\x80",
      "abc\xf4\x90\x80\x80",
      "abc\xe2\x82",
      "abc\xc2\x9b",
      ("\xcc\x81"
       "abcd"),
      "abc界",
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    cr_assert_eq(session_frame_validate(invalid[i], strlen(invalid[i]), 4, 1), ERROR_NETWORK_PROTOCOL,
                 "negative frame case %zu", i);
    CLEAR_ERRNO();
  }
  const char embedded[] = {'a', 'b', 'c', 'd', 0, 'x'};
  cr_assert_eq(session_frame_validate(embedded, sizeof(embedded), 4, 1), ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  // Every possible truncation of a color token and UTF-8 token is rejected;
  // buffers have no accessible NUL terminator requirement.
  const char *tokens[] = {"\033[38;2;255;0;128m", "界"};
  for (size_t t = 0; t < 2; ++t)
    for (size_t n = 1; n < strlen(tokens[t]); ++n) {
      char *bytes = SAFE_MALLOC(n + 4, char *);
      memcpy(bytes, "abcd", 4);
      memcpy(bytes + 4, tokens[t], n);
      cr_assert_eq(session_frame_validate(bytes, n + 4, 4, 1), ERROR_NETWORK_PROTOCOL);
      SAFE_FREE(bytes);
      CLEAR_ERRNO();
    }
  acip_render_settings_t rendering = {.revision = 2, .caps.render_mode = RENDER_MODE_FOREGROUND};
  SAFE_STRNCPY(rendering.palette, "é界e\xcc\x81", sizeof(rendering.palette));
  cr_assert_eq(session_frame_validate_contract("界é ", strlen("界é "), 4, 1, &rendering, 0), ASCIICHAT_OK);
  cr_assert_eq(session_frame_validate_contract("    ", 4, 4, 1, &rendering, 0), ASCIICHAT_OK);
  cr_assert_eq(session_frame_validate_contract("XXXX", 4, 4, 1, &rendering, 0), ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  rendering.caps.render_mode = RENDER_MODE_HALF_BLOCK;
  cr_assert_eq(session_frame_validate_contract("▀▄█ ", strlen("▀▄█ "), 4, 1, &rendering, 0), ASCIICHAT_OK);
  cr_assert_eq(session_frame_validate_contract("░▒▓█", strlen("░▒▓█"), 4, 1, &rendering, 0), ASCIICHAT_OK);
  rendering.caps.render_mode = RENDER_MODE_BACKGROUND;
  cr_assert_eq(session_frame_validate_contract("    ", 4, 4, 1, &rendering, 0), ASCIICHAT_OK);
  cr_assert_eq(session_frame_validate_contract("abcd", 4, 4, 1, &rendering, 0), ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  rendering.caps.render_mode = RENDER_MODE_FOREGROUND;
  SAFE_STRNCPY(rendering.palette, ".", sizeof(rendering.palette));
  cr_assert_eq(session_frame_validate_contract(",,,,", 4, 4, 1, &rendering, FRAME_RENDER_CONTRACT_RAIN), ASCIICHAT_OK);
  cr_assert_eq(session_frame_validate_contract("|_+ ", 4, 4, 1, &rendering, FRAME_RENDER_CONTRACT_GRID), ASCIICHAT_OK);
  uint8_t metadata[FRAME_RENDER_CONTRACT_MAX];
  size_t metadata_size, consumed;
  uint32_t effects;
  acip_render_settings_t decoded;
  cr_assert_eq(frame_render_contract_encode(&rendering, 0, metadata, &metadata_size), ASCIICHAT_OK);
  cr_assert_eq(frame_render_contract_decode(metadata, metadata_size, &decoded, &effects, &consumed), ASCIICHAT_OK);
  cr_assert(consumed == metadata_size && decoded.revision == 2 && !strcmp(decoded.palette, "."));
  for (size_t n = 0; n < metadata_size; ++n) {
    cr_assert_eq(frame_render_contract_decode(metadata, n, &decoded, &effects, &consumed), ERROR_NETWORK_PROTOCOL);
    CLEAR_ERRNO();
  }
  metadata[3] = 2;
  cr_assert_eq(frame_render_contract_decode(metadata, metadata_size, &decoded, &effects, &consumed),
               ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  // Explicit wire contract is attached to the frame, not whichever settings
  // happen to be current when a queued frame is received.
  SAFE_STRNCPY(rendering.palette, "abcd", sizeof(rendering.palette));
  rendering.revision = 1;
  cr_assert_eq(frame_render_contract_encode(&rendering, 0, metadata, &metadata_size), ASCIICHAT_OK);
  uint8_t packet[sizeof(ascii_frame_packet_t) + FRAME_RENDER_CONTRACT_MAX + 4];
  ascii_frame_packet_t wire_header = {.width = HOST_TO_NET_U32(4),
                                      .height = HOST_TO_NET_U32(1),
                                      .original_size = HOST_TO_NET_U32(4),
                                      .checksum = HOST_TO_NET_U32(asciichat_crc32("abcd", 4)),
                                      .flags = HOST_TO_NET_U32(FRAME_FLAG_RENDER_CONTRACT)};
  memcpy(packet, &wire_header, sizeof(wire_header));
  memcpy(packet + sizeof(wire_header), metadata, metadata_size);
  memcpy(packet + sizeof(wire_header) + metadata_size, "abcd", 4);
  sink_t sink = {0};
  session_protocol_t protocol;
  acip_transport_t transport = {0};
  session_protocol_init(&protocol, &transport, (session_protocol_media_t){.on_frame = frame, .context = &sink},
                        (acip_client_callbacks_t){0});
  protocol.settings_revision = 2; // A later ACK need not invalidate a queued older frame.
  asciichat_error_t dispatch_result =
      session_protocol_dispatch(&protocol, PACKET_TYPE_ASCII_FRAME, packet, sizeof(wire_header) + metadata_size + 4);
  asciichat_error_context_t dispatch_error = {0};
  errno_has(&dispatch_error);
  cr_assert_eq(dispatch_result, ASCIICHAT_OK, "Valid contract dispatch: %s", dispatch_error.context_message);
  cr_assert_eq(sink.frames, 1);
  packet[sizeof(wire_header) + FRAME_RENDER_CONTRACT_HEADER] = 'X';
  cr_assert_eq(
      session_protocol_dispatch(&protocol, PACKET_TYPE_ASCII_FRAME, packet, sizeof(wire_header) + metadata_size + 4),
      ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(sink.frames, 1);
  CLEAR_ERRNO();
  packet[sizeof(wire_header) + FRAME_RENDER_CONTRACT_HEADER] = 'a';
  cr_assert_eq(
      session_protocol_dispatch(&protocol, PACKET_TYPE_ASCII_FRAME, packet, sizeof(wire_header) + metadata_size + 3),
      ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  packet[sizeof(wire_header) + 3] = 9; // Schema, never reinterpret as text/legacy.
  cr_assert_eq(
      session_protocol_dispatch(&protocol, PACKET_TYPE_ASCII_FRAME, packet, sizeof(wire_header) + metadata_size + 4),
      ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();
  packet[sizeof(wire_header) + 3] = 1;
  cr_assert_eq(
      session_protocol_dispatch(&protocol, PACKET_TYPE_ASCII_FRAME, packet, sizeof(wire_header) + metadata_size + 4),
      ASCIICHAT_OK);
  cr_assert_eq(sink.frames, 2);
  // Deterministic arbitrary-byte corpus. Neither rejection nor a subsequent
  // valid frame may poison parser state; no content is emitted by validation.
  uint32_t random = 253;
  for (size_t n = 1; n <= 128; ++n) {
    char bytes[128];
    for (size_t i = 0; i < n; ++i) {
      random = (uint32_t)((uint64_t)random * 1664525u + 1013904223u);
      bytes[i] = (char)(random >> 24);
    }
    asciichat_error_t result = session_frame_validate(bytes, n, 4, 1);
    cr_assert(result == ASCIICHAT_OK || result == ERROR_NETWORK_PROTOCOL);
    CLEAR_ERRNO();
    cr_assert_eq(session_frame_validate("    ", 4, 4, 1), ASCIICHAT_OK);
  }
}

void test_session_protocol(void) {
  test_rendered_frame_validation();
  test_dimensions();
  test_participant_settings();
  sink_t sinks[2] = {0};
  const acip_transport_methods_t methods = {.sendv = send_vectors};
  acip_transport_t transports[2] = {{.methods = &methods, .impl_data = &sinks[0]},
                                    {.methods = &methods, .impl_data = &sinks[1]}};
  session_protocol_t protocols[2];
  buffer_workspace_t ascii_workspace = {0};
  for (int i = 0; i < 2; i++) {
    session_protocol_init(&protocols[i], &transports[i],
                          (session_protocol_media_t){.on_frame = frame,
                                                     .on_samples = samples,
                                                     .decode_opus = decode,
                                                     .context = &sinks[i],
                                                     .frame_workspace = i == 0 ? &ascii_workspace : NULL},
                          (acip_client_callbacks_t){.on_pong = pong, .app_ctx = &sinks[i]});
  }
  uint8_t payload[256];
  ascii_frame_packet_t header = {.width = HOST_TO_NET_U32(4),
                                 .height = HOST_TO_NET_U32(1),
                                 .original_size = HOST_TO_NET_U32(4),
                                 .checksum = HOST_TO_NET_U32(asciichat_crc32("abcd", 4))};
  memcpy(payload, &header, sizeof(header));
  memcpy(payload + sizeof(header), "abcd", 4);
  cr_assert_eq(session_protocol_dispatch(&protocols[0], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + 4),
               ASCIICHAT_OK);
  cr_assert(sinks[0].frames == 1 && sinks[1].frames == 0);
  cr_assert(protocols[0].frames_received == 1 && protocols[1].frames_received == 0);
  // Even a checksum-valid hostile frame must never reach presentation. A
  // rejected frame does not commit snapshot/FPS/UI state or poison recovery.
  const char hostile[] = "a\033]52;c;AAAA\a";
  ascii_frame_packet_t hostile_header = header;
  hostile_header.original_size = HOST_TO_NET_U32(sizeof(hostile) - 1);
  hostile_header.checksum = HOST_TO_NET_U32(asciichat_crc32(hostile, sizeof(hostile) - 1));
  memcpy(payload, &hostile_header, sizeof(hostile_header));
  memcpy(payload + sizeof(hostile_header), hostile, sizeof(hostile) - 1);
  cr_assert_eq(session_protocol_dispatch(&protocols[0], PACKET_TYPE_ASCII_FRAME, payload,
                                         sizeof(hostile_header) + sizeof(hostile) - 1),
               ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(sinks[0].frames, 1);
  cr_assert_eq(protocols[0].frames_received, 1);
  CLEAR_ERRNO();
  memcpy(payload, &header, sizeof(header));
  memcpy(payload + sizeof(header), "abcd", 4);
  // Resize/explicit-size change: a structurally valid old frame is skipped,
  // then accepted again once the viewport grows. The protocol stays usable.
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("width", 20), ASCIICHAT_OK);
  cr_assert_eq(options_set_int("height", 10), ASCIICHAT_OK);
  ascii_frame_packet_t wide = header;
  wide.width = HOST_TO_NET_U32(21);
  wide.original_size = HOST_TO_NET_U32(21);
  wide.checksum = HOST_TO_NET_U32(asciichat_crc32("                     ", 21));
  memcpy(payload, &wide, sizeof(wide));
  memset(payload + sizeof(wide), ' ', 21);
  protocols[0].media.terminal_output = true;
  cr_assert_eq(session_protocol_dispatch(&protocols[0], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + 21),
               ASCIICHAT_OK);
  cr_assert_eq(sinks[0].frames, 1);
  cr_assert_eq(protocols[0].frames_skipped_viewport, 1);
  cr_assert_eq(options_set_int("width", 80), ASCIICHAT_OK);
  memcpy(payload, &header, sizeof(header));
  memcpy(payload + sizeof(header), "abcd", 4);
  cr_assert_eq(session_protocol_dispatch(&protocols[0], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + 4),
               ASCIICHAT_OK);
  cr_assert_eq(sinks[0].frames, 2);
  protocols[0].media.terminal_output = false;
  options_state_destroy();
  payload[sizeof(header)] ^= 1;
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + 4),
               ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(sinks[1].frames, 0);
  CLEAR_ERRNO();
  void *encoded = NULL;
  size_t compressed = 0;
  cr_assert_eq(compress_data("abcd", 4, &encoded, &compressed, 1), ASCIICHAT_OK);
  cr_assert(compressed <= sizeof(payload) - sizeof(header));
  memcpy(payload + sizeof(header), encoded, compressed);
  buffer_release(encoded);
  header.flags = HOST_TO_NET_U32(FRAME_FLAG_IS_COMPRESSED);
  header.compressed_size = HOST_TO_NET_U32((uint32_t)compressed);
  memcpy(payload, &header, sizeof(header));
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed),
               ASCIICHAT_OK);
  cr_assert_eq(sinks[1].frames, 1);
  buffer_stats_t before, after;
  buffer_domain_stats(buffer_default_domain(), &before);
  for (unsigned i = 0; i < 100; ++i)
    cr_assert_eq(
        session_protocol_dispatch(&protocols[0], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed),
        ASCIICHAT_OK);
  buffer_domain_stats(buffer_default_domain(), &after);
  cr_assert(before.allocations == after.allocations && before.hits == after.hits);
  // A short successful zstd decode must fail before CRC reads stale workspace
  // bytes. Check both reusable and allocation fallback receive paths.
  header.original_size = HOST_TO_NET_U32(5);
  header.width = HOST_TO_NET_U32(5);
  memcpy(payload, &header, sizeof(header));
  for (unsigned i = 0; i < 2; ++i) {
    uint64_t received = protocols[i].frames_received;
    cr_assert_eq(
        session_protocol_dispatch(&protocols[i], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed),
        ERROR_COMPRESSION);
    cr_assert_eq(protocols[i].frames_received, received);
    CLEAR_ERRNO();
  }
  header.original_size = HOST_TO_NET_U32(4);
  header.width = HOST_TO_NET_U32(4);
  memcpy(payload, &header, sizeof(header));

  cr_assert_eq(
      session_protocol_dispatch(&protocols[1], PACKET_TYPE_ASCII_FRAME, payload, sizeof(header) + compressed - 1),
      ERROR_NETWORK_PROTOCOL);
  CLEAR_ERRNO();

  audio_batch_packet_t audio = {.batch_count = HOST_TO_NET_U32(1),
                                .total_samples = HOST_TO_NET_U32(2),
                                .sample_rate = HOST_TO_NET_U32(48000),
                                .channels = HOST_TO_NET_U32(1)};
  memcpy(payload, &audio, sizeof(audio));
  uint32_t quantized[2] = {HOST_TO_NET_U32(2147483647U), HOST_TO_NET_U32((uint32_t)-2147483647)};
  memcpy(payload + sizeof(audio), quantized, sizeof(quantized));
  cr_assert_eq(
      session_protocol_dispatch(&protocols[0], PACKET_TYPE_AUDIO_BATCH, payload, sizeof(audio) + sizeof(quantized)),
      ASCIICHAT_OK);
  cr_assert(sinks[0].samples == 2 && sinks[1].samples == 0);
  sinks[0].fail_audio = true;
  cr_assert_eq(
      session_protocol_dispatch(&protocols[0], PACKET_TYPE_AUDIO_BATCH, payload, sizeof(audio) + sizeof(quantized)),
      ERROR_AUDIO);
  CLEAR_ERRNO();

  // Deliberately unaligned input checks size-table parsing without uint16_t loads.
  uint8_t storage[24] = {0};
  uint8_t *opus = storage + 1;
  uint32_t opus_header[4] = {HOST_TO_NET_U32(48000), HOST_TO_NET_U32(20), HOST_TO_NET_U32(2), 0};
  memcpy(opus, opus_header, sizeof(opus_header));
  uint16_t sizes[2] = {HOST_TO_NET_U16(1), HOST_TO_NET_U16(1)};
  memcpy(opus + 16, sizes, sizeof(sizes));
  opus[20] = opus[21] = 42;
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_AUDIO_OPUS_BATCH, opus, 22), ASCIICHAT_OK);
  cr_assert(sinks[1].decodes == 2 && sinks[1].samples == 4);
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_AUDIO_OPUS_BATCH, opus, 21),
               ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(sinks[1].decodes, 2); // Malformed batches deliver no partial audio.
  CLEAR_ERRNO();

  server_state_packet_t state = {.active_client_count = HOST_TO_NET_U32(3)};
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_SERVER_STATE, &state, sizeof(state)), ASCIICHAT_OK);
  cr_assert(protocols[1].active_client_count == 3 && protocols[1].layout_changed);
  cr_assert(!protocols[0].server_state_initialized && !protocols[0].layout_changed);
  cr_assert_eq(session_protocol_dispatch(&protocols[0], PACKET_TYPE_PING, NULL, 0), ASCIICHAT_OK);
  cr_assert(sinks[0].pongs == 1 && sinks[1].pongs == 0);
  cr_assert_eq(session_protocol_dispatch(&protocols[1], PACKET_TYPE_PONG, NULL, 0), ASCIICHAT_OK);
  cr_assert(sinks[1].controls == 1 && sinks[0].controls == 0);
  buffer_workspace_destroy(&ascii_workspace);
  log_info("PASS: shared participant protocol, compressed frames, CRC, PCM, Opus bounds, error propagation and owner "
           "isolation");
}
